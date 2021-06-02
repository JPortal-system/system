/* This is a Intel PT decoder for Java Virtual Machine
 * It outputs the bytecode-level execution
 * both in Interpretation mode and in JIT mode
 */
#include <limits.h>

#include "decoder/jvm_dump_decoder.hpp"
#include "decoder/ptjvm_decoder.hpp"
#include "decoder/sideband_decoder.hpp"
#include "structure/PT/codelets_entry.hpp"
#include "structure/PT/decode_result.hpp"
#include "structure/PT/jit_image.hpp"
#include "structure/PT/jit_section.hpp"
#include "structure/PT/load_file.hpp"
#include "structure/PT/pt_ild.hpp"
#include "structure/PT/pt_insn.hpp"
#include "structure/java/analyser.hpp"

#define PERF_RECORD_AUXTRACE 71
#define PERF_RECORD_AUX_ADVANCE 72

struct attr_config {
  struct pt_cpu cpu;
  int nr_cpus;
  uint8_t mtc_freq;
  uint8_t nom_freq;
  uint32_t cpuid_0x15_eax;
  uint32_t cpuid_0x15_ebx;
  uint64_t sample_type;
  uint16_t time_shift;
  uint32_t time_mult;
  uint64_t time_zero;
  uint64_t addr0_a;
  uint64_t addr0_b;
};

static attr_config attr;

struct aux_advance_event {
  __u32 cpu;
  __u32 tid;
};

/* The decoder to use. */
struct ptjvm_decoder {
  /* The actual decoder. qry helps insn to decode bytecode*/
  struct pt_query_decoder *qry;

  /* A collection of decoder-specific flags. */
  struct pt_conf_flags flags;

  /* The current time */
  uint64_t time;

  /* The current ip */
  uint64_t ip;

  /* The current execution mode. */
  enum pt_exec_mode mode;

  /* The status of the last successful decoder query.
   * Errors are reported directly; the status is always a non-negative
   * pt_status_flag bit-vector.
   */
  int status;

  /* to indicate data loss */
  bool loss;

  /* to indicate there is an unresolved ip for query decoder */
  bool unresolved;

  /* A collection of flags defining how to proceed flow reconstruction:
   * - tracing is enabled.
   */
  uint32_t enabled : 1;

  /* - process @event. */
  uint32_t process_event : 1;

  /* - instructions are executed speculatively. */
  uint32_t speculative : 1;

  /* - process @insn/@iext.
   *   We have started processing events binding to @insn/@iext.  We have
   *   not yet proceeded past it.
   */
  uint32_t process_insn : 1;

  /* - a paging event has already been bound to @insn/@iext. */
  uint32_t bound_paging : 1;

  /* - a vmcs event has already been bound to @insn/@iext. */
  uint32_t bound_vmcs : 1;

  /* - a ptwrite event has already been bound to @insn/@iext. */
  uint32_t bound_ptwrite : 1;

  /* The current address space. */
  struct pt_asid asid;

  /* The current event. */
  struct pt_event event;

  /* pt config */
  struct pt_config config;

  /* instruction */
  struct pt_insn insn;

  /* instruction ext */
  struct pt_insn_ext iext;

  /* The current thread id */
  long tid;

  /* The jit image */
  struct jit_image *image;

  /* inline cache targets */
  map<pair<uint64_t, jit_section *>, uint64_t> *ics;

  /* Jvm dump infomation decoder */
  JvmDumpDecoder *jvmdump;

  /* The perf event sideband decoder configuration. */
  SidebandDecoder *sideband;

  /* for decoding sideband infomation */
  struct pev_config pevent;

  Analyser *analyser;

  PCStackInfo *last_pcinfo = nullptr;
  uint64_t last_ip = 0;
};

struct auxtrace_event {
  __u64 size;
  __u64 offset;
  __u64 reference;
  __u32 idx;
  __u32 tid;
  __u32 cpu;
  __u32 reservered__; /* for alignment */
};

static int ptjvm_init_decoder(struct ptjvm_decoder *decoder, jit_image *image) {
  if (!decoder)
    return -pte_internal;

  memset(decoder, 0, sizeof(*decoder));

  pev_config_init(&decoder->pevent);

  decoder->tid = -1;

  decoder->image = image;
  return 0;
}

static void ptjvm_sb_event(ptjvm_decoder *decoder, TraceDataRecord &record) {
  int errcode;
  if (!decoder || !decoder->sideband)
    return;

  struct pev_event pevent;
  while (decoder->sideband->sideband_event(decoder->time, pevent)) {
    switch (pevent.type) {
    case PERF_RECORD_AUX:
      if (pevent.record.aux->flags && PERF_AUX_FLAG_TRUNCATED) {
        decoder->loss = true;
      }
      break;
    case PERF_RECORD_ITRACE_START:
    default:
      break;
    }
    long sys_tid = pevent.sample.tid ? (*(pevent.sample.tid)) : -1;
    decoder->tid = decoder->jvmdump->get_java_tid(sys_tid);
    record.switch_out(decoder->loss);
    record.switch_in(decoder->tid, decoder->time, decoder->loss);
  }
  record.switch_in(decoder->tid, decoder->time, decoder->loss);
  decoder->loss = false;
  return;
}

static void ptjvm_add_ic(struct ptjvm_decoder *decoder,
                          uint64_t src, uint64_t dest) {
  if (!decoder || !decoder->ics || !decoder->image)
    return;

  jit_section *section;
  if (jit_image_find(decoder->image, &section, src) < 0)
    return;

  (*decoder->ics)[make_pair(src, section)] = dest;

  jit_section_put(section);

  return;
}

static void ptjvm_clear_ic(struct ptjvm_decoder *decoder, uint64_t src) {
  if (!decoder || !decoder->ics || !decoder->image)
    return;

  jit_section *section;
  if (jit_image_find(decoder->image, &section, src) < 0)
    return;

  auto iter = decoder->ics->find(make_pair(src, section));
  if (iter == decoder->ics->end())
    return;
  decoder->ics->erase(iter);

  jit_section_put(section);

  return;
}

static bool ptjvm_get_ic(struct ptjvm_decoder *decoder,
                          uint64_t &ip, struct jit_section *section) {
  if (!decoder || !decoder->ics || !decoder->image)
    return false;

  auto iter = decoder->ics->find(make_pair(ip, section));
  if (iter == decoder->ics->end())
    return false;
  ip = iter->second;
  return true;
}

static void ptjvm_dump_event(struct ptjvm_decoder *decoder,
                             TraceDataRecord &record) {
  if (!decoder || !decoder->jvmdump)
    return;

  while (true) {
    JvmDumpDecoder::DumpInfoType type;
    JvmDumpDecoder *dumper = decoder->jvmdump;
    const void *data = nullptr;
    type = dumper->dumper_event(decoder->time, decoder->tid, data);
    if (type == JvmDumpDecoder::_interpreter_info) {
      continue;
    } else if (type == JvmDumpDecoder::_compiled_method_load ||
               type == JvmDumpDecoder::_dynamic_code_generated) {
      jit_section *section = (jit_section *)data;
      int errcode = jit_image_add(decoder->image, section);
      if (errcode < 0) {
        fprintf(stderr, "Ptjvm decoder: fail to add section.\n");
      }
      continue;
    } else if (type == JvmDumpDecoder::_compiled_method_unload) {
      auto cmu = (JvmDumpDecoder::CompiledMethodUnloadInfo *)data;
      jit_image_remove(decoder->image, cmu->code_begin);
    } else if (type == JvmDumpDecoder::_method_entry) {
      record.add_method_desc(*(MethodDesc *)data);
    } else if (type == JvmDumpDecoder::_method_exit) {
      // record.add_method_desc(*((MethodDesc *)data));
    } else if (type == JvmDumpDecoder::_illegal) {
      break;
    } else if (type == JvmDumpDecoder::_thread_start) {
      continue;
    } else if (type == JvmDumpDecoder::_no_thing) {
      continue;
    } else if (type == JvmDumpDecoder::_inline_cache_add) {
      auto ic = (JvmDumpDecoder::InlineCacheAdd *)data;
      ptjvm_add_ic(decoder, ic->src, ic->dest);
    } else if (type == JvmDumpDecoder::_inline_cache_clear) {
      auto ic = (JvmDumpDecoder::InlineCacheClear *)data;
      ptjvm_clear_ic(decoder, ic->src);
    } else {
      continue;
    }
  }
  return;
}

static int check_erratum_skd022(struct ptjvm_decoder *decoder) {
  struct pt_insn_ext iext;
  struct pt_insn insn;
  int errcode;

  if (!decoder)
    return -pte_internal;

  insn.mode = decoder->mode;
  insn.ip = decoder->ip;

  errcode = pt_insn_decode(&insn, &iext, decoder->image);
  if (errcode < 0)
    return 0;

  switch (iext.iclass) {
  default:
    return 0;

  case PTI_INST_VMLAUNCH:
  case PTI_INST_VMRESUME:
    return 1;
  }
}

