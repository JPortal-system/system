#include "structure/PT/jit_section.hpp"
#include "structure/PT/pcDesc.hpp"
#include "structure/PT/scopeDesc.hpp"
#include "structure/PT/compressedStream.hpp"
#include <intel-pt.h>
#include <stdlib.h>

int create_inline_record(CompiledMethodLoadInlineRecord *record,
                                const uint8_t *scopes_pc, size_t scopes_pc_size,
                                const uint8_t *scopes_data, size_t scopes_data_size,
                                uint64_t insts_begin) {
    if (!record)
        return -pte_internal;

    if (!scopes_data_size || !scopes_pc_size)
        return 0;

    for (PcDesc *p = (PcDesc *)scopes_pc;
         p < (PcDesc *)(scopes_pc + scopes_pc_size); p++) {
        /* 0: serialized_null */
        if (p->scope_decode_offset() == 0)
            continue;
        record->numpcs++;
    }

    record->pcinfo =
        (PCStackInfo *)malloc(record->numpcs * sizeof(PCStackInfo));
    int scope = 0;

    for (PcDesc *p = (PcDesc *)scopes_pc;
         p < (PcDesc *)(scopes_pc + scopes_pc_size); p++) {
        /* 0: serialized_null */
        if (p->scope_decode_offset() == 0)
            continue;
        uint64_t pc_address = p->real_pc(insts_begin);
        record->pcinfo[scope].pc = pc_address;
        int numstackframes = 0;
        ScopeDesc *sd =
            new ScopeDesc(p->scope_decode_offset(), p->obj_decode_offset(),
                          p->should_reexecute(), p->rethrow_exception(),
                          p->return_oop(), scopes_data);
        while (sd != NULL) {
            ScopeDesc *sd_p = sd->sender();
            delete sd;
            sd = sd_p;
            numstackframes++;
        }
        record->pcinfo[scope].methods =
            (jint *)malloc(sizeof(jint) * numstackframes);
        record->pcinfo[scope].bcis =
            (jint *)malloc(sizeof(jint) * numstackframes);
        record->pcinfo[scope].numstackframes = numstackframes;
        int stackframe = 0;
        sd = new ScopeDesc(p->scope_decode_offset(), p->obj_decode_offset(),
                           p->should_reexecute(), p->rethrow_exception(),
                           p->return_oop(), scopes_data);
        while (sd != NULL) {
            record->pcinfo[scope].methods[stackframe] = sd->method_index();
            record->pcinfo[scope].bcis[stackframe] = sd->bci();
            ScopeDesc *sd_p = sd->sender();
            delete sd;
            sd = sd_p;
            stackframe++;
        }
        scope++;
    }

    return 0;
}

int jit_mk_section(struct jit_section **psection,
                        const uint8_t *code,
                        uint64_t code_begin, uint64_t code_size, 
                        const uint8_t *scopes_pc,
                        const uint8_t *scopes_data,
                        CompiledMethodDesc *cmd,
                        const char *name) {
    struct jit_section *section;
    int errcode;

    if (!psection || !code)
        goto out_status;

    section = (struct jit_section *)malloc(sizeof(*section));
    if (!section) {
        errcode = -pte_nomem;
        goto out_status;
    }

    memset(section, 0, sizeof(*section));

    section->code = code;
    section->code_begin = code_begin;
    section->code_size = code_size;
    section->cmd = cmd;
    section->name = name;

    if (cmd) {
        section->record = (CompiledMethodLoadInlineRecord *)malloc(
                                sizeof(CompiledMethodLoadInlineRecord));
        memset(section->record, 0, sizeof(CompiledMethodLoadInlineRecord));
        int errcode = create_inline_record(section->record, scopes_pc,
                                cmd->get_scopes_pc_size(), scopes_data,
                                cmd->get_scopes_data_size(), code_begin);
        if (errcode < 0)
            goto out_status;
    }

    errcode = mtx_init(&section->lock, mtx_plain);
    if (errcode != thrd_success) {
        free(section);
        errcode = -pte_bad_lock;
        goto out_status;
    }

    errcode = mtx_init(&section->alock, mtx_plain);
    if (errcode != thrd_success) {
        mtx_destroy(&section->lock);
        free(section);
        errcode = -pte_bad_lock;
        goto out_status;
    }

    *psection = section;
    return 0;

out_status:
    return errcode;
}

