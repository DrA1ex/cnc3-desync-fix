// FPU determinism probe: runs x87/SSE ops on deterministic inputs and dumps results.
// Build: i686-w64-mingw32-gcc -O1 -o fputest.exe fputest.c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static uint32_t rng = 0x12345678;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * ((xs() >> 8) * (1.0f / 16777216.0f)); }

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; } return h;
}

static void setcw(uint16_t cw) { __asm__ volatile("fldcw %0" :: "m"(cw)); }
static uint16_t getcw(void) { uint16_t cw; __asm__ volatile("fnstcw %0" : "=m"(cw)); return cw; }

typedef struct { uint8_t b[10]; } ext80;

// each op: input float(s) -> result stored as float and as 80-bit
#define OP1(name, insn) \
static void name(float a, float *rf, ext80 *rx) { \
    __asm__ volatile("flds %2\n\t" insn "\n\tfld %%st(0)\n\tfstps %0\n\tfstpt %1" : "=m"(*rf), "=m"(*rx) : "m"(a) : "st", "st(1)"); }
OP1(op_fsin, "fsin")
OP1(op_fcos, "fcos")
OP1(op_fsqrt, "fsqrt")
OP1(op_frndint, "frndint")
OP1(op_f2xm1, "f2xm1")
static void op_fptan(float a, float *rf, ext80 *rx) {
    __asm__ volatile("flds %2\n\tfptan\n\tfstp %%st(0)\n\tfld %%st(0)\n\tfstps %0\n\tfstpt %1" : "=m"(*rf), "=m"(*rx) : "m"(a) : "st", "st(1)");
}
static void op_fsincos(float a, float *rf, ext80 *rx) { // stores sin (ST1) via rf, cos via rx
    __asm__ volatile("flds %2\n\tfsincos\n\tfstpt %1\n\tfstps %0" : "=m"(*rf), "=m"(*rx) : "m"(a) : "st", "st(1)");
}
static void op_fpatan(float y, float x, float *rf, ext80 *rx) {
    __asm__ volatile("flds %2\n\tflds %3\n\tfpatan\n\tfld %%st(0)\n\tfstps %0\n\tfstpt %1" : "=m"(*rf), "=m"(*rx) : "m"(y), "m"(x) : "st", "st(1)");
}
static void op_fyl2x(float y, float x, float *rf, ext80 *rx) {
    __asm__ volatile("flds %2\n\tflds %3\n\tfyl2x\n\tfld %%st(0)\n\tfstps %0\n\tfstpt %1" : "=m"(*rf), "=m"(*rx) : "m"(y), "m"(x) : "st", "st(1)");
}
// arithmetic chain kept in x87 registers: ((a*b)+c)/d - a  -> tests precision control
static void op_chain(float a, float b, float *rf, ext80 *rx) {
    float c = 1.1f, d = 3.3f;
    __asm__ volatile("flds %2\n\tfmuls %3\n\tfadds %4\n\tfdivs %5\n\tfsubs %2\n\tfld %%st(0)\n\tfstps %0\n\tfstpt %1"
        : "=m"(*rf), "=m"(*rx) : "m"(a), "m"(b), "m"(c), "m"(d) : "st", "st(1)");
}

static int N = 1 << 20;

typedef struct { const char *name; int kind; void *fn; float lo, hi; } test_t;
// kind 1: unary, 2: binary (y in [lo,hi], x in [lo,hi])

static FILE *dumpf;

static void run(const char *tag, test_t *t) {
    float *rf = malloc(sizeof(float) * N);
    ext80 *rx = malloc(sizeof(ext80) * N);
    rng = 0x9e3779b9u ^ (uint32_t)(uintptr_t)t->name[0] ^ (uint32_t)strlen(t->name) * 7919u;
    for (int i = 0; i < N; i++) {
        float a = frand(t->lo, t->hi);
        if (t->kind == 1) ((void(*)(float, float*, ext80*))t->fn)(a, &rf[i], &rx[i]);
        else { float b = frand(t->lo, t->hi); ((void(*)(float, float, float*, ext80*))t->fn)(a, b, &rf[i], &rx[i]); }
    }
    uint64_t hf = fnv(0xcbf29ce484222325ULL, rf, sizeof(float) * N);
    uint64_t hx = fnv(0xcbf29ce484222325ULL, rx, sizeof(ext80) * N);
    printf("%-6s %-10s [%9g,%9g] float=%016llx ext80=%016llx\n", tag, t->name, t->lo, t->hi,
           (unsigned long long)hf, (unsigned long long)hx);
    if (dumpf) {
        char hdr[64] = {0}; snprintf(hdr, sizeof hdr, "%s/%s/%g/%g", tag, t->name, t->lo, t->hi);
        fwrite(hdr, 1, 64, dumpf); fwrite(rf, sizeof(float), N, dumpf); fwrite(rx, sizeof(ext80), N, dumpf);
    }
    free(rf); free(rx);
}

