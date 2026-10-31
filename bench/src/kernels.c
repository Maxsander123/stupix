/* C reference kernels (bit-exact twins of asm/kernels.S), dispatch and calibration. */
#include "stx.h"

#include <string.h>
#include <math.h>

stx_kernels stx_k;

static uint64_t stx_c_int(uint64_t n, uint64_t seed)
{
    const uint64_t K = 0x9E3779B97F4A7C15ULL;
    uint64_t a = seed, b = seed + 1, c = seed + 2, d = seed + 3, i;
    for (i = n; i; i--) {
        a = a * K + i;
        a ^= a >> 29;
        b = b * K + i;
        b ^= b >> 29;
        c = c * K + i;
        c ^= c >> 29;
        d = d * K + i;
        d ^= d >> 29;
    }
    return a ^ b ^ c ^ d;
}

static double stx_c_fp(uint64_t n)
{
    const double m = 0.9999999, add = 1e-7;
    double acc[8][2], s0, s1;
    uint64_t i;
    int k;
    for (k = 0; k < 8; k++)
        acc[k][0] = acc[k][1] = 0.5;
    for (i = 0; i < n; i++) {
        for (k = 0; k < 8; k++) {
            acc[k][0] = acc[k][0] * m;
            acc[k][1] = acc[k][1] * m;
        }
        for (k = 0; k < 8; k++) {
            acc[k][0] = acc[k][0] + add;
            acc[k][1] = acc[k][1] + add;
        }
    }
    s0 = acc[0][0];
    s1 = acc[0][1];
    for (k = 1; k < 8; k++) {
        s0 += acc[k][0];
        s1 += acc[k][1];
    }
    return s0 + s1;
}

static uint64_t stx_c_read(const uint64_t *p, size_t words)
{
    uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    size_t i;
    words &= ~(size_t)7;
    for (i = 0; i < words; i += 8) {
        s0 += p[i] + p[i + 4];
        s1 += p[i + 1] + p[i + 5];
        s2 += p[i + 2] + p[i + 6];
        s3 += p[i + 3] + p[i + 7];
    }
    return s0 + s1 + s2 + s3;
}

static void stx_c_write(uint64_t *p, size_t words, uint64_t v)
{
    size_t i;
    words &= ~(size_t)7;
    for (i = 0; i < words; i++)
        p[i] = v;
}

static void *stx_c_chase(void *p, uint64_t steps)
{
    void *volatile *q = (void *volatile *)p;
    steps &= ~(uint64_t)3;
    while (steps--)
        q = (void *volatile *)*q;
    return (void *)q;
}

#ifdef STX_ASM
uint64_t stx_a_int(uint64_t iters, uint64_t seed);
double stx_a_fp(uint64_t iters);
uint64_t stx_a_read(const uint64_t *p, size_t words);
void stx_a_write(uint64_t *p, size_t words, uint64_t v);
void *stx_a_chase(void *p, uint64_t steps);
uint64_t stx_a_clock(uint64_t iters);
#ifdef STX_ARCH_X86_64
void stx_a_cpuid(uint32_t leaf, uint32_t sub, uint32_t out[4]);
#endif
#elif defined(STX_ASM_CLOCK)
uint64_t stx_a_clock(uint64_t iters);
#ifdef __i386__
void stx_a_cpuid(uint32_t leaf, uint32_t sub, uint32_t out[4]);
#endif
#endif

void stx_kernels_init(void)
{
#ifdef STX_ASM
    stx_k.k_int = stx_a_int;
    stx_k.k_fp = stx_a_fp;
    stx_k.k_read = stx_a_read;
    stx_k.k_write = stx_a_write;
    stx_k.k_chase = stx_a_chase;
    stx_k.k_clock = stx_a_clock;
    stx_k.impl = "assembly (" STX_ARCH_NAME ")";
#else
    stx_k.k_int = stx_c_int;
    stx_k.k_fp = stx_c_fp;
    stx_k.k_read = stx_c_read;
    stx_k.k_write = stx_c_write;
    stx_k.k_chase = stx_c_chase;
#ifdef STX_ASM_CLOCK
    stx_k.k_clock = stx_a_clock;
    stx_k.impl = "portable C + assembly clock (" STX_ARCH_NAME ")";
#else
    stx_k.k_clock = NULL;
    stx_k.impl = "portable C (" STX_ARCH_NAME ")";
#endif
#endif
}

