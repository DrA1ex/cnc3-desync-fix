// Loads MSVCR80 + d3dx9_29 the same way cnc3game.dat does (same SxS manifest) and
// hashes the math the game imports from them.
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; } return h;
}
static uint32_t rng;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * ((xs() >> 8) * (1.0f / 16777216.0f)); }

static void describe(const char *name, HMODULE m) {
    char path[MAX_PATH] = "?";
    GetModuleFileNameA(m, path, sizeof path);
    int builtin = m && !memcmp((char *)m + 0x40, "Wine builtin DLL", 16);
    printf("%-12s %s  [%s]\n", name, path, m ? (builtin ? "Wine builtin" : "NATIVE") : "not loaded");
}

static void *ci[16];
static const char *ci_names[] = {"_CIsin", "_CIcos", "_CItan", "_CIatan", "_CIsqrt", "_CIlog", "_CIexp", "_CIasin", "_CIacos", "_CIlog10", 0};
static const char *ci2_names[] = {"_CIatan2", "_CIpow", "_CIfmod", 0};

// MSVC _CI* convention: args on x87 stack, result in ST0
static float call_ci1(void *f, float a) {
    float r; __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0" : "=m"(r) : "m"(a), "r"(f) : "eax", "ecx", "edx", "memory", "st");
    return r;
}
static float call_ci2(void *f, float a, float b) {
    float r; __asm__ volatile("flds %1\n\tflds %2\n\tcall *%3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "r"(f) : "eax", "ecx", "edx", "memory", "st");
    return r;
}

typedef struct { float m[16]; } M;
typedef struct { float x, y, z; } V3;
typedef struct { float x, y, z, w; } V4;

int main(int argc, char **argv) {
    int N = argc > 1 ? atoi(argv[1]) : 200000;
    HMODULE crt = LoadLibraryA("msvcr80.dll");
    HMODULE dx = LoadLibraryA("d3dx9_29.dll");
    describe("msvcr80", crt);
    describe("d3dx9_29", dx);
    if (!crt) return 1;

    // game init: _set_SSE2_enable(0); then setFPMode -> PC24, RN
    int (*set_sse2)(int) = (void *)GetProcAddress(crt, "_set_SSE2_enable");
    unsigned (*controlfp)(unsigned, unsigned) = (void *)GetProcAddress(crt, "_controlfp");
    printf("_set_SSE2_enable(0) -> %d\n", set_sse2 ? set_sse2(0) : -99);
    controlfp(0x20000, 0x30300);

    float ranges[][2] = {{-3.2f, 3.2f}, {-1.0f, 1.0f}, {0.01f, 500.0f}};
    for (int k = 0; ci_names[k]; k++) {
        void *f = GetProcAddress(crt, ci_names[k]);
        for (int r = 0; r < 3; r++) {
            uint64_t h = 0xcbf29ce484222325ULL; rng = 0x1234567 + k * 31 + r;
            for (int i = 0; i < N; i++) { float x = call_ci1(f, frand(ranges[r][0], ranges[r][1])); h = fnv(h, &x, 4); }
            printf("%-9s [%6g,%6g] %016llx\n", ci_names[k], ranges[r][0], ranges[r][1], (unsigned long long)h);
        }
    }
    for (int k = 0; ci2_names[k]; k++) {
        void *f = GetProcAddress(crt, ci2_names[k]);
        uint64_t h = 0xcbf29ce484222325ULL; rng = 0x7654321 + k;
        for (int i = 0; i < N; i++) { float a = frand(0.01f, 50.0f), b = frand(-3.0f, 3.0f); float x = call_ci2(f, a, b); h = fnv(h, &x, 4); }
        printf("%-9s [mixed]        %016llx\n", ci2_names[k], (unsigned long long)h);
    }
    // strtod / atof: game parses INI data with these
    double (*p_strtod)(const char *, char **) = (void *)GetProcAddress(crt, "strtod");
    double (*p_atof)(const char *) = (void *)GetProcAddress(crt, "atof");
    {
        uint64_t h = 0xcbf29ce484222325ULL, h2 = h; char buf[64]; rng = 99;
        for (int i = 0; i < N; i++) {
            uint32_t a = xs() % 100000, b = xs() % 1000000000u, e = xs() % 9;
            snprintf(buf, sizeof buf, "%u.%09ue-%u", a, b, e);
            double d = p_strtod(buf, NULL); float f = (float)d; h = fnv(h, &d, 8); h2 = fnv(h2, &f, 4);
        }
        snprintf(buf, sizeof buf, "0.1"); double q = p_atof(buf);
        printf("strtod double=%016llx float=%016llx atof(0.1)=%016llx\n", (unsigned long long)h, (unsigned long long)h2, *(unsigned long long *)&q);
    }
    // qsort with many equal keys: order of equal elements is implementation-defined
    void (*p_qsort)(void *, size_t, size_t, int (*)(const void *, const void *)) = (void *)GetProcAddress(crt, "qsort");
    {
        static uint32_t arr[50000]; rng = 5;
        for (int i = 0; i < 50000; i++) arr[i] = ((xs() % 64) << 16) | i;
        int cmp(const void *a, const void *b) { return (int)(*(uint32_t *)a >> 16) - (int)(*(uint32_t *)b >> 16); }
        p_qsort(arr, 50000, 4, cmp);
        printf("qsort(equal keys) order=%016llx\n", (unsigned long long)fnv(0xcbf29ce484222325ULL, arr, sizeof arr));
    }
    // rand
    {
        void (*p_srand)(unsigned) = (void *)GetProcAddress(crt, "srand"); int (*p_rand)(void) = (void *)GetProcAddress(crt, "rand");
        p_srand(12345); uint64_t h = 0xcbf29ce484222325ULL; for (int i = 0; i < 1000; i++) { int v = p_rand(); h = fnv(h, &v, 4); }
        printf("rand %016llx\n", (unsigned long long)h);
    }
    if (dx) {
        M *(WINAPI *rotAxis)(M *, const V3 *, float) = (void *)GetProcAddress(dx, "D3DXMatrixRotationAxis");
        M *(WINAPI *rotX)(M *, float) = (void *)GetProcAddress(dx, "D3DXMatrixRotationX");
        M *(WINAPI *rotY)(M *, float) = (void *)GetProcAddress(dx, "D3DXMatrixRotationY");
        M *(WINAPI *rotZ)(M *, float) = (void *)GetProcAddress(dx, "D3DXMatrixRotationZ");
        M *(WINAPI *inv)(M *, float *, const M *) = (void *)GetProcAddress(dx, "D3DXMatrixInverse");
        V4 *(WINAPI *v4t)(V4 *, const V4 *, const M *) = (void *)GetProcAddress(dx, "D3DXVec4Transform");
        V3 *(WINAPI *cr)(V3 *, const V3 *, const V3 *, const V3 *, const V3 *, float) = (void *)GetProcAddress(dx, "D3DXVec3CatmullRom");
        uint64_t hR = 0xcbf29ce484222325ULL, hI = hR, hT = hR, hC = hR; rng = 77;
        for (int i = 0; i < N / 10; i++) {
            V3 ax = {frand(-1, 1), frand(-1, 1), frand(-1, 1)}; float ang = frand(-6.3f, 6.3f);
            M m, mx, my, mz, mi; float det;
            rotAxis(&m, &ax, ang); rotX(&mx, ang); rotY(&my, ang * 0.5f); rotZ(&mz, ang * 0.25f);
            hR = fnv(hR, &m, sizeof m); hR = fnv(hR, &mx, sizeof m); hR = fnv(hR, &my, sizeof m); hR = fnv(hR, &mz, sizeof m);
            inv(&mi, &det, &m); hI = fnv(hI, &mi, sizeof mi);
            V4 v = {frand(-100, 100), frand(-100, 100), frand(-100, 100), 1}, o; v4t(&o, &v, &m); hT = fnv(hT, &o, sizeof o);
            V3 a = {frand(-9, 9), 1, 2}, b = {3, frand(-9, 9), 4}, c = {5, 6, frand(-9, 9)}, d = {7, 8, 9}, e;
            cr(&e, &a, &b, &c, &d, frand(0, 1)); hC = fnv(hC, &e, sizeof e);
        }
        printf("d3dx rot=%016llx inv=%016llx vec4transform=%016llx catmullrom=%016llx\n",
               (unsigned long long)hR, (unsigned long long)hI, (unsigned long long)hT, (unsigned long long)hC);
    }
    return 0;
}
