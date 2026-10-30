/* Temperature, frequency and energy sensors (Linux sysfs; other OSes report unknown). */
#include "stx.h"

#include <stdlib.h>
#include <string.h>

#ifdef STX_OS_LINUX

/* hwmon chips that describe the CPU package/cores */
static int is_cpu_hwmon(const char *name)
{
    static const char *n[] = {"coretemp", "k10temp", "zenpower", "cpu_thermal", "cpu-thermal", "soc_thermal",
                              "x86_pkg_temp", "fam15h_power", "tctl", "cpu", "scpi_sensors", NULL};
    int i;
    for (i = 0; n[i]; i++)
        if (strcmp(name, n[i]) == 0 || strstr(name, "cpu"))
            return 1;
    return 0;
}

typedef struct {
    double max, crit;
    int cpu_only;
} temp_ctx;

static void hwmon_temps(const char *dir, temp_ctx *c)
{
    char p[512];
    int i;
    for (i = 1; i < 64; i++) {
        int64_t v;
        snprintf(p, sizeof(p), "%s/temp%d_input", dir, i);
        if (stx_read_i64(p, &v) != 0) {
            if (i > 16)
                break;
            continue;
        }
        if (v / 1000.0 > c->max && v > -40000 && v < 150000)
            c->max = v / 1000.0;
        snprintf(p, sizeof(p), "%s/temp%d_crit", dir, i);
        if (stx_read_i64(p, &v) == 0 && v > 0 && v < 150000 && (c->crit < -200 || v / 1000.0 < c->crit))
            c->crit = v / 1000.0;
    }
}

static void temp_cb(const char *name, void *ctx)
{
    temp_ctx *c = (temp_ctx *)ctx;
    char p[512], n[64];
    snprintf(p, sizeof(p), "/sys/class/hwmon/%s/name", name);
    if (stx_read_text(p, n, sizeof(n)) <= 0)
        return;
    if (c->cpu_only && !is_cpu_hwmon(n))
        return;
    snprintf(p, sizeof(p), "/sys/class/hwmon/%s", name);
    hwmon_temps(p, c);
}

static void zone_cb(const char *name, void *ctx)
{
    temp_ctx *c = (temp_ctx *)ctx;
    char p[512], type[64];
    int64_t v;
    if (strncmp(name, "thermal_zone", 12) != 0)
        return;
    snprintf(p, sizeof(p), "/sys/class/thermal/%s/type", name);
    stx_read_text(p, type, sizeof(type));
    if (c->cpu_only && !(strstr(type, "cpu") || strstr(type, "pkg") || strstr(type, "soc") ||
                         strstr(type, "acpitz") || strstr(type, "CPU")))
        return;
    snprintf(p, sizeof(p), "/sys/class/thermal/%s/temp", name);
    if (stx_read_i64(p, &v) == 0 && v > -40000 && v < 150000 && v / 1000.0 > c->max)
        c->max = v / 1000.0;
}

static temp_ctx cpu_temps(void)
{
    temp_ctx c = {-300, -300, 1};
    stx_list_dir("/sys/class/hwmon", temp_cb, &c);
    if (c.max < -200)
        stx_list_dir("/sys/class/thermal", zone_cb, &c);
    return c;
}

double stx_cpu_temp(void)
{
    return cpu_temps().max;
}

double stx_cpu_temp_crit(void)
{
    temp_ctx c = cpu_temps();
    return c.crit;
}

typedef struct {
    double sum;
    int n;
} freq_ctx;

static void freq_cb(const char *name, void *ctx)
{
    freq_ctx *c = (freq_ctx *)ctx;
    char p[256];
    uint64_t khz;
    if (strncmp(name, "cpu", 3) != 0 || name[3] < '0' || name[3] > '9')
        return;
    snprintf(p, sizeof(p), "/sys/devices/system/cpu/%s/cpufreq/scaling_cur_freq", name);
    if (stx_read_u64(p, &khz) == 0 && khz > 0) {
        c->sum += khz / 1000.0;
        c->n++;
    }
}

double stx_cpu_freq_mhz(void)
{
    freq_ctx c = {0, 0};
    stx_list_dir("/sys/devices/system/cpu", freq_cb, &c);
    return c.n ? c.sum / c.n : -1;
}

/* RAPL: top-level package domains intel-rapl:N (no sub-domains like intel-rapl:0:0) */
typedef struct {
    double joules;
    int n;
} rapl_ctx;

