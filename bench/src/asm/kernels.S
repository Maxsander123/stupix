/*
 * stupix-bench hand-written benchmark kernels.
 *
 *   x86_64   System V (Linux, macOS, BSD) and Microsoft x64 (Windows/MinGW)
 *   aarch64  AAPCS64 (Linux, macOS/Apple Silicon, Windows on ARM)
 *   riscv64  LP64D (RV64GC)
 *
 * Every kernel has a bit-exact C twin in kernels.c; `stupix-bench --selftest`
 * compares both. Only caller-saved registers are used (Win64 xmm6-9 are
 * saved explicitly), so no stack frames or unwind tables are needed.
 *
 *   uint64_t stx_a_int(uint64_t iters, uint64_t seed)    4 dependent mul/add/shift/xor chains
 *   double   stx_a_fp(uint64_t iters)                    8 x 2-lane double mul+add chains
 *   uint64_t stx_a_read(const uint64_t *p, size_t w)     sum, w multiple of 8, p 16-aligned
 *   void     stx_a_write(uint64_t *p, size_t w, uint64_t v)
 *   void    *stx_a_chase(void *p, uint64_t steps)        p = *p, steps multiple of 4
 *   uint64_t stx_a_clock(uint64_t iters)                 64 dependent 1-cycle adds per iter
 *   void     stx_a_cpuid(uint32_t leaf, uint32_t sub, uint32_t out[4])   x86_64 only
 */

#include "../stx.h"

#if defined(STX_ASM)

#if defined(__APPLE__)
#define SYM(x) _##x
#else
#define SYM(x) x
#endif

#if defined(__ELF__)
#define TYPE(x) .type SYM(x), %function
#define SIZE(x) .size SYM(x), . - SYM(x)
#else
#define TYPE(x)
#define SIZE(x)
#endif

/* double constants: 0.5, 0.9999999, 1e-7 */
#define C_INIT 0x3fe0000000000000
#define C_MUL  0x3fefffffca501acb
#define C_ADD  0x3e7ad7f29abcaf48

    .text

/* ======================================================================= */
#if defined(STX_ARCH_X86_64)

#if defined(_WIN32)
#define A0 %rcx
#define A1 %rdx
#define A2 %r8
#define A0d %ecx
#define A1d %edx
#else
#define A0 %rdi
#define A1 %rsi
#define A2 %rdx
#define A0d %edi
#define A1d %esi
#endif

/* ---- integer ---- */
    .globl SYM(stx_a_int)
    TYPE(stx_a_int)
    .p2align 4
SYM(stx_a_int):
    mov     A0, %r10
    mov     A1, %r11
    mov     %r11, %rax
    lea     1(%r11), %rcx
    lea     2(%r11), %rdx
    lea     3(%r11), %r8
    movabs  $0x9E3779B97F4A7C15, %r9
    test    %r10, %r10
    jz      2f
    .p2align 4
1:
    imul    %r9, %rax
    add     %r10, %rax
    mov     %rax, %r11
    shr     $29, %r11
    xor     %r11, %rax

    imul    %r9, %rcx
    add     %r10, %rcx
    mov     %rcx, %r11
    shr     $29, %r11
    xor     %r11, %rcx

    imul    %r9, %rdx
    add     %r10, %rdx
    mov     %rdx, %r11
    shr     $29, %r11
    xor     %r11, %rdx

    imul    %r9, %r8
    add     %r10, %r8
    mov     %r8, %r11
    shr     $29, %r11
    xor     %r11, %r8

    dec     %r10
    jnz     1b
2:
    xor     %rcx, %rax
    xor     %rdx, %rax
    xor     %r8, %rax
    ret
    SIZE(stx_a_int)

/* ---- floating point (SSE2 is baseline on x86_64) ---- */
    .globl SYM(stx_a_fp)
    TYPE(stx_a_fp)
    .p2align 4
SYM(stx_a_fp):
    mov     A0, %r10
#if defined(_WIN32)
    sub     $72, %rsp
    movups  %xmm6, 0(%rsp)
    movups  %xmm7, 16(%rsp)
    movups  %xmm8, 32(%rsp)
    movups  %xmm9, 48(%rsp)
#endif
    movabs  $C_INIT, %rax
    movq    %rax, %xmm0
    unpcklpd %xmm0, %xmm0
    movapd  %xmm0, %xmm1
    movapd  %xmm0, %xmm2
    movapd  %xmm0, %xmm3
    movapd  %xmm0, %xmm4
    movapd  %xmm0, %xmm5
    movapd  %xmm0, %xmm6
    movapd  %xmm0, %xmm7
    movabs  $C_MUL, %rax
    movq    %rax, %xmm8
    unpcklpd %xmm8, %xmm8
    movabs  $C_ADD, %rax
    movq    %rax, %xmm9
    unpcklpd %xmm9, %xmm9
    test    %r10, %r10
    jz      2f
    .p2align 4
