/* Result collection, scoring, console report and JSON export. */
#include "stx.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>

enum { E_INFO, E_METRIC, E_CHECK, E_SKIP, E_NOTE };

typedef struct {
    int kind;
    int section;
    char key[64];
    char text[256];
    double value, ref, score;
    char unit[16];
    int dir;
    int health;
} entry;

typedef struct {
    char name[32];
    char title[64];
    double log_sum;
    int scored;
} section;

static entry *entries;
static int n_entries, cap_entries;
static section sections[32];
static int n_sections;
static int checks[3];

#define C_RESET (stx_cfg.color ? "\033[0m" : "")
#define C_BOLD (stx_cfg.color ? "\033[1m" : "")
#define C_DIM (stx_cfg.color ? "\033[2m" : "")
#define C_GREEN (stx_cfg.color ? "\033[32m" : "")
#define C_YELLOW (stx_cfg.color ? "\033[33m" : "")
#define C_RED (stx_cfg.color ? "\033[31m" : "")
#define C_CYAN (stx_cfg.color ? "\033[36m" : "")

static FILE *out(void)
{
    /* keep stdout clean for JSON when --json - is used */
    return (stx_cfg.json_path && strcmp(stx_cfg.json_path, "-") == 0) ? stderr : stdout;
}

static entry *add_entry(int kind, const char *key)
{
    entry *e;
    if (n_entries == cap_entries) {
        int nc = cap_entries ? cap_entries * 2 : 128;
        entry *ne = (entry *)realloc(entries, (size_t)nc * sizeof(entry));
        if (!ne)
            return NULL;
        entries = ne;
        cap_entries = nc;
    }
    e = &entries[n_entries++];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->section = n_sections - 1;
    snprintf(e->key, sizeof(e->key), "%s", key ? key : "");
    return e;
}

void stx_section(const char *name, const char *title)
{
    FILE *o = out();
    section *s;
    int i, pad;
    if (n_sections == (int)(sizeof(sections) / sizeof(sections[0])))
        return;
    s = &sections[n_sections++];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    snprintf(s->title, sizeof(s->title), "%s", title);
    if (stx_cfg.quiet)
        return;
    fprintf(o, "\n%s%s== %s ", C_BOLD, C_CYAN, title);
    pad = 66 - (int)strlen(title);
    for (i = 0; i < pad; i++)
        fputc('=', o);
    fprintf(o, "%s\n", C_RESET);
}

static void clear_progress(void)
{
    if (stx_cfg.color && !stx_cfg.quiet)
        fprintf(out(), "\r\033[K");
}

void stx_progress(const char *fmt, ...)
{
    va_list ap;
    if (!stx_cfg.color || stx_cfg.quiet)
        return;
    fprintf(out(), "\r\033[K  %s... ", C_DIM);
    va_start(ap, fmt);
    vfprintf(out(), fmt, ap);
    va_end(ap);
    fprintf(out(), "%s", C_RESET);
    fflush(out());
}

static void print_key(FILE *o, const char *key)
{
    int n = (int)strlen(key), i;
    fprintf(o, "  %s ", key);
    for (i = n; i < 30; i++)
        fputc(i == n ? ' ' : '.', o);
    fputc(' ', o);
}

void stx_info(const char *key, const char *fmt, ...)
{
    entry *e = add_entry(E_INFO, key);
    va_list ap;
    if (!e)
        return;
    va_start(ap, fmt);
    vsnprintf(e->text, sizeof(e->text), fmt, ap);
    va_end(ap);
    if (stx_cfg.quiet)
        return;
    clear_progress();
    print_key(out(), key);
    fprintf(out(), "%s\n", e->text);
}

static void fmt_value(char *buf, size_t len, double v)
{
    double a = fabs(v);
    if (a >= 1000)
        snprintf(buf, len, "%.0f", v);
    else if (a >= 100)
        snprintf(buf, len, "%.1f", v);
    else if (a >= 1)
        snprintf(buf, len, "%.2f", v);
    else
        snprintf(buf, len, "%.3g", v);
}

