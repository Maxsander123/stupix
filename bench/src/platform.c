/* Platform abstraction: POSIX (Linux, macOS, BSD, ...) and Windows. */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _GNU_SOURCE
#endif
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601 /* Windows 7: GetActiveProcessorCount */
#endif
#include "stx.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef STX_OS_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#if !defined(STX_NO_DLOPEN)
#include <dlfcn.h>
#endif
#if defined(STX_OS_MACOS) || defined(STX_OS_BSD)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif
#endif

double stx_now(void)
{
#ifdef STX_OS_WINDOWS
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

void stx_sleep_ms(int ms)
{
#ifdef STX_OS_WINDOWS
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

int stx_cpu_count(void)
{
#ifdef STX_OS_WINDOWS
    DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return n > 0 ? (int)n : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

uint64_t stx_mem_total(void)
{
#if defined(STX_OS_WINDOWS)
    MEMORYSTATUSEX m;
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m))
        return m.ullTotalPhys;
    return 0;
#elif defined(STX_OS_MACOS)
    uint64_t v = 0;
    size_t len = sizeof(v);
    if (sysctlbyname("hw.memsize", &v, &len, NULL, 0) == 0)
        return v;
    return 0;
#elif defined(_SC_PHYS_PAGES)
    long pages = sysconf(_SC_PHYS_PAGES), psz = sysconf(_SC_PAGESIZE);
    if (pages > 0 && psz > 0)
        return (uint64_t)pages * (uint64_t)psz;
    return 0;
#else
    return 0;
#endif
}

void *stx_alloc(size_t size, size_t align)
{
#ifdef STX_OS_WINDOWS
    return _aligned_malloc(size, align);
#else
    void *p = NULL;
    if (align < sizeof(void *))
        align = sizeof(void *);
    if (posix_memalign(&p, align, size) != 0)
        return NULL;
    return p;
#endif
}

void stx_free(void *p)
{
#ifdef STX_OS_WINDOWS
    _aligned_free(p);
#else
    free(p);
#endif
}

int stx_is_root(void)
{
#ifdef STX_OS_WINDOWS
    return 0;
#else
    return geteuid() == 0;
#endif
}

void stx_hostname(char *buf, size_t len)
{
#ifdef STX_OS_WINDOWS
    DWORD n = (DWORD)len;
    if (!GetComputerNameA(buf, &n))
        snprintf(buf, len, "unknown");
#else
    if (gethostname(buf, len) != 0)
        snprintf(buf, len, "unknown");
    buf[len - 1] = 0;
#endif
}

void stx_os_release(char *buf, size_t len)
{
    snprintf(buf, len, "%s", STX_OS_NAME);
#if defined(STX_OS_LINUX)
    {
        char tmp[4096], kern[128] = "";
        stx_read_text("/proc/sys/kernel/osrelease", kern, sizeof(kern));
        if (stx_read_text("/etc/os-release", tmp, sizeof(tmp)) > 0) {
            char *p = strstr(tmp, "PRETTY_NAME=");
            if (p) {
                char *e;
                p += 12;
                if (*p == '"')
                    p++;
                e = p;
                while (*e && *e != '"' && *e != '\n')
                    e++;
                *e = 0;
                snprintf(buf, len, "%s, kernel %s", p, kern);
                return;
            }
        }
        snprintf(buf, len, "Linux %s", kern);
    }
#elif defined(STX_OS_MACOS) || defined(STX_OS_BSD)
    {
        char rel[128] = "";
        size_t l = sizeof(rel);
#ifdef STX_OS_MACOS
        if (sysctlbyname("kern.osproductversion", rel, &l, NULL, 0) == 0)
            snprintf(buf, len, "macOS %s", rel);
#else
        if (sysctlbyname("kern.osrelease", rel, &l, NULL, 0) == 0)
            snprintf(buf, len, "BSD %s", rel);
#endif
    }
#elif defined(STX_OS_WINDOWS)
    {
        OSVERSIONINFOA v;
        memset(&v, 0, sizeof(v));
        v.dwOSVersionInfoSize = sizeof(v);
#if defined(_MSC_VER)
#pragma warning(suppress : 4996)
#endif
        if (GetVersionExA(&v))
            snprintf(buf, len, "Windows %lu.%lu build %lu", (unsigned long)v.dwMajorVersion,
                     (unsigned long)v.dwMinorVersion, (unsigned long)v.dwBuildNumber);
    }
#endif
}

/* ---- threads ---- */
struct stx_thread {
#ifdef STX_OS_WINDOWS
    HANDLE h;
#else
    pthread_t t;
#endif
    stx_thread_fn fn;
    void *arg;
};

#ifdef STX_OS_WINDOWS
static DWORD WINAPI thread_tramp(LPVOID p)
{
    struct stx_thread *t = (struct stx_thread *)p;
    t->fn(t->arg);
    return 0;
}
#else
static void *thread_tramp(void *p)
{
    struct stx_thread *t = (struct stx_thread *)p;
    t->fn(t->arg);
    return NULL;
}
#endif

stx_thread *stx_thread_start(stx_thread_fn fn, void *arg)
{
    stx_thread *t = (stx_thread *)calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    t->fn = fn;
    t->arg = arg;
#ifdef STX_OS_WINDOWS
    t->h = CreateThread(NULL, 0, thread_tramp, t, 0, NULL);
    if (!t->h) {
        free(t);
        return NULL;
    }
#else
    if (pthread_create(&t->t, NULL, thread_tramp, t) != 0) {
        free(t);
        return NULL;
    }
#endif
    return t;
}

void stx_thread_join(stx_thread *t)
{
    if (!t)
        return;
#ifdef STX_OS_WINDOWS
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
#else
    pthread_join(t->t, NULL);
#endif
    free(t);
}

int stx_run_threads(int n, stx_thread_fn fn, void **args)
{
    stx_thread **ts;
    int i, rc = 0;
    if (n <= 1) {
        fn(args[0]);
        return 0;
    }
    ts = (stx_thread **)calloc((size_t)n, sizeof(*ts));
    if (!ts)
        return -1;
    for (i = 0; i < n; i++) {
        ts[i] = stx_thread_start(fn, args[i]);
        if (!ts[i]) {
            fn(args[i]); /* degrade gracefully: run inline */
            rc = -1;
        }
    }
    for (i = 0; i < n; i++)
        stx_thread_join(ts[i]);
    free(ts);
    return rc;
}

/* ---- files ---- */
int stx_read_text(const char *path, char *buf, size_t len)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) {
        if (len)
            buf[0] = 0;
        return -1;
    }
    n = fread(buf, 1, len - 1, f);
    fclose(f);
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '))
        buf[--n] = 0;
    return (int)n;
}

