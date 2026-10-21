// Game pattern at 0x5fc86d / 0x5fe5d0: rel = atan2(dy,dx) - orientation, all in x87 at PC24.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
static uint32_t rng = 1;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * ((xs() >> 8) * (1.0f / 16777216.0f)); }
static float atan2f_x87(float y, float x) { float r; __asm__ volatile("flds %1\n\tflds %2\n\tfpatan\n\tfstps %0" : "=m"(r) : "m"(y), "m"(x) : "st", "st(1)"); return r; }
static float relangle(float dy, float dx, float orient) {
    float r; __asm__ volatile("flds %1\n\tflds %2\n\tfpatan\n\tfsubs %3\n\tfstps %0" : "=m"(r) : "m"(dy), "m"(dx), "m"(orient) : "st", "st(1)"); return r;
}
int main(int argc, char **argv) {
    int N = argc > 1 ? atoi(argv[1]) : 16000000;
    uint16_t cw = 0x007f; __asm__ volatile("fldcw %0" :: "m"(cw));
    FILE *f = fopen(argc > 2 ? argv[2] : "relangle.bin", "wb");
    uint64_t h = 0xcbf29ce484222325ULL; int nonzero = 0;
    for (int i = 0; i < N; i++) {
        // unit at random spot, target within 500 units; orientation = what the unit stored last frame
        float dx = frand(-500, 500), dy = frand(-500, 500);
        float orient = atan2f_x87(dy, dx);           // already facing the target (float-rounded)
        if (i & 1) orient = atan2f_x87(dy + frand(-0.01f, 0.01f), dx);  // target moved slightly
        float r = relangle(dy, dx, orient);
        uint32_t b; memcpy(&b, &r, 4); fwrite(&b, 4, 1, f);
        h = (h ^ b) * 0x100000001b3ULL; nonzero += r != 0;
    }
    fclose(f);
    printf("relangle N=%d nonzero=%d hash=%016llx\n", N, nonzero, (unsigned long long)h);
    return 0;
}
