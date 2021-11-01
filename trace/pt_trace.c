/* An Intel PT tracing tool using system call in linux
 * of perf_event_open()
 * It only supports tracing user code space code.
 * It configures to trace one process/thread execution
 * on each CPU and supports inherition
 */


#define MSR_IA32_RTIT_CTL 0x00000570
#define MSR_IA32_RTIT_STATUS 0x00000571
#define MSR_IA32_RTIT_ADDR0_A 0x00000580
#define MSR_IA32_RTIT_ADDR0_B 0x00000581
#define MSR_IA32_RTIT_ADDR1_A 0x00000582
#define MSR_IA32_RTIT_ADDR1_B 0x00000583
#define MSR_IA32_RTIT_ADDR2_A 0x00000584
#define MSR_IA32_RTIT_ADDR2_B 0x00000585
#define MSR_IA32_RTIT_ADDR3_A 0x00000586
#define MSR_IA32_RTIT_ADDR3_B 0x00000587
#define MSR_IA32_RTIT_CR3_MATCH 0x00000572
#define MSR_IA32_RTIT_OUTPUT_BASE 0x00000560
#define MSR_IA32_RTIT_OUTPUT_MASK 0x00000561


#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdarg.h>
#include <inttypes.h>
#include <cpuid.h>

extern void pt_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
			 uint32_t *ecx, uint32_t *edx)
{
	__get_cpuid(leaf, eax, ebx, ecx, edx);
}

static const char * const cpu_vendors[] = {
	"",
	"GenuineIntel"
};

enum {
	pt_cpuid_vendor_size = 12
};

union cpu_vendor {
	/* The raw data returned from cpuid. */
	struct {
		uint32_t ebx;
		uint32_t edx;
		uint32_t ecx;
	} cpuid;

	/* The resulting vendor string. */
	char vendor_string[pt_cpuid_vendor_size];
};

/** A cpu vendor. */
enum pt_cpu_vendor {
	pcv_unknown,
	pcv_intel
};

static enum pt_cpu_vendor cpu_vendor(void)
{
	union cpu_vendor vendor;
	uint32_t eax;
	size_t i;

	memset(&vendor, 0, sizeof(vendor));
	eax = 0;

	pt_cpuid(0u, &eax, &vendor.cpuid.ebx, &vendor.cpuid.ecx,
		 &vendor.cpuid.edx);

	for (i = 0; i < sizeof(cpu_vendors)/sizeof(*cpu_vendors); i++)
		if (strncmp(vendor.vendor_string,
				cpu_vendors[i], pt_cpuid_vendor_size) == 0)
			return (enum pt_cpu_vendor) i;

	return pcv_unknown;
}

static uint32_t cpu_info(void)
{
	uint32_t eax, ebx, ecx, edx;

	eax = 0;
	ebx = 0;
	ecx = 0;
	edx = 0;
	pt_cpuid(1u, &eax, &ebx, &ecx, &edx);

	return eax;
}


/** A cpu identifier. */
struct pt_cpu {
	/** The cpu vendor. */
	enum pt_cpu_vendor vendor;

	/** The cpu family. */
	uint16_t family;

	/** The cpu model. */
	uint8_t model;

	/** The stepping. */
	uint8_t stepping;
};

int pt_cpu_read(struct pt_cpu *cpu)
{
	uint32_t info;
	uint16_t family;

	if (!cpu)
		return -1;

	cpu->vendor = cpu_vendor();

	info = cpu_info();

	cpu->family = family = (info>>8) & 0xf;
	if (family == 0xf)
		cpu->family += (info>>20) & 0xf;

	cpu->model = (info>>4) & 0xf;
	if (family == 0x6 || family == 0xf)
		cpu->model += (info>>12) & 0xf0;

	cpu->stepping = (info>>0) & 0xf;

	return 0;
}

#define PERF_RECORD_AUXTRACE 71
#define PERF_RECORD_AUX_ADVANCE 72

/* global tracing info */
struct trace_record
{
	int nr_cpus;
	/* perf event open fd */
	int *trace_fd;
	struct perf_event_attr attr;

	pid_t pid;

	/* needed to be power of 2*/
	int mmap_pages;
	int aux_pages;

	/* mmap base and aux base for each cpu*/
	void **mmap_base;
	void **aux_base;
	/* start of write */
	__u64 *mmap_start;
	__u64 *aux_start;

	/* file descriptor returned by open file */
	int fd;
};