static void rapl_cb(const char *name, void *ctx)
{
    rapl_ctx *c = (rapl_ctx *)ctx;
    char p[256];
    uint64_t uj;
    const char *colon = strchr(name, ':');
    if (strncmp(name, "intel-rapl:", 11) != 0 || !colon || strchr(colon + 1, ':'))
        return;
    snprintf(p, sizeof(p), "/sys/class/powercap/%s/energy_uj", name);
    if (stx_read_u64(p, &uj) == 0) {
        c->joules += uj / 1e6;
        c->n++;
    }
}

double stx_rapl_joules(void)
{
    rapl_ctx c = {0, 0};
    stx_list_dir("/sys/class/powercap", rapl_cb, &c);
    return c.n ? c.joules : -1;
}

static void bat_cb(const char *name, void *ctx)
{
    double *w = (double *)ctx;
    char p[256], st[32];
    uint64_t v, i, u;
    snprintf(p, sizeof(p), "/sys/class/power_supply/%s/type", name);
    if (stx_read_text(p, st, sizeof(st)) <= 0 || strcmp(st, "Battery") != 0)
        return;
    snprintf(p, sizeof(p), "/sys/class/power_supply/%s/status", name);
    stx_read_text(p, st, sizeof(st));
    if (strcmp(st, "Discharging") != 0)
        return;
    snprintf(p, sizeof(p), "/sys/class/power_supply/%s/power_now", name);
    if (stx_read_u64(p, &v) == 0) {
        *w += v / 1e6;
        return;
    }
    snprintf(p, sizeof(p), "/sys/class/power_supply/%s/current_now", name);
    if (stx_read_u64(p, &i) != 0)
        return;
    snprintf(p, sizeof(p), "/sys/class/power_supply/%s/voltage_now", name);
    if (stx_read_u64(p, &u) != 0)
        return;
    *w += (i / 1e6) * (u / 1e6);
}

double stx_battery_watts(void)
{
    double w = 0;
    stx_list_dir("/sys/class/power_supply", bat_cb, &w);
    return w > 0 ? w : -1;
}

static void report_cb(const char *name, void *ctx)
{
    char p[512], n[64], label[64], key[160];
    int i;
    (void)ctx;
    snprintf(p, sizeof(p), "/sys/class/hwmon/%s/name", name);
    if (stx_read_text(p, n, sizeof(n)) <= 0)
        return;
    for (i = 1; i < 32; i++) {
        int64_t v, crit;
        snprintf(p, sizeof(p), "/sys/class/hwmon/%s/temp%d_input", name, i);
        if (stx_read_i64(p, &v) != 0)
            continue;
        snprintf(p, sizeof(p), "/sys/class/hwmon/%s/temp%d_label", name, i);
        if (stx_read_text(p, label, sizeof(label)) <= 0)
            snprintf(label, sizeof(label), "temp%d", i);
        snprintf(key, sizeof(key), "%s %s", n, label);
        snprintf(p, sizeof(p), "/sys/class/hwmon/%s/temp%d_crit", name, i);
        if (stx_read_i64(p, &crit) == 0 && crit > 0 && crit < 200000)
            stx_info(key, "%.1f C (crit %.0f C)", v / 1000.0, crit / 1000.0);
        else
            stx_info(key, "%.1f C", v / 1000.0);
        /* only the first few per chip to keep the report readable */
        if (i >= 4 && strcmp(n, "coretemp") == 0)
            break;
    }
    for (i = 1; i < 16; i++) {
        int64_t v;
        snprintf(p, sizeof(p), "/sys/class/hwmon/%s/fan%d_input", name, i);
        if (stx_read_i64(p, &v) != 0)
            continue;
        snprintf(p, sizeof(p), "/sys/class/hwmon/%s/fan%d_label", name, i);
        if (stx_read_text(p, label, sizeof(label)) <= 0)
            snprintf(label, sizeof(label), "fan%d", i);
        snprintf(key, sizeof(key), "%s %s", n, label);
        stx_info(key, "%lld RPM", (long long)v);
    }
}

void stx_sensors_report(void)
{
    stx_list_dir("/sys/class/hwmon", report_cb, NULL);
}

#else /* !STX_OS_LINUX */

double stx_cpu_temp(void) { return -300; }
double stx_cpu_temp_crit(void) { return -300; }
double stx_cpu_freq_mhz(void) { return -1; }
double stx_rapl_joules(void) { return -1; }
double stx_battery_watts(void) { return -1; }
void stx_sensors_report(void) {}

#endif
