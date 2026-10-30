/*
 * Chipset / platform: host bridge and PCH identification, PCIe topology,
 * negotiated link width/speed vs. capability, AER error counters, IOMMU.
 * Reads sysfs only; pci.ids is used for names when it happens to be installed.
 */
#include "stx.h"

#include <stdlib.h>
#include <string.h>

/* reference: total negotiated PCIe bandwidth of endpoints on the reference laptop */
#define REF_PCIE_GBS 24.0

#ifdef STX_OS_LINUX

#define PCI_DIR "/sys/bus/pci/devices"

static FILE *pci_ids;

static void pci_name(unsigned vendor, unsigned device, char *out, size_t len)
{
    static const char *paths[] = {"/usr/share/misc/pci.ids", "/usr/share/hwdata/pci.ids", "/usr/share/pci.ids",
                                  NULL};
    char line[256], vname[128] = "";
    int i, in_vendor = 0;
    snprintf(out, len, "%04x:%04x", vendor, device);
    if (!pci_ids)
        for (i = 0; paths[i] && !pci_ids; i++)
            pci_ids = fopen(paths[i], "r");
    if (!pci_ids)
        return;
    rewind(pci_ids);
    while (fgets(line, sizeof(line), pci_ids)) {
        unsigned id;
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '#' || !line[0])
            continue;
        if (line[0] != '\t') {
            if (in_vendor)
                break;
            if (sscanf(line, "%x", &id) == 1 && id == vendor) {
                in_vendor = 1;
                snprintf(vname, sizeof(vname), "%s", line + 6);
            }
        } else if (in_vendor && line[1] != '\t' && sscanf(line + 1, "%x", &id) == 1 && id == device) {
            snprintf(out, len, "%s %s", vname, line + 7);
            return;
        }
    }
    if (vname[0])
        snprintf(out, len, "%s [%04x:%04x]", vname, vendor, device);
}

static double gts(const char *s)
{
    /* "16.0 GT/s PCIe" -> 16.0 ; "Unknown" -> 0 */
    return atof(s);
}

/* usable GB/s per lane after line encoding */
static double lane_gbs(double gt)
{
    if (gt <= 0)
        return 0;
    if (gt < 6)
        return gt * 0.8 / 8; /* gen1/2: 8b/10b */
    return gt * (128.0 / 130.0) / 8;
}

typedef struct {
    int devices, bridges, endpoints_with_link;
    int degraded_width, slow_speed;
    double total_gbs;
    uint64_t aer_cor, aer_nonfatal, aer_fatal;
    int class_count[256];
} pci_ctx;

static uint64_t aer_total(const char *bdf, const char *file)
{
    char p[256], buf[2048], *t;
    snprintf(p, sizeof(p), PCI_DIR "/%s/%s", bdf, file);
    if (stx_read_text(p, buf, sizeof(buf)) <= 0)
        return 0;
    t = strstr(buf, "TOTAL_ERR_");
    if (!t)
        return 0;
    t = strchr(t, ' ');
    return t ? strtoull(t + 1, NULL, 10) : 0;
}

/* parent bridge of a PCI function, from the sysfs path */
static int parent_bdf(const char *bdf, char *out, size_t len)
{
#ifdef STX_OS_POSIX
    char p[256], real[4096], *slash, *prev;
    snprintf(p, sizeof(p), PCI_DIR "/%s", bdf);
    if (!realpath(p, real))
        return -1;
    slash = strrchr(real, '/');
    if (!slash)
        return -1;
    *slash = 0;
    prev = strrchr(real, '/');
    if (!prev || strlen(prev + 1) != 12 || prev[5] != ':')
        return -1;
    snprintf(out, len, "%s", prev + 1);
    return 0;
#else
    return -1;
#endif
}

