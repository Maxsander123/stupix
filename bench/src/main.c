/* stupix-bench entry point: argument parsing, system overview, module dispatch. */
#include "stx.h"

#include <stdlib.h>
#include <string.h>
#ifdef STX_OS_POSIX
#include <unistd.h>
#endif
#ifdef STX_OS_WINDOWS
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#endif

stx_config stx_cfg;
double stx_bmc_power_watts = -1;

static const struct {
    const char *name;
    unsigned bit;
    const char *desc;
} mods[] = {
    {"cpu", STX_MOD_CPU, "integer, floating point, branch, crypto, multi-core scaling, clock"},
    {"mem", STX_MOD_MEM, "bandwidth (read/write/copy), cache + DRAM latency, DIMM inventory"},
    {"disk", STX_MOD_DISK, "file I/O (seq/random/sync) + read-only raw device tests, SMART"},
    {"gpu", STX_MOD_GPU, "GPU inventory, OpenCL compute + memory + PCIe transfer, NVML sensors"},
    {"chipset", STX_MOD_CHIPSET, "platform/board, PCIe topology, link width/speed, AER errors"},
    {"bmc", STX_MOD_BMC, "in-band IPMI: BMC identity, self test, latency, SEL, DCMI power"},
    {"load", STX_MOD_LOAD, "sustained all-core load: thermal headroom, throttling, power, efficiency"},
};
#define N_MODS (sizeof(mods) / sizeof(mods[0]))

static void usage(void)
{
    size_t i;
    printf("stupix-bench %s - portable whole-system benchmark\n\n"
           "usage: stupix-bench [options]\n\n"
           "  --quick              short run (~1 min)\n"
           "  --full               long run (sustained load 60 s, 1 GiB disk file)\n"
           "  --only LIST          run only these modules (comma separated)\n"
           "  --skip LIST          skip these modules\n"
           "  --threads N          threads for multi-core tests (default: all CPUs)\n"
           "  --time SEC           seconds per measurement (default 0.5)\n"
           "  --load-seconds N     sustained load duration (default 20)\n"
           "  --disk-path DIR      directory for the file I/O test (default: .)\n"
           "  --disk-size MB       size of the test file (default 256)\n"
           "  --no-disk-file       skip the file I/O test\n"
           "  --disk-dev DEV       raw read-only device test: auto | none | /dev/xyz (default auto)\n"
           "  --json FILE          write JSON report (- = stdout)\n"
           "  --quiet              no console report (use with --json)\n"
           "  --no-color           plain output\n"
           "  --selftest           verify assembly kernels against the C reference\n"
           "  --version            print version\n\n"
           "modules:\n",
           STX_VERSION);
    for (i = 0; i < N_MODS; i++)
        printf("  %-8s %s\n", mods[i].name, mods[i].desc);
    printf("\nScores: 1000 = reference system, higher is better. Raw device tests never write.\n");
}

static unsigned parse_mods(const char *list)
{
    unsigned m = 0;
    char buf[256], *tok;
    size_t i;
    snprintf(buf, sizeof(buf), "%s", list);
    for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        int found = 0;
        for (i = 0; i < N_MODS; i++) {
            if (strcmp(tok, mods[i].name) == 0 || (strcmp(tok, "thermal") == 0 && mods[i].bit == STX_MOD_LOAD) ||
                (strcmp(tok, "power") == 0 && mods[i].bit == STX_MOD_LOAD) ||
                (strcmp(tok, "ram") == 0 && mods[i].bit == STX_MOD_MEM)) {
                m |= mods[i].bit;
                found = 1;
            }
        }
        if (!found) {
            fprintf(stderr, "unknown module '%s'\n", tok);
            exit(2);
        }
    }
    return m;
}

