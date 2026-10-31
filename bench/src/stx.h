/*
 * stupix-bench - portable whole-system benchmark
 *
 * Shared declarations: platform layer, assembly kernels, result reporting
 * and the benchmark modules. Only depends on the C library.
 */
#ifndef STX_H
#define STX_H

#define STX_VERSION "1.0.0"

/* ---- operating system ---- */
#if defined(_WIN32)
#define STX_OS_WINDOWS 1
#define STX_OS_NAME "windows"
#elif defined(__APPLE__)
#define STX_OS_MACOS 1
#define STX_OS_POSIX 1
#define STX_OS_NAME "macos"
#elif defined(__linux__)
#define STX_OS_LINUX 1
#define STX_OS_POSIX 1
#define STX_OS_NAME "linux"
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#define STX_OS_BSD 1
#define STX_OS_POSIX 1
#define STX_OS_NAME "bsd"
#else
#define STX_OS_POSIX 1
#define STX_OS_NAME "unix"
#endif

/* ---- architecture; STX_ASM is set when a hand-written kernel file exists ---- */
#if defined(__x86_64__) || defined(_M_X64)
#define STX_ARCH_NAME "x86_64"
#define STX_ARCH_X86_64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define STX_ARCH_NAME "aarch64"
#define STX_ARCH_AARCH64 1
#elif defined(__riscv) && __riscv_xlen == 64
#define STX_ARCH_NAME "riscv64"
#define STX_ARCH_RISCV64 1
#elif defined(__riscv) && __riscv_xlen == 32
#define STX_ARCH_NAME "riscv32"
#elif defined(__i386__) || defined(_M_IX86)
#define STX_ARCH_NAME "x86"
#elif defined(__arm__)
#define STX_ARCH_NAME "arm"
#elif defined(__powerpc64__)
#define STX_ARCH_NAME "ppc64"
#elif defined(__s390x__)
#define STX_ARCH_NAME "s390x"
#elif defined(__mips__)
#define STX_ARCH_NAME "mips"
#elif defined(__loongarch64)
#define STX_ARCH_NAME "loongarch64"
#else
#define STX_ARCH_NAME "unknown"
#endif

#if !defined(STX_NO_ASM) && defined(__GNUC__) && \
    (defined(STX_ARCH_X86_64) || defined(STX_ARCH_AARCH64) || \
     (defined(STX_ARCH_RISCV64) && defined(__riscv_d)))
#define STX_ASM 1
#endif

/* 32-bit x86 and ARM: C kernels plus asm clock (and cpuid on x86) */
#if !defined(STX_NO_ASM) && !defined(STX_ASM) && defined(__GNUC__) && \
    (defined(__i386__) || (defined(__arm__) && defined(__ELF__)))
#define STX_ASM_CLOCK 1
#endif

#ifndef __ASSEMBLER__
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* ---- configuration ---- */
enum {
    STX_MOD_CPU     = 1 << 0,
    STX_MOD_MEM     = 1 << 1,
    STX_MOD_DISK    = 1 << 2,
    STX_MOD_GPU     = 1 << 3,
    STX_MOD_CHIPSET = 1 << 4,
    STX_MOD_BMC     = 1 << 5,
    STX_MOD_LOAD    = 1 << 6, /* sustained load: thermal + power */
    STX_MOD_ALL     = 0x7f
};

typedef struct {
    double unit_time;       /* seconds per single measurement */
    int threads;            /* worker threads for multi-threaded tests */
    int load_seconds;       /* duration of the sustained load phase */
    unsigned modules;       /* STX_MOD_* bitmask */
    const char *disk_path;  /* directory for the file based disk test, NULL = skip */
    uint64_t disk_size;     /* bytes of the disk test file */
    const char *disk_dev;   /* "auto", "none" or a block device path (read-only) */
    const char *json_path;  /* NULL, "-" or a file */
    int color;
    int quiet;
} stx_config;

extern stx_config stx_cfg;

/* ---- platform layer (platform.c) ---- */
double stx_now(void);
void stx_sleep_ms(int ms);
int stx_cpu_count(void);
uint64_t stx_mem_total(void);
void *stx_alloc(size_t size, size_t align);
void stx_free(void *p);
int stx_is_root(void);
void stx_hostname(char *buf, size_t len);
void stx_os_release(char *buf, size_t len);