int stx_read_u64(const char *path, uint64_t *out)
{
    char b[64];
    char *end;
    if (stx_read_text(path, b, sizeof(b)) <= 0)
        return -1;
    *out = strtoull(b, &end, 0);
    return end == b ? -1 : 0;
}

int stx_read_i64(const char *path, int64_t *out)
{
    char b[64];
    char *end;
    if (stx_read_text(path, b, sizeof(b)) <= 0)
        return -1;
    *out = strtoll(b, &end, 0);
    return end == b ? -1 : 0;
}

int stx_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

int stx_list_dir(const char *path, void (*cb)(const char *name, void *ctx), void *ctx)
{
#ifdef STX_OS_WINDOWS
    (void)path;
    (void)cb;
    (void)ctx;
    return -1;
#else
    DIR *d = opendir(path);
    struct dirent *e;
    int n = 0;
    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' && (e->d_name[1] == 0 || (e->d_name[1] == '.' && e->d_name[2] == 0)))
            continue;
        cb(e->d_name, ctx);
        n++;
    }
    closedir(d);
    return n;
#endif
}

void *stx_dlopen(const char *const *names)
{
    int i;
    for (i = 0; names[i]; i++) {
#if defined(STX_OS_WINDOWS)
        HMODULE h = LoadLibraryA(names[i]);
        if (h)
            return (void *)h;
#elif !defined(STX_NO_DLOPEN)
        void *h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
        if (h)
            return h;
#endif
    }
    return NULL;
}

void *stx_dlsym(void *lib, const char *sym)
{
#if defined(STX_OS_WINDOWS)
    return (void *)GetProcAddress((HMODULE)lib, sym);
#elif !defined(STX_NO_DLOPEN)
    return dlsym(lib, sym);
#else
    (void)lib;
    (void)sym;
    return NULL;
#endif
}

uint64_t stx_rand(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *s = x;
    return x * 0x2545F4914F6CDD1DULL;
}
