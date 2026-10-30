/*
 * Sustained load: all threads run the integer kernel for --load-seconds while
 * the main thread samples throughput, temperature, clock and power. Produces
 * the "Thermal" and "Power" sections (headroom, throttling, efficiency).
 */
#include "stx.h"

#include <stdlib.h>
#include <string.h>

/* reference: i7-13700H laptop on AC, stock cooling */
#define REF_HEADROOM 10.0   /* C below critical at the end of the load */
#define REF_SUSTAIN 85.0    /* % of initial throughput kept */
#define REF_EFFICIENCY 3.0  /* GIOPS per package watt */

double stx_bmc_read_power(void);

#define CHUNK (1u << 18)
#define SAMPLE_MS 500
#define MAX_SAMPLES 4096

typedef struct {
    volatile uint64_t count;
    char pad[56];
} counter;

static volatile int stop;
static volatile uint64_t sink;

static void load_worker(void *p)
{
    counter *c = (counter *)p;
    while (!stop) {
        sink += stx_k.k_int(CHUNK, c->count);
        c->count += CHUNK;
    }
}

typedef struct {
    double t, rate, temp, freq, pkg_w, bat_w, bmc_w;
} sample;

static double avg_of(const sample *s, int from, int to, size_t off)
{
    double sum = 0;
    int i, n = 0;
    for (i = from; i < to; i++) {
        double v = *(const double *)((const char *)&s[i] + off);
        if (v > -200 && v >= 0) {
            sum += v;
            n++;
        }
    }
    return n ? sum / n : -1;
}

static double max_of(const sample *s, int n, size_t off)
{
    double m = -300;
    int i;
    for (i = 0; i < n; i++) {
        double v = *(const double *)((const char *)&s[i] + off);
        if (v > m)
            m = v;
    }
    return m;
}

#define F(x) offsetof(sample, x)

