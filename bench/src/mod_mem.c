/* Memory: bandwidth, cache/DRAM latency ladder, DIMM inventory from raw SMBIOS. */
#include "stx.h"

#include <stdlib.h>
#include <string.h>

/* reference system: Dell Precision 3581, i7-13700H, dual channel DDR5 */
#define REF_READ_ST 20.0  /* GB/s */
#define REF_READ_MT 34.0  /* GB/s */
#define REF_WRITE_MT 41.0 /* GB/s */
#define REF_COPY_MT 52.0  /* GB/s */
#define REF_LAT_DRAM 110.0 /* ns */

static volatile uint64_t sink;

typedef struct {
    uint64_t *buf, *dst;
    size_t words;
    int mode; /* 0 read, 1 write, 2 copy */
} bw_job;

static void bw_worker(void *p)
{
    bw_job *j = (bw_job *)p;
    if (j->mode == 0)
        sink += stx_k.k_read(j->buf, j->words);
    else if (j->mode == 1)
        stx_k.k_write(j->buf, j->words, 0x5555AAAA5555AAAAULL);
    else
        memcpy(j->dst, j->buf, j->words * 8);
}

typedef struct {
    bw_job *jobs;
    void **args;
    int threads;
} bw_ctx;

static double run_bw(uint64_t n, void *p)
{
    bw_ctx *c = (bw_ctx *)p;
    double t = stx_now();
    uint64_t k;
    for (k = 0; k < n; k++)
        stx_run_threads(c->threads, bw_worker, c->args);
    return stx_now() - t;
}

/* bytes/s moved for the whole buffer per pass */
static double bandwidth(uint64_t *buf, uint64_t *dst, size_t words, int threads, int mode)
{
    bw_ctx c;
    size_t per = (words / (size_t)threads) & ~(size_t)7;
    double rate;
    int i;
    c.threads = threads;
    c.jobs = (bw_job *)calloc((size_t)threads, sizeof(bw_job));
    c.args = (void **)calloc((size_t)threads, sizeof(void *));
    for (i = 0; i < threads; i++) {
        c.jobs[i].buf = buf + per * (size_t)i;
        c.jobs[i].dst = dst ? dst + per * (size_t)i : NULL;
        c.jobs[i].words = per;
        c.jobs[i].mode = mode;
        c.args[i] = &c.jobs[i];
    }
    rate = stx_measure(run_bw, &c, stx_cfg.unit_time * 2) * (double)(per * (size_t)threads * 8);
    if (mode == 2)
        rate *= 2; /* copy reads and writes */
    free(c.jobs);
    free(c.args);
    return rate;
}

/* random cyclic pointer chain, one pointer per 64-byte line (Sattolo shuffle) */
static void build_chain(void **buf, size_t bytes)
{
    size_t lines = bytes / 64, i;
    size_t *idx = (size_t *)malloc(lines * sizeof(size_t));
    uint64_t s = 0x9876543210ULL;
    if (!idx)
        return;
    for (i = 0; i < lines; i++)
        idx[i] = i;
    for (i = lines - 1; i > 0; i--) {
        size_t j = (size_t)(stx_rand(&s) % i), t = idx[i];
        idx[i] = idx[j];
        idx[j] = t;
    }
    for (i = 0; i < lines; i++)
        buf[idx[i] * 8] = &buf[idx[(i + 1) % lines] * 8];
    free(idx);
}

static double run_chase(uint64_t n, void *p)
{
    double t = stx_now();
    sink += (uint64_t)(size_t)stx_k.k_chase(p, n * 64);
    return stx_now() - t;
}

static double latency_ns(void *buf, size_t bytes)
{
    double rate;
    build_chain((void **)buf, bytes);
    rate = stx_measure(run_chase, buf, stx_cfg.unit_time / 2) * 64;
    return rate > 0 ? 1e9 / rate : 0;
}

#ifdef STX_OS_LINUX
/* ---- SMBIOS type 16 (memory array) and 17 (memory device) ---- */
static const char *smbios_string(const uint8_t *s, int len, int idx)
{
    const uint8_t *p = s + s[1], *end = s + len;
    if (idx == 0)
        return "";
    while (p < end && --idx > 0) {
        while (p < end && *p)
            p++;
        p++;
    }
    return (p < end && *p) ? (const char *)p : "";
}