void stx_metric(const char *key, double value, const char *unit, double ref, stx_dir dir)
{
    entry *e = add_entry(E_METRIC, key);
    char vb[32];
    if (!e)
        return;
    e->value = value;
    e->ref = ref;
    e->dir = dir;
    snprintf(e->unit, sizeof(e->unit), "%s", unit);
    if (ref > 0 && value > 0 && isfinite(value)) {
        e->score = dir == STX_HIGHER ? 1000.0 * value / ref : 1000.0 * ref / value;
        if (n_sections > 0) {
            sections[n_sections - 1].log_sum += log(e->score);
            sections[n_sections - 1].scored++;
        }
    }
    if (stx_cfg.quiet)
        return;
    clear_progress();
    fmt_value(vb, sizeof(vb), value);
    print_key(out(), key);
    if (e->score > 0)
        fprintf(out(), "%s%10s%s %-8s %sscore %5.0f%s\n", C_BOLD, vb, C_RESET, unit, C_DIM, e->score, C_RESET);
    else
        fprintf(out(), "%s%10s%s %s\n", C_BOLD, vb, C_RESET, unit);
}

void stx_check(stx_health h, const char *key, const char *fmt, ...)
{
    static const char *tag[] = {"PASS", "WARN", "FAIL"};
    entry *e = add_entry(E_CHECK, key);
    const char *col = h == STX_PASS ? C_GREEN : h == STX_WARN ? C_YELLOW : C_RED;
    va_list ap;
    if (!e)
        return;
    e->health = h;
    checks[h]++;
    va_start(ap, fmt);
    vsnprintf(e->text, sizeof(e->text), fmt, ap);
    va_end(ap);
    if (stx_cfg.quiet)
        return;
    clear_progress();
    fprintf(out(), "  %s[%s]%s %s: %s\n", col, tag[h], C_RESET, key, e->text);
}

void stx_skip(const char *fmt, ...)
{
    entry *e = add_entry(E_SKIP, "skipped");
    va_list ap;
    if (!e)
        return;
    va_start(ap, fmt);
    vsnprintf(e->text, sizeof(e->text), fmt, ap);
    va_end(ap);
    if (stx_cfg.quiet)
        return;
    clear_progress();
    fprintf(out(), "  %s(skipped: %s)%s\n", C_DIM, e->text, C_RESET);
}

void stx_note(const char *fmt, ...)
{
    entry *e = add_entry(E_NOTE, "note");
    va_list ap;
    if (!e)
        return;
    va_start(ap, fmt);
    vsnprintf(e->text, sizeof(e->text), fmt, ap);
    va_end(ap);
    if (stx_cfg.quiet)
        return;
    clear_progress();
    fprintf(out(), "  %snote: %s%s\n", C_DIM, e->text, C_RESET);
}

static double section_score(const section *s)
{
    return s->scored ? exp(s->log_sum / s->scored) : 0;
}

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (c == '\n')
            fputs("\\n", f);
        else if (c == '\t')
            fputs("\\t", f);
        else if (c < 0x20)
            fprintf(f, "\\u%04x", c);
        else
            fputc(c, f);
    }
    fputc('"', f);
}

static void json_num(FILE *f, double v)
{
    if (isfinite(v))
        fprintf(f, "%.6g", v);
    else
        fputs("null", f);
}