1:
    mulpd   %xmm8, %xmm0
    mulpd   %xmm8, %xmm1
    mulpd   %xmm8, %xmm2
    mulpd   %xmm8, %xmm3
    mulpd   %xmm8, %xmm4
    mulpd   %xmm8, %xmm5
    mulpd   %xmm8, %xmm6
    mulpd   %xmm8, %xmm7
    addpd   %xmm9, %xmm0
    addpd   %xmm9, %xmm1
    addpd   %xmm9, %xmm2
    addpd   %xmm9, %xmm3
    addpd   %xmm9, %xmm4
    addpd   %xmm9, %xmm5
    addpd   %xmm9, %xmm6
    addpd   %xmm9, %xmm7
    dec     %r10
    jnz     1b
2:
    addpd   %xmm1, %xmm0
    addpd   %xmm2, %xmm0
    addpd   %xmm3, %xmm0
    addpd   %xmm4, %xmm0
    addpd   %xmm5, %xmm0
    addpd   %xmm6, %xmm0
    addpd   %xmm7, %xmm0
    movapd  %xmm0, %xmm1
    unpckhpd %xmm1, %xmm1
    addsd   %xmm1, %xmm0
#if defined(_WIN32)
    movups  0(%rsp), %xmm6
    movups  16(%rsp), %xmm7
    movups  32(%rsp), %xmm8
    movups  48(%rsp), %xmm9
    add     $72, %rsp
#endif
    ret
    SIZE(stx_a_fp)

/* ---- memory read ---- */
    .globl SYM(stx_a_read)
    TYPE(stx_a_read)
    .p2align 4
SYM(stx_a_read):
    mov     A0, %r10
    mov     A1, %r11
    shr     $3, %r11
    pxor    %xmm0, %xmm0
    pxor    %xmm1, %xmm1
    pxor    %xmm2, %xmm2
    pxor    %xmm3, %xmm3
    test    %r11, %r11
    jz      2f
    .p2align 4
1:
    paddq   0(%r10), %xmm0
    paddq   16(%r10), %xmm1
    paddq   32(%r10), %xmm2
    paddq   48(%r10), %xmm3
    add     $64, %r10
    dec     %r11
    jnz     1b
2:
    paddq   %xmm1, %xmm0
    paddq   %xmm3, %xmm2
    paddq   %xmm2, %xmm0
    movq    %xmm0, %rax
    pshufd  $0x4e, %xmm0, %xmm1
    movq    %xmm1, %rcx
    add     %rcx, %rax
    ret
    SIZE(stx_a_read)

/* ---- memory write ---- */
    .globl SYM(stx_a_write)
    TYPE(stx_a_write)
    .p2align 4
SYM(stx_a_write):
    mov     A0, %r10
    mov     A1, %r11
    mov     A2, %rax
    shr     $3, %r11
    movq    %rax, %xmm0
    punpcklqdq %xmm0, %xmm0
    test    %r11, %r11
    jz      2f
    .p2align 4
1:
    movdqa  %xmm0, 0(%r10)
    movdqa  %xmm0, 16(%r10)
    movdqa  %xmm0, 32(%r10)
    movdqa  %xmm0, 48(%r10)
    add     $64, %r10
    dec     %r11
    jnz     1b
2:
    ret
    SIZE(stx_a_write)

/* ---- pointer chase (latency) ---- */
    .globl SYM(stx_a_chase)
    TYPE(stx_a_chase)
    .p2align 4
SYM(stx_a_chase):
    mov     A0, %rax
    mov     A1, %r11
    shr     $2, %r11
    test    %r11, %r11
    jz      2f
    .p2align 4
1:
    mov     (%rax), %rax
    mov     (%rax), %rax
    mov     (%rax), %rax
    mov     (%rax), %rax
    dec     %r11
    jnz     1b
2:
    ret
    SIZE(stx_a_chase)

/* ---- effective clock: register-register adds cannot be eliminated by rename ---- */
    .globl SYM(stx_a_clock)
    TYPE(stx_a_clock)
    .p2align 4
SYM(stx_a_clock):
    mov     A0, %r10
    xor     %eax, %eax
    mov     $1, %ecx
    test    %r10, %r10
    jz      2f
    .p2align 4
1:
    .rept 64
    add     %rcx, %rax
    .endr
    dec     %r10
    jnz     1b