static void pci_cb(const char *bdf, void *ctx)
{
    pci_ctx *c = (pci_ctx *)ctx;
    char p[256], s[64], name[256];
    uint64_t cls = 0, vendor = 0, device = 0;
    unsigned base;
    uint64_t cw = 0, mw = 0;
    double cs, ms;

    snprintf(p, sizeof(p), PCI_DIR "/%s/class", bdf);
    if (stx_read_u64(p, &cls) != 0)
        return;
    c->devices++;
    base = (unsigned)(cls >> 16) & 0xff;
    c->class_count[base]++;
    snprintf(p, sizeof(p), PCI_DIR "/%s/vendor", bdf);
    stx_read_u64(p, &vendor);
    snprintf(p, sizeof(p), PCI_DIR "/%s/device", bdf);
    stx_read_u64(p, &device);

    /* host bridge (0600) and ISA/LPC bridge (0601) identify CPU uncore and PCH */
    if ((cls >> 8) == 0x0600 || (cls >> 8) == 0x0601) {
        pci_name((unsigned)vendor, (unsigned)device, name, sizeof(name));
        stx_info((cls >> 8) == 0x0600 ? "host bridge" : "pch / lpc bridge", "%s (%s)", name, bdf);
    }

    c->aer_cor += aer_total(bdf, "aer_dev_correctable");
    c->aer_nonfatal += aer_total(bdf, "aer_dev_nonfatal");
    c->aer_fatal += aer_total(bdf, "aer_dev_fatal");

    if (base == 0x06) {
        c->bridges++;
        return; /* links are judged from the endpoint side */
    }
    snprintf(p, sizeof(p), PCI_DIR "/%s/current_link_width", bdf);
    if (stx_read_u64(p, &cw) != 0 || cw == 0 || cw > 32)
        return;
    snprintf(p, sizeof(p), PCI_DIR "/%s/max_link_width", bdf);
    stx_read_u64(p, &mw);
    snprintf(p, sizeof(p), PCI_DIR "/%s/current_link_speed", bdf);
    stx_read_text(p, s, sizeof(s));
    cs = gts(s);
    snprintf(p, sizeof(p), PCI_DIR "/%s/max_link_speed", bdf);
    stx_read_text(p, s, sizeof(s));
    ms = gts(s);
    c->endpoints_with_link++;

    /* the slot can be narrower/slower than the device: compare with the upstream port */
    {
        char par[32];
        if (parent_bdf(bdf, par, sizeof(par)) == 0) {
            uint64_t pw = 0;
            snprintf(p, sizeof(p), PCI_DIR "/%s/max_link_width", par);
            if (stx_read_u64(p, &pw) == 0 && pw > 0 && pw < mw)
                mw = pw;
            snprintf(p, sizeof(p), PCI_DIR "/%s/max_link_speed", par);
            if (stx_read_text(p, s, sizeof(s)) > 0 && gts(s) > 0 && gts(s) < ms)
                ms = gts(s);
        }
    }
    c->total_gbs += lane_gbs(ms > 0 ? ms : cs) * (double)(mw ? mw : cw);
    pci_name((unsigned)vendor, (unsigned)device, name, sizeof(name));
    if (mw && cw < mw) {
        c->degraded_width++;
        stx_check(STX_WARN, "pcie width", "%s %s runs x%llu of x%llu", bdf, name, (unsigned long long)cw,
                  (unsigned long long)mw);
    }
    if (ms > 0 && cs > 0 && cs < ms) {
        c->slow_speed++;
        stx_info(bdf, "%s: %.1f of %.1f GT/s (may be power saving)", name, cs, ms);
    }
}

static void count_cb(const char *name, void *ctx)
{
    (void)name;
    (*(int *)ctx)++;
}