static void cpuinfo(void) {
    uint32_t a, b, c, d; char v[13] = {0};
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    memcpy(v, &b, 4); memcpy(v + 4, &d, 4); memcpy(v + 8, &c, 4);
    uint32_t a1, b1, c1, d1;
    __asm__ volatile("cpuid" : "=a"(a1), "=b"(b1), "=c"(c1), "=d"(d1) : "a"(1));
    printf("cpuid vendor=%s leaf1 eax=%08x ecx=%08x edx=%08x\n", v, a1, c1, d1);
}

static void sse_misc(void) {
    // rcpss/rsqrtss are implementation-defined approximations
    uint64_t h1 = 0xcbf29ce484222325ULL, h2 = h1, h3 = h1;
    rng = 42;
    for (int i = 0; i < N; i++) {
        float a = frand(0.001f, 1000.0f), r1, r2;
        __asm__ volatile("rcpss %1, %%xmm0\n\tmovss %%xmm0, %0" : "=m"(r1) : "m"(a) : "xmm0");
        __asm__ volatile("rsqrtss %1, %%xmm0\n\tmovss %%xmm0, %0" : "=m"(r2) : "m"(a) : "xmm0");
        h1 = fnv(h1, &r1, 4); h2 = fnv(h2, &r2, 4);
        float s; __asm__ volatile("sqrtss %1, %%xmm0\n\tmovss %%xmm0, %0" : "=m"(s) : "m"(a) : "xmm0");
        h3 = fnv(h3, &s, 4);
    }
    printf("sse    rcpss=%016llx rsqrtss=%016llx sqrtss=%016llx\n", (unsigned long long)h1, (unsigned long long)h2, (unsigned long long)h3);
    // NaN propagation: mulss qnan1, qnan2 -> x86 returns first source operand's NaN
    uint32_t n1 = 0x7fc00001, n2 = 0x7fc00002, out; float f1, f2; memcpy(&f1, &n1, 4); memcpy(&f2, &n2, 4);
    __asm__ volatile("movss %1, %%xmm0\n\tmulss %2, %%xmm0\n\tmovss %%xmm0, %0" : "=m"(out) : "m"(f1), "m"(f2) : "xmm0");
    printf("sse    nan-prop mulss(n1,n2)=%08x", out);
    float z = 0.0f, inf;
    __asm__ volatile("movss %1, %%xmm0\n\tdivss %1, %%xmm0\n\tmovss %%xmm0, %0" : "=m"(out) : "m"(z) : "xmm0");
    printf(" 0/0=%08x", out);
    uint32_t big = 0x4f800000; memcpy(&inf, &big, 4); int32_t ci;
    __asm__ volatile("cvttss2si %1, %0" : "=r"(ci) : "m"(inf));
    printf(" cvttss2si(2^32)=%08x", (uint32_t)ci);
    uint32_t den = 0x00000123; float fd, dm; memcpy(&fd, &den, 4); float two = 2.0f;
    __asm__ volatile("movss %1, %%xmm0\n\tmulss %2, %%xmm0\n\tmovss %%xmm0, %0" : "=m"(dm) : "m"(fd), "m"(two) : "xmm0");
    memcpy(&out, &dm, 4); printf(" denorm*2=%08x", out);
    uint32_t mx; __asm__ volatile("stmxcsr %0" : "=m"(mx)); printf(" mxcsr=%08x\n", mx);
    // x87 NaN / integer-indefinite
    int32_t fi; __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(fi) : "m"(inf)); printf("x87    fistp(2^32)=%08x\n", (uint32_t)fi);
}

int main(int argc, char **argv) {
    if (argc > 1) N = atoi(argv[1]);
    if (argc > 2) dumpf = fopen(argv[2], "wb");
    cpuinfo();
    printf("default fpu cw=%04x\n", getcw());
    test_t tests[] = {
        {"fsin", 1, op_fsin, -3.2f, 3.2f},
        {"fsin", 1, op_fsin, -100.0f, 100.0f},
        {"fcos", 1, op_fcos, -3.2f, 3.2f},
        {"fcos", 1, op_fcos, -100.0f, 100.0f},
        {"fsincos", 1, op_fsincos, -3.2f, 3.2f},
        {"fptan", 1, op_fptan, -1.5f, 1.5f},
        {"fpatan", 2, op_fpatan, -1000.0f, 1000.0f},
        {"fpatan", 2, op_fpatan, -1.0f, 1.0f},
        {"fyl2x", 2, op_fyl2x, 0.001f, 1000.0f},
        {"f2xm1", 1, op_f2xm1, -1.0f, 1.0f},
        {"fsqrt", 1, op_fsqrt, 0.0f, 100000.0f},
        {"frndint", 1, op_frndint, -1000.0f, 1000.0f},
        {"chain", 2, op_chain, -1000.0f, 1000.0f},
    };
    struct { const char *tag; uint16_t cw; } modes[] = {
        {"PC24", 0x007f}, {"PC53", 0x027f}, {"PC64", 0x037f},
    };
    for (int m = 0; m < 3; m++) {
        for (size_t t = 0; t < sizeof tests / sizeof tests[0]; t++) {
            setcw(modes[m].cw);
            run(modes[m].tag, &tests[t]);
        }
    }
    setcw(0x037f);
    sse_misc();
    if (dumpf) fclose(dumpf);
    return 0;
}