void stx_mod_load(void)
{
    int n = stx_cfg.threads, i, ns = 0;
    counter *ctr = (counter *)stx_alloc(sizeof(counter) * (size_t)n, 64);
    stx_thread **th = (stx_thread **)calloc((size_t)n, sizeof(*th));
    sample *s = (sample *)calloc(MAX_SAMPLES, sizeof(sample));
    double idle_temp, idle_pkg = -1, idle_bat, idle_bmc, crit, e0, e1, t0, tlast, last_total = 0, last_e;
    int has_rapl;

    stx_section("thermal", "Thermal (sustained load)");
    if (!ctr || !th || !s) {
        stx_skip("out of memory");
        return;
    }
    memset(ctr, 0, sizeof(counter) * (size_t)n);

    /* idle baseline */
    stx_progress("idle baseline");
    idle_temp = stx_cpu_temp();
    e0 = stx_rapl_joules();
    has_rapl = e0 >= 0;
    t0 = stx_now();
    stx_sleep_ms(2000);
    if (has_rapl) {
        e1 = stx_rapl_joules();
        if (e1 >= e0)
            idle_pkg = (e1 - e0) / (stx_now() - t0);
    }
    idle_bat = stx_battery_watts();
    idle_bmc = stx_bmc_read_power();
    if (stx_cpu_temp() < idle_temp || idle_temp < -200)
        idle_temp = stx_cpu_temp();

    /* load */
    stop = 0;
    for (i = 0; i < n; i++)
        th[i] = stx_thread_start(load_worker, &ctr[i]);
    t0 = tlast = stx_now();
    last_e = stx_rapl_joules();
    while (ns < MAX_SAMPLES) {
        double now, total = 0, e;
        stx_sleep_ms(SAMPLE_MS);
        now = stx_now();
        for (i = 0; i < n; i++)
            total += (double)ctr[i].count;
        s[ns].t = now - t0;
        s[ns].rate = (total - last_total) / (now - tlast) * STX_INT_OPS_PER_ITER / 1e9;
        s[ns].temp = stx_cpu_temp();
        s[ns].freq = stx_cpu_freq_mhz();
        e = stx_rapl_joules();
        s[ns].pkg_w = (e >= 0 && last_e >= 0 && e >= last_e) ? (e - last_e) / (now - tlast) : -1;
        s[ns].bat_w = stx_battery_watts();
        s[ns].bmc_w = (ns % 4 == 3) ? stx_bmc_read_power() : -1;
        last_e = e;
        last_total = total;
        tlast = now;
        stx_progress("sustained load %2.0f/%d s: %.1f GIOPS, %.0f C, %.0f MHz", s[ns].t, stx_cfg.load_seconds,
                     s[ns].rate, s[ns].temp, s[ns].freq);
        ns++;
        if (now - t0 >= stx_cfg.load_seconds)
            break;
    }
    stop = 1;
    for (i = 0; i < n; i++)
        stx_thread_join(th[i]);

    {
        /* sample 0 contains thread start-up; "initial" is the best rate of the first ~3 s */
        int skip0 = ns >= 4, first_n = skip0 + (ns >= 12 ? 6 : 1), tail_from = ns - (ns / 4 > 0 ? ns / 4 : 1);
        double initial = 0, sustained = avg_of(s, tail_from, ns, F(rate)), max_temp = max_of(s, ns, F(temp));
        double f_start = avg_of(s, skip0, first_n, F(freq)), f_end = avg_of(s, tail_from, ns, F(freq));
        double pkg = avg_of(s, tail_from, ns, F(pkg_w)), bat = avg_of(s, tail_from, ns, F(bat_w));
        double bmc = avg_of(s, 0, ns, F(bmc_w));
        for (i = skip0; i < first_n && i < ns; i++)
            if (s[i].rate > initial)
                initial = s[i].rate;

        stx_info("duration", "%.0f s on %d threads", ns ? s[ns - 1].t : 0, n);
        stx_metric("throughput at start", initial, "GIOPS", 0, STX_HIGHER);
        stx_metric("throughput sustained", sustained, "GIOPS", 0, STX_HIGHER);
        if (initial > 0 && sustained > 0) {
            double pct = 100.0 * sustained / initial;
            if (pct > 100)
                pct = 100;
            stx_metric("sustained performance", pct, "%", REF_SUSTAIN, STX_HIGHER);
            if (pct < 80)
                stx_check(STX_WARN, "throttling", "throughput dropped to %.0f %% under sustained load", pct);
            else
                stx_check(STX_PASS, "throttling", "%.0f %% of initial throughput sustained", pct);
        }
        if (f_start > 0)
            stx_info("cpu clock start -> end", "%.0f -> %.0f MHz", f_start, f_end);

        crit = stx_cpu_temp_crit();
        if (max_temp > -200) {
            stx_info("cpu temperature idle", "%.1f C", idle_temp);
            stx_metric("cpu temperature max", max_temp, "C", 0, STX_LOWER);
            stx_info("temperature rise", "%.1f C", max_temp - idle_temp);
            if (crit < -200) {
                crit = 100;
                stx_note("no critical trip point exposed, assuming 100 C");
            } else {
                stx_info("critical temperature", "%.0f C", crit);
            }
            /* clamp so a CPU sitting at its limit scores near zero instead of dropping out */
            stx_metric("thermal headroom", crit - max_temp > 0.5 ? crit - max_temp : 0.5, "C", REF_HEADROOM,
                       STX_HIGHER);
            if (max_temp >= crit - 2)
                stx_check(STX_WARN, "thermal", "CPU reached its thermal limit (%.0f of %.0f C)", max_temp, crit);
            else
                stx_check(STX_PASS, "thermal", "%.0f C below critical under full load", crit - max_temp);
        } else {
            stx_note("no CPU temperature sensor readable on this platform");
        }
        stx_sensors_report();

        /* ---- power ---- */
        stx_section("power", "Power / Efficiency");
        if (pkg > 0) {
            if (idle_pkg >= 0)
                stx_metric("cpu package idle (rapl)", idle_pkg, "W", 0, STX_LOWER);
            stx_metric("cpu package load (rapl)", pkg, "W", 0, STX_LOWER);
            stx_metric("efficiency per package watt", sustained / pkg, "GIOPS/W", REF_EFFICIENCY, STX_HIGHER);
        } else if (has_rapl) {
            stx_note("RAPL counters not readable (needs root)");
        }
        if (bat > 0) {
            stx_info("battery discharge idle", idle_bat > 0 ? "%.1f W" : "n/a", idle_bat);
            stx_metric("battery discharge load", bat, "W", 0, STX_LOWER);
            if (pkg <= 0)
                stx_metric("efficiency per system watt", sustained / bat, "GIOPS/W", 0, STX_HIGHER);
        } else if (stx_file_exists("/sys/class/power_supply/BAT0/status") ||
                   stx_file_exists("/sys/class/power_supply/BAT1/status")) {
            stx_info("battery", "on AC power");
        }
        if (bmc > 0) {
            if (idle_bmc > 0)
                stx_metric("wall power idle (bmc)", idle_bmc, "W", 0, STX_LOWER);
            stx_metric("wall power load (bmc)", bmc, "W", 0, STX_LOWER);
            stx_metric("efficiency per wall watt", sustained / bmc, "GIOPS/W", 0, STX_HIGHER);
        }
        if (!has_rapl && stx_file_exists("/sys/class/powercap/intel-rapl:0/name"))
            stx_note("RAPL energy counters need root");
        else if (pkg <= 0 && bat <= 0 && bmc <= 0 && !has_rapl)
            stx_skip("no power telemetry (RAPL, battery or BMC) on this platform");
    }
    stx_free(ctr);
    free(th);
    free(s);
}