2:
    ret
    SIZE(stx_a_clock)

/* ---- cpuid ---- */
    .globl SYM(stx_a_cpuid)
    TYPE(stx_a_cpuid)
    .p2align 4
SYM(stx_a_cpuid):
    mov     A2, %r9
    mov     A0d, %eax
    mov     A1d, %ecx
    push    %rbx
    cpuid
    mov     %eax, 0(%r9)
    mov     %ebx, 4(%r9)
    mov     %ecx, 8(%r9)
    mov     %edx, 12(%r9)
    pop     %rbx
    ret
    SIZE(stx_a_cpuid)

/* ======================================================================= */
#elif defined(STX_ARCH_AARCH64)

    .globl SYM(stx_a_int)
    TYPE(stx_a_int)
    .p2align 4
SYM(stx_a_int):
    mov     x9, x0
    mov     x2, x1
    add     x3, x1, #1
    add     x4, x1, #2
    add     x5, x1, #3
    movz    x6, #0x7C15
    movk    x6, #0x7F4A, lsl #16
    movk    x6, #0x79B9, lsl #32
    movk    x6, #0x9E37, lsl #48
    cbz     x9, 2f
1:
    madd    x2, x2, x6, x9
    eor     x2, x2, x2, lsr #29
    madd    x3, x3, x6, x9
    eor     x3, x3, x3, lsr #29
    madd    x4, x4, x6, x9
    eor     x4, x4, x4, lsr #29
    madd    x5, x5, x6, x9
    eor     x5, x5, x5, lsr #29
    subs    x9, x9, #1
    b.ne    1b
2:
    eor     x0, x2, x3
    eor     x0, x0, x4
    eor     x0, x0, x5
    ret
    SIZE(stx_a_int)

    .globl SYM(stx_a_fp)
    TYPE(stx_a_fp)
    .p2align 4
SYM(stx_a_fp):
    mov     x9, x0
    movz    x10, #0x3fe0, lsl #48
    dup     v0.2d, x10
    mov     v1.16b, v0.16b
    mov     v2.16b, v0.16b
    mov     v3.16b, v0.16b
    mov     v4.16b, v0.16b
    mov     v5.16b, v0.16b
    mov     v6.16b, v0.16b
    mov     v7.16b, v0.16b
    movz    x10, #0x1acb
    movk    x10, #0xca50, lsl #16
    movk    x10, #0xffff, lsl #32
    movk    x10, #0x3fef, lsl #48
    dup     v16.2d, x10
    movz    x10, #0xaf48
    movk    x10, #0x9abc, lsl #16
    movk    x10, #0xd7f2, lsl #32
    movk    x10, #0x3e7a, lsl #48
    dup     v17.2d, x10
    cbz     x9, 2f
1:
    fmul    v0.2d, v0.2d, v16.2d
    fmul    v1.2d, v1.2d, v16.2d
    fmul    v2.2d, v2.2d, v16.2d
    fmul    v3.2d, v3.2d, v16.2d
    fmul    v4.2d, v4.2d, v16.2d
    fmul    v5.2d, v5.2d, v16.2d
    fmul    v6.2d, v6.2d, v16.2d
    fmul    v7.2d, v7.2d, v16.2d
    fadd    v0.2d, v0.2d, v17.2d
    fadd    v1.2d, v1.2d, v17.2d
    fadd    v2.2d, v2.2d, v17.2d
    fadd    v3.2d, v3.2d, v17.2d
    fadd    v4.2d, v4.2d, v17.2d
    fadd    v5.2d, v5.2d, v17.2d
    fadd    v6.2d, v6.2d, v17.2d
    fadd    v7.2d, v7.2d, v17.2d
    subs    x9, x9, #1
    b.ne    1b
2:
    fadd    v0.2d, v0.2d, v1.2d
    fadd    v0.2d, v0.2d, v2.2d
    fadd    v0.2d, v0.2d, v3.2d
    fadd    v0.2d, v0.2d, v4.2d
    fadd    v0.2d, v0.2d, v5.2d
    fadd    v0.2d, v0.2d, v6.2d
    fadd    v0.2d, v0.2d, v7.2d
    faddp   d0, v0.2d
    ret
    SIZE(stx_a_fp)

    .globl SYM(stx_a_read)
    TYPE(stx_a_read)
    .p2align 4
SYM(stx_a_read):
    lsr     x9, x1, #3
    movi    v0.2d, #0
    movi    v1.2d, #0
    movi    v2.2d, #0
    movi    v3.2d, #0
    cbz     x9, 2f