static inline int handle_erratum_skd022(struct ptjvm_decoder *decoder) {
  struct pt_event *ev;
  uint64_t ip;
  int errcode;

  if (!decoder)
    return -pte_internal;

  errcode = check_erratum_skd022(decoder);
  if (errcode <= 0)
    return errcode;

  /* We turn the async disable into a sync disable.  It will be processed
   * after decoding the instruction.
   */
  ev = &decoder->event;

  ip = ev->variant.async_disabled.ip;

  ev->type = ptev_disabled;
  ev->variant.disabled.ip = ip;

  return 1;
}

/* Supported address range configurations. */
enum pt_addr_cfg {
  pt_addr_cfg_disabled = 0,
  pt_addr_cfg_filter = 1,
  pt_addr_cfg_stop = 2
};

/* The maximum number of filter addresses that fit into the configuration. */
static inline size_t pt_filter_addr_ncfg(void) {
  return (sizeof(struct pt_conf_addr_filter) -
          offsetof(struct pt_conf_addr_filter, addr0_a)) /
         (2 * sizeof(uint64_t));
}

uint32_t pt_filter_addr_cfg(const struct pt_conf_addr_filter *filter,
                            uint8_t n) {
  if (!filter)
    return 0u;

  if (pt_filter_addr_ncfg() <= n)
    return 0u;

  return (filter->config.addr_cfg >> (4 * n)) & 0xf;
}

uint64_t pt_filter_addr_a(const struct pt_conf_addr_filter *filter, uint8_t n) {
  const uint64_t *addr;

  if (!filter)
    return 0ull;

  if (pt_filter_addr_ncfg() <= n)
    return 0ull;

  addr = &filter->addr0_a;
  return addr[2 * n];
}

uint64_t pt_filter_addr_b(const struct pt_conf_addr_filter *filter, uint8_t n) {
  const uint64_t *addr;

  if (!filter)
    return 0ull;

  if (pt_filter_addr_ncfg() <= n)
    return 0ull;

  addr = &filter->addr0_a;
  return addr[(2 * n) + 1];
}

static int pt_filter_check_cfg_filter(const struct pt_conf_addr_filter *filter,
                                      uint64_t addr) {
  uint8_t n;

  if (!filter)
    return -pte_internal;

  for (n = 0; n < pt_filter_addr_ncfg(); ++n) {
    uint64_t addr_a, addr_b;
    uint32_t addr_cfg;

    addr_cfg = pt_filter_addr_cfg(filter, n);
    if (addr_cfg != pt_addr_cfg_filter)
      continue;

    addr_a = pt_filter_addr_a(filter, n);
    addr_b = pt_filter_addr_b(filter, n);

    /* Note that both A and B are inclusive. */
    if ((addr_a <= addr) && (addr <= addr_b))
      return 1;
  }

  /* No filter hit.  If we have at least one FilterEn filter, this means
   * that tracing is disabled; otherwise, tracing is enabled.
   */
  for (n = 0; n < pt_filter_addr_ncfg(); ++n) {
    uint32_t addr_cfg;

    addr_cfg = pt_filter_addr_cfg(filter, n);
    if (addr_cfg == pt_addr_cfg_filter)
      return 0;
  }

  return 1;
}

static int pt_filter_check_cfg_stop(const struct pt_conf_addr_filter *filter,
                                    uint64_t addr) {
  uint8_t n;

  if (!filter)
    return -pte_internal;

  for (n = 0; n < pt_filter_addr_ncfg(); ++n) {
    uint64_t addr_a, addr_b;
    uint32_t addr_cfg;

    addr_cfg = pt_filter_addr_cfg(filter, n);
    if (addr_cfg != pt_addr_cfg_stop)
      continue;

    addr_a = pt_filter_addr_a(filter, n);
    addr_b = pt_filter_addr_b(filter, n);

    /* Note that both A and B are inclusive. */
    if ((addr_a <= addr) && (addr <= addr_b))
      return 0;
  }

  return 1;
}

static int pt_filter_addr_check(const struct pt_conf_addr_filter *filter,
                                uint64_t addr) {
  int status;

  status = pt_filter_check_cfg_stop(filter, addr);
  if (status <= 0)
    return status;

  return pt_filter_check_cfg_filter(filter, addr);
}

static int pt_insn_at_skl014(const struct pt_event *ev,
                             const struct pt_insn *insn,
                             const struct pt_insn_ext *iext,
                             const struct pt_config *config) {
  uint64_t ip;
  int status;

  if (!ev || !insn || !iext || !config)
    return -pte_internal;

  if (!ev->ip_suppressed)
    return 0;

  switch (insn->iclass) {
  case ptic_call:
  case ptic_jump:
    /* The erratum only applies to unconditional direct branches. */
    if (!iext->variant.branch.is_direct)
      break;

    /* Check the filter against the branch target. */
    ip = insn->ip;
    ip += insn->size;
    ip += (uint64_t)(int64_t)iext->variant.branch.displacement;

    status = pt_filter_addr_check(&config->addr_filter, ip);
    if (status <= 0) {
      if (status < 0)
        return status;

      return 1;
    }
    break;

  default:
    break;
  }

  return 0;
}

static int pt_insn_at_disabled_event(const struct pt_event *ev,
                                     const struct pt_insn *insn,
                                     const struct pt_insn_ext *iext,
                                     const struct pt_config *config) {
  if (!ev || !insn || !iext || !config)
    return -pte_internal;

  if (ev->ip_suppressed) {
    if (pt_insn_is_far_branch(insn, iext) || pt_insn_changes_cpl(insn, iext) ||
        pt_insn_changes_cr3(insn, iext))
      return 1;

    /* If we don't have a filter configuration we assume that no
     * address filters were used and the erratum does not apply.
     *
     * We might otherwise disable tracing too early.
     */
    if (config->addr_filter.config.addr_cfg && config->errata.skl014 &&
        pt_insn_at_skl014(ev, insn, iext, config))
      return 1;
  } else {
    switch (insn->iclass) {
    case ptic_ptwrite:
    case ptic_other:
      break;

    case ptic_call:
    case ptic_jump:
      /* If we got an IP with the disabled event, we may
       * ignore direct branches that go to a different IP.
       */
      if (iext->variant.branch.is_direct) {
        uint64_t ip;

        ip = insn->ip;
        ip += insn->size;
        ip += (uint64_t)(int64_t)iext->variant.branch.displacement;

        if (ip != ev->variant.disabled.ip)
          break;
      }

    case ptic_return:
    case ptic_far_call:
    case ptic_far_return:
    case ptic_far_jump:
    case ptic_indirect:
    case ptic_cond_jump:
      return 1;

    case ptic_unknown:
      return -pte_bad_insn;
    }
  }

  return 0;
}

static inline int event_pending(struct ptjvm_decoder *decoder) {
  int status;

  if (!decoder)
    return -pte_invalid;

  if (decoder->process_event)
    return 1;

  status = decoder->status;
  if (!(status & pts_event_pending))
    return 0;
  status = pt_qry_event(decoder->qry, &decoder->event, sizeof(decoder->event));
  if (status < 0)
    return status;

  decoder->process_event = 1;
  decoder->status = status;
  return 1;
}

static int pt_insn_status(const struct ptjvm_decoder *decoder, int flags) {
  int status;

  if (!decoder)
    return -pte_internal;

  status = decoder->status;

  /* Indicate whether tracing is disabled or enabled.
   *
   * This duplicates the indication in struct pt_insn and covers the case
   * where we indicate the status after synchronizing.
   */
  if (!decoder->enabled)
    flags |= pts_ip_suppressed;

  /* Forward end-of-trace indications.
   *
   * Postpone it as long as we're still processing events, though.
   */
  if ((status & pts_eos) && !decoder->process_event)
    flags |= pts_eos;

  return flags;
}

enum {
  /* The maximum number of steps to take when determining whether the
   * event location can be reached.
   */
  bdm64_max_steps = 0x100
};

/* Try to work around erratum BDM64.
 *
 * If we got a transaction abort immediately following a branch that produced
 * trace, the trace for that branch might have been corrupted.
 *
 * Returns a positive integer if the erratum was handled.
 * Returns zero if the erratum does not seem to apply.
 * Returns a negative error code otherwise.
 */