static void pci_report(void)
{
    static const char *classes[] = {"legacy",    "storage",    "network",   "display",  "multimedia", "memory",
                                    "bridge",    "comm",       "system",    "input",    "docking",    "processor",
                                    "serial bus", "wireless",  "intelligent", "satellite", "crypto",   "signal proc",
                                    "accelerator", "instrumentation"};
    pci_ctx c;
    char summary[512] = "";
    int i, groups = 0;
    memset(&c, 0, sizeof(c));
    if (stx_list_dir(PCI_DIR, pci_cb, &c) <= 0) {
        stx_skip("no PCI bus visible");
        return;
    }
    for (i = 0; i < 20; i++) {
        size_t l = strlen(summary);
        if (c.class_count[i])
            snprintf(summary + l, sizeof(summary) - l, "%s%d %s", l ? ", " : "", c.class_count[i], classes[i]);
    }
    stx_info("pci functions", "%d (%s)", c.devices, summary);
    stx_info("pcie endpoints with link", "%d, %d running below max speed", c.endpoints_with_link, c.slow_speed);
    if (c.endpoints_with_link) {
        stx_metric("pcie negotiable bandwidth", c.total_gbs, "GB/s", REF_PCIE_GBS, STX_HIGHER);
        if (!c.degraded_width)
            stx_check(STX_PASS, "pcie width", "all %d links at full width", c.endpoints_with_link);
    }
    if (c.aer_fatal || c.aer_nonfatal)
        stx_check(STX_FAIL, "pcie aer", "%llu fatal, %llu non-fatal uncorrectable errors since boot",
                  (unsigned long long)c.aer_fatal, (unsigned long long)c.aer_nonfatal);
    else if (c.aer_cor > 100)
        stx_check(STX_WARN, "pcie aer", "%llu correctable errors since boot (signal integrity?)",
                  (unsigned long long)c.aer_cor);
    else
        stx_check(STX_PASS, "pcie aer", "%llu correctable, no uncorrectable errors", (unsigned long long)c.aer_cor);

    stx_list_dir("/sys/kernel/iommu_groups", count_cb, &groups);
    stx_info("iommu", groups > 0 ? "enabled (%d groups)" : "disabled or not supported", groups);
    if (pci_ids) {
        fclose(pci_ids);
        pci_ids = NULL;
    }
}

static void platform_report(void)
{
    char b[256];
    if (stx_file_exists("/sys/firmware/efi/systab") || stx_list_dir("/sys/firmware/efi", count_cb, &(int){0}) > 0) {
        stx_info("firmware", "UEFI");
        if (stx_read_text("/sys/firmware/efi/fw_platform_size", b, sizeof(b)) > 0)
            stx_info("uefi platform size", "%s bit", b);
    } else {
        stx_info("firmware", "legacy BIOS or non-UEFI");
    }
    {
        /* secure boot state lives in an efivar: 4 attribute bytes + 1 value byte */
        FILE *f = fopen("/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c", "rb");
        if (f) {
            unsigned char sb[5];
            if (fread(sb, 1, 5, f) == 5)
                stx_info("secure boot", "%s", sb[4] ? "enabled" : "disabled");
            fclose(f);
        }
    }
    if (stx_read_text("/sys/class/tpm/tpm0/tpm_version_major", b, sizeof(b)) > 0)
        stx_info("tpm", "%s.0", b);
    else if (stx_file_exists("/sys/class/tpm/tpm0/device/description") || stx_file_exists("/dev/tpm0"))
        stx_info("tpm", "present");
    {
        int nodes = 0;
        stx_list_dir("/sys/devices/system/node", count_cb, &nodes);
        if (nodes > 0) {
            char online[64];
            stx_read_text("/sys/devices/system/node/online", online, sizeof(online));
            stx_info("numa nodes online", "%s", online[0] ? online : "?");
        }
    }
    if (stx_read_text("/sys/devices/system/cpu/intel_pstate/status", b, sizeof(b)) > 0)
        stx_info("intel_pstate", "%s", b);
    if (stx_read_text("/sys/devices/system/cpu/amd_pstate/status", b, sizeof(b)) > 0)
        stx_info("amd_pstate", "%s", b);
    if (stx_read_text("/sys/module/pcie_aspm/parameters/policy", b, sizeof(b)) > 0)
        stx_info("pcie aspm policy", "%s", b);
}
#endif

void stx_mod_chipset(void)
{
    stx_section("chipset", "Chipset / Platform");
#ifdef STX_OS_LINUX
    platform_report();
    pci_report();
#else
    stx_skip("PCIe topology and platform probes are implemented for Linux");
#endif
}
