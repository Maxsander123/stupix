/* CPU: identification, clock, integer/FP/branch/crypto throughput, multi-core scaling. */
#include "stx.h"

#include <stdlib.h>
#include <string.h>
#if defined(STX_OS_MACOS) || defined(STX_OS_BSD)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

/* reference system (1000 points): Dell Precision 3581, Intel Core i7-13700H (6P+8E, 20T), Linux */
#define REF_INT_ST 10.5     /* GIOPS */
#define REF_FP_ST 17.5      /* GFLOPS */
#define REF_SORT_ST 27.0    /* Melem/s */
#define REF_CHACHA_ST 720.0 /* MB/s */
#define REF_INT_MT 95.0    /* GIOPS */
#define REF_FP_MT 165.0     /* GFLOPS */
#define REF_CHACHA_MT 7400.0

static volatile uint64_t sink;

/* ---- kernel wrappers for stx_measure ---- */
static double run_int(uint64_t n, void *ctx)
{
    double t = stx_now();
    (void)ctx;
    sink += stx_k.k_int(n, 7);
    return stx_now() - t;
}

static double run_fp(uint64_t n, void *ctx)
{
    double t = stx_now();
    (void)ctx;
    sink += (uint64_t)stx_k.k_fp(n);
    return stx_now() - t;
}

static double run_clock(uint64_t n, void *ctx)
{
    double t = stx_now();
    (void)ctx;
    sink += stx_k.k_clock(n);
    return stx_now() - t;
}

/* branch-heavy: quicksort 4096 random keys per iteration */
#define SORT_N 4096
static void qsort_u32(uint32_t *a, int lo, int hi)
{
    while (hi - lo > 16) {
        uint32_t p = a[(lo + hi) >> 1], t;
        int i = lo, j = hi;
        while (i <= j) {
            while (a[i] < p)
                i++;
            while (a[j] > p)
                j--;
            if (i <= j) {
                t = a[i];
                a[i] = a[j];
                a[j] = t;
                i++;
                j--;
            }
        }
        if (j - lo < hi - i) {
            qsort_u32(a, lo, j);
            lo = i;
        } else {
            qsort_u32(a, i, hi);
            hi = j;
        }
    }
    {
        int i, j;
        for (i = lo + 1; i <= hi; i++) {
            uint32_t v = a[i];
            for (j = i - 1; j >= lo && a[j] > v; j--)
                a[j + 1] = a[j];
            a[j + 1] = v;
        }
    }
}

static double run_sort(uint64_t n, void *ctx)
{
    uint32_t *a = (uint32_t *)ctx;
    uint64_t s = 0x1234567 + n, k;
    double t = stx_now();
    int i;
    for (k = 0; k < n; k++) {
        for (i = 0; i < SORT_N; i++)
            a[i] = (uint32_t)stx_rand(&s);
        qsort_u32(a, 0, SORT_N - 1);
        sink += a[SORT_N / 2];
    }
    return stx_now() - t;
}

/* ChaCha20 block function, one 64-byte block per iteration */
#define ROTL(v, c) (((v) << (c)) | ((v) >> (32 - (c))))
#define QR(a, b, c, d)                                                                                          \
    a += b; d ^= a; d = ROTL(d, 16);                                                                            \
    c += d; b ^= c; b = ROTL(b, 12);                                                                            \
    a += b; d ^= a; d = ROTL(d, 8);                                                                             \
    c += d; b ^= c; b = ROTL(b, 7)

static double run_chacha(uint64_t n, void *ctx)
{
    uint32_t in[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 9, 10};
    uint32_t acc = 0;
    uint64_t k;
    double t = stx_now();
    (void)ctx;
    for (k = 0; k < n; k++) {
        uint32_t x[16];
        int i;
        in[12] = (uint32_t)k;
        memcpy(x, in, sizeof(x));
        for (i = 0; i < 10; i++) {
            QR(x[0], x[4], x[8], x[12]);
            QR(x[1], x[5], x[9], x[13]);
            QR(x[2], x[6], x[10], x[14]);
            QR(x[3], x[7], x[11], x[15]);
            QR(x[0], x[5], x[10], x[15]);
            QR(x[1], x[6], x[11], x[12]);
            QR(x[2], x[7], x[8], x[13]);
            QR(x[3], x[4], x[9], x[14]);
        }
        for (i = 0; i < 16; i++)
            acc ^= x[i] + in[i];
    }
    sink += acc;
    return stx_now() - t;
}

