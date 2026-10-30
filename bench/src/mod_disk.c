/*
 * Disk: file based I/O test (sequential, random, sync writes) and read-only
 * raw block device tests with native NVMe/SATA SMART health (Linux).
 * Raw devices are opened O_RDONLY and are never written.
 */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _GNU_SOURCE
#endif
#include "stx.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef STX_OS_WINDOWS
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#endif
#ifdef STX_OS_LINUX
#include <sys/statfs.h>
#include <scsi/sg.h>
#endif

/* reference system: Samsung PM9A1-class PCIe 4.0 NVMe, ext4 */
#define REF_SEQ_WRITE 2300.0  /* MB/s */
#define REF_SEQ_READ 2400.0   /* MB/s */
#define REF_RAND_READ 9000.0 /* IOPS QD1 */
#define REF_RAND_READ_MT 235000.0
#define REF_SYNC_WRITE 120.0 /* IOPS, write + fsync */
#define REF_DEV_SEQ 2400.0
#define REF_DEV_RAND 9000.0
#define REF_DEV_RAND_MT 235000.0

#define BLK (1u << 20)
#define SMALL 4096u

typedef struct {
#ifdef STX_OS_WINDOWS
    HANDLE h;
#else
    int fd;
#endif
    int direct;
} dfile;