#ifdef STX_OS_LINUX
static void detect_virt(char *buf, size_t len)
{
    char v[128];
    uint32_t r[4];
    buf[0] = 0;
    if (stx_cpuid(1, 0, r) && (r[2] & (1u << 31))) {
        char sig[13];
        stx_cpuid(0x40000000, 0, r);
        memcpy(sig, &r[1], 4);
        memcpy(sig + 4, &r[2], 4);
        memcpy(sig + 8, &r[3], 4);
        sig[12] = 0;
        snprintf(buf, len, "yes (hypervisor '%s')", sig);
        return;
    }
    if (stx_read_text("/sys/hypervisor/type", v, sizeof(v)) > 0) {
        snprintf(buf, len, "yes (%s)", v);
        return;
    }
    if (stx_file_exists("/.dockerenv") || stx_file_exists("/run/.containerenv")) {
        snprintf(buf, len, "container");
        return;
    }
    snprintf(buf, len, "no (bare metal)");
}
#endif

void stx_mod_system(void)
{
    char b[512];
    uint64_t mem = stx_mem_total();
    stx_section("system", "System");
    stx_hostname(b, sizeof(b));
    stx_info("hostname", "%s", b);
    stx_os_release(b, sizeof(b));
    stx_info("os", "%s", b);
    stx_info("architecture", "%s", STX_ARCH_NAME);
    stx_info("kernels", "%s", stx_k.impl);
    stx_info("logical cpus", "%d", stx_cpu_count());
    if (mem)
        stx_info("memory", "%.1f GiB", mem / 1073741824.0);
#ifdef STX_OS_LINUX
    {
        static const char *dmi[][2] = {{"sys_vendor", "vendor"},       {"product_name", "product"},
                                       {"product_serial", "serial"},   {"board_vendor", "board vendor"},
                                       {"board_name", "board"},        {"bios_vendor", "bios vendor"},
                                       {"bios_version", "bios version"}, {"bios_date", "bios date"}};
        size_t i;
        for (i = 0; i < sizeof(dmi) / sizeof(dmi[0]); i++) {
            char p[128];
            snprintf(p, sizeof(p), "/sys/class/dmi/id/%s", dmi[i][0]);
            if (stx_read_text(p, b, sizeof(b)) > 0)
                stx_info(dmi[i][1], "%s", b);
        }
        detect_virt(b, sizeof(b));
        stx_info("virtualized", "%s", b);
        if (strncmp(b, "yes", 3) == 0 || strcmp(b, "container") == 0)
            stx_note("running virtualized: hardware sensors and scores reflect the guest view");
        if (stx_battery_watts() > 0)
            stx_note("running on battery: power limits usually lower CPU/GPU scores, plug in AC for comparable "
                     "results");
    }
#endif
    if (!stx_is_root()) {
#ifdef STX_OS_WINDOWS
        stx_note("hardware-level probes (BMC, SMART, raw devices) are Linux-only");
#else
        stx_note("not running as root: BMC, SMART, raw device, DIMM and RAPL data may be unavailable");
#endif
    }
}