/* ---- multi-threaded: every thread runs the same single-thread routine ---- */
typedef struct {
    double (*fn)(uint64_t, void *);
    uint64_t iters;
    void *ctx;
} mt_job;

static void mt_worker(void *p)
{
    mt_job *j = (mt_job *)p;
    j->fn(j->iters, j->ctx);
}

typedef struct {
    double (*fn)(uint64_t, void *);
    int threads;
    void **ctxs;
} mt_ctx;

static double run_mt(uint64_t n, void *p)
{
    mt_ctx *m = (mt_ctx *)p;
    mt_job *jobs = (mt_job *)calloc((size_t)m->threads, sizeof(mt_job));
    void **args = (void **)calloc((size_t)m->threads, sizeof(void *));
    double t;
    int i;
    for (i = 0; i < m->threads; i++) {
        jobs[i].fn = m->fn;
        jobs[i].iters = n;
        jobs[i].ctx = m->ctxs ? m->ctxs[i] : NULL;
        args[i] = &jobs[i];
    }
    t = stx_now();
    stx_run_threads(m->threads, mt_worker, args);
    t = stx_now() - t;
    free(jobs);
    free(args);
    return t;
}

/* returns total iterations per second across all threads */
static double measure_mt(double (*fn)(uint64_t, void *), void **ctxs)
{
    mt_ctx m;
    m.fn = fn;
    m.threads = stx_cfg.threads;
    m.ctxs = ctxs;
    return stx_measure(run_mt, &m, stx_cfg.unit_time * 2) * stx_cfg.threads;
}

/* ---- identification ---- */
#if defined(STX_OS_LINUX)
/* ARM cores from "CPU implementer" / "CPU part" (aarch64 /proc/cpuinfo has no model name) */
static void arm_model(const char *cpuinfo, char *out, size_t len)
{
    static const struct {
        unsigned impl, part;
        const char *name;
    } parts[] = {
        {0x41, 0xb76, "ARM1176"},          {0x41, 0xc07, "Cortex-A7"},         {0x41, 0xc08, "Cortex-A8"},
        {0x41, 0xc09, "Cortex-A9"},        {0x41, 0xc0f, "Cortex-A15"},        {0x41, 0xc0e, "Cortex-A17"},
        {0x41, 0xd03, "Cortex-A53"},       {0x41, 0xd04, "Cortex-A35"},        {0x41, 0xd05, "Cortex-A55"},
        {0x41, 0xd07, "Cortex-A57"},       {0x41, 0xd08, "Cortex-A72"},        {0x41, 0xd09, "Cortex-A73"},
        {0x41, 0xd0a, "Cortex-A75"},       {0x41, 0xd0b, "Cortex-A76"},        {0x41, 0xd0c, "Neoverse-N1"},
        {0x41, 0xd0d, "Cortex-A77"},       {0x41, 0xd40, "Neoverse-V1"},       {0x41, 0xd41, "Cortex-A78"},
        {0x41, 0xd44, "Cortex-X1"},        {0x41, 0xd46, "Cortex-A510"},       {0x41, 0xd47, "Cortex-A710"},
        {0x41, 0xd48, "Cortex-X2"},        {0x41, 0xd49, "Neoverse-N2"},       {0x41, 0xd4a, "Neoverse-E1"},
        {0x41, 0xd4b, "Cortex-A78C"},      {0x41, 0xd4d, "Cortex-A715"},       {0x41, 0xd4e, "Cortex-X3"},
        {0x41, 0xd4f, "Neoverse-V2"},      {0x41, 0xd80, "Cortex-A520"},       {0x41, 0xd81, "Cortex-A720"},
        {0x41, 0xd82, "Cortex-X4"},        {0x41, 0xd84, "Neoverse-V3"},       {0x41, 0xd8e, "Neoverse-N3"},
        {0x48, 0xd01, "HiSilicon Kunpeng 920"}, {0x46, 0x001, "Fujitsu A64FX"}, {0xc0, 0xac3, "AmpereOne"},
        {0x51, 0x800, "Qualcomm Kryo 2xx Gold"}, {0x51, 0x801, "Qualcomm Kryo 2xx Silver"},
        {0x51, 0x001, "Qualcomm Oryon"},   {0x4e, 0x004, "NVIDIA Carmel"},     {0x61, 0x022, "Apple M1 Icestorm"},
        {0x61, 0x023, "Apple M1 Firestorm"},
    };
    static const struct {
        unsigned impl;
        const char *name;
    } impls[] = {{0x41, "ARM"},       {0x42, "Broadcom"}, {0x43, "Cavium"},  {0x46, "Fujitsu"}, {0x48, "HiSilicon"},
                 {0x4e, "NVIDIA"},    {0x50, "APM"},      {0x51, "Qualcomm"}, {0x61, "Apple"},  {0x69, "Intel"},
                 {0xc0, "Ampere"},    {0x6d, "Microsoft"}};
    const char *p;
    unsigned impl = 0, part = 0;
    size_t i;
    char seen[256] = "";
    out[0] = 0;
    /* collect distinct cores (big.LITTLE lists several) */
    for (p = cpuinfo; (p = strstr(p, "CPU implementer")) != NULL;) {
        const char *q = strchr(p, ':'), *r;
        if (!q)
            break;
        impl = (unsigned)strtoul(q + 1, NULL, 0);
        r = strstr(q, "CPU part");
        if (!r || !(q = strchr(r, ':')))
            break;
        part = (unsigned)strtoul(q + 1, NULL, 0);
        p = q;
        {
            char name[64] = "";
            for (i = 0; i < sizeof(parts) / sizeof(parts[0]); i++)
                if (parts[i].impl == impl && parts[i].part == part)
                    snprintf(name, sizeof(name), "%s", parts[i].name);
            if (!name[0]) {
                const char *iv = "unknown";
                for (i = 0; i < sizeof(impls) / sizeof(impls[0]); i++)
                    if (impls[i].impl == impl)
                        iv = impls[i].name;
                snprintf(name, sizeof(name), "%s part 0x%03x", iv, part);
            }
            if (!strstr(seen, name)) {
                size_t l = strlen(seen);
                snprintf(seen + l, sizeof(seen) - l, "%s%s", l ? " + " : "", name);
            }
        }
    }
    snprintf(out, len, "%s", seen);
}
#endif