static int d_open(dfile *f, const char *path, int write, int create)
{
#ifdef STX_OS_WINDOWS
    DWORD acc = GENERIC_READ | (write ? GENERIC_WRITE : 0);
    f->h = CreateFileA(path, acc, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, create ? CREATE_ALWAYS : OPEN_EXISTING,
                       FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    f->direct = 1;
    return f->h == INVALID_HANDLE_VALUE ? -1 : 0;
#else
    int flags = write ? O_RDWR : O_RDONLY;
    if (create)
        flags |= O_CREAT | O_TRUNC;
    f->direct = 0;
#ifdef O_DIRECT
    f->fd = open(path, flags | O_DIRECT, 0600);
    if (f->fd >= 0) {
        f->direct = 1;
        return 0;
    }
#endif
    f->fd = open(path, flags, 0600);
    if (f->fd < 0)
        return -1;
#ifdef F_NOCACHE
    if (fcntl(f->fd, F_NOCACHE, 1) == 0)
        f->direct = 1;
#endif
    return 0;
#endif
}

static long d_pread(dfile *f, void *buf, size_t len, uint64_t off)
{
#ifdef STX_OS_WINDOWS
    OVERLAPPED o;
    DWORD got = 0;
    memset(&o, 0, sizeof(o));
    o.Offset = (DWORD)off;
    o.OffsetHigh = (DWORD)(off >> 32);
    return ReadFile(f->h, buf, (DWORD)len, &got, &o) ? (long)got : -1;
#else
    return (long)pread(f->fd, buf, len, (off_t)off);
#endif
}

static long d_pwrite(dfile *f, const void *buf, size_t len, uint64_t off)
{
#ifdef STX_OS_WINDOWS
    OVERLAPPED o;
    DWORD put = 0;
    memset(&o, 0, sizeof(o));
    o.Offset = (DWORD)off;
    o.OffsetHigh = (DWORD)(off >> 32);
    return WriteFile(f->h, buf, (DWORD)len, &put, &o) ? (long)put : -1;
#else
    return (long)pwrite(f->fd, buf, len, (off_t)off);
#endif
}

static void d_sync(dfile *f)
{
#ifdef STX_OS_WINDOWS
    FlushFileBuffers(f->h);
#else
    fsync(f->fd);
#endif
}

static void d_drop_cache(dfile *f)
{
#if defined(POSIX_FADV_DONTNEED) && !defined(STX_OS_WINDOWS)
    if (!f->direct)
        posix_fadvise(f->fd, 0, 0, POSIX_FADV_DONTNEED);
#else
    (void)f;
#endif
}

static void d_close(dfile *f)
{
#ifdef STX_OS_WINDOWS
    CloseHandle(f->h);
#else
    close(f->fd);
#endif
}

/* ---- timed loops ---- */
static double seq_read(dfile *f, uint64_t size, uint8_t *buf, double tmax)
{
    uint64_t off = 0, done = 0;
    double t0 = stx_now(), t;
    do {
        long n = d_pread(f, buf, BLK, off);
        if (n <= 0)
            break;
        done += (uint64_t)n;
        off += BLK;
        if (off + BLK > size)
            off = 0;
        t = stx_now() - t0;
    } while (t < tmax);
    t = stx_now() - t0;
    return t > 0 ? done / t / 1e6 : 0;
}

typedef struct {
    dfile *f;
    uint64_t blocks;
    double tmax;
    uint64_t ops;
    uint64_t seed;
    int write_sync;
    int errors;
} rand_job;

static void rand_worker(void *p)
{
    rand_job *j = (rand_job *)p;
    uint8_t *buf = (uint8_t *)stx_alloc(SMALL, SMALL);
    double t0 = stx_now();
    if (!buf)
        return;
    memset(buf, 0x5a, SMALL);
    while (stx_now() - t0 < j->tmax) {
        uint64_t off = (stx_rand(&j->seed) % j->blocks) * SMALL;
        long n;
        if (j->write_sync) {
            n = d_pwrite(j->f, buf, SMALL, off);
            d_sync(j->f);
        } else {
            n = d_pread(j->f, buf, SMALL, off);
        }
        if (n != (long)SMALL) {
            if (++j->errors > 16)
                break;
            continue;
        }
        j->ops++;
    }
    stx_free(buf);
}

/* returns IOPS of random 4K operations with `threads` workers (each queue depth 1) */
static double rand_io(dfile *f, uint64_t size, int threads, int write_sync, double tmax)
{
    rand_job *jobs = (rand_job *)calloc((size_t)threads, sizeof(rand_job));
    void **args = (void **)calloc((size_t)threads, sizeof(void *));
    uint64_t ops = 0;
    double t;
    int i;
    for (i = 0; i < threads; i++) {
        jobs[i].f = f;
        jobs[i].blocks = size / SMALL;
        jobs[i].tmax = tmax;
        jobs[i].seed = 0xABCDEF + (uint64_t)i * 7919;
        jobs[i].write_sync = write_sync;
        args[i] = &jobs[i];
    }
    t = stx_now();
    stx_run_threads(threads, rand_worker, args);
    t = stx_now() - t;
    for (i = 0; i < threads; i++)
        ops += jobs[i].ops;
    free(jobs);
    free(args);
    return t > 0 ? ops / t : 0;
}

/* ---- file test ---- */
static int memory_backed(const char *dir, char *fstype, size_t len)
{
#ifdef STX_OS_LINUX
    struct statfs s;
    snprintf(fstype, len, "unknown");
    if (statfs(dir, &s) != 0)
        return 0;
    switch ((unsigned long)s.f_type) {
    case 0x01021994: snprintf(fstype, len, "tmpfs"); return 1;
    case 0x858458f6: snprintf(fstype, len, "ramfs"); return 1;
    case 0x794c7630: snprintf(fstype, len, "overlayfs"); return 0;
    case 0xEF53: snprintf(fstype, len, "ext2/3/4"); return 0;
    case 0x58465342: snprintf(fstype, len, "xfs"); return 0;
    case 0x9123683E: snprintf(fstype, len, "btrfs"); return 0;
    case 0x2FC12FC1: snprintf(fstype, len, "zfs"); return 0;
    case 0x6969: snprintf(fstype, len, "nfs"); return 0;
    case 0x4d44: snprintf(fstype, len, "vfat"); return 0;
    case 0x5346544e: snprintf(fstype, len, "ntfs"); return 0;
    case 0xF2F52010: snprintf(fstype, len, "f2fs"); return 0;
    default: snprintf(fstype, len, "0x%lx", (unsigned long)s.f_type); return 0;
    }
#else
    (void)dir;
    snprintf(fstype, len, "n/a");
    return 0;
#endif
}

static void file_test(void)
{
    char path[1024], fstype[32];
    uint64_t size = stx_cfg.disk_size & ~(uint64_t)(BLK - 1), off;
    uint8_t *buf;
    dfile f;
    double t, r;
    int scored;

    snprintf(path, sizeof(path), "%s/.stupix-bench-%d.tmp", stx_cfg.disk_path, (int)(stx_now() * 1000) % 100000);
    scored = !memory_backed(stx_cfg.disk_path, fstype, sizeof(fstype));
    stx_info("file test", "%s (%s, %llu MiB)", stx_cfg.disk_path, fstype, (unsigned long long)(size >> 20));
    if (!scored)
        stx_note("%s is memory backed: file results are not scored (use --disk-path on a real disk)", fstype);

    buf = (uint8_t *)stx_alloc(BLK, 4096);
    if (!buf || d_open(&f, path, 1, 1) != 0) {
        stx_skip("cannot create test file in %s (%s)", stx_cfg.disk_path, strerror(errno));
        stx_free(buf);
        return;
    }
    if (!f.direct)
        stx_note("direct I/O not supported here, using buffered I/O with cache flush");
    {
        uint64_t s = 42;
        size_t i;
        for (i = 0; i < BLK / 8; i++)
            ((uint64_t *)buf)[i] = stx_rand(&s); /* incompressible */
    }

    stx_progress("sequential write");
    t = stx_now();
    for (off = 0; off < size; off += BLK) {
        if (d_pwrite(&f, buf, BLK, off) != (long)BLK) {
            stx_skip("write failed at %llu MiB (%s)", (unsigned long long)(off >> 20), strerror(errno));
            d_close(&f);
            remove(path);
            stx_free(buf);
            return;
        }
    }
    d_sync(&f);
    t = stx_now() - t;
    stx_metric("file seq write 1M", size / t / 1e6, "MB/s", scored ? REF_SEQ_WRITE : 0, STX_HIGHER);

    d_drop_cache(&f);
    stx_progress("sequential read");
    r = seq_read(&f, size, buf, stx_cfg.unit_time * 2);
    stx_metric("file seq read 1M", r, "MB/s", scored ? REF_SEQ_READ : 0, STX_HIGHER);

    d_drop_cache(&f);
    stx_progress("random read QD1");
    r = rand_io(&f, size, 1, 0, stx_cfg.unit_time * 2);
    stx_metric("file rand read 4K QD1", r, "IOPS", scored && f.direct ? REF_RAND_READ : 0, STX_HIGHER);

    stx_progress("random read 16 threads");
    r = rand_io(&f, size, 16, 0, stx_cfg.unit_time * 2);
    stx_metric("file rand read 4K 16 thr", r, "IOPS", scored && f.direct ? REF_RAND_READ_MT : 0, STX_HIGHER);

    stx_progress("sync write");
    r = rand_io(&f, size, 1, 1, stx_cfg.unit_time * 2);
    stx_metric("file 4K write+fsync", r, "IOPS", scored ? REF_SYNC_WRITE : 0, STX_HIGHER);
    if (r > 0)
        stx_info("fsync latency", "%.3f ms", 1000.0 / r);

    d_close(&f);
    remove(path);
    stx_free(buf);
}

#ifdef STX_OS_LINUX
/* ---- NVMe SMART / health log via admin passthrough ---- */
struct stx_nvme_admin_cmd {
    uint8_t opcode, flags;
    uint16_t rsvd1;
    uint32_t nsid, cdw2, cdw3;
    uint64_t metadata, addr;
    uint32_t metadata_len, data_len;
    uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
    uint32_t timeout_ms, result;
};
#define STX_NVME_IOCTL_ADMIN_CMD _IOWR('N', 0x41, struct stx_nvme_admin_cmd)

static double u128_le(const uint8_t *p)
{
    double v = 0;
    int i;
    for (i = 15; i >= 0; i--)
        v = v * 256.0 + p[i];
    return v;
}

static void nvme_smart(const char *dev)
{
    struct stx_nvme_admin_cmd c;
    uint8_t log[512];
    int fd = open(dev, O_RDONLY);
    if (fd < 0)
        return;
    memset(&c, 0, sizeof(c));
    memset(log, 0, sizeof(log));
    c.opcode = 0x02; /* get log page */
    c.nsid = 0xFFFFFFFF;
    c.addr = (uint64_t)(uintptr_t)log;
    c.data_len = sizeof(log);
    c.cdw10 = 0x02 | ((sizeof(log) / 4 - 1) << 16); /* SMART / health information */
    if (ioctl(fd, STX_NVME_IOCTL_ADMIN_CMD, &c) == 0) {
        int temp = (log[1] | log[2] << 8) - 273;
        double media_err = u128_le(log + 160), hours = u128_le(log + 128), written = u128_le(log + 48);
        stx_info("  temperature", "%d C", temp);
        stx_info("  wear (percentage used)", "%u %%, spare %u %%", log[5], log[3]);
        stx_info("  power on hours", "%.0f", hours);
        stx_info("  data written", "%.1f TB", written * 512000.0 / 1e12);
        if (log[0])
            stx_check(STX_FAIL, "nvme health", "%s critical warning 0x%02x", dev, log[0]);
        else if (media_err > 0)
            stx_check(STX_WARN, "nvme health", "%s %.0f media errors", dev, media_err);
        else if (log[5] >= 90)
            stx_check(STX_WARN, "nvme health", "%s %u %% of rated endurance used", dev, log[5]);
        else
            stx_check(STX_PASS, "nvme health", "%s ok", dev);
        if (temp >= 70)
            stx_check(STX_WARN, "nvme temperature", "%s at %d C", dev, temp);
    }
    close(fd);
}

/* ---- SATA SMART via SCSI ATA PASS-THROUGH(16) ---- */
static int ata_pt(int fd, uint8_t feature, int data_in, uint8_t *buf, uint8_t *sense)
{
    uint8_t cdb[16];
    sg_io_hdr_t io;
    memset(cdb, 0, sizeof(cdb));
    memset(&io, 0, sizeof(io));
    cdb[0] = 0x85;
    cdb[1] = data_in ? (4 << 1) : (3 << 1);   /* PIO data-in / non-data */
    cdb[2] = data_in ? 0x0e : 0x20;           /* t_dir|byt_blok|t_length=sector count, or ck_cond */
    cdb[4] = feature;
    cdb[6] = data_in ? 1 : 0;
    cdb[10] = 0x4f;
    cdb[12] = 0xc2;
    cdb[14] = 0xb0; /* SMART */
    io.interface_id = 'S';
    io.cmd_len = sizeof(cdb);
    io.cmdp = cdb;
    io.dxfer_direction = data_in ? SG_DXFER_FROM_DEV : SG_DXFER_NONE;
    io.dxferp = buf;
    io.dxfer_len = data_in ? 512 : 0;
    io.sbp = sense;
    io.mx_sb_len = 32;
    io.timeout = 5000;
    if (ioctl(fd, SG_IO, &io) != 0)
        return -1;
    if (data_in && (io.status != 0 || io.host_status != 0 || io.driver_status != 0))
        return -1;
    return 0;
}

static void sata_smart(const char *dev)
{
    uint8_t data[512], sense[32];
    int fd = open(dev, O_RDONLY | O_NONBLOCK), i;
    if (fd < 0)
        return;
    memset(sense, 0, sizeof(sense));
    if (ata_pt(fd, 0xda, 0, NULL, sense) == 0 && sense[0] == 0x72 && sense[8] == 0x09) {
        uint8_t mid = sense[17], high = sense[19];
        if (mid == 0xf4 && high == 0x2c)
            stx_check(STX_FAIL, "smart", "%s reports threshold exceeded (disk failing)", dev);
        else if (mid == 0x4f && high == 0xc2)
            stx_check(STX_PASS, "smart", "%s overall health passed", dev);
    }
    if (ata_pt(fd, 0xd0, 1, data, sense) == 0) {
        uint64_t realloc = 0, pending = 0, uncorr = 0;
        for (i = 0; i < 30; i++) {
            const uint8_t *a = data + 2 + i * 12;
            uint64_t raw = (uint64_t)a[5] | (uint64_t)a[6] << 8 | (uint64_t)a[7] << 16 | (uint64_t)a[8] << 24;
            switch (a[0]) {
            case 5: realloc = raw; break;
            case 9: stx_info("  power on hours", "%llu", (unsigned long long)(raw & 0xffffffff)); break;
            case 194: stx_info("  temperature", "%u C", a[5]); break;
            case 197: pending = raw; break;
            case 198: uncorr = raw; break;
            default: break;
            }
        }
        if (realloc || pending || uncorr)
            stx_check(pending || uncorr ? STX_FAIL : STX_WARN, "smart sectors",
                      "%s reallocated %llu, pending %llu, uncorrectable %llu", dev, (unsigned long long)realloc,
                      (unsigned long long)pending, (unsigned long long)uncorr);
    }
    close(fd);
}

/* ---- raw block devices ---- */
static int skip_block(const char *n)
{
    static const char *pre[] = {"loop", "ram", "zram", "sr", "fd", "dm-", "nbd", "md", "mmcblk0boot", NULL};
    int i;
    for (i = 0; pre[i]; i++)
        if (strncmp(n, pre[i], strlen(pre[i])) == 0)
            return 1;
    return 0;
}

static void dev_test(const char *name)
{
    char p[512], dev[300], model[128] = "", b[64];
    uint64_t sectors = 0, rot = 0, size;
    uint8_t *buf;
    dfile f;
    double r;

    snprintf(dev, sizeof(dev), "/dev/%s", name);
    snprintf(p, sizeof(p), "/sys/block/%s/size", name);
    if (stx_read_u64(p, &sectors) != 0 || sectors == 0)
        return;
    size = sectors * 512;
    snprintf(p, sizeof(p), "/sys/block/%s/device/model", name);
    stx_read_text(p, model, sizeof(model));
    snprintf(p, sizeof(p), "/sys/block/%s/queue/rotational", name);
    stx_read_u64(p, &rot);
    snprintf(p, sizeof(p), "/sys/block/%s/device/transport", name);
    if (stx_read_text(p, b, sizeof(b)) <= 0)
        snprintf(b, sizeof(b), "%s", strncmp(name, "nvme", 4) == 0 ? "nvme" : rot ? "hdd" : "ssd");
    stx_info(dev, "%s, %.0f GB, %s", model[0] ? model : "unknown model", size / 1e9, rot ? "rotational" : b);

    if (strncmp(name, "nvme", 4) == 0)
        nvme_smart(dev);
    else if (strncmp(name, "sd", 2) == 0)
        sata_smart(dev);

    if (size < 64u * BLK) {
        stx_note("%s too small for throughput tests", dev);
        return;
    }
    buf = (uint8_t *)stx_alloc(BLK, 4096);
    if (!buf)
        return;
    if (d_open(&f, dev, 0, 0) != 0) {
        stx_note("cannot open %s read-only (%s)", dev, strerror(errno));
        stx_free(buf);
        return;
    }
    {
        char key[96];
        stx_progress("%s sequential read", dev);
        r = seq_read(&f, size, buf, stx_cfg.unit_time * 2);
        snprintf(key, sizeof(key), "%s seq read 1M", name);
        stx_metric(key, r, "MB/s", f.direct ? REF_DEV_SEQ : 0, STX_HIGHER);
        stx_progress("%s random read QD1", dev);
        r = rand_io(&f, size, 1, 0, stx_cfg.unit_time * 2);
        snprintf(key, sizeof(key), "%s rand read 4K QD1", name);
        stx_metric(key, r, "IOPS", f.direct ? REF_DEV_RAND : 0, STX_HIGHER);
        if (r > 0)
            stx_info("  read latency QD1", "%.3f ms", 1000.0 / r);
        stx_progress("%s random read 16 threads", dev);
        r = rand_io(&f, size, 16, 0, stx_cfg.unit_time * 2);
        snprintf(key, sizeof(key), "%s rand read 4K 16 thr", name);
        stx_metric(key, r, "IOPS", f.direct ? REF_DEV_RAND_MT : 0, STX_HIGHER);
    }
    d_close(&f);
    stx_free(buf);
}

static void block_cb(const char *name, void *ctx)
{
    char p[256];
    uint64_t removable = 0;
    int *count = (int *)ctx;
    if (skip_block(name))
        return;
    snprintf(p, sizeof(p), "/sys/block/%s/removable", name);
    stx_read_u64(p, &removable);
    if (removable)
        return;
    (*count)++;
    dev_test(name);
}
#endif

void stx_mod_disk(void)
{
    stx_section("disk", "Storage");
    if (stx_cfg.disk_path)
        file_test();
#ifdef STX_OS_LINUX
    if (strcmp(stx_cfg.disk_dev, "none") != 0) {
        if (!stx_is_root()) {
            stx_skip("raw device tests and SMART need root");
        } else if (strcmp(stx_cfg.disk_dev, "auto") == 0) {
            int count = 0;
            stx_list_dir("/sys/block", block_cb, &count);
            if (!count)
                stx_note("no physical block devices found");
        } else {
            const char *n = strrchr(stx_cfg.disk_dev, '/');
            dev_test(n ? n + 1 : stx_cfg.disk_dev);
        }
    }
#endif
}
