/*
 * SIMD Feature Detector for EqualizerAPO-XT
 * Detects CPU SIMD capabilities and recommends the optimal build variant.
 *
 * Compile (MSVC):   cl /O2 simd_check.c /Fe:simd_check.exe
 * Compile (GCC):    gcc -O2 -o simd_check simd_check.c
 * Compile (Clang):  clang -O2 -o simd_check simd_check.c
 */

#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#include <intrin.h>
#define cpuid(info, leaf)       __cpuid(info, leaf)
#define cpuidex(info, leaf, sub) __cpuidex(info, leaf, sub)
typedef unsigned __int64 uint64_t;
#else
#include <cpuid.h>
#include <stdint.h>
static inline void cpuid(int info[4], int leaf) {
    __cpuid(leaf, info[0], info[1], info[2], info[3]);
}
static inline void cpuidex(int info[4], int leaf, int sub) {
    __cpuid_count(leaf, sub, info[0], info[1], info[2], info[3]);
}
#endif

/* XCR0 read — OS가 AVX 상태를 보존하는지 확인 */
static uint64_t xgetbv(unsigned int index) {
#ifdef _MSC_VER
    return _xgetbv(index);
#else
    unsigned int eax, edx;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
#endif
}

typedef struct {
    char brand[49];
    int sse2;
    int avx;
    int avx2;
    int fma3;
    int avx512f;
    int avx512bw;
    int avx512vl;
    int avx512dq;
    int avx10;
    int avx10_version;
    int neon;  /* ARM64 — x86에서는 항상 0 */
    int os_avx;    /* OS가 AVX 상태를 보존하는가 */
    int os_avx512; /* OS가 AVX-512 상태를 보존하는가 */
} simd_info_t;

static void get_brand(char brand[49]) {
    int info[4];
    cpuid(info, 0x80000000);
    unsigned int max_ext = (unsigned int)info[0];
    memset(brand, 0, 49);
    if (max_ext >= 0x80000004) {
        cpuid(info, 0x80000002);
        memcpy(brand, info, 16);
        cpuid(info, 0x80000003);
        memcpy(brand + 16, info, 16);
        cpuid(info, 0x80000004);
        memcpy(brand + 32, info, 16);
    }
    /* trim leading spaces */
    char *p = brand;
    while (*p == ' ') p++;
    if (p != brand) memmove(brand, p, strlen(p) + 1);
}

static simd_info_t detect(void) {
    simd_info_t si;
    memset(&si, 0, sizeof(si));
    int info[4];

    get_brand(si.brand);

    /* CPUID leaf 1: basic features */
    cpuid(info, 1);
    int ecx1 = info[2];
    int edx1 = info[3];

    si.sse2 = (edx1 >> 26) & 1;
    int osxsave = (ecx1 >> 27) & 1;
    int avx_hw  = (ecx1 >> 28) & 1;
    si.fma3     = (ecx1 >> 12) & 1;

    /* OS support check via XCR0 */
    if (osxsave) {
        uint64_t xcr0 = xgetbv(0);
        si.os_avx    = ((xcr0 & 0x06) == 0x06);       /* XMM + YMM */
        si.os_avx512 = ((xcr0 & 0xE6) == 0xE6);       /* XMM + YMM + opmask + ZMM */
    }

    si.avx = avx_hw && si.os_avx;

    /* CPUID leaf 7 sub 0: extended features */
    cpuid(info, 0);
    int max_leaf = info[0];
    if (max_leaf >= 7) {
        cpuidex(info, 7, 0);
        int ebx7 = info[1];
        int edx7 = info[3];

        si.avx2     = ((ebx7 >> 5) & 1) && si.os_avx;
        si.avx512f  = ((ebx7 >> 16) & 1) && si.os_avx512;
        si.avx512bw = ((ebx7 >> 30) & 1) && si.os_avx512;
        si.avx512vl = ((ebx7 >> 31) & 1) && si.os_avx512;
        si.avx512dq = ((ebx7 >> 17) & 1) && si.os_avx512;

        /* CPUID leaf 7 sub 1: AVX10 */
        cpuidex(info, 7, 1);
        int eax7_1 = info[0];
        int avx10_flag = (eax7_1 >> 19) & 1;
        if (avx10_flag && max_leaf >= 0x24) {
            cpuidex(info, 0x24, 0);
            si.avx10 = 1;
            si.avx10_version = info[1] & 0xFF;
        }
    }

    return si;
}

static const char* recommend(const simd_info_t* si) {
    if (si->avx10)
        return "x64-avx10";
    if (si->avx512f && si->avx512bw && si->avx512vl && si->avx512dq)
        return "x64-avx512";
    if (si->avx2 && si->fma3)
        return "x64-avx2";
    if (si->avx)
        return "x64-avx";
    if (si->sse2)
        return "x64-sse2";
    return "unknown";
}

int main(void) {
    simd_info_t si = detect();

    printf("=== EqualizerAPO-XT SIMD Feature Detector ===\n\n");
    printf("  CPU:  %s\n\n", si.brand);

    printf("  Instruction Set       Hardware    OS Support    Status\n");
    printf("  -----------------------------------------------------------\n");

    #define ROW(name, hw, os_ok) \
        printf("  %-20s  %-10s  %-12s  %s\n", \
            name, \
            (hw) ? "Yes" : "No", \
            (os_ok) ? "Yes" : "N/A", \
            ((hw) && (os_ok)) ? "[OK]" : ((hw) && !(os_ok)) ? "[OS blocked]" : "")

    ROW("SSE2",     si.sse2,    1);
    ROW("AVX",      si.avx,     si.os_avx);
    ROW("AVX2",     si.avx2,    si.os_avx);
    ROW("FMA3",     si.fma3,    si.os_avx);
    ROW("AVX-512F", si.avx512f, si.os_avx512);
    ROW("AVX-512BW",si.avx512bw,si.os_avx512);
    ROW("AVX-512VL",si.avx512vl,si.os_avx512);
    ROW("AVX-512DQ",si.avx512dq,si.os_avx512);

    if (si.avx10)
        printf("  %-20s  Yes         Yes           [OK] (v%d)\n", "AVX10", si.avx10_version);
    else
        printf("  %-20s  No\n", "AVX10");

    #undef ROW

    printf("\n  -----------------------------------------------------------\n");
    printf("  Recommended build:  %s\n", recommend(&si));
    printf("  -----------------------------------------------------------\n\n");

    /* Alder Lake 특수 케이스 안내 */
    if (strstr(si.brand, "12th Gen") || strstr(si.brand, "12th gen")) {
        if (!si.avx512f) {
            printf("  [Info] 12th Gen (Alder Lake) detected.\n");
            printf("  P-core has AVX-512 hardware, but it is disabled\n");
            printf("  because E-core does not support it.\n\n");
        }
    }

    return 0;
}