static int handle_erratum_bdm64(struct ptjvm_decoder *decoder,
                                const struct pt_event *ev,
                                const struct pt_insn *insn,
                                const struct pt_insn_ext *iext) {
  int status;

  if (!decoder || !ev || !insn || !iext)
    return -pte_internal;

  /* This only affects aborts. */
  if (!ev->variant.tsx.aborted)
    return 0;

  /* This only affects branches. */
  if (!pt_insn_is_branch(insn, iext))
    return 0;

  /* Let's check if we can reach the event location from here.
   *
   * If we can, let's assume the erratum did not hit.  We might still be
   * wrong but we're not able to tell.
   */
  status = pt_insn_range_is_contiguous(decoder->ip, ev->variant.tsx.ip,
                                       decoder->mode, decoder->image,
                                       bdm64_max_steps);
  if (status > 0)
    return 0;

  /* We can't reach the event location.  This could either mean that we
   * stopped too early (and status is zero) or that the erratum hit.
   *
   * We assume the latter and pretend that the previous branch brought us
   * to the event location, instead.
   */
  decoder->ip = ev->variant.tsx.ip;

  return 1;
}

static inline int pt_insn_postpone_tsx(struct ptjvm_decoder *decoder,
                                       const struct pt_insn *insn,
                                       const struct pt_insn_ext *iext,
                                       const struct pt_event *ev) {
  int status;

  if (!decoder || !ev)
    return -pte_internal;

  if (ev->ip_suppressed)
    return 0;

  if (insn && iext && decoder->config.errata.bdm64) {
    status = handle_erratum_bdm64(decoder, ev, insn, iext);
    if (status < 0)
      return status;
  }

  if (decoder->ip != ev->variant.tsx.ip)
    return 1;

  return 0;
}

static int pt_insn_check_ip_event(struct ptjvm_decoder *decoder,
                                  const struct pt_insn *insn,
                                  const struct pt_insn_ext *iext) {
  struct pt_event *ev;
  int status;

  if (!decoder)
    return -pte_internal;

  status = event_pending(decoder);
  if (status <= 0) {
    if (status < 0)
      return status;

    return pt_insn_status(decoder, 0);
  }

  ev = &decoder->event;
  switch (ev->type) {
  case ptev_disabled:
    break;

  case ptev_enabled:
    return pt_insn_status(decoder, pts_event_pending);

  case ptev_async_disabled:
    if (ev->variant.async_disabled.at != decoder->ip)
      break;

    if (decoder->config.errata.skd022) {
      int errcode;

      errcode = handle_erratum_skd022(decoder);
      if (errcode != 0) {
        if (errcode < 0)
          return errcode;

        /* If the erratum applies, we postpone the
         * modified event to the next call to
         * pt_insn_next().
         */
        break;
      }
    }

    return pt_insn_status(decoder, pts_event_pending);

  case ptev_tsx:
    status = pt_insn_postpone_tsx(decoder, insn, iext, ev);
    if (status != 0) {
      if (status < 0)
        return status;

      break;
    }

    return pt_insn_status(decoder, pts_event_pending);

  case ptev_async_branch:
    if (ev->variant.async_branch.from != decoder->ip)
      break;

    return pt_insn_status(decoder, pts_event_pending);

  case ptev_overflow:
    return pt_insn_status(decoder, pts_event_pending);

  case ptev_exec_mode:
    if (!ev->ip_suppressed && ev->variant.exec_mode.ip != decoder->ip)
      break;

    return pt_insn_status(decoder, pts_event_pending);

  case ptev_paging:
    if (decoder->enabled)
      break;

    return pt_insn_status(decoder, pts_event_pending);

  case ptev_async_paging:
    if (!ev->ip_suppressed && ev->variant.async_paging.ip != decoder->ip)
      break;

    return pt_insn_status(decoder, pts_event_pending);

  case ptev_vmcs:
    if (decoder->enabled)
      break;

    return pt_insn_status(decoder, pts_event_pending);

  case ptev_async_vmcs:
    if (!ev->ip_suppressed && ev->variant.async_vmcs.ip != decoder->ip)
      break;

    return pt_insn_status(decoder, pts_event_pending);

  case ptev_stop:
    return pt_insn_status(decoder, pts_event_pending);

  case ptev_exstop:
    if (!ev->ip_suppressed && decoder->enabled &&
        decoder->ip != ev->variant.exstop.ip)
      break;

    return pt_insn_status(decoder, pts_event_pending);

  case ptev_mwait:
    if (!ev->ip_suppressed && decoder->enabled &&
        decoder->ip != ev->variant.mwait.ip)
      break;

    return pt_insn_status(decoder, pts_event_pending);

  case ptev_pwre:
  case ptev_pwrx:
    return pt_insn_status(decoder, pts_event_pending);

  case ptev_ptwrite:
    /* Any event binding to the current PTWRITE instruction is
     * handled in pt_insn_check_insn_event().
     *
     * Any subsequent ptwrite event binds to a different instruction
     * and must wait until the next iteration - as long as tracing
     * is enabled.
     *
     * When tracing is disabled, we forward all ptwrite events
     * immediately to the user.
     */
    if (decoder->enabled)
      break;

    return pt_insn_status(decoder, pts_event_pending);

  case ptev_tick:
  case ptev_cbr:
  case ptev_mnt:
    return pt_insn_status(decoder, pts_event_pending);
  }

  return pt_insn_status(decoder, 0);
}

static int pt_insn_postpone(struct ptjvm_decoder *decoder,
                            const struct pt_insn *insn,
                            const struct pt_insn_ext *iext) {
  if (!decoder || !insn || !iext)
    return -pte_internal;

  if (!decoder->process_insn) {
    decoder->process_insn = 1;
    decoder->insn = *insn;
    decoder->iext = *iext;
  }

  return pt_insn_status(decoder, pts_event_pending);
}

static int pt_insn_check_insn_event(struct ptjvm_decoder *decoder,
                                    const struct pt_insn *insn,
                                    const struct pt_insn_ext *iext) {
  struct pt_event *ev;
  int status;

  if (!decoder)
    return -pte_internal;

  status = event_pending(decoder);
  if (status <= 0)
    return status;

  ev = &decoder->event;
  switch (ev->type) {
  case ptev_enabled:
  case ptev_overflow:
  case ptev_async_paging:
  case ptev_async_vmcs:
  case ptev_async_disabled:
  case ptev_async_branch:
  case ptev_exec_mode:
  case ptev_tsx:
  case ptev_stop:
  case ptev_exstop:
  case ptev_mwait:
  case ptev_pwre:
  case ptev_pwrx:
  case ptev_tick:
  case ptev_cbr:
  case ptev_mnt:
    /* We're only interested in events that bind to instructions. */
    return 0;

  case ptev_disabled:
    status = pt_insn_at_disabled_event(ev, insn, iext, &decoder->config);
    if (status <= 0)
      return status;

    /* We're at a synchronous disable event location.
     *
     * Let's determine the IP at which we expect tracing to resume.
     */
    status = pt_insn_next_ip(&decoder->ip, insn, iext);
    if (status < 0) {
      /* We don't know the IP on error. */
      decoder->ip = 0ull;

      /* For indirect calls, assume that we return to the next
       * instruction.
       *
       * We only check the instruction class, not the
       * is_direct property, since direct calls would have
       * been handled by pt_insn_nex_ip() or would have
       * provoked a different error.
       */
      if (status != -pte_bad_query)
        return status;

      switch (insn->iclass) {
      case ptic_call:
      case ptic_far_call:
        decoder->ip = insn->ip + insn->size;
        break;

      default:
        break;
      }
    }

    break;

  case ptev_paging:
    /* We bind at most one paging event to an instruction. */
    if (decoder->bound_paging)
      return 0;

    if (!pt_insn_binds_to_pip(insn, iext))
      return 0;

    /* We bound a paging event.  Make sure we do not bind further
     * paging events to this instruction.
     */
    decoder->bound_paging = 1;

    return pt_insn_postpone(decoder, insn, iext);

  case ptev_vmcs:
    /* We bind at most one vmcs event to an instruction. */
    if (decoder->bound_vmcs)
      return 0;

    if (!pt_insn_binds_to_vmcs(insn, iext))
      return 0;

    /* We bound a vmcs event.  Make sure we do not bind further vmcs
     * events to this instruction.
     */
    decoder->bound_vmcs = 1;

    return pt_insn_postpone(decoder, insn, iext);

  case ptev_ptwrite:
    /* We bind at most one ptwrite event to an instruction. */
    if (decoder->bound_ptwrite)
      return 0;

    if (ev->ip_suppressed) {
      if (!pt_insn_is_ptwrite(insn, iext))
        return 0;

      /* Fill in the event IP.  Our users will need them to
       * make sense of the PTWRITE payload.
       */
      ev->variant.ptwrite.ip = decoder->ip;
      ev->ip_suppressed = 0;
    } else {
      /* The ptwrite event contains the IP of the ptwrite
       * instruction (CLIP) unlike most events that contain
       * the IP of the first instruction that did not complete
       * (NLIP).
       *
       * It's easier to handle this case here, as well.
       */
      if (decoder->ip != ev->variant.ptwrite.ip)
        return 0;
    }

    /* We bound a ptwrite event.  Make sure we do not bind further
     * ptwrite events to this instruction.
     */
    decoder->bound_ptwrite = 1;

    return pt_insn_postpone(decoder, insn, iext);
  }

  return pt_insn_status(decoder, pts_event_pending);
}