typedef void (*stx_thread_fn)(void *arg);
/* run fn(args[i]) on n threads, wait for all; returns 0 on success */
int stx_run_threads(int n, stx_thread_fn fn, void **args);
/* background thread for the sustained load phase */
typedef struct stx_thread stx_thread;
stx_thread *stx_thread_start(stx_thread_fn fn, void *arg);
void stx_thread_join(stx_thread *t);

/* file helpers, mostly for sysfs/procfs; return -1 when unavailable */
int stx_read_text(const char *path, char *buf, size_t len);
int stx_read_u64(const char *path, uint64_t *out);
int stx_read_i64(const char *path, int64_t *out);
int stx_file_exists(const char *path);
/* call cb(name, ctx) for each entry of a directory (skips . and ..); returns count or -1 */
int stx_list_dir(const char *path, void (*cb)(const char *name, void *ctx), void *ctx);

/* runtime library loading; NULL when unsupported (static builds) */
void *stx_dlopen(const char *const *names);
void *stx_dlsym(void *lib, const char *sym);

/* simple PRNG (xorshift64*) */
uint64_t stx_rand(uint64_t *state);

/* ---- kernels (asm/kernels.S with C reference in kernels.c) ---- */
typedef struct {
    uint64_t (*k_int)(uint64_t iters, uint64_t seed);
    double (*k_fp)(uint64_t iters);
    uint64_t (*k_read)(const uint64_t *p, size_t words);
    void (*k_write)(uint64_t *p, size_t words, uint64_t v);
    void *(*k_chase)(void *p, uint64_t steps);
    uint64_t (*k_clock)(uint64_t iters); /* NULL without asm */
    const char *impl;
} stx_kernels;

extern stx_kernels stx_k;
#define STX_INT_OPS_PER_ITER 16.0     /* 4 chains x (mul, add, shift, xor) */
#define STX_FP_FLOPS_PER_ITER 32.0    /* 8 accumulators x 2 lanes x (mul + add) */
#define STX_CLOCK_ADDS_PER_ITER 64.0  /* dependent adds, 1 cycle each */
void stx_kernels_init(void);
int stx_kernels_selftest(void);
int stx_cpuid(uint32_t leaf, uint32_t sub, uint32_t out[4]); /* 0 when unsupported */

/* calibrate: grow iterations until fn takes >= target seconds; returns best iters/s */
double stx_measure(double (*fn)(uint64_t iters, void *ctx), void *ctx, double target);

/* ---- reporting (report.c) ---- */
typedef enum { STX_HIGHER = 0, STX_LOWER = 1 } stx_dir;
typedef enum { STX_PASS = 0, STX_WARN = 1, STX_FAIL = 2 } stx_health;

void stx_section(const char *name, const char *title);
void stx_info(const char *key, const char *fmt, ...);
/* ref > 0: scored relative to the reference system (1000 points) */
void stx_metric(const char *key, double value, const char *unit, double ref, stx_dir dir);
void stx_check(stx_health h, const char *key, const char *fmt, ...);
void stx_skip(const char *fmt, ...);
void stx_note(const char *fmt, ...);
void stx_progress(const char *fmt, ...);
void stx_report_finish(void);

/* ---- modules ---- */
void stx_mod_system(void);
void stx_mod_cpu(void);
void stx_mod_mem(void);
void stx_mod_disk(void);
void stx_mod_gpu(void);
void stx_mod_chipset(void);
void stx_mod_bmc(void);
void stx_mod_load(void);

/* measurements shared between modules */
extern double stx_bmc_power_watts;    /* DCMI reading, < 0 if unknown */

/* sensors.c */
double stx_cpu_temp(void);            /* hottest CPU sensor in C, < -200 unknown */
double stx_cpu_temp_crit(void);       /* critical trip point in C, < -200 unknown */
double stx_cpu_freq_mhz(void);        /* average current frequency, < 0 unknown */
double stx_rapl_joules(void);         /* sum of package domains, < 0 unknown */
double stx_battery_watts(void);       /* discharge power, < 0 unknown */
void stx_sensors_report(void);        /* list all temperature/fan sensors */

#endif /* __ASSEMBLER__ */
#endif