static void cpu_identify(void)
{
    char model[256] = "", features[512] = "";
    uint32_t r[4];

    if (stx_cpuid(0, 0, r)) {
        char vendor[13];
        uint32_t max_ext, max_std = r[0];
        memcpy(vendor, &r[1], 4);
        memcpy(vendor + 4, &r[3], 4);
        memcpy(vendor + 8, &r[2], 4);
        vendor[12] = 0;
        stx_info("vendor", "%s", vendor);
        stx_cpuid(0x80000000, 0, r);
        max_ext = r[0];
        if (max_ext >= 0x80000004) {
            uint32_t b[12];
            char *s;
            stx_cpuid(0x80000002, 0, b);
            stx_cpuid(0x80000003, 0, b + 4);
            stx_cpuid(0x80000004, 0, b + 8);
            memcpy(model, b, 48);
            model[48] = 0;
            for (s = model; *s == ' '; s++)
                ;
            memmove(model, s, strlen(s) + 1);
        }
        stx_cpuid(1, 0, r);
        {
            uint32_t fam = (r[0] >> 8) & 0xf, mod = (r[0] >> 4) & 0xf, step = r[0] & 0xf;
            if (fam == 0xf)
                fam += (r[0] >> 20) & 0xff;
            if (fam == 6 || fam >= 0xf)
                mod |= ((r[0] >> 16) & 0xf) << 4;
            stx_info("family/model/stepping", "%u / 0x%x / %u", fam, mod, step);
        }
#define FEAT(cond, name) if (cond) strcat(features, name " ")
        FEAT(r[3] & (1u << 26), "sse2");
        FEAT(r[2] & (1u << 0), "sse3");
        FEAT(r[2] & (1u << 9), "ssse3");
        FEAT(r[2] & (1u << 19), "sse4.1");
        FEAT(r[2] & (1u << 20), "sse4.2");
        FEAT(r[2] & (1u << 25), "aes");
        FEAT(r[2] & (1u << 1), "pclmul");
        FEAT(r[2] & (1u << 28), "avx");
        FEAT(r[2] & (1u << 12), "fma");
        FEAT(r[2] & (1u << 5), "vmx");
        if (max_std >= 7) {
            stx_cpuid(7, 0, r);
            FEAT(r[1] & (1u << 5), "avx2");
            FEAT(r[1] & (1u << 3), "bmi1");
            FEAT(r[1] & (1u << 8), "bmi2");
            FEAT(r[1] & (1u << 16), "avx512f");
            FEAT(r[1] & (1u << 29), "sha");
            FEAT(r[2] & (1u << 8), "gfni");
            FEAT(r[2] & (1u << 9), "vaes");
            FEAT(r[3] & (1u << 15), "hybrid");
            FEAT(r[3] & (1u << 24), "amx");
        }
        if (max_ext >= 0x80000001) {
            stx_cpuid(0x80000001, 0, r);
            FEAT(r[2] & (1u << 2), "svm");
        }
#undef FEAT
    }
#if defined(STX_OS_LINUX)
    if (!model[0] || !features[0]) {
        static char buf[65536];
        char *p;
        if (stx_read_text("/proc/cpuinfo", buf, sizeof(buf)) > 0) {
            static const char *keys[] = {"model name", "Model", "Hardware", "cpu model", "uarch", "cpu", NULL};
            int k;
            for (k = 0; keys[k] && !model[0]; k++) {
                p = strstr(buf, keys[k]);
                if (p && (p == buf || p[-1] == '\n')) {
                    p = strchr(p, ':');
                    if (p) {
                        char *e;
                        p += 2;
                        e = strchr(p, '\n');
                        snprintf(model, sizeof(model), "%.*s", e ? (int)(e - p) : (int)strlen(p), p);
                    }
                }
            }
            if (!model[0])
                arm_model(buf, model, sizeof(model));
            if (!features[0]) {
                p = strstr(buf, "Features");
                if (!p)
                    p = strstr(buf, "isa");
                if (p && (p = strchr(p, ':')) != NULL) {
                    char *e;
                    p += 2;
                    e = strchr(p, '\n');
                    snprintf(features, sizeof(features), "%.*s", e ? (int)(e - p) : (int)strlen(p), p);
                }
            }
        }
    }
#elif defined(STX_OS_MACOS)
    if (!model[0]) {
        size_t l = sizeof(model);
        sysctlbyname("machdep.cpu.brand_string", model, &l, NULL, 0);
    }
#elif defined(STX_OS_BSD)
    if (!model[0]) {
        size_t l = sizeof(model);
        sysctlbyname("hw.model", model, &l, NULL, 0);
    }
#endif
    stx_info("model", "%s", model[0] ? model : "unknown");
    if (features[0])
        stx_info("features", "%s", features);
    stx_info("logical cpus", "%d (benchmark threads: %d)", stx_cpu_count(), stx_cfg.threads);

#if defined(STX_OS_LINUX)
    {
        static const char *lvl_name[] = {"", "L1", "L2", "L3", "L4"};
        char p[256], b[64], type[32], caches[256] = "";
        uint64_t khz, lvl;
        int i;
        for (i = 0; i < 8; i++) {
            snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu0/cache/index%d/level", i);
            if (stx_read_u64(p, &lvl) != 0 || lvl > 4)
                continue;
            snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu0/cache/index%d/type", i);
            stx_read_text(p, type, sizeof(type));
            snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu0/cache/index%d/size", i);
            if (stx_read_text(p, b, sizeof(b)) > 0) {
                size_t l = strlen(caches);
                snprintf(caches + l, sizeof(caches) - l, "%s%s%s %s", l ? ", " : "", lvl_name[lvl],
                         strcmp(type, "Data") == 0 ? "d" : strcmp(type, "Instruction") == 0 ? "i" : "", b);
            }
        }
        if (caches[0])
            stx_info("caches (cpu0)", "%s", caches);
        if (stx_read_u64("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", &khz) == 0)
            stx_info("max frequency", "%.0f MHz", khz / 1000.0);
        if (stx_read_text("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", b, sizeof(b)) > 0) {
            char drv[64] = "";
            stx_read_text("/sys/devices/system/cpu/cpu0/cpufreq/scaling_driver", drv, sizeof(drv));
            stx_info("governor", "%s (%s)", b, drv[0] ? drv : "?");
            /* with intel_pstate / amd-pstate "powersave" is the normal dynamic mode */
            if ((strcmp(b, "powersave") == 0 && !strstr(drv, "pstate")) || strcmp(b, "conservative") == 0)
                stx_note("cpufreq governor '%s' may lower results", b);
        }
        if (stx_read_text("/sys/devices/system/cpu/smt/control", b, sizeof(b)) > 0)
            stx_info("smt", "%s", b);
        if (stx_read_text("/sys/devices/system/cpu/vulnerabilities/spectre_v2", b, sizeof(b)) > 0)
            stx_info("spectre_v2", "%.60s", b);
    }
#elif defined(STX_OS_MACOS)
    {
        int64_t v = 0;
        size_t l = sizeof(v);
        if (sysctlbyname("hw.perflevel0.physicalcpu", &v, &l, NULL, 0) == 0)
            stx_info("performance cores", "%lld", (long long)v);
        l = sizeof(v);
        if (sysctlbyname("hw.perflevel1.physicalcpu", &v, &l, NULL, 0) == 0)
            stx_info("efficiency cores", "%lld", (long long)v);
        l = sizeof(v);
        if (sysctlbyname("hw.l2cachesize", &v, &l, NULL, 0) == 0)
            stx_info("L2 cache", "%lld KiB", (long long)(v / 1024));
    }
#endif
}