static int pt_insn_clear_postponed(struct ptjvm_decoder *decoder) {
  if (!decoder)
    return -pte_internal;

  decoder->process_insn = 0;
  decoder->bound_paging = 0;
  decoder->bound_vmcs = 0;
  decoder->bound_ptwrite = 0;

  return 0;
}

/* Query an indirect branch.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_insn_indirect_branch(struct ptjvm_decoder *decoder,
                                   uint64_t *ip) {
  uint64_t evip;
  int status, errcode;

  if (!decoder || !decoder->qry)
    return -pte_internal;

  evip = decoder->ip;

  status = pt_qry_indirect_branch(decoder->qry, ip);
  if (status < 0)
    return status;

  return status;
}

/* Query a conditional branch.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_insn_cond_branch(struct ptjvm_decoder *decoder, int *taken) {
  int status, errcode;

  if (!decoder || !decoder->qry)
    return -pte_internal;

  status = pt_qry_cond_branch(decoder->qry, taken);
  if (status < 0)
    return status;

  return status;
}

static int pt_insn_proceed(struct ptjvm_decoder *decoder,
                           const struct pt_insn *insn,
                           const struct pt_insn_ext *iext) {
  if (!decoder || !decoder->qry || !insn || !iext)
    return -pte_internal;

  decoder->ip += insn->size;
  switch (insn->iclass) {
  case ptic_ptwrite:
  case ptic_other:
    return 0;

  case ptic_cond_jump: {
    int status, taken;

    status = pt_insn_cond_branch(decoder, &taken);
    if (status < 0)
      return status;

    decoder->status = status;
    if (!taken)
      return 0;

    break;
  }

  case ptic_call:
    break;

  /* return compression is disabled */
  case ptic_return:
    break;

  case ptic_jump:
  case ptic_far_call:
  case ptic_far_return:
  case ptic_far_jump:
  case ptic_indirect:
    break;

  case ptic_unknown:
    return -pte_bad_insn;
  }

  /* Process a direct or indirect branch.
   *
   * This combines calls, uncompressed returns, taken conditional jumps,
   * and all flavors of far transfers.
   */
  if (iext->variant.branch.is_direct)
    decoder->ip += (uint64_t)(int64_t)iext->variant.branch.displacement;
  else {
    int status;

    status = pt_insn_indirect_branch(decoder, &decoder->ip);

    if (status < 0)
      return status;

    decoder->status = status;

    /* We do need an IP to proceed. */
    if (status & pts_ip_suppressed)
      return -pte_noip;
  }

  return 0;
}

/* Proceed past a postponed instruction.
 *
 * Returns zero on success, a negative pt_error_code otherwise.
 */
static int pt_insn_proceed_postponed(struct ptjvm_decoder *decoder) {
  int status;

  if (!decoder)
    return -pte_internal;

  if (!decoder->process_insn)
    return -pte_internal;

  /* There's nothing to do if tracing got disabled. */
  if (!decoder->enabled)
    return pt_insn_clear_postponed(decoder);

  status = pt_insn_proceed(decoder, &decoder->insn, &decoder->iext);
  if (status < 0)
    return status;

  return pt_insn_clear_postponed(decoder);
}

static int pt_insn_process_enabled(struct ptjvm_decoder *decoder) {
  struct pt_event *ev;

  if (!decoder)
    return -pte_internal;

  ev = &decoder->event;

  /* This event can't be a status update. */
  if (ev->status_update)
    return -pte_bad_context;

  /* We must have an IP in order to start decoding. */
  if (ev->ip_suppressed)
    return -pte_noip;

  // /* We must currently be disabled. */
  // if (decoder->enabled)
  //   return -pte_bad_context;

  decoder->ip = ev->variant.enabled.ip;
  decoder->enabled = 1;

  return 0;
}

static int pt_insn_process_disabled(struct ptjvm_decoder *decoder) {
  struct pt_event *ev;

  if (!decoder)
    return -pte_internal;

  ev = &decoder->event;

  /* This event can't be a status update. */
  if (ev->status_update)
    return -pte_bad_context;

  // /* We must currently be enabled. */
  // if (!decoder->enabled)
  //   return -pte_bad_context;

  /* We preserve @decoder->ip.  This is where we expect tracing to resume
   * and we'll indicate that on the subsequent enabled event if tracing
   * actually does resume from there.
   */
  decoder->enabled = 0;

  return 0;
}

static int pt_insn_process_async_branch(struct ptjvm_decoder *decoder) {
  struct pt_event *ev;

  if (!decoder)
    return -pte_internal;

  ev = &decoder->event;

  /* This event can't be a status update. */
  if (ev->status_update)
    return -pte_bad_context;

  // /* Tracing must be enabled in order to make sense of the event. */
  // if (!decoder->enabled)
  //   return -pte_bad_context;

  decoder->ip = ev->variant.async_branch.to;

  return 0;
}

static int pt_insn_process_paging(struct ptjvm_decoder *decoder) {
  uint64_t cr3;
  int errcode;

  if (!decoder)
    return -pte_internal;

  cr3 = decoder->event.variant.paging.cr3;
  if (decoder->asid.cr3 != cr3) {
    decoder->asid.cr3 = cr3;
  }

  return 0;
}

static int pt_insn_process_overflow(struct ptjvm_decoder *decoder) {
  struct pt_event *ev;

  if (!decoder)
    return -pte_internal;

  ev = &decoder->event;

  /* This event can't be a status update. */
  if (ev->status_update)
    return -pte_bad_context;

  /* If the IP is suppressed, the overflow resolved while tracing was
   * disabled.  Otherwise it resolved while tracing was enabled.
   */
  if (ev->ip_suppressed) {
    /* Tracing is disabled.
     *
     * It doesn't make sense to preserve the previous IP.  This will
     * just be misleading.  Even if tracing had been disabled
     * before, as well, we might have missed the re-enable in the
     * overflow.
     */
    decoder->enabled = 0;
    decoder->ip = 0ull;
  } else {
    /* Tracing is enabled and we're at the IP at which the overflow
     * resolved.
     */
    decoder->ip = ev->variant.overflow.ip;
    decoder->enabled = 1;
  }

  /* We don't know the TSX state.  Let's assume we execute normally.
   *
   * We also don't know the execution mode.  Let's keep what we have
   * in case we don't get an update before we have to decode the next
   * instruction.
   */
  decoder->speculative = 0;

  return 0;
}

static int pt_insn_process_exec_mode(struct ptjvm_decoder *decoder) {
  enum pt_exec_mode mode;
  struct pt_event *ev;

  if (!decoder)
    return -pte_internal;

  ev = &decoder->event;
  mode = ev->variant.exec_mode.mode;

  /* Use status update events to diagnose inconsistencies. */
  if (ev->status_update && decoder->enabled && decoder->mode != ptem_unknown &&
      decoder->mode != mode)
    return -pte_bad_status_update;

  decoder->mode = mode;

  return 0;
}

static int pt_insn_process_tsx(struct ptjvm_decoder *decoder) {
  if (!decoder)
    return -pte_internal;

  decoder->speculative = decoder->event.variant.tsx.speculative;

  return 0;
}

static int pt_insn_process_stop(struct ptjvm_decoder *decoder) {
  struct pt_event *ev;

  if (!decoder)
    return -pte_internal;

  ev = &decoder->event;

  /* This event can't be a status update. */
  if (ev->status_update)
    return -pte_bad_context;

  // /* Tracing is always disabled before it is stopped. */
  // if (decoder->enabled)
  //   return -pte_bad_context;

  return 0;
}

static int pt_insn_process_vmcs(struct ptjvm_decoder *decoder) {
  uint64_t vmcs;
  int errcode;

  if (!decoder)
    return -pte_internal;

  vmcs = decoder->event.variant.vmcs.base;
  if (decoder->asid.vmcs != vmcs) {
    decoder->asid.vmcs = vmcs;
  }
  return 0;
}