struct attr_config {
	struct pt_cpu cpu; 
	int nr_cpus;
	__u8 mtc_freq;
	__u8 nom_freq;
	__u32 cpuid_0x15_eax;
	__u32 cpuid_0x15_ebx;
	__u64 sample_type;
	__u16 time_shift;
	__u32 time_mult;
	__u64 time_zero;
	__u64 addr0_a;
	__u64 addr0_b;
};

struct auxtrace_event {
	struct perf_event_header header;
	__u64 size;
	__u64 offset;
	__u64 reference;
	__u32 idx;
	__u32 tid;
	__u32 cpu;
	__u32 reservered__; /* for alignment */
};

struct aux_advance_event {
	struct perf_event_header header;
	__u32 cpu;
	__u32 tid;
};

#define PAGE_SIZE 4096

static volatile int done = 0;
static volatile unsigned long long record_samples = 0;

static inline int perf_event_open(struct perf_event_attr *hw_event,
								  pid_t pid, int cpu, int group_fd,
								  unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid, cpu,
				   group_fd, flags);
}

static inline int cpu_num()
{
	int nr_cpus;
	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);

	return (nr_cpus < 0) ? 0 : nr_cpus;
}

void wrmsr_on_cpu(uint32_t reg, int cpu, uint64_t data)
{
	int fd;
	char msr_file_name[64];

	sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
	fd = open(msr_file_name, O_WRONLY);
	if (fd < 0) {
		if (errno == ENXIO) {
			fprintf(stderr, "wrmsr: No CPU %d\n", cpu);
			exit(2);
		} else if (errno == EIO) {
			fprintf(stderr, "wrmsr: CPU %d doesn't support MSRs\n",
					cpu);
			exit(3);
		} else {
			perror("wrmsr: open");
			exit(127);
		}
	}


	if (pwrite(fd, &data, sizeof data, reg) != sizeof data) {
		if (errno == EIO) {
			fprintf(stderr,
					"wrmsr: CPU %d cannot set MSR "
					"0x%08"PRIx32" to 0x%016"PRIx64"\n",
					cpu, reg, data);
			exit(4);
		} else {
			perror("wrmsr: pwrite");
			exit(127);
		}
	}

	close(fd);

	return;
}

void wrmsr_on_all_cpus(int nr_cpus, uint32_t reg, uint64_t data){
	int cpu = 0;

	for (; cpu < nr_cpus; cpu++){
		wrmsr_on_cpu(reg, cpu, data);
	}
}

static void trace_record_free(struct trace_record *record)
{
	if (!record)
		return;

	if (record->mmap_base)
		free(record->mmap_base);
	if (record->aux_base)
		free(record->aux_base);
	if (record->mmap_start)
		free(record->mmap_start);
	if (record->aux_start)
		free(record->aux_start);

	if (record->trace_fd)
		free(record->trace_fd);
}

static struct trace_record *trace_record_alloc()
{
	struct trace_record *record = malloc(sizeof(struct trace_record));
	if (!record)
		return NULL;
	memset(record, 0, sizeof(*record));

	int nr_cpus = cpu_num();
	record->nr_cpus = nr_cpus;
	record->fd = -1;

	record->mmap_base = malloc(sizeof(void *) * nr_cpus);
	if (!record->mmap_base)
	{
		trace_record_free(record);
		return NULL;
	}

	memset(record->mmap_base, 0, sizeof(void *) * nr_cpus);
	record->aux_base = malloc(sizeof(void *) * nr_cpus);
	if (!record->aux_base)
	{
		trace_record_free(record);
		return NULL;
	}

	memset(record->aux_base, 0, sizeof(void *) * nr_cpus);
	record->mmap_start = malloc(sizeof(__u64) * nr_cpus);
	if (!record->mmap_start)
	{
		trace_record_free(record);
		return NULL;
	}

	memset(record->mmap_start, 0, sizeof(__u64) * nr_cpus);

	record->aux_start = malloc(sizeof(__u64) * nr_cpus);
	if (!record->aux_start)
	{
		trace_record_free(record);
		return NULL;
	}
	memset(record->aux_start, 0, sizeof(__u64) * nr_cpus);

	record->trace_fd = malloc(sizeof(int) * nr_cpus);
	if (!record->trace_fd)
	{
		trace_record_free(record);
		return NULL;
	}
	memset(record->trace_fd, -1, sizeof(int) * nr_cpus);