typedef struct {
    int populated, slots;
    uint64_t total_mb;
    int speed, conf_speed;
    char type[16], part[64], maker[64];
    int ecc; /* -1 unknown */
} dimm_info;

static void smbios_cb(const char *name, void *ctx)
{
    dimm_info *d = (dimm_info *)ctx;
    char path[256];
    uint8_t raw[1024];
    FILE *f;
    int len;
    if (strncmp(name, "17-", 3) != 0 && strncmp(name, "16-", 3) != 0)
        return;
    snprintf(path, sizeof(path), "/sys/firmware/dmi/entries/%s/raw", name);
    f = fopen(path, "rb");
    if (!f)
        return;
    len = (int)fread(raw, 1, sizeof(raw) - 1, f);
    fclose(f);
    if (len < 4)
        return;
    raw[len] = 0;
    if (raw[0] == 16 && raw[1] > 6) {
        uint8_t ecc = raw[6];
        /* 3 = none, 4 = parity, 5 = single-bit ECC, 6 = multi-bit ECC, 7 = CRC */
        if (ecc >= 3 && ecc <= 7 && d->ecc != 1)
            d->ecc = ecc >= 4;
        return;
    }
    if (raw[0] == 17 && raw[1] >= 0x15) {
        static const char *types[] = {"?",     "Other", "Unknown", "DRAM",   "EDRAM",  "VRAM",   "SRAM",
                                      "RAM",   "ROM",   "Flash",   "EEPROM", "FEPROM", "EPROM",  "CDRAM",
                                      "3DRAM", "SDRAM", "SGRAM",   "RDRAM",  "DDR",    "DDR2",   "DDR2 FB",
                                      "",      "",      "",        "DDR3",   "FBD2",   "DDR4",   "LPDDR",
                                      "LPDDR2", "LPDDR3", "LPDDR4", "Logical NV", "HBM", "HBM2", "DDR5", "LPDDR5",
                                      "HBM3"};
        uint16_t size = (uint16_t)(raw[0x0C] | raw[0x0D] << 8);
        uint64_t mb;
        d->slots++;
        if (size == 0 || size == 0xFFFF)
            return;
        if (size == 0x7FFF && raw[1] >= 0x20)
            mb = (uint64_t)(raw[0x1C] | raw[0x1D] << 8 | raw[0x1E] << 16 | (uint32_t)raw[0x1F] << 24);
        else
            mb = (size & 0x8000) ? (size & 0x7FFF) / 1024 : size;
        d->populated++;
        d->total_mb += mb;
        if (raw[0x12] < sizeof(types) / sizeof(types[0]))
            snprintf(d->type, sizeof(d->type), "%s", types[raw[0x12]]);
        d->speed = raw[0x15] | raw[0x16] << 8;
        if (raw[1] >= 0x22)
            d->conf_speed = raw[0x20] | raw[0x21] << 8;
        if (!d->maker[0])
            snprintf(d->maker, sizeof(d->maker), "%s", smbios_string(raw, len, raw[0x17]));
        if (!d->part[0] && raw[1] > 0x1A)
            snprintf(d->part, sizeof(d->part), "%s", smbios_string(raw, len, raw[0x1A]));
    }
}

static void dimm_report(void)
{
    dimm_info d;
    memset(&d, 0, sizeof(d));
    d.ecc = -1;
    if (stx_list_dir("/sys/firmware/dmi/entries", smbios_cb, &d) < 0 || d.slots == 0) {
        stx_note("DIMM details need root (SMBIOS tables)");
        return;
    }
    stx_info("dimm slots", "%d populated of %d", d.populated, d.slots);
    if (d.populated) {
        stx_info("dimm type", "%s, %llu GiB total", d.type, (unsigned long long)(d.total_mb / 1024));
        if (d.speed)
            stx_info("dimm speed", "%d MT/s rated, %d MT/s configured", d.speed, d.conf_speed ? d.conf_speed : d.speed);
        if (d.maker[0] || d.part[0])
            stx_info("dimm part", "%s %s", d.maker, d.part);
        if (d.conf_speed && d.speed && d.conf_speed < d.speed)
            stx_check(STX_WARN, "dimm speed", "running at %d of %d MT/s", d.conf_speed, d.speed);
    }
    if (d.ecc >= 0)
        stx_info("ecc", "%s", d.ecc ? "yes" : "no");
}

