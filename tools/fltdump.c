// Dumps float-rounded x87 results for one block of 2^20 consecutive float inputs, per op, at PC24/RN.
//   fltdump.exe <hex start bits> <out.bin>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define U(name, insn) static float name(float a) { float r; __asm__ volatile("flds %1\n\t" insn "\n\tfstps %0" : "=m"(r) : "m"(a) : "st", "st(1)", "st(2)"); return r; }
U(f_atan, "fld1\n\tfpatan")
U(f_atan2r, "fld1\n\tfxch\n\tfpatan")
U(f_log2, "fld1\n\tfxch\n\tfyl2x")
U(f_2xm1, "f2xm1")
U(f_sin, "fsin")
// atan2(a, 0.7f*a+1): generic two-argument case with y and x of similar magnitude
static float f_atan2g(float a) { float x = a * 0.7f + 1.0f, r; __asm__ volatile("flds %1\n\tflds %2\n\tfpatan\n\tfstps %0" : "=m"(r) : "m"(a), "m"(x) : "st", "st(1)"); return r; }
int main(int argc, char **argv) {
    uint32_t start = strtoul(argv[1], 0, 16);
    uint16_t cw = 0x007f; __asm__ volatile("fldcw %0" :: "m"(cw));
    FILE *f = fopen(argv[2], "wb");
    float (*fn[])(float) = {f_atan, f_atan2r, f_log2, f_2xm1, f_sin, f_atan2g};
    static float r[1 << 20];
    for (int k = 0; k < 6; k++) {
        for (uint32_t i = 0; i < (1u << 20); i++) { uint32_t b = start + i; float a; memcpy(&a, &b, 4); r[i] = fn[k](a); }
        fwrite(r, 4, 1 << 20, f);
    }
    fclose(f);
    return 0;
}