static void write_json(FILE *f, double overall)
{
    static const char *hname[] = {"pass", "warn", "fail"};
    char host[128], os[256], ts[64];
    time_t now = time(NULL);
    int s, i, first;
    stx_hostname(host, sizeof(host));
    stx_os_release(os, sizeof(os));
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    fprintf(f, "{\n  \"tool\": \"stupix-bench\",\n  \"version\": \"%s\",\n", STX_VERSION);
    fprintf(f, "  \"timestamp\": \"%s\",\n  \"host\": ", ts);
    json_str(f, host);
    fprintf(f, ",\n  \"os\": ");
    json_str(f, os);
    fprintf(f, ",\n  \"arch\": \"%s\",\n  \"kernels\": ", STX_ARCH_NAME);
    json_str(f, stx_k.impl);
    fprintf(f, ",\n  \"overall_score\": ");
    if (overall > 0)
        json_num(f, overall);
    else
        fputs("null", f);
    fprintf(f, ",\n  \"health\": {\"pass\": %d, \"warn\": %d, \"fail\": %d},\n", checks[0], checks[1],
            checks[2]);
    fprintf(f, "  \"sections\": [");
    for (s = 0; s < n_sections; s++) {
        fprintf(f, "%s\n    {\"name\": ", s ? "," : "");
        json_str(f, sections[s].name);
        fprintf(f, ", \"title\": ");
        json_str(f, sections[s].title);
        fprintf(f, ", \"score\": ");
        if (sections[s].scored)
            json_num(f, section_score(&sections[s]));
        else
            fputs("null", f);
        /* info */
        fprintf(f, ",\n     \"info\": {");
        first = 1;
        for (i = 0; i < n_entries; i++) {
            if (entries[i].section != s || entries[i].kind != E_INFO)
                continue;
            fprintf(f, "%s", first ? "" : ", ");
            json_str(f, entries[i].key);
            fputs(": ", f);
            json_str(f, entries[i].text);
            first = 0;
        }
        fprintf(f, "},\n     \"metrics\": [");
        first = 1;
        for (i = 0; i < n_entries; i++) {
            entry *e = &entries[i];
            if (e->section != s || e->kind != E_METRIC)
                continue;
            fprintf(f, "%s\n       {\"key\": ", first ? "" : ",");
            json_str(f, e->key);
            fputs(", \"value\": ", f);
            json_num(f, e->value);
            fputs(", \"unit\": ", f);
            json_str(f, e->unit);
            fprintf(f, ", \"better\": \"%s\"", e->dir == STX_HIGHER ? "higher" : "lower");
            if (e->score > 0) {
                fputs(", \"reference\": ", f);
                json_num(f, e->ref);
                fputs(", \"score\": ", f);
                json_num(f, e->score);
            }
            fputc('}', f);
            first = 0;
        }
        fprintf(f, "],\n     \"checks\": [");
        first = 1;
        for (i = 0; i < n_entries; i++) {
            entry *e = &entries[i];
            if (e->section != s || e->kind != E_CHECK)
                continue;
            fprintf(f, "%s\n       {\"status\": \"%s\", \"key\": ", first ? "" : ",", hname[e->health]);
            json_str(f, e->key);
            fputs(", \"message\": ", f);
            json_str(f, e->text);
            fputc('}', f);
            first = 0;
        }
        fprintf(f, "],\n     \"notes\": [");
        first = 1;
        for (i = 0; i < n_entries; i++) {
            entry *e = &entries[i];
            if (e->section != s || (e->kind != E_NOTE && e->kind != E_SKIP))
                continue;
            {
                char buf[300];
                snprintf(buf, sizeof(buf), "%s%s", e->kind == E_SKIP ? "skipped: " : "", e->text);
                fprintf(f, "%s", first ? "" : ", ");
                json_str(f, buf);
            }
            first = 0;
        }
        fprintf(f, "]}");
    }
    fprintf(f, "\n  ]\n}\n");
}

void stx_report_finish(void)
{
    FILE *o = out();
    double log_sum = 0, overall = 0;
    int s, n = 0;
    for (s = 0; s < n_sections; s++) {
        if (sections[s].scored) {
            log_sum += log(section_score(&sections[s]));
            n++;
        }
    }
    if (n)
        overall = exp(log_sum / n);

    if (!stx_cfg.quiet || !stx_cfg.json_path) {
        clear_progress();
        fprintf(o, "\n%s== Summary ===========================================================%s\n", C_BOLD,
                C_RESET);
        for (s = 0; s < n_sections; s++) {
            if (!sections[s].scored)
                continue;
            fprintf(o, "  %-30s %s%8.0f%s\n", sections[s].title, C_BOLD, section_score(&sections[s]), C_RESET);
        }
        if (n)
            fprintf(o, "  %-30s %s%s%8.0f%s   (1000 = reference system)\n", "OVERALL", C_BOLD, C_CYAN, overall,
                    C_RESET);
        else
            fprintf(o, "  %-30s %8s   (nothing scored)\n", "OVERALL", "n/a");
        fprintf(o, "  %-30s %s%d pass%s, %s%d warn%s, %s%d fail%s\n", "Health checks", C_GREEN, checks[0], C_RESET,
                checks[1] ? C_YELLOW : "", checks[1], C_RESET, checks[2] ? C_RED : "", checks[2], C_RESET);
    }

    if (stx_cfg.json_path) {
        if (strcmp(stx_cfg.json_path, "-") == 0) {
            write_json(stdout, overall);
        } else {
            FILE *f = fopen(stx_cfg.json_path, "w");
            if (f) {
                write_json(f, overall);
                fclose(f);
                if (!stx_cfg.quiet)
                    fprintf(o, "  JSON report written to %s\n", stx_cfg.json_path);
            } else {
                fprintf(stderr, "cannot write %s\n", stx_cfg.json_path);
            }
        }
    }
}