static int pt_insn_event(struct ptjvm_decoder *decoder) {
  struct pt_event *ev;
  int status;

  if (!decoder)
    return -pte_invalid;

  /* We must currently process an event. */
  if (!decoder->process_event)
    return -pte_bad_query;

  ev = &decoder->event;
  switch (ev->type) {
  default:
    /* This is not a user event.
     *
     * We either indicated it wrongly or the user called
     * pt_insn_event() without a pts_event_pending indication.
     */
    return -pte_bad_query;

  case ptev_enabled:
    /* Indicate that tracing resumes from the IP at which tracing
     * had been disabled before (with some special treatment for
     * calls).
     */
    if (decoder->ip == ev->variant.enabled.ip)
      ev->variant.enabled.resumed = 1;

    status = pt_insn_process_enabled(decoder);
    if (status < 0)
      return status;

    break;

  case ptev_async_disabled:
    if (!ev->ip_suppressed && decoder->ip != ev->variant.async_disabled.at)
      return -pte_bad_query;

  case ptev_disabled:
    status = pt_insn_process_disabled(decoder);
    if (status < 0)
      return status;

    break;

  case ptev_async_branch:
    if (decoder->ip != ev->variant.async_branch.from)
      return -pte_bad_query;

    status = pt_insn_process_async_branch(decoder);
    if (status < 0)
      return status;

    break;

  case ptev_async_paging:
    if (!ev->ip_suppressed && decoder->ip != ev->variant.async_paging.ip)
      return -pte_bad_query;

  case ptev_paging:
    status = pt_insn_process_paging(decoder);
    if (status < 0)
      return status;

    break;

  case ptev_async_vmcs:
    if (!ev->ip_suppressed && decoder->ip != ev->variant.async_vmcs.ip)
      return -pte_bad_query;

  case ptev_vmcs:
    status = pt_insn_process_vmcs(decoder);
    if (status < 0)
      return status;

    break;

  case ptev_overflow:
    status = pt_insn_process_overflow(decoder);
    if (status < 0)
      return status;

    break;

  case ptev_exec_mode:
    status = pt_insn_process_exec_mode(decoder);
    if (status < 0)
      return status;

    break;

  case ptev_tsx:
    status = pt_insn_process_tsx(decoder);
    if (status < 0)
      return status;

    break;

  case ptev_stop:
    status = pt_insn_process_stop(decoder);
    if (status < 0)
      return status;

    break;

  case ptev_exstop:
    if (!ev->ip_suppressed && decoder->enabled &&
        decoder->ip != ev->variant.exstop.ip)
      return -pte_bad_query;

    break;

  case ptev_mwait:
    if (!ev->ip_suppressed && decoder->enabled &&
        decoder->ip != ev->variant.mwait.ip)
      return -pte_bad_query;

    break;

  case ptev_pwre:
  case ptev_pwrx:
  case ptev_ptwrite:
  case ptev_tick:
  case ptev_cbr:
  case ptev_mnt:
    break;
  }

  /* This completes processing of the current event. */
  decoder->process_event = 0;

  /* If we just handled an instruction event, check for further events
   * that bind to this instruction.
   *
   * If we don't have further events, proceed beyond the instruction so we
   * can check for IP events, as well.
   */
  if (decoder->process_insn) {
    status = pt_insn_check_insn_event(decoder, &decoder->insn, &decoder->iext);

    if (status != 0) {
      if (status < 0)
        return status;

      if (status & pts_event_pending)
        return status;
    }

    /* Proceed to the next instruction. */
    status = pt_insn_proceed_postponed(decoder);
    if (status < 0)
      return status;
  }

  /* Indicate further events that bind to the same IP. */
  return pt_insn_check_ip_event(decoder, NULL, NULL);
}

static int drain_insn_events(struct ptjvm_decoder *decoder, int status) {
  while (status & pts_event_pending) {
    status = pt_insn_event(decoder);
    if (status < 0)
      return status;
  }
  return status;
}

static int handle_compiled_code_result(struct ptjvm_decoder *decoder,
                                        TraceDataRecord &record,
                                        jit_section *section, bool &entry) {
  if (!decoder || !section)
    return -pte_internal;

  PCStackInfo *pcinfo;
  /* error: errcode < 0, no debug info: errcode = 0, else find debug info */
  int find = jit_section_read_debug_info(section, decoder->ip, pcinfo);
  if (find <= 0) {
    return find;
  } else if (pcinfo != decoder->last_pcinfo || 
        (pcinfo == decoder->last_pcinfo && decoder->ip < decoder->last_ip)) {
    decoder->last_pcinfo = pcinfo;
    decoder->last_ip = decoder->ip;
    int method_index = pcinfo->methods[0];
    const CompiledMethodDesc *cmd = jit_section_cmd(section);
    if (!cmd)
      return 0;
    string klass_name, name, sig;
    cmd->get_method_desc(method_index, klass_name, name, sig);
    const Klass* klass = decoder->analyser->getKlass(klass_name);
    if (!klass)
      return 0;
    const Method *method = klass->getMethod(name + sig);
    if (!method)
      return 0;
    record.add_jitcode(decoder->time, section, pcinfo, entry);
    entry = false;
  }
  return 0;
}

static int pt_insn_reset(struct ptjvm_decoder *decoder) {
  if (!decoder)
    return -pte_internal;

  decoder->process_insn = 0;
  decoder->bound_paging = 0;
  decoder->bound_vmcs = 0;
  decoder->bound_ptwrite = 0;

  return 0;
}

static int pt_insn_start(struct ptjvm_decoder *decoder) {
  if (!decoder)
    return -pte_internal;

  int status = decoder->status;

  if (!(status & pts_ip_suppressed))
    decoder->enabled = 1;

  return pt_insn_check_ip_event(decoder, NULL, NULL);
}

static int handle_compiled_code(struct ptjvm_decoder *decoder,
                                TraceDataRecord &record, const char *prog) {
  if (!decoder || !decoder->qry || !decoder->image)
    return -pte_internal;

  int status;
  int errcode;
  bool entry = false;
  struct pt_insn insn;
  struct pt_insn_ext iext;
  jit_section *section = nullptr;

  errcode = pt_insn_reset(decoder);
  if (errcode < 0)
    return errcode;

  status = pt_insn_start(decoder);
  if (status != 0) {
    if (status < 0)
      return status;

    if (status & pts_event_pending) {
      status = drain_insn_events(decoder, status);

      if (status < 0)
        return status;
    }
  }

  for (;;) {
    memset(&insn, 0, sizeof(insn));

    if (decoder->speculative)
      insn.speculative = 1;
    insn.mode = decoder->mode;
    insn.ip = decoder->ip;

    errcode =
        jit_section_read(section, insn.raw, sizeof(insn.raw), decoder->ip);
    if (errcode < 0) {
      if (section) {
        jit_section_put(section);
        section = nullptr;
      }

      errcode = jit_image_find(decoder->image, &section, decoder->ip);
      if (errcode < 0) {
        if (errcode != -pte_nomap)
          fprintf(stderr, "%s: find image error(%d)\n", prog, errcode);
        break;
      }

      const CompiledMethodDesc *cmd = jit_section_cmd(section);
      if (cmd && cmd->get_osr_entry_point() != cmd->get_entry_point() &&
            cmd->get_osr_entry_point() != cmd->get_verified_entry_point() &&
            decoder->ip == cmd->get_osr_entry_point()) {
          record.add_osr_entry();
      }
      if (cmd && (decoder->ip == cmd->get_entry_point() ||
            decoder->ip == cmd->get_verified_entry_point()))
        entry = true;
      else
        entry = false;

      errcode =
          jit_section_read(section, insn.raw, sizeof(insn.raw), decoder->ip);
      if (errcode < 0) {
        fprintf(stderr, "%s: compiled code's section error(%d) (%ld).\n",
                            prog, errcode, section->code_begin);
        break;
      }
    }

    insn.size = (uint8_t)errcode;
    errcode = pt_ild_decode(&insn, &iext);
    if (errcode < 0) {
      fprintf(stderr, "%s: compiled code's ild error(%d) (%ld).\n",
                        prog, errcode, section->code_begin);
      break;
    }
    uint64_t ip = decoder->ip;

    errcode = handle_compiled_code_result(decoder, record, section, entry);
    if (errcode < 0) {
      fprintf(stderr, "%s: compiled code's result error(%d) (%ld).\n",
                        prog, errcode, section->code_begin);
      break;
    }

    status = pt_insn_check_insn_event(decoder, &insn, &iext);
    if (status != 0) {
      if (status < 0)
        break;
      if (status & pts_event_pending) {
        status = drain_insn_events(decoder, status);
        if (status < 0)
          break;
        continue;
      }
    }

    pt_qry_time(decoder->qry, &decoder->time, NULL, NULL);
    ptjvm_sb_event(decoder, record);
    ptjvm_dump_event(decoder, record);

    if (ptjvm_get_ic(decoder, ip, section) && ip != decoder->ip) {
      decoder->ip = ip;
      status = pt_insn_check_ip_event(decoder, &insn, &iext);
      if (status != 0) {
        if (status < 0)
          break;
        if (status & pts_event_pending) {
          status = drain_insn_events(decoder, status);

          if (status < 0)
            break;
        }
      }
      continue;
    }

    errcode = pt_insn_proceed(decoder, &insn, &iext);
    if (errcode < 0) {
      if (decoder->process_event && (decoder->event.type == ptev_disabled || decoder->event.type == ptev_tsx)) {
        fprintf(stderr, "disable(%d): %ld %ld (%ld)\n", errcode,
                  section->code_begin, decoder->ip, decoder->time);
      } else if (errcode != -pte_eos)
        fprintf(stderr, "%s: compiled code's proceed error(%d, %d, %d) %ld %ld (%ld)\n",
                  prog, errcode, decoder->process_event, decoder->event.type,
                  section->code_begin, decoder->ip, decoder->time);
      break;
    } else if (insn.iclass != ptic_cond_jump &&
                iext.variant.branch.is_direct &&
                insn.ip == decoder->ip) {
      break;
    }

    status = pt_insn_check_ip_event(decoder, &insn, &iext);
    if (status != 0) {
      if (status < 0)
        break;
      if (status & pts_event_pending) {
        status = drain_insn_events(decoder, status);
        if (status < 0)
          break;
      }
    }
  }

  if (section)
    jit_section_put(section);
  return status;
}