int main(int argc, char **argv)
{
    int i, selftest = 0;
    unsigned only = 0, skip = 0;

    stx_cfg.unit_time = 0.5;
    stx_cfg.threads = 0;
    stx_cfg.load_seconds = 20;
    stx_cfg.modules = STX_MOD_ALL;
    stx_cfg.disk_path = ".";
    stx_cfg.disk_size = 256ULL << 20;
    stx_cfg.disk_dev = "auto";
    stx_cfg.color = isatty(fileno(stdout));
#ifdef STX_OS_WINDOWS
    stx_cfg.color = 0;
#endif

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (fprintf(stderr, "%s needs a value\n", a), exit(2), ""))
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage();
            return 0;
        } else if (!strcmp(a, "--version")) {
            printf("stupix-bench %s (%s, %s)\n", STX_VERSION, STX_OS_NAME, STX_ARCH_NAME);
            return 0;
        } else if (!strcmp(a, "--quick")) {
            stx_cfg.unit_time = 0.2;
            stx_cfg.load_seconds = 8;
            stx_cfg.disk_size = 64ULL << 20;
        } else if (!strcmp(a, "--full")) {
            stx_cfg.unit_time = 1.0;
            stx_cfg.load_seconds = 60;
            stx_cfg.disk_size = 1024ULL << 20;
        } else if (!strcmp(a, "--only")) {
            only = parse_mods(NEXT());
        } else if (!strcmp(a, "--skip")) {
            skip |= parse_mods(NEXT());
        } else if (!strcmp(a, "--threads")) {
            stx_cfg.threads = atoi(NEXT());
        } else if (!strcmp(a, "--time")) {
            stx_cfg.unit_time = atof(NEXT());
        } else if (!strcmp(a, "--load-seconds")) {
            stx_cfg.load_seconds = atoi(NEXT());
        } else if (!strcmp(a, "--disk-path")) {
            stx_cfg.disk_path = NEXT();
        } else if (!strcmp(a, "--disk-size")) {
            stx_cfg.disk_size = (uint64_t)atoll(NEXT()) << 20;
        } else if (!strcmp(a, "--no-disk-file")) {
            stx_cfg.disk_path = NULL;
        } else if (!strcmp(a, "--disk-dev")) {
            stx_cfg.disk_dev = NEXT();
        } else if (!strcmp(a, "--json")) {
            stx_cfg.json_path = NEXT();
        } else if (!strcmp(a, "--quiet")) {
            stx_cfg.quiet = 1;
        } else if (!strcmp(a, "--no-color")) {
            stx_cfg.color = 0;
        } else if (!strcmp(a, "--selftest")) {
            selftest = 1;
        } else {
            fprintf(stderr, "unknown option '%s' (see --help)\n", a);
            return 2;
        }
#undef NEXT
    }
    if (stx_cfg.unit_time < 0.05)
        stx_cfg.unit_time = 0.05;
    if (stx_cfg.load_seconds < 2)
        stx_cfg.load_seconds = 2;
    if (stx_cfg.threads <= 0)
        stx_cfg.threads = stx_cpu_count();
    if (stx_cfg.threads > 1024)
        stx_cfg.threads = 1024;
    if (stx_cfg.disk_size < (4ULL << 20))
        stx_cfg.disk_size = 4ULL << 20;
    if (stx_cfg.json_path && strcmp(stx_cfg.json_path, "-") == 0)
        stx_cfg.color = stx_cfg.color && isatty(fileno(stderr));
    stx_cfg.modules = (only ? only : STX_MOD_ALL) & ~skip;

    stx_kernels_init();
    if (selftest) {
        int f;
        printf("stupix-bench selftest (%s)\n", stx_k.impl);
        f = stx_kernels_selftest();
        printf("%s\n", f ? "SELFTEST FAILED" : "selftest passed");
        return f ? 1 : 0;
    }

    if (!stx_cfg.quiet)
        fprintf(stx_cfg.json_path && !strcmp(stx_cfg.json_path, "-") ? stderr : stdout,
                "stupix-bench %s - %s/%s, %d threads, %.2fs per test\n", STX_VERSION, STX_OS_NAME, STX_ARCH_NAME,
                stx_cfg.threads, stx_cfg.unit_time);

    stx_mod_system();
    if (stx_cfg.modules & STX_MOD_CPU)
        stx_mod_cpu();
    if (stx_cfg.modules & STX_MOD_MEM)
        stx_mod_mem();
    if (stx_cfg.modules & STX_MOD_DISK)
        stx_mod_disk();
    if (stx_cfg.modules & STX_MOD_GPU)
        stx_mod_gpu();
    if (stx_cfg.modules & STX_MOD_CHIPSET)
        stx_mod_chipset();
    if (stx_cfg.modules & STX_MOD_BMC)
        stx_mod_bmc();
    if (stx_cfg.modules & STX_MOD_LOAD)
        stx_mod_load();
    stx_report_finish();
    return 0;
}