1:
    ldp     q4, q5, [x0], #32
    ldp     q6, q7, [x0], #32
    add     v0.2d, v0.2d, v4.2d
    add     v1.2d, v1.2d, v5.2d
    add     v2.2d, v2.2d, v6.2d
    add     v3.2d, v3.2d, v7.2d
    subs    x9, x9, #1
    b.ne    1b
2:
    add     v0.2d, v0.2d, v1.2d
    add     v2.2d, v2.2d, v3.2d
    add     v0.2d, v0.2d, v2.2d
    addp    d0, v0.2d
    fmov    x0, d0
    ret
    SIZE(stx_a_read)

    .globl SYM(stx_a_write)
    TYPE(stx_a_write)
    .p2align 4
SYM(stx_a_write):
    lsr     x9, x1, #3
    dup     v0.2d, x2
    cbz     x9, 2f
1:
    stp     q0, q0, [x0], #32
    stp     q0, q0, [x0], #32
    subs    x9, x9, #1
    b.ne    1b
2:
    ret
    SIZE(stx_a_write)

    .globl SYM(stx_a_chase)
    TYPE(stx_a_chase)
    .p2align 4
SYM(stx_a_chase):
    lsr     x9, x1, #2
    cbz     x9, 2f
1:
    ldr     x0, [x0]
    ldr     x0, [x0]
    ldr     x0, [x0]
    ldr     x0, [x0]
    subs    x9, x9, #1
    b.ne    1b
2:
    ret
    SIZE(stx_a_chase)

    .globl SYM(stx_a_clock)
    TYPE(stx_a_clock)
    .p2align 4
SYM(stx_a_clock):
    mov     x9, x0
    mov     x0, #0
    mov     x11, #1
    cbz     x9, 2f
1:
    .rept 64
    add     x0, x0, x11
    .endr
    subs    x9, x9, #1
    b.ne    1b
2:
    ret
    SIZE(stx_a_clock)

/* ======================================================================= */
#elif defined(STX_ARCH_RISCV64)

    .globl SYM(stx_a_int)
    TYPE(stx_a_int)
    .p2align 2
SYM(stx_a_int):
    mv      t0, a0
    mv      t1, a1
    addi    t2, a1, 1
    addi    t3, a1, 2
    addi    t4, a1, 3
    li      t5, 0x9E3779B97F4A7C15
    beqz    t0, 2f
1:
    mul     t1, t1, t5
    add     t1, t1, t0
    srli    t6, t1, 29
    xor     t1, t1, t6
    mul     t2, t2, t5
    add     t2, t2, t0
    srli    t6, t2, 29
    xor     t2, t2, t6
    mul     t3, t3, t5
    add     t3, t3, t0
    srli    t6, t3, 29
    xor     t3, t3, t6
    mul     t4, t4, t5
    add     t4, t4, t0
    srli    t6, t4, 29
    xor     t4, t4, t6
    addi    t0, t0, -1
    bnez    t0, 1b
2:
    xor     a0, t1, t2
    xor     a0, a0, t3
    xor     a0, a0, t4
    ret
    SIZE(stx_a_int)

/* lane 0 accumulators: ft0-ft7, lane 1: ft8-ft11, fa1-fa4 */
    .globl SYM(stx_a_fp)
    TYPE(stx_a_fp)
    .p2align 2
SYM(stx_a_fp):
    mv      t0, a0
    li      t1, C_INIT
    fmv.d.x ft0, t1
    fmv.d.x ft1, t1
    fmv.d.x ft2, t1
    fmv.d.x ft3, t1
    fmv.d.x ft4, t1
    fmv.d.x ft5, t1
    fmv.d.x ft6, t1
    fmv.d.x ft7, t1
    fmv.d.x ft8, t1
    fmv.d.x ft9, t1
    fmv.d.x ft10, t1
    fmv.d.x ft11, t1
    fmv.d.x fa1, t1
    fmv.d.x fa2, t1
    fmv.d.x fa3, t1
    fmv.d.x fa4, t1
    li      t1, C_MUL
    fmv.d.x fa5, t1
    li      t1, C_ADD
    fmv.d.x fa6, t1
    beqz    t0, 2f