int stx_cpuid(uint32_t leaf, uint32_t sub, uint32_t out[4])
{
#if (defined(STX_ASM) && defined(STX_ARCH_X86_64)) || (defined(STX_ASM_CLOCK) && defined(__i386__))
    stx_a_cpuid(leaf, sub, out);
    return 1;
#else
    (void)leaf;
    (void)sub;
    out[0] = out[1] = out[2] = out[3] = 0;
    return 0;
#endif
}

/* compare asm kernels with the C reference; returns number of failures */
int stx_kernels_selftest(void)
{
    int fails = 0;
#ifdef STX_ASM
    size_t words = 4096, i;
    uint64_t *buf = (uint64_t *)stx_alloc(words * 8, 64);
    uint64_t seed = 12345, r1, r2;
    double f1, f2;
    void **ring;

    r1 = stx_a_int(100000, 42);
    r2 = stx_c_int(100000, 42);
    printf("  int    asm=%016llx c=%016llx %s\n", (unsigned long long)r1, (unsigned long long)r2,
           r1 == r2 ? "OK" : "MISMATCH");
    fails += r1 != r2;

    f1 = stx_a_fp(100000);
    f2 = stx_c_fp(100000);
    printf("  fp     asm=%.17g c=%.17g %s\n", f1, f2, f1 == f2 ? "OK" : "MISMATCH");
    fails += f1 != f2;

    for (i = 0; i < words; i++)
        buf[i] = stx_rand(&seed);
    r1 = stx_a_read(buf, words);
    r2 = stx_c_read(buf, words);
    printf("  read   asm=%016llx c=%016llx %s\n", (unsigned long long)r1, (unsigned long long)r2,
           r1 == r2 ? "OK" : "MISMATCH");
    fails += r1 != r2;

    stx_a_write(buf, words, 0xA5A5A5A5DEADBEEFULL);
    r1 = 0;
    for (i = 0; i < words; i++)
        r1 += buf[i] != 0xA5A5A5A5DEADBEEFULL;
    stx_c_write(buf, words, 0xA5A5A5A5DEADBEEFULL);
    for (i = 0; i < words; i++)
        r1 += buf[i] != 0xA5A5A5A5DEADBEEFULL;
    printf("  write  %llu wrong words %s\n", (unsigned long long)r1, r1 == 0 ? "OK" : "MISMATCH");
    fails += r1 != 0;

    ring = (void **)buf;
    for (i = 0; i < 512; i++)
        ring[i * 8] = &ring[((i * 37 + 11) % 512) * 8];
    f1 = (double)(size_t)((char *)stx_a_chase(ring, 1000) - (char *)ring);
    f2 = (double)(size_t)((char *)stx_c_chase(ring, 1000) - (char *)ring);
    printf("  chase  asm=%.0f c=%.0f %s\n", f1, f2, f1 == f2 ? "OK" : "MISMATCH");
    fails += f1 != f2;

    r1 = stx_a_clock(1000);
    printf("  clock  %llu adds %s\n", (unsigned long long)r1, r1 == 64000 ? "OK" : "MISMATCH");
    fails += r1 != 64000;
    stx_free(buf);
#else
    printf("  no full assembly kernel set for %s, using portable C\n", STX_ARCH_NAME);
#ifdef STX_ASM_CLOCK
    {
        uint64_t r = stx_a_clock(1000);
        printf("  clock  %llu adds %s\n", (unsigned long long)r, r == 64000 ? "OK" : "MISMATCH");
        fails += r != 64000;
    }
#endif
#endif
    return fails;
}

double stx_measure(double (*fn)(uint64_t iters, void *ctx), void *ctx, double target)
{
    uint64_t iters = 1;
    double t, best = 0;
    int i;
    /* grow until a run is long enough to scale reliably */
    for (;;) {
        t = fn(iters, ctx);
        if (t >= target / 10 || iters >= (1ULL << 50))
            break;
        iters *= (t < target / 1000) ? 16 : 2;
    }
    if (t > 0)
        iters = (uint64_t)((double)iters * (target / t)) + 1;
    for (i = 0; i < 3; i++) {
        double r;
        t = fn(iters, ctx);
        r = t > 0 ? (double)iters / t : 0;
        if (r > best)
            best = r;
    }
    return best;
}