static void edac_report(void)
{
    uint64_t ce = 0, ue = 0, v;
    char p[128];
    int i, found = 0;
    for (i = 0; i < 16; i++) {
        snprintf(p, sizeof(p), "/sys/devices/system/edac/mc/mc%d/ce_count", i);
        if (stx_read_u64(p, &v) != 0)
            continue;
        found = 1;
        ce += v;
        snprintf(p, sizeof(p), "/sys/devices/system/edac/mc/mc%d/ue_count", i);
        if (stx_read_u64(p, &v) == 0)
            ue += v;
    }
    if (!found)
        return;
    if (ue)
        stx_check(STX_FAIL, "ecc errors", "%llu uncorrectable, %llu corrected", (unsigned long long)ue,
                  (unsigned long long)ce);
    else if (ce)
        stx_check(STX_WARN, "ecc errors", "%llu corrected errors since boot", (unsigned long long)ce);
    else
        stx_check(STX_PASS, "ecc errors", "no corrected or uncorrectable errors");
}
#endif

void stx_mod_mem(void)
{
    uint64_t total = stx_mem_total();
    size_t bytes = 512u << 20, words;
    uint64_t *buf, *dst;
    static const size_t lat_sizes[] = {16u << 10, 128u << 10, 1u << 20, 8u << 20, 32u << 20, 128u << 20, 512u << 20};
    double r, lat = 0;
    size_t i;
    int threads = stx_cfg.threads;

    stx_section("mem", "Memory");
    if (total)
        stx_info("installed", "%.1f GiB", total / 1073741824.0);
#ifdef STX_OS_LINUX
    dimm_report();
    edac_report();
#endif
    if (total && bytes > total / 8)
        bytes = (size_t)(total / 8);
    if (sizeof(size_t) == 4 && bytes > (128u << 20))
        bytes = 128u << 20;
    if (bytes < (16u << 20))
        bytes = 16u << 20;
    bytes &= ~(size_t)((1u << 20) - 1);
    buf = (uint64_t *)stx_alloc(bytes, 4096);
    dst = (uint64_t *)stx_alloc(bytes, 4096);
    if (!buf || !dst) {
        stx_skip("cannot allocate %zu MiB", bytes >> 20);
        stx_free(buf);
        stx_free(dst);
        return;
    }
    words = bytes / 8;
    stx_k.k_write(buf, words, 1); /* fault in pages */
    stx_k.k_write(dst, words, 2);
    stx_info("test buffer", "%zu MiB", bytes >> 20);

    stx_progress("read bandwidth, 1 thread");
    r = bandwidth(buf, NULL, words, 1, 0) / 1e9;
    stx_metric("read 1 thread", r, "GB/s", REF_READ_ST, STX_HIGHER);
    stx_progress("read bandwidth, %d threads", threads);
    r = bandwidth(buf, NULL, words, threads, 0) / 1e9;
    stx_metric("read all threads", r, "GB/s", REF_READ_MT, STX_HIGHER);
    stx_progress("write bandwidth");
    r = bandwidth(buf, NULL, words, threads, 1) / 1e9;
    stx_metric("write all threads", r, "GB/s", REF_WRITE_MT, STX_HIGHER);
    stx_progress("copy bandwidth");
    r = bandwidth(buf, dst, words, threads, 2) / 1e9;
    stx_metric("copy all threads", r, "GB/s", REF_COPY_MT, STX_HIGHER);
    stx_free(dst);

    for (i = 0; i < sizeof(lat_sizes) / sizeof(lat_sizes[0]); i++) {
        char key[48];
        size_t sz = lat_sizes[i];
        if (sz > bytes)
            break;
        stx_progress("latency %zu KiB", sz >> 10);
        lat = latency_ns(buf, sz);
        if (sz >= (1u << 20))
            snprintf(key, sizeof(key), "latency %zu MiB block", sz >> 20);
        else
            snprintf(key, sizeof(key), "latency %zu KiB block", sz >> 10);
        stx_metric(key, lat, "ns", 0, STX_LOWER);
    }
    if (lat > 0)
        stx_metric("dram latency (largest block)", lat, "ns", REF_LAT_DRAM, STX_LOWER);
    stx_free(buf);
}