1:
    fmul.d  ft0, ft0, fa5
    fmul.d  ft1, ft1, fa5
    fmul.d  ft2, ft2, fa5
    fmul.d  ft3, ft3, fa5
    fmul.d  ft4, ft4, fa5
    fmul.d  ft5, ft5, fa5
    fmul.d  ft6, ft6, fa5
    fmul.d  ft7, ft7, fa5
    fmul.d  ft8, ft8, fa5
    fmul.d  ft9, ft9, fa5
    fmul.d  ft10, ft10, fa5
    fmul.d  ft11, ft11, fa5
    fmul.d  fa1, fa1, fa5
    fmul.d  fa2, fa2, fa5
    fmul.d  fa3, fa3, fa5
    fmul.d  fa4, fa4, fa5
    fadd.d  ft0, ft0, fa6
    fadd.d  ft1, ft1, fa6
    fadd.d  ft2, ft2, fa6
    fadd.d  ft3, ft3, fa6
    fadd.d  ft4, ft4, fa6
    fadd.d  ft5, ft5, fa6
    fadd.d  ft6, ft6, fa6
    fadd.d  ft7, ft7, fa6
    fadd.d  ft8, ft8, fa6
    fadd.d  ft9, ft9, fa6
    fadd.d  ft10, ft10, fa6
    fadd.d  ft11, ft11, fa6
    fadd.d  fa1, fa1, fa6
    fadd.d  fa2, fa2, fa6
    fadd.d  fa3, fa3, fa6
    fadd.d  fa4, fa4, fa6
    addi    t0, t0, -1
    bnez    t0, 1b
2:
    fadd.d  ft0, ft0, ft1
    fadd.d  ft0, ft0, ft2
    fadd.d  ft0, ft0, ft3
    fadd.d  ft0, ft0, ft4
    fadd.d  ft0, ft0, ft5
    fadd.d  ft0, ft0, ft6
    fadd.d  ft0, ft0, ft7
    fadd.d  ft8, ft8, ft9
    fadd.d  ft8, ft8, ft10
    fadd.d  ft8, ft8, ft11
    fadd.d  ft8, ft8, fa1
    fadd.d  ft8, ft8, fa2
    fadd.d  ft8, ft8, fa3
    fadd.d  ft8, ft8, fa4
    fadd.d  fa0, ft0, ft8
    ret
    SIZE(stx_a_fp)

    .globl SYM(stx_a_read)
    TYPE(stx_a_read)
    .p2align 2
SYM(stx_a_read):
    srli    t0, a1, 3
    li      t1, 0
    li      t2, 0
    li      t3, 0
    li      t4, 0
    beqz    t0, 2f
1:
    ld      t5, 0(a0)
    ld      t6, 8(a0)
    add     t1, t1, t5
    add     t2, t2, t6
    ld      t5, 16(a0)
    ld      t6, 24(a0)
    add     t3, t3, t5
    add     t4, t4, t6
    ld      t5, 32(a0)
    ld      t6, 40(a0)
    add     t1, t1, t5
    add     t2, t2, t6
    ld      t5, 48(a0)
    ld      t6, 56(a0)
    add     t3, t3, t5
    add     t4, t4, t6
    addi    a0, a0, 64
    addi    t0, t0, -1
    bnez    t0, 1b
2:
    add     a0, t1, t2
    add     a0, a0, t3
    add     a0, a0, t4
    ret
    SIZE(stx_a_read)

    .globl SYM(stx_a_write)
    TYPE(stx_a_write)
    .p2align 2
SYM(stx_a_write):
    srli    t0, a1, 3
    beqz    t0, 2f
1:
    sd      a2, 0(a0)
    sd      a2, 8(a0)
    sd      a2, 16(a0)
    sd      a2, 24(a0)
    sd      a2, 32(a0)
    sd      a2, 40(a0)
    sd      a2, 48(a0)
    sd      a2, 56(a0)
    addi    a0, a0, 64
    addi    t0, t0, -1
    bnez    t0, 1b
2:
    ret
    SIZE(stx_a_write)

    .globl SYM(stx_a_chase)
    TYPE(stx_a_chase)
    .p2align 2
SYM(stx_a_chase):
    srli    t0, a1, 2
    beqz    t0, 2f
1:
    ld      a0, 0(a0)
    ld      a0, 0(a0)
    ld      a0, 0(a0)
    ld      a0, 0(a0)
    addi    t0, t0, -1
    bnez    t0, 1b
2:
    ret
    SIZE(stx_a_chase)

    .globl SYM(stx_a_clock)
    TYPE(stx_a_clock)
    .p2align 2
SYM(stx_a_clock):
    mv      t0, a0
    li      a0, 0
    li      t1, 1
    beqz    t0, 2f
1:
    .rept 64
    add     a0, a0, t1
    .endr
    addi    t0, t0, -1
    bnez    t0, 1b
2:
    ret
    SIZE(stx_a_clock)

#endif /* arch */

#endif /* STX_ASM */

#if defined(__ELF__)
    .section .note.GNU-stack, "", %progbits
#endif