	return record;
}

static int trace_event_enable(struct trace_record *record)
{
	if (!record || !record->trace_fd)
		return -1;

	int cpu;
	for (cpu = 0; cpu < record->nr_cpus; cpu++)
	{
		ioctl(record->trace_fd[cpu],
			  PERF_EVENT_IOC_ENABLE, 0);
	}

	return 0;
}

static int trace_event_disable(struct trace_record *record)
{
	if (!record || !record->trace_fd)
		return -1;

	int cpu;
	for (cpu = 0; cpu < record->nr_cpus; cpu++)
	{
		ioctl(record->trace_fd[cpu],
			  PERF_EVENT_IOC_DISABLE, 0);
	}

	return 0;
}

int read_tsc_conversion(const struct perf_event_mmap_page *pc,
						__u32 *time_mult,
						__u16 *time_shift,
						__u64 *time_zero)
{
	bool cap_user_time_zero;
	__u32 seq;
	int i = 0;

	while (1)
	{
		seq = pc->lock;
		__sync_synchronize();
		*time_mult = pc->time_mult;
		*time_shift = pc->time_shift;
		*time_zero = pc->time_zero;
		cap_user_time_zero = pc->cap_user_time_zero;
		__sync_synchronize();
		if (pc->lock == seq && !(seq & 1))
			break;
		if (++i > 10000)
		{
			/*("failed to get perf_event_mmap_page lock\n");*/
			return -EINVAL;
		}
	}

	if (!cap_user_time_zero)
		return -EOPNOTSUPP;

	return 0;
}

static void tsc_ctc_ratio(__u32 *n, __u32 *d)
{
	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

	__get_cpuid(0x15, &eax, &ebx, &ecx, &edx);
	*n = ebx;
	*d = eax;
}

static __u64 auxtrace_mmap_read_head(struct perf_event_mmap_page *header)
{
	__u64 head = header->aux_head;

	/* To ensure all read are done after read head; */
	__sync_synchronize();
	return head;
}

static void auxtrace_mmap_write_tail(struct perf_event_mmap_page *header,
									 __u64 tail)
{
	/* To ensure all read are done before write tail */
	__sync_synchronize();

	header->aux_tail = tail;
}

static __u64 mmap_read_head(struct perf_event_mmap_page *header)
{
	__u64 head = header->data_head;

	/* To ensure all read are done after read head; */
	__sync_synchronize();
	return head;
}

static void mmap_write_tail(struct perf_event_mmap_page *header,
							__u64 tail)
{
	/* To ensure all read are done before write tail */
	__sync_synchronize();

	header->data_tail = tail;
}

static FILE *pt_open_file(const char *name)
{
	struct stat st;
	char path[4096];

	snprintf(path, 4096,
		 "%s%s", "/sys/bus/event_source/devices/intel_pt/", name);

	if (stat(path, &st) < 0)
		return NULL;

	return fopen(path, "r");
}

int pt_scan_file(const char *name, const char *fmt,
				 ...)
{
	va_list args;
	FILE *file;
	int ret = EOF;

	va_start(args, fmt);
	file = pt_open_file(name);
	if (file)
	{
		ret = vfscanf(file, fmt, args);
		fclose(file);
	}
	va_end(args);
	return ret;
}

static int pt_pick_bit(int bits, int target)
{
	int pos, pick = -1;

	for (pos = 0; bits; bits >>= 1, pos++)
	{
		if (bits & 1)
		{
			if (pos <= target || pick < 0)
				pick = pos;
			if (pos >= target)
				break;
		}
	}

	return pick;
}

/* default config int perf_event_attr for Intel PT event*/
static __u64 pt_default_config()
{
	int mtc, mtc_periods = 0, mtc_period;
	int psb_cyc, psb_periods, psb_period;
	__u64 config = 0;
	char c;

	/* pt bit */
	config |= 1;
	/* tsc */
	config |= (1 << 10);
	/* disable ret compress */
	config |= (1 << 11);
	if (pt_scan_file("caps/mtc", "%d",
					 &mtc) != 1)
		mtc = 1;

	if (mtc)
	{
		/* mtc enable */
		config |= (1 << 9);
		if (pt_scan_file("caps/mtc_periods", "%x",
						 &mtc_periods) != 1)
			mtc_periods = 0;
		if (mtc_periods)
			mtc_period = pt_pick_bit(mtc_periods, 3);

		/* mtc period */
		config |= ((mtc_period & 0xf) << 14);
	}

	if (pt_scan_file("caps/psb_cyc", "%d",
					 &psb_cyc) != 1)
		psb_cyc = 1;

	if (psb_cyc && mtc_periods)
	{
		if (pt_scan_file("caps/psb_periods", "%x",
						 &psb_periods) != 1)
			psb_periods = 0;
		if (psb_periods)
		{
			psb_period = pt_pick_bit(psb_periods, 3);
		}
		config |= ((psb_period & 0xf) << 24);
	}

	/* branch enable */
	if (pt_scan_file("format/pt", "%c", &c) == 1 &&
		pt_scan_file("format/branch", "%c", &c) == 1)
		config |= (1 << 13);

	return config;
}

