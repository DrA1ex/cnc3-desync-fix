// Exhaustive x87 transcendental test: every positive float with exponent in range, PC24/RN.
// Prints FNV hash of float-rounded results per block of 2^20 inputs.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static void setcw(uint16_t cw) { __asm__ volatile("fldcw %0" :: "m"(cw)); }

#define U(name, insn) static float name(float a) { float r; __asm__ volatile("flds %1\n\t" insn "\n\tfstps %0" : "=m"(r) : "m"(a) : "st", "st(1)"); return r; }
U(f_sin, "fsin")
U(f_cos, "fcos")
U(f_tan, "fptan\n\tfstp %%st(0)")
U(f_sc_s, "fsincos\n\tfstp %%st(0)")
U(f_sc_c, "fsincos\n\tfstp %%st(1)")
U(f_atan, "fld1\n\tfpatan")
U(f_atan2r, "fld1\n\tfxch\n\tfpatan")
U(f_log2, "fld1\n\tfxch\n\tfyl2x")
U(f_2xm1, "f2xm1")
// x87 result kept in register then multiplied (game pattern: fsin; fmul [x]; fstp)
static float f_sinmul(float a) { float r, k = 37.123f; __asm__ volatile("flds %1\n\tfsin\n\tfmuls %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(k) : "st"); return r; }

typedef float (*fn_t)(float);
struct { const char *n; fn_t f; int emin, emax; } T[] = {
    {"sin", f_sin, 100, 135}, {"cos", f_cos, 100, 135}, {"tan", f_tan, 100, 127},
    {"sc_s", f_sc_s, 100, 135}, {"sc_c", f_sc_c, 100, 135},
    {"atan", f_atan, 100, 150}, {"atan2r", f_atan2r, 100, 150},
    {"log2", f_log2, 100, 150}, {"2xm1", f_2xm1, 100, 126}, {"sinmul", f_sinmul, 100, 135},
};

int main(int argc, char **argv) {
    const char *only = argc > 1 ? argv[1] : NULL;
    setcw(0x007f);
    for (size_t t = 0; t < sizeof T / sizeof T[0]; t++) {
        if (only && strcmp(only, T[t].n)) continue;
        // exponents emin..emax (biased) cover ~[2^-27, 2^8..2^23]
        for (uint32_t bits = (uint32_t)T[t].emin << 23; bits < ((uint32_t)T[t].emax + 1) << 23; bits += 1u << 20) {
            uint64_t h = 0xcbf29ce484222325ULL;
            for (uint32_t i = 0; i < (1u << 20); i++) {
                uint32_t b = bits + i; float a; memcpy(&a, &b, 4);
                float r = T[t].f(a); uint32_t rb; memcpy(&rb, &r, 4);
                h = (h ^ rb) * 0x100000001b3ULL;
            }
            printf("%s %08x %016llx\n", T[t].n, bits, (unsigned long long)h);
        }
        fflush(stdout);
    }
    return 0;
}