static int drain_qry_events(struct ptjvm_decoder *decoder) {
  if (!decoder || !decoder->qry)
    return -pte_internal;

  int status = decoder->status;
  decoder->unresolved = false;

  while (status & pts_event_pending) {
    status = event_pending(decoder);
    if (status < 0)
      return status;

    switch (decoder->event.type) {
    default:
      return -pte_bad_query;

    case ptev_enabled:
      status = pt_insn_process_enabled(decoder);
      if (status < 0)
        return status;
      decoder->unresolved = true;
      decoder->process_event = 0;
      return status;

    case ptev_async_disabled:
    case ptev_disabled:
      status = pt_insn_process_disabled(decoder);
      if (status < 0)
        return status;
      break;

    case ptev_async_branch:
      status = pt_insn_process_async_branch(decoder);
      if (status < 0)
        return status;
      decoder->unresolved = true;
      decoder->process_event = 0;
      return status;

    case ptev_async_paging:
    case ptev_paging:
      status = pt_insn_process_paging(decoder);
      if (status < 0)
        return status;
      break;

    case ptev_async_vmcs:
    case ptev_vmcs:
      status = pt_insn_process_vmcs(decoder);
      if (status < 0)
        return status;
      break;

    case ptev_overflow:
      status = pt_insn_process_overflow(decoder);
      if (status < 0)
        return status;
      decoder->unresolved = true;
      decoder->process_event = 0;
      return status;

    case ptev_exec_mode:
      status = pt_insn_process_exec_mode(decoder);
      if (status < 0)
        return status;
      break;

    case ptev_tsx:
      status = pt_insn_process_tsx(decoder);
      if (status < 0)
        return status;
      break;

    case ptev_stop:
      status = pt_insn_process_stop(decoder);
      if (status < 0)
        return status;
      break;

    case ptev_exstop:
    case ptev_mwait:
    case ptev_pwre:
    case ptev_pwrx:
    case ptev_ptwrite:
    case ptev_tick:
    case ptev_cbr:
    case ptev_mnt:
      break;
    }

    /* This completes processing of the current event. */
    decoder->process_event = 0;
    status = decoder->status;
  }

  return status;
}

static int handle_bytecode(struct ptjvm_decoder *decoder,
                           TraceDataRecord &record, Bytecodes::Code bytecode,
                           const char *prog) {
  if (!decoder)
    return -pte_internal;

  int status = decoder->status;
  Bytecodes::Code java_code = bytecode;
  Bytecodes::Code follow_code;
  int errcode;
  Bytecodes::java_bytecode(java_code, follow_code);
  record.add_bytecode(decoder->time, java_code);
  if (follow_code != Bytecodes::_illegal) {
    record.add_bytecode(decoder->time, follow_code);
  }
  if (Bytecodes::is_branch(java_code)) {
    int taken = 0;
    status = drain_qry_events(decoder);
    if (status < 0 || decoder->unresolved) {
      record.add_branch(2);
      return status;
    }
    status = pt_qry_cond_branch(decoder->qry, &taken);
    if (status < 0) {
      record.add_branch(2);
      return status;
    }
    decoder->status = status;
    if (!taken) {
      status = drain_qry_events(decoder);
      if (status < 0 || decoder->unresolved) {
        record.add_branch(2);
        return status;
      }
      status = pt_qry_cond_branch(decoder->qry, &taken);
      if (status < 0) {
        record.add_branch(2);
        return status;
      }
      decoder->status = status;
    }
    /* if taken we query the bytecode branch
     * else we cannot determine */
    if (taken) {
      status = drain_qry_events(decoder);
      if (status < 0 || decoder->unresolved) {
        record.add_branch(2);
        return status;
      }
      status = pt_qry_cond_branch(decoder->qry, &taken);
      if (status < 0)
        return status;
      decoder->status = status;
      record.add_branch((u1)(1 - taken));
    } else
      record.add_branch(2);
  }
  return status;
}

static int ptjvm_result_decode(struct ptjvm_decoder *decoder,
                               TraceDataRecord &record, const char *prog) {
  if (!decoder || !decoder->qry)
    return -pte_internal;

  int status = 0;
  Bytecodes::Code bytecode;
  CodeletsEntry::Codelet codelet =
      CodeletsEntry::entry_match(decoder->ip, bytecode);
  switch (codelet) {
  case (CodeletsEntry::_illegal): {
    status = handle_compiled_code(decoder, record, prog);
    if (status < 0) {
      if (status != -pte_eos) {
        record.switch_out(true);
        fprintf(stderr, "%s: compiled code decode error(%d).\n", prog, status);
      }
      return 0;
    }
    /* might query a non-compiled-code ip */
    codelet = CodeletsEntry::entry_match(decoder->ip, bytecode);
    if (codelet != CodeletsEntry::_illegal)
      return ptjvm_result_decode(decoder, record, prog);
    return status;
  }
  case (CodeletsEntry::_bytecode): {
    status = handle_bytecode(decoder, record, bytecode, prog);
    if (status < 0) {
      if (status != -pte_eos) {
        record.switch_out(true);
        fprintf(stderr, "%s: bytecode decode error(%d).\n", prog, status);
      }
      return 0;
    }

    pt_qry_time(decoder->qry, &decoder->time, NULL, NULL);
    ptjvm_sb_event(decoder, record);
    ptjvm_dump_event(decoder, record);

    if (decoder->unresolved) {
      decoder->unresolved = false;
      status = ptjvm_result_decode(decoder, record, prog);
      if (status < 0)
        return status;
    }
    return status;
  }
  default:
    record.add_codelet(codelet);
    return status;
  }
}

static void reset_decoder(ptjvm_decoder *decoder) {
  if (!decoder)
    return;

  decoder->mode = ptem_unknown;
  decoder->ip = 0ull;
  decoder->status = 0;
  decoder->enabled = 0;
  decoder->process_event = 0;
  decoder->speculative = 0;
  decoder->process_insn = 0;
  decoder->bound_paging = 0;
  decoder->bound_vmcs = 0;
  decoder->bound_ptwrite = 0;

  pt_asid_init(&decoder->asid);
}

static int decode(ptjvm_decoder *decoder, TraceDataRecord &record,
                  const char *prog) {
  if (!decoder || !decoder->qry) {
    return -pte_internal;
  }

  struct pt_query_decoder *qry = decoder->qry;
  int status, taken, errcode;

  for (;;) {
    reset_decoder(decoder);
    status = pt_qry_sync_forward(qry, &decoder->ip);
    if (status < 0) {
      if (status == -pte_eos) {
        break;
      }
      fprintf(stderr, "%s: decode sync error %s.\n", prog,
              pt_errstr(pt_errcode(status)));
      return status;
    }

    decoder->status = status;

    for (;;) {
      status = drain_qry_events(decoder);
      if (status < 0)
        break;

      pt_qry_time(decoder->qry, &decoder->time, NULL, NULL);
      ptjvm_sb_event(decoder, record);
      ptjvm_dump_event(decoder, record);

      if (decoder->unresolved) {
        decoder->unresolved = false;
        status = ptjvm_result_decode(decoder, record, prog);
        if (status < 0)
          break;
        continue;
      }

      status = pt_qry_cond_branch(decoder->qry, &taken);
      if (status < 0) {
        status = pt_qry_indirect_branch(decoder->qry, &decoder->ip);
        if (status < 0)
          break;
        decoder->status = status;
        status = ptjvm_result_decode(decoder, record, prog);
        if (status < 0)
          break;
      } else {
        decoder->status = status;
      }
    }

    if (!status)
      status = -pte_internal;

    /* We're done when we reach the end of the trace stream. */
    if (status == -pte_eos)
      break;
    else {
      fprintf(stderr, "%s: decode error %d time: %lx.\n", prog, status,
              decoder->time);
      record.switch_out(decoder->loss);
    }
  }
  record.switch_out(decoder->loss);
  return status;
}