/* deafault perf_event_attr config for Intel PT*/
static int pt_default_attr(struct perf_event_attr *attr)
{
	if (!attr)
		return -1;

	attr->size = sizeof(struct perf_event_attr);
	attr->exclude_kernel = 1;
	attr->exclude_hv = 1;
	attr->sample_period = 1;
	attr->sample_freq = 1;
	attr->sample_type = 0x10087;
	attr->sample_id_all = 1;
	attr->read_format = 4;
	attr->inherit = 1;
	attr->context_switch = 1;

	__u64 config = pt_default_config();
	attr->config = config;

	int type;
	if (pt_scan_file("type", "%d", &type) != 1)
		return -1;
	attr->type = type;
	attr->enable_on_exec = 1;
	attr->disabled = 1;
}

/* trace open : perf_event_open()*/
static int trace_event_open(struct trace_record *record)
{
	if (!record)
		return -1;

	int nr_cpus = record->nr_cpus;
	pid_t pid = record->pid;
	int cpu;
	pt_default_attr(&record->attr);

	for (cpu = 0; cpu < nr_cpus; cpu++)
	{
		record->trace_fd[cpu] = perf_event_open(&record->attr,
												pid, cpu, -1, PERF_FLAG_FD_CLOEXEC);
		if (record->trace_fd[cpu] < 0)
			return -1;
	}

	for (cpu = 0; cpu < nr_cpus; cpu++)
	{
		struct perf_event_mmap_page *header;

		record->mmap_base[cpu] = mmap(NULL, (record->mmap_pages + 1) * PAGE_SIZE,
									  PROT_WRITE | PROT_READ,
									  MAP_SHARED, record->trace_fd[cpu], 0);

		if (record->mmap_base[cpu] == MAP_FAILED)
			return -1;

		header = record->mmap_base[cpu];
		header->aux_offset = header->data_offset + header->data_size;
		header->aux_size = record->aux_pages * PAGE_SIZE;

		record->aux_base[cpu] = mmap(NULL, header->aux_size,
									 PROT_READ | PROT_WRITE, MAP_SHARED,
									 record->trace_fd[cpu], header->aux_offset);
		if (record->aux_base[cpu] == MAP_FAILED)
			return -1;
	}

	record->fd = open("JPortalTrace.data", O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
	if (record->fd < 0)
	{
		return -1;
	}

	return 0;
}

/* trace close */
static void trace_event_close(struct trace_record *record)
{
	if (!record)
		return;

	int nr_cpus = record->nr_cpus;
	pid_t pid = record->pid;
	int cpu, ev;

	for (cpu = 0; cpu < nr_cpus; cpu++)
		if (record->trace_fd[cpu] >= 0)
			close(record->trace_fd[cpu]);

	for (cpu = 0; cpu < nr_cpus; cpu++)
	{
		if (record->mmap_base[cpu] > 0)
		{
			struct perf_event_mmap_page *header = record->mmap_base[cpu];
			if (record->aux_base && record->aux_base[cpu] > 0)
			{
				munmap(record->aux_base[cpu], header->aux_size);
			}
			munmap(header, header->data_size + header->data_offset);
		}
	}

	if (record->fd >= 0)
	{
		close(record->fd);
	}
}

inline static ssize_t ion(bool is_read, int fd, void *buf, size_t n)
{
	void *buf_start = buf;
	size_t left = n;

	while (left)
	{
		/* buf must be treated as const if !is_read. */
		ssize_t ret = is_read ? read(fd, buf, left) :
			write(fd, buf, left);

		if (ret < 0 && errno == EINTR)
		{
			continue;
		}

		if (ret <= 0)
		{
			return ret;
		}

		left -= ret;
		buf  += ret;
	}

	return n;
}

inline static ssize_t record_write(int fd, const void *buf, size_t n)
{
	record_samples++;
	return ion(false, fd, (void *)buf, n);
}

static inline uint64_t get_timestamp() {
#if defined(__i386__) || defined(__x86_64__)
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((uint64_t)high) << 32;
#else
	return 0;
#endif
}

static int auxtrace_mmap_advance(void *mmap_base, __u64 *aux_start,
									int cpu, int fd)
{
	struct perf_event_mmap_page *header = mmap_base;
	__u64 head, old;
	old = *aux_start;
	size_t size, head_off, old_off;
	size_t len = header->aux_size;

	head = auxtrace_mmap_read_head(header);

	if (old == head)
	{
		return 0;
	}

	int mask = len - 1;
	head_off = head & mask;
	old_off = old & mask;

	if (head_off > old_off)
	{
		size = head_off - old_off;
	}
	else
	{
		size = len - (old_off - head_off);
	}

	if (size >= len/2*1) {
		struct aux_advance_event ev;
		memset(&ev, 0, sizeof(ev));
		ev.header.type = PERF_RECORD_AUX_ADVANCE;
		ev.header.size = sizeof(ev);
		ev.cpu = cpu;
		ev.tid = -1;

		if (record_write(fd, &ev, sizeof(ev)) < 0)
		{
			return -1;
		}

		__u64 loc = (old + len/4);
		*aux_start = loc;
		auxtrace_mmap_write_tail(header, loc);
	}
	return 0;
}

static int auxtrace_mmap_record(void *mmap_base, void *aux_base,
								__u64 *aux_start, int cpu, int fd)
{
	struct perf_event_mmap_page *header = mmap_base;
	__u64 head, old;
	old = *aux_start;
	__u64 offset;
	unsigned char *data = aux_base;
	size_t size, head_off, old_off, len1, len2;
	void *data1, *data2;
	size_t len = header->aux_size;

	head = auxtrace_mmap_read_head(header);

	if (old == head)
	{
		return 0;
	}

	int mask = len - 1;
	head_off = head & mask;
	old_off = old & mask;

	if (head_off > old_off)
	{
		size = head_off - old_off;
	}
	else
	{
		size = len - (old_off - head_off);
	}

	/* Per read limitation */
	if (size >= len/64*1) {
		size = len/64*1;
		head = (old + len/64*1);
		head_off = head & mask;
	}

	if (head > old || size <= head || mask)
	{
		offset = head - size;
	}
	else
	{
		/*
		 * When the buffer size is not a power of 2, 'head' wraps at the
		 * highest multiple of the buffer size, so we have to subtract
		 * the remainder here.
		 */
		__u64 rem = (0ULL - len) % len;

		offset = head - size - rem;
	}

	if (size > head_off)
	{
		len1 = size - head_off;
		data1 = &data[len - len1];
		len2 = head_off;
		data2 = &data[0];
	}
	else
	{
		len1 = size;
		data1 = &data[head_off - len1];
		len2 = 0;
		data2 = NULL;
	}

	struct auxtrace_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.header.type = PERF_RECORD_AUXTRACE;
	ev.header.size = sizeof(ev);
	ev.size = size;
	ev.offset = offset;
	ev.cpu = cpu;
	ev.tid = -1;

	if (record_write(fd, &ev, sizeof(ev)) < 0)
	{
		return -1;
	}

	if (record_write(fd, data1, len1) < 0)
	{
		return -1;
	}

	if (len2 && record_write(fd, data2, len2) < 0)
	{
		return -1;
	}

	*aux_start = head;
	auxtrace_mmap_write_tail(header, head);

	/*enable*/
	return 0;
}

static int mmap_record(void *mmap_base, __u64 *start, int fd)
{
	struct perf_event_mmap_page *header = mmap_base;
	__u64 head, old;
	old = *start;
	head = mmap_read_head(header);
	__u64 size = head - old;
	int mask = header->data_size - 1;
	unsigned char *data = mmap_base + header->data_offset;
	void *buff;

	if (old == head)
	{
		return 0;
	}

	if ((old & mask) + size != (head & mask))
	{
		buff = &data[old & mask];
		size = mask + 1 - (old & mask);
		old += size;

		if (record_write(fd, buff, size) < 0)
		{
			return -1;
		}
	}

	buff = &data[old & mask];
	size = (head & mask) - (old & mask);
	old += size;

	if (record_write(fd, buff, size) < 0)
	{
		return -1;
	}

	*start = head;
	mmap_write_tail(header, head);
	return 0;
}

static int record_all(struct trace_record *record)
{
	int nr_cpus = record->nr_cpus;

	for (int i = 0; i < nr_cpus; i++)
	{
		if (auxtrace_mmap_advance(record->mmap_base[i], &record->aux_start[i],
									i, record->fd) < 0)
		{
			return -1;
		}

		if (auxtrace_mmap_record(record->mmap_base[i], record->aux_base[i],
								 &record->aux_start[i], i, record->fd) < 0)
		{
			return -1;
		}

		if (mmap_record(record->mmap_base[i], &record->mmap_start[i],
						record->fd) < 0)
		{
			return -1;
		}
	}

	return 0;
}

static void sig_handler(int sig)
{
	done = 1;
}

/* test */
int main(int argc, char *argv[])
{
	
	if (argc != 5)
	{
		fprintf(stderr, "pt_trace error: arguments error\n");
		exit(-1);
	}

	int ret;
	int write_pipe;
	pid_t java_pid;
	uint64_t _low_bound, _high_bound;
	sscanf(argv[1], "%d", &java_pid);
	sscanf(argv[2], "%d", &write_pipe);
	sscanf(argv[3], "%lx", &_low_bound);
	sscanf(argv[4], "%lx", &_high_bound);

	printf("PT Tracing process: %d with ip filter: %lx-%lx\n",
			java_pid, _low_bound, _high_bound);
	
	struct trace_record *rec = trace_record_alloc();
	if (!rec)
	{
		fprintf(stderr, "fail to alloc record.\n");
		return -1;
	}
	rec->pid = java_pid;
	rec->mmap_pages = 1024;
	rec->aux_pages = 1024*8;

	/* write msr to set ip filter */
	wrmsr_on_all_cpus(rec->nr_cpus, MSR_IA32_RTIT_ADDR0_A, _low_bound);
	wrmsr_on_all_cpus(rec->nr_cpus, MSR_IA32_RTIT_ADDR0_B, _high_bound);

	if (trace_event_open(rec) < 0)
	{
		fprintf(stderr, "trace open error\n");
		return -1;
	}

	for (int cpu = 0; cpu < rec->nr_cpus; cpu++)
	{
		int errr = ioctl(rec->trace_fd[cpu], PERF_EVENT_IOC_SET_FILTER, 
			"filter 0x580/580@/bin/bash");
		if (errr < 0)
		{
			fprintf(stderr, "set filter error\n");
			goto err;
		}
	}

	struct attr_config attr;
	read_tsc_conversion(rec->mmap_base[0], &attr.time_mult,
							&attr.time_shift, &attr.time_zero);
	pt_cpu_read(&attr.cpu);
	tsc_ctc_ratio(&attr.cpuid_0x15_ebx, &attr.cpuid_0x15_eax);
	int max_nonturbo_ratio;
	pt_scan_file("max_nonturbo_ratio", "%d", &max_nonturbo_ratio);
	attr.nom_freq = (__u8)max_nonturbo_ratio;
	attr.mtc_freq = ((rec->attr.config >> 14) & 0xf);
	attr.sample_type = 0x10087;
	attr.nr_cpus = rec->nr_cpus;
	attr.addr0_a = _low_bound;
	attr.addr0_b = _high_bound;
	if (record_write(rec->fd, &attr, sizeof(attr)) < 0)
	{
		goto err;
	}

	signal(SIGTERM, sig_handler);

	trace_event_enable(rec);

	/* notify parent process */
	char bf;
	if (write(write_pipe, &bf, 1) < 0)
	{
		fprintf(stderr, "write pipe error\n");
		goto err;
	}
	close(write_pipe);

	for (;;)
	{
		unsigned long long hits = record_samples;

		if (record_all(rec) < 0)
		{
			fprintf(stderr, "record error\n");
			goto err;
		}

		if (done && record_samples == hits)
		{
			printf("tracing end\n");
			break;
		}
	}

out:
	trace_event_disable(rec);
	trace_event_close(rec);
	trace_record_free(rec);
	return 0;

err:
	trace_event_disable(rec);
	trace_event_close(rec);
	trace_record_free(rec);
	return -1;
}
