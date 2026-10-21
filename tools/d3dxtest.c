// Runs the D3DX math that cnc3game.dat imports on fixed pseudo-random inputs, with the game's
// FPU mode (x87 PC24, round-nearest), and writes every float result to a file for bitwise comparison.
//   d3dxtest.exe <dll name or path> <out.bin> [N]
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef struct { float m[16]; } M;
typedef struct { float x, y, z; } V3;
typedef struct { float x, y, z, w; } V4;

static uint32_t rng;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * ((xs() >> 8) * (1.0f / 16777216.0f)); }
static uint64_t fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = p; while (n--) { h ^= *b++; h *= 0x100000001b3ULL; } return h; }

// BezierSegment basis matrix, copied from cnc3game.dat .data:0xb9d230
static const M bez = {{-1, 3, -3, 1, 3, -6, 3, 0, -3, 3, 0, 0, 1, 0, 0, 0}};

static FILE *out;
static uint64_t h;
static void put(const void *p, size_t n) { fwrite(p, 1, n, out); h = fnv(h, p, n); }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <dll> <out.bin> [N]\n", argv[0]); return 2; }
    int N = argc > 3 ? atoi(argv[3]) : 200000;
    HMODULE dx = LoadLibraryA(argv[1]);
    if (!dx) { printf("cannot load %s (%lu)\n", argv[1], GetLastError()); return 1; }
    char path[MAX_PATH]; GetModuleFileNameA(dx, path, sizeof path);
    printf("%s [%s]\n", path, memcmp((char *)dx + 0x40, "Wine builtin DLL", 16) ? "native" : "Wine builtin");
    V4 *(WINAPI *v4t)(V4 *, const V4 *, const M *) = (void *)GetProcAddress(dx, "D3DXVec4Transform");
    M *(WINAPI *rotAxis)(M *, const V3 *, float) = (void *)GetProcAddress(dx, "D3DXMatrixRotationAxis");
    M *(WINAPI *rotX)(M *, float) = (void *)GetProcAddress(dx, "D3DXMatrixRotationX");
    M *(WINAPI *rotY)(M *, float) = (void *)GetProcAddress(dx, "D3DXMatrixRotationY");
    M *(WINAPI *rotZ)(M *, float) = (void *)GetProcAddress(dx, "D3DXMatrixRotationZ");
    M *(WINAPI *inv)(M *, float *, const M *) = (void *)GetProcAddress(dx, "D3DXMatrixInverse");
    M *(WINAPI *tr)(M *, const M *) = (void *)GetProcAddress(dx, "D3DXMatrixTranspose");
    V3 *(WINAPI *cr)(V3 *, const V3 *, const V3 *, const V3 *, const V3 *, float) = (void *)GetProcAddress(dx, "D3DXVec3CatmullRom");
    out = fopen(argv[2], "wb");
    uint16_t cw = 0x007f; __asm__ volatile("fldcw %0" :: "m"(cw));   // what setFPMode leaves: PC24, RN, all masked

    #define SECTION(name, ...) do { h = 0xcbf29ce484222325ULL; rng = 0x9e3779b9u ^ (uint32_t)sizeof(name); \
        char tag[16] = name; fwrite(tag, 1, 16, out); __VA_ARGS__; printf("%-14s %016llx\n", name, (unsigned long long)h); } while (0)

    // game: BezierSegment::getSegmentPoints - 3 x Vec4Transform(coords of 4 control points, basis)
    SECTION("bezier", for (int i = 0; i < N; i++) {
        V4 c = {frand(0, 5000), frand(0, 5000), frand(0, 5000), frand(0, 5000)}, o;
        v4t(&o, &c, &bez); put(&o, sizeof o); });
    SECTION("vec4transform", for (int i = 0; i < N; i++) {
        M m; for (int k = 0; k < 16; k++) m.m[k] = frand(-2, 2);
        V4 v = {frand(-1000, 1000), frand(-1000, 1000), frand(-1000, 1000), 1}, o;
        v4t(&o, &v, &m); put(&o, sizeof o); });
    SECTION("rotation", for (int i = 0; i < N / 4; i++) {
        V3 ax = {frand(-1, 1), frand(-1, 1), frand(-1, 1)}; float a = frand(-6.3f, 6.3f); M m;
        rotAxis(&m, &ax, a); put(&m, sizeof m); rotX(&m, a); put(&m, sizeof m);
        rotY(&m, a); put(&m, sizeof m); rotZ(&m, a); put(&m, sizeof m); });
    SECTION("inverse", for (int i = 0; i < N / 4; i++) {
        M m, r; float det; for (int k = 0; k < 16; k++) m.m[k] = frand(-2, 2);
        inv(&r, &det, &m); put(&r, sizeof r); put(&det, 4); tr(&r, &m); put(&r, sizeof r); });
    SECTION("catmullrom", for (int i = 0; i < N; i++) {
        V3 p[4], o; for (int k = 0; k < 4; k++) p[k] = (V3){frand(0, 5000), frand(0, 5000), frand(0, 100)};
        cr(&o, &p[0], &p[1], &p[2], &p[3], frand(0, 1)); put(&o, sizeof o); });
    fclose(out);
    return 0;
}