static int alloc_decoder(struct ptjvm_decoder *decoder,
                         const struct pt_config *conf, const char *prog) {
  struct pt_config config;
  int errcode;

  if (!decoder || !conf || !prog)
    return -pte_internal;

  config = *conf;
  config.flags = decoder->flags;
  decoder->config = *conf;

  decoder->qry = pt_qry_alloc_decoder(&config);
  if (!decoder->qry) {
    fprintf(stderr, "%s: failed to create query decoder.\n", prog);
    return -pte_nomem;
  }

  return 0;
}

static void free_decoder(struct ptjvm_decoder *decoder) {
  if (!decoder)
    return;

  if (decoder->qry)
    pt_qry_free_decoder(decoder->qry);
}

static int get_arg_uint64(uint64_t *value, const char *option, const char *arg,
                          const char *prog) {
  char *rest;

  if (!value || !option || !prog) {
    fprintf(stderr, "%s: internal error.\n", prog ? prog : "?");
    return 0;
  }

  if (!arg || arg[0] == 0 || (arg[0] == '-' && arg[1] == '-')) {
    fprintf(stderr, "%s: %s: missing argument.\n", prog, option);
    return 0;
  }

  errno = 0;
  *value = strtoull(arg, &rest, 0);
  if (errno || *rest) {
    fprintf(stderr, "%s: %s: bad argument: %s.\n", prog, option, arg);
    return 0;
  }

  return 1;
}

static int get_arg_uint32(uint32_t *value, const char *option, const char *arg,
                          const char *prog) {
  uint64_t val;

  if (!get_arg_uint64(&val, option, arg, prog))
    return 0;

  if (val > UINT32_MAX) {
    fprintf(stderr, "%s: %s: value too big: %s.\n", prog, option, arg);
    return 0;
  }

  *value = (uint32_t)val;

  return 1;
}

static int get_arg_uint16(uint16_t *value, const char *option, const char *arg,
                          const char *prog) {
  uint64_t val;

  if (!get_arg_uint64(&val, option, arg, prog))
    return 0;

  if (val > UINT16_MAX) {
    fprintf(stderr, "%s: %s: value too big: %s.\n", prog, option, arg);
    return 0;
  }

  *value = (uint16_t)val;

  return 1;
}

static int get_arg_uint8(uint8_t *value, const char *option, const char *arg,
                         const char *prog) {
  uint64_t val;

  if (!get_arg_uint64(&val, option, arg, prog))
    return 0;

  if (val > UINT8_MAX) {
    fprintf(stderr, "%s: %s: value too big: %s.\n", prog, option, arg);
    return 0;
  }

  *value = (uint8_t)val;

  return 1;
}

static int pt_cpu_parse(struct pt_cpu *cpu, const char *s) {
  const char sep = '/';
  char *endptr;
  long family, model, stepping;

  if (!cpu || !s)
    return -pte_invalid;

  family = strtol(s, &endptr, 0);
  if (s == endptr || *endptr == '\0' || *endptr != sep)
    return -pte_invalid;

  if (family < 0 || family > USHRT_MAX)
    return -pte_invalid;

  /* skip separator */
  s = endptr + 1;

  model = strtol(s, &endptr, 0);
  if (s == endptr || (*endptr != '\0' && *endptr != sep))
    return -pte_invalid;

  if (model < 0 || model > UCHAR_MAX)
    return -pte_invalid;

  if (*endptr == '\0')
    /* stepping was omitted, it defaults to 0 */
    stepping = 0;
  else {
    /* skip separator */
    s = endptr + 1;

    stepping = strtol(s, &endptr, 0);
    if (*endptr != '\0')
      return -pte_invalid;

    if (stepping < 0 || stepping > UCHAR_MAX)
      return -pte_invalid;
  }

  cpu->vendor = pcv_intel;
  cpu->family = (uint16_t)family;
  cpu->model = (uint8_t)model;
  cpu->stepping = (uint8_t)stepping;

  return 0;
}

int ptjvm_decode(TracePart tracepart, TraceDataRecord record,
                 Analyser *analyser) {
  jit_image *image = nullptr;
  struct ptjvm_decoder decoder;
  struct pt_config config;
  int errcode;
  size_t off;
  const char *prog = "Ptjvm decoder";

  if (!tracepart.pt_size || !tracepart.sb_size || !analyser)
    return 0;

  image = jit_image_alloc("jitted-code");
  if (!image) {
    fprintf(stderr, "%s: Fail ti alloc jit image.\n", prog);
    goto err;
  }

  pt_config_init(&config);
  errcode = ptjvm_init_decoder(&decoder, image);
  if (errcode < 0) {
    fprintf(stderr, "%s: Fail initializing decoder: %s.\n", prog,
            pt_errstr(pt_errcode(errcode)));
    goto err;
  }
  decoder.analyser = analyser;
  decoder.pevent.sample_type = attr.sample_type;
  decoder.pevent.time_zero = attr.time_zero;
  decoder.pevent.time_shift = attr.time_shift;
  decoder.pevent.time_mult = attr.time_mult;

  config.cpu = attr.cpu;
  config.mtc_freq = attr.mtc_freq;
  config.nom_freq = attr.nom_freq;
  config.cpuid_0x15_eax = attr.cpuid_0x15_eax;
  config.cpuid_0x15_ebx = attr.cpuid_0x15_ebx;
  config.addr_filter.config.addr_cfg = pt_addr_cfg_filter;
  config.addr_filter.addr0_a = attr.addr0_a;
  config.addr_filter.addr0_b = attr.addr0_b;
  config.flags.variant.query.keep_tcal_on_ovf = 1;
  config.begin = tracepart.pt_buffer;
  config.end = tracepart.pt_buffer + tracepart.pt_size;

  decoder.sideband =
      new SidebandDecoder(tracepart.sb_buffer, tracepart.sb_size);
  decoder.sideband->set_config(decoder.pevent);

  decoder.jvmdump = new JvmDumpDecoder();
  decoder.ics = new map<pair<uint64_t, struct jit_section *>, uint64_t>();
  decoder.loss = tracepart.loss;

  if (config.cpu.vendor) {
    errcode = pt_cpu_errata(&config.errata, &config.cpu);
    if (errcode < 0) {
      fprintf(stderr, "%s: [0, 0: config error: %s]\n", prog,
              pt_errstr(pt_errcode(errcode)));
      goto err;
    }
  }

  errcode = alloc_decoder(&decoder, &config, prog);
  if (errcode < 0) {
    fprintf(stderr, "%s: fail to allocate decoder.\n", prog);
    goto err;
  }

  decode(&decoder, record, prog);

  jit_image_free(image);
  free_decoder(&decoder);
  delete decoder.sideband;
  delete decoder.jvmdump;
  delete decoder.ics;
  return 0;

err:
  jit_image_free(image);
  free_decoder(&decoder);
  delete decoder.sideband;
  delete decoder.jvmdump;
  delete decoder.ics;
  return -1;
}

static size_t get_sample_size(uint64_t sample_type, bool &has_cpu,
                              size_t &cpu_off) {
  size_t size = 0;
  if (sample_type & PERF_SAMPLE_TID) {
    size += 8;
  }

  if (sample_type & PERF_SAMPLE_TIME) {
    size += 8;
  }

  if (sample_type & PERF_SAMPLE_ID) {
    size += 8;
  }

  if (sample_type & PERF_SAMPLE_STREAM_ID) {
    size += 8;
  }

  has_cpu = false;
  if (sample_type & PERF_SAMPLE_CPU) {
    cpu_off = size;
    has_cpu = true;
    size += 8;
  }

  if (sample_type & PERF_SAMPLE_IDENTIFIER) {
    size += 8;
  }
  return size;
}

