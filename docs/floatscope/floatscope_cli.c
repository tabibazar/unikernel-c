// floatscope_cli.c -- floatscope with maskable interrupts masked.
//
// The control that attributes any fault to the guest's own interrupt handling:
// cli cannot stop a VM-exit, only a guest interrupt. Privileged, so this is
// built but never run on the host -- it is a BareMetal-only experiment.
//
// docs/arithmetic-fault/ proved the ISR clobbers a general-purpose register
// live inside __umodti3, and proved it by contrast: the single-instruction
// mulq was never wrong, the multi-instruction software routine was. XMM was
// never tested. Every number in the ant-colony workload is floating point,
// so this closes that gap before anything is built on it.
//
// Same experimental design as faultscope, one register file over:
//
//   fmul      a*b            single mulsd   -- atomic, expected clean
//   fdiv      a/b            single divsd   -- atomic, expected clean
//   sw_sqrt   Newton sqrt    ~25 instrs     -- the suspect
//   hw_sqrt   sqrtsd         single instr   -- the control for sw_sqrt
//
// If only sw_sqrt faults, the mechanism is the one already root-caused and it
// extends to SSE. If nothing faults, the fault is confined to the integer
// path and the existing bug report narrows.
//
// Inputs are volatile so the compiler recomputes every iteration rather than
// hoisting the constant answer out of the loop.

#include <stdio.h>
#include <stdint.h>

#ifndef ITERS
#define ITERS 200000000ULL
#endif

static volatile double va = 1.4142135623730951;   // sqrt(2)
static volatile double vb = 3.1415926535897931;   // pi

// Bit-identical to aco.c's a_sqrt. Multi-instruction by construction: an
// interrupt can land between any two of its steps.
static double sw_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    union { double d; uint64_t u; } v = { x };
    v.u = (v.u + 0x3FF0000000000000ULL) >> 1;
    double r = v.d;
    for (int i = 0; i < 8; i++) r = 0.5 * (r + x / r);
    return r;
}

// The atomic control for sw_sqrt: one instruction, uninterruptible. On a host
// without a hardware sqrt instruction this degenerates into sw_sqrt, which
// makes the control worthless -- so say so loudly rather than compare a
// function against itself.
#if defined(__x86_64__)
#define HW_SQRT_IS_REAL 1
static inline double hw_sqrt(double x) {
    double r;
    __asm__ ("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}
#elif defined(__aarch64__)
#define HW_SQRT_IS_REAL 1
static inline double hw_sqrt(double x) {
    double r;
    __asm__ ("fsqrt %d0, %d1" : "=w"(r) : "w"(x));
    return r;
}
#else
#define HW_SQRT_IS_REAL 0
static inline double hw_sqrt(double x) { return sw_sqrt(x); }
#endif

static inline uint64_t bits(double d) {
    union { double d; uint64_t u; } v = { d };
    return v.u;
}

int main(void) {
#if defined(__x86_64__)
    __asm__ volatile ("cli");
#else
#error "floatscope_cli targets x86-64 BareMetal; cli has no meaning elsewhere"
#endif
    setvbuf(stdout, NULL, _IOLBF, 0);

    double a = va, b = vb;
    uint64_t r_mul = bits(a * b);
    uint64_t r_div = bits(a / b);
    uint64_t r_sws = bits(sw_sqrt(a * b));
    uint64_t r_hws = bits(hw_sqrt(a * b));

    printf("SCOPE_START hw_sqrt_is_real=%d iters=%llu mul=%016llx div=%016llx sw_sqrt=%016llx hw_sqrt=%016llx\n",
           HW_SQRT_IS_REAL, (unsigned long long)ITERS,
           (unsigned long long)r_mul, (unsigned long long)r_div,
           (unsigned long long)r_sws, (unsigned long long)r_hws);

    uint64_t mul_bad = 0, div_bad = 0, sws_bad = 0, hws_bad = 0;
    int shown = 0;

    for (uint64_t i = 0; i < ITERS; i++) {
        double la = va, lb = vb;
        uint64_t m = bits(la * lb);
        uint64_t d = bits(la / lb);
        uint64_t s = bits(sw_sqrt(la * lb));
        uint64_t h = bits(hw_sqrt(la * lb));

        int mb = (m != r_mul), db = (d != r_div);
        int sb = (s != r_sws), hb = (h != r_hws);
        mul_bad += mb; div_bad += db; sws_bad += sb; hws_bad += hb;

        if ((mb || db || sb || hb) && shown < 12) {
            shown++;
            printf("EVENT i=%llu mul=%d div=%d sw_sqrt=%d hw_sqrt=%d  "
                   "got %016llx %016llx %016llx %016llx  "
                   "xor %016llx %016llx %016llx %016llx\n",
                   (unsigned long long)i, mb, db, sb, hb,
                   (unsigned long long)m, (unsigned long long)d,
                   (unsigned long long)s, (unsigned long long)h,
                   (unsigned long long)(m ^ r_mul), (unsigned long long)(d ^ r_div),
                   (unsigned long long)(s ^ r_sws), (unsigned long long)(h ^ r_hws));
        }
    }

    printf("SCOPE_DONE iters=%llu mul_bad=%llu div_bad=%llu sw_sqrt_bad=%llu hw_sqrt_bad=%llu "
           "sw_sqrt_rate=%.3e\n",
           (unsigned long long)ITERS,
           (unsigned long long)mul_bad, (unsigned long long)div_bad,
           (unsigned long long)sws_bad, (unsigned long long)hws_bad,
           (double)sws_bad / (double)ITERS);
    return 0;
}