void stx_mod_cpu(void)
{
    double st_int, st_fp, mt_int, mt_fp, r;
    uint32_t **sort_bufs;
    void **ctxs;
    int i, n = stx_cfg.threads;

    stx_section("cpu", "CPU");
    cpu_identify();

    if (stx_k.k_clock) {
        stx_progress("measuring effective clock");
        r = stx_measure(run_clock, NULL, stx_cfg.unit_time);
        stx_metric("effective clock (1 thread)", r * STX_CLOCK_ADDS_PER_ITER / 1e6, "MHz", 0, STX_HIGHER);
    }

    stx_progress("integer, 1 thread");
    st_int = stx_measure(run_int, NULL, stx_cfg.unit_time) * STX_INT_OPS_PER_ITER / 1e9;
    stx_metric("integer 1 thread", st_int, "GIOPS", REF_INT_ST, STX_HIGHER);

    stx_progress("floating point, 1 thread");
    st_fp = stx_measure(run_fp, NULL, stx_cfg.unit_time) * STX_FP_FLOPS_PER_ITER / 1e9;
    stx_metric("float64 1 thread", st_fp, "GFLOPS", REF_FP_ST, STX_HIGHER);

    sort_bufs = (uint32_t **)calloc((size_t)n, sizeof(*sort_bufs));
    ctxs = (void **)calloc((size_t)n, sizeof(void *));
    for (i = 0; i < n; i++) {
        sort_bufs[i] = (uint32_t *)stx_alloc(SORT_N * sizeof(uint32_t), 64);
        ctxs[i] = sort_bufs[i];
    }
    stx_progress("branchy sort, 1 thread");
    r = stx_measure(run_sort, sort_bufs[0], stx_cfg.unit_time) * SORT_N / 1e6;
    stx_metric("sort 1 thread", r, "Melem/s", REF_SORT_ST, STX_HIGHER);

    stx_progress("chacha20, 1 thread");
    r = stx_measure(run_chacha, NULL, stx_cfg.unit_time) * 64 / 1e6;
    stx_metric("chacha20 1 thread", r, "MB/s", REF_CHACHA_ST, STX_HIGHER);

    stx_progress("integer, %d threads", n);
    mt_int = measure_mt(run_int, NULL) * STX_INT_OPS_PER_ITER / 1e9;
    stx_metric("integer all threads", mt_int, "GIOPS", REF_INT_MT, STX_HIGHER);

    stx_progress("floating point, %d threads", n);
    mt_fp = measure_mt(run_fp, NULL) * STX_FP_FLOPS_PER_ITER / 1e9;
    stx_metric("float64 all threads", mt_fp, "GFLOPS", REF_FP_MT, STX_HIGHER);

    stx_progress("chacha20, %d threads", n);
    r = measure_mt(run_chacha, NULL) * 64 / 1e6;
    stx_metric("chacha20 all threads", r, "MB/s", REF_CHACHA_MT, STX_HIGHER);

    stx_progress("sort, %d threads", n);
    r = measure_mt(run_sort, ctxs) * SORT_N / 1e6;
    stx_metric("sort all threads", r, "Melem/s", 0, STX_HIGHER);

    for (i = 0; i < n; i++)
        stx_free(sort_bufs[i]);
    free(sort_bufs);
    free(ctxs);

    if (st_int > 0 && n > 1) {
        double speedup = mt_int / st_int;
        stx_metric("multi-core speedup", speedup, "x", 0, STX_HIGHER);
        stx_info("scaling efficiency", "%.0f %% of %d threads", 100.0 * speedup / n, n);
        if (speedup < 0.5 * n / 2.0 && n >= 4)
            stx_check(STX_WARN, "cpu scaling", "only %.1fx speedup on %d threads (busy system, cgroup limit or "
                                              "throttling?)", speedup, n);
        else
            stx_check(STX_PASS, "cpu scaling", "%.1fx speedup on %d threads", speedup, n);
    }
}