static int trace_file_handle(FILE *trace, size_t &begin, size_t &end,
                             bool &pt_trace, int &cpu, size_t sample_size,
                             size_t cpu_off) {
  struct perf_event_header header;
  int errcode;

  pt_trace = false;
  if (!trace) {
    return -pte_internal;
  }

  begin = ftell(trace);
  errcode = fread(&header, sizeof(header), 1, trace);
  if (errcode <= 0)
    return errcode;

  if (header.type == PERF_RECORD_AUXTRACE) {
    pt_trace = true;
    struct auxtrace_event aux;
    errcode = fread(&aux, sizeof(aux), 1, trace);
    if (errcode <= 0)
      return errcode;
    cpu = aux.cpu;
    begin = ftell(trace);
    errcode = fseek(trace, aux.size, SEEK_CUR);
    if (errcode)
      return -1;
    end = ftell(trace);
    return 1;
  } else if (header.type == PERF_RECORD_AUX_ADVANCE) {
    pt_trace = true;
    struct aux_advance_event aux;
    errcode = fread(&aux, sizeof(aux), 1, trace);
    if (errcode < 0)
      return errcode;
    cpu = aux.cpu;
    begin = ftell(trace);
    end = begin;
    return 1;
  }

  size_t loc = header.size - sizeof(header) - sample_size + cpu_off;
  errcode = fseek(trace, loc, SEEK_CUR);
  if (errcode)
    return -1;

  errcode = fread(&cpu, sizeof(cpu), 1, trace);
  if (errcode <= 0)
    return -1;

  errcode = fseek(trace, sample_size - cpu_off - sizeof(cpu), SEEK_CUR);
  if (errcode)
    return -1;

  end = ftell(trace);
  return 1;
}

#define SYNC_SPLIT_NUMBER 500

struct SBTracePart {
  list<pair<size_t, size_t>> sb_offsets;
  size_t sb_size = 0;
};

struct PTTracePart {
  bool loss = false;
  list<pair<size_t, size_t>> pt_offsets;
  size_t pt_size = 0;
};

static int ptjvm_fine_split(FILE *trace, PTTracePart &part,
                            list<TracePart> &splits) {
  uint8_t *pt_buffer = nullptr;
  struct pt_config config;
  int errcode;

  pt_config_init(&config);
  config.cpu = attr.cpu;
  config.mtc_freq = attr.mtc_freq;
  config.nom_freq = attr.nom_freq;
  config.cpuid_0x15_eax = attr.cpuid_0x15_eax;
  config.cpuid_0x15_ebx = attr.cpuid_0x15_ebx;
  config.addr_filter.config.addr_cfg = pt_addr_cfg_filter;
  config.addr_filter.addr0_a = attr.addr0_a;
  config.addr_filter.addr0_b = attr.addr0_b;

  pt_buffer = (uint8_t *)malloc(part.pt_size);
  if (!pt_buffer) {
    fprintf(stderr, "Ptjvm split: Unable to alloc pt buffer.\n");
    fclose(trace);
    return -pte_nomem;
  }
  size_t off = 0;
  for (auto offset : part.pt_offsets) {
    fseek(trace, offset.first, SEEK_SET);
    errcode = fread(pt_buffer + off, offset.second - offset.first, 1, trace);
    off += offset.second - offset.first;
  }

  if (config.cpu.vendor) {
    errcode = pt_cpu_errata(&config.errata, &config.cpu);
    if (errcode < 0) {
      fprintf(stderr, "Ptjvm split: [0, 0: config error: %s]\n",
              pt_errstr(pt_errcode(errcode)));
      return errcode;
    }
  }
  config.begin = pt_buffer;
  config.end = pt_buffer + part.pt_size;

  pt_packet_decoder *decoder = pt_pkt_alloc_decoder(&config);
  if (!decoder) {
    fprintf(stderr, "Ptjvm split: fail to allocate decoder.\n");
    free(pt_buffer);
    return -pte_nomem;
  }
  int cnt = 0;
  size_t begin_offset = 0;
  size_t end_offset;
  size_t offset;
  for (;;) {
    int errcode = pt_pkt_sync_forward(decoder);
    if (errcode < 0) {
      if (errcode != -pte_eos) {
        fprintf(stderr, "Ptjvm split: packet decoder split error.\n");
        free(pt_buffer);
        return -1;
      }
      end_offset = part.pt_size;
      size_t split_size = end_offset - begin_offset;
      uint8_t *split_buffer = (uint8_t *)malloc(split_size);
      if (!split_buffer) {
        fprintf(stderr, "Ptjvm split: packet decoder split error.\n");
        return -1;
      }
      memcpy(split_buffer, pt_buffer + begin_offset, split_size);
      splits.push_back(TracePart());
      splits.back().pt_buffer = split_buffer;
      splits.back().pt_size = split_size;
      break;
    }
    errcode = pt_pkt_get_sync_offset(decoder, &offset);
    if (errcode < 0) {
      fprintf(stderr, "Ptjvm split: packet decoder split error.\n");
      return -1;
    }
    if (cnt == 0)
      begin_offset = offset;
    end_offset = offset;
    if (cnt == SYNC_SPLIT_NUMBER) {
      size_t split_size = end_offset - begin_offset;
      uint8_t *split_buffer = (uint8_t *)malloc(split_size);
      if (!split_buffer) {
        fprintf(stderr, "Ptjvm split: packet decoder split error.\n");
        return -1;
      }
      memcpy(split_buffer, pt_buffer + begin_offset, split_size);
      splits.push_back(TracePart());
      splits.back().pt_buffer = split_buffer;
      splits.back().pt_size = split_size;
      cnt = 0;
      continue;
    }
    cnt++;
  }
  return 0;
}

int ptjvm_split(const char *trace_data, map<int, list<TracePart>> &splits) {
  int errcode;
  FILE *trace = fopen(trace_data, "rb");
  if (!trace) {
    fprintf(stderr, "Ptjvm split: trace data cannot be opened.\n");
    return -1;
  }

  errcode = fread(&attr, sizeof(attr), 1, trace);
  if (errcode <= 0) {
    fclose(trace);
    fprintf(stderr, "Ptjvm split: Ivalid trace data format.\n");
    return errcode;
  }

  size_t sample_size, cpu_off;
  bool has_cpu;
  sample_size = get_sample_size(attr.sample_type, has_cpu, cpu_off);
  if (!has_cpu) {
    fclose(trace);
    fprintf(stderr, "Ptjvm split: Tracing with cpu sample disabled.\n");
    return -1;
  }

  map<int, pair<list<PTTracePart>, SBTracePart>> coarse_traceparts;
  while (true) {
    size_t begin, end;
    bool pt_trace;
    int cpu_id;
    errcode = trace_file_handle(trace, begin, end, pt_trace, cpu_id,
                                sample_size, cpu_off);
    if (errcode < 0) {
      fclose(trace);
      fprintf(stderr, "Ptjvm split: Illegal trace data format.\n");
      return -1;
    } else if (errcode == 0) {
      break;
    }
    if (cpu_id >= attr.nr_cpus) {
      fclose(trace);
      fprintf(stderr, "Ptjvm split: Sample has an illegal cpu id %d.\n",
              cpu_id);
      return -1;
    }
    if (pt_trace) {
      if (begin == end) {
        coarse_traceparts[cpu_id].first.push_back(PTTracePart());
        coarse_traceparts[cpu_id].first.back().loss = true;
      } else {
        if (coarse_traceparts[cpu_id].first.empty()) {
          coarse_traceparts[cpu_id].first.push_back(PTTracePart());
        }
        coarse_traceparts[cpu_id].first.back().pt_offsets.push_back(
            make_pair(begin, end));
        coarse_traceparts[cpu_id].first.back().pt_size += (end - begin);
      }
    } else {
      coarse_traceparts[cpu_id].second.sb_offsets.push_back(
          make_pair(begin, end));
      coarse_traceparts[cpu_id].second.sb_size += (end - begin);
    }
  }

  for (auto cpupart : coarse_traceparts) {
    uint8_t *sb_buffer = (uint8_t *)malloc(cpupart.second.second.sb_size);
    if (!sb_buffer) {
      fprintf(stderr, "Ptjvm split: Unable to alloc sb buffer.\n");
      fclose(trace);
      return -pte_nomem;
    }
    size_t off = 0;
    for (auto offset : cpupart.second.second.sb_offsets) {
      fseek(trace, offset.first, SEEK_SET);
      errcode = fread(sb_buffer + off, offset.second - offset.first, 1, trace);
      off += offset.second - offset.first;
    }

    for (auto part : cpupart.second.first) {
      list<TracePart> split_parts;
      ptjvm_fine_split(trace, part, split_parts);
      if (part.loss && !split_parts.empty())
        split_parts.front().loss = true;
      for (auto iter = split_parts.begin(); iter != split_parts.end(); iter++) {
        uint8_t *sb_buffer_p = (uint8_t *)malloc(cpupart.second.second.sb_size);
        if (!sb_buffer_p) {
          fprintf(stderr, "Ptjvm split: Unable to alloc sb buffer.\n");
          fclose(trace);
          return -pte_nomem;
        }
        memcpy(sb_buffer_p, sb_buffer, cpupart.second.second.sb_size);
        iter->sb_buffer = sb_buffer_p;
        iter->sb_size = cpupart.second.second.sb_size;
      }
      splits[cpupart.first].insert(splits[cpupart.first].end(),
                                   split_parts.begin(), split_parts.end());
    }
    free(sb_buffer);
  }
  fclose(trace);
  return 0;
}