int jit_section_lock(struct jit_section *section) {
    if (!section)
        return -pte_internal;

    {
        int errcode;

        errcode = mtx_lock(&section->lock);
        if (errcode != thrd_success)
            return -pte_bad_lock;
    }

    return 0;
}

int jit_section_unlock(struct jit_section *section) {
    if (!section)
        return -pte_internal;

    {
        int errcode;

        errcode = mtx_unlock(&section->lock);
        if (errcode != thrd_success)
            return -pte_bad_lock;
    }

    return 0;
}

void jit_section_free(struct jit_section *section) {
    if (!section)
        return;

    mtx_destroy(&section->alock);
    mtx_destroy(&section->lock);

    if (section->record) {
        for (int i = 0; i < section->record->numpcs; i++) {
            free(section->record->pcinfo[i].bcis);
            free(section->record->pcinfo[i].methods);
        }
    
        free(section->record->pcinfo);
        free(section->record);
    }

    free(section);
    
    return;
}

int jit_section_get(struct jit_section *section) {
    uint16_t ucount;
    int errcode;

    if (!section)
        return -pte_internal;

    errcode = jit_section_lock(section);
    if (errcode < 0)
        return errcode;

    ucount = section->ucount + 1;
    if (!ucount) {
        (void)jit_section_unlock(section);
        return -pte_overflow;
    }

    section->ucount = ucount;

    return jit_section_unlock(section);
}

int jit_section_put(struct jit_section *section) {
    uint16_t ucount, mcount;
    int errcode;

    if (!section)
        return -pte_internal;

    errcode = jit_section_lock(section);
    if (errcode < 0)
        return errcode;

    mcount = section->mcount;
    ucount = section->ucount;
    if (ucount > 1) {
        section->ucount = ucount - 1;
        return jit_section_unlock(section);
    }

    errcode = jit_section_unlock(section);
    if (errcode < 0)
        return errcode;

    if (!ucount || mcount)
        return -pte_internal;

    jit_section_free(section);
    return 0;
}

static int jit_section_lock_attach(struct jit_section *section) {
    if (!section)
        return -pte_internal;

    {
        int errcode;

        errcode = mtx_lock(&section->alock);
        if (errcode != thrd_success)
            return -pte_bad_lock;
    }

    return 0;
}

static int jit_section_unlock_attach(struct jit_section *section) {
    if (!section)
        return -pte_internal;

    {
        int errcode;

        errcode = mtx_unlock(&section->alock);
        if (errcode != thrd_success)
            return -pte_bad_lock;
    }

    return 0;
}

const CompiledMethodDesc *jit_section_cmd(const struct jit_section *section) {
    if (!section || !section->cmd)
        return nullptr;
    
    return section->cmd;
}

uint64_t jit_section_code_size(const struct jit_section *section) {
    if (!section)
        return 0ull;

    return section->code_size;
}

uint64_t jit_section_code_begin(const struct jit_section *section) {
    if (!section)
        return 0ull;

    return section->code_begin;
}

int jit_section_read(struct jit_section *section, uint8_t *buffer,
                     uint16_t size, uint64_t vaddr) {
    uint64_t limit, space;
    uint64_t offset;

    if (!section)
        return -pte_internal;

    offset = vaddr - jit_section_code_begin(section);
    limit = jit_section_code_size(section);
    if (limit <= offset)
        return -pte_nomap;

    /* Truncate if we try to read past the end of the section. */
    space = limit - offset;
    if (space < size)
        size = (uint16_t)space;

    const u_char *code = section->code;
    if (!code)
        return -pte_nomap;

    if (!buffer)
        return 0;

    memcpy(buffer, code + offset, size);
    return (int)size;
}

int jit_section_read_debug_info(struct jit_section *section, uint64_t vaddr,
                                PCStackInfo *&pcinfo) {
    if (!section)
        return -pte_internal;

    if (!section->record)
        return 0;

    uint64_t begin = jit_section_code_begin(section);
    uint64_t end = begin + jit_section_code_size(section);

    if (vaddr < begin || vaddr >= end)
        return -pte_nomap;
    
    for (int i = 0; i < section->record->numpcs; i++) {
        if (vaddr < section->record->pcinfo[i].pc) {
            pcinfo = &section->record->pcinfo[i];
            return 1;
        }
    }
    return 0;
}