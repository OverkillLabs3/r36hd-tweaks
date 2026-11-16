/* QEMU test: load reloc_test.c2o at several 64 KB aligned bases inside an RWX buffer, run it, compare with the host reference. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "c2obj.h"

static const uint16_t tab[8] = { 3, 1, 4, 1, 5, 9, 2, 6 };
static uint32_t helperr(uint32_t x, uint32_t *c) { (*c)++; return x * 2654435761u + tab[x & 7]; }
static uint32_t sw(uint32_t k, uint32_t a) {
    switch (k) { case 0: return a + 10; case 1: return a * 3; case 2: return a ^ 0xff; case 3: return a - 7;
    case 4: return a << 2; case 5: return a >> 1; case 6: return ~a; default: return a; }
}
uint32_t reloc_test_expected(uint32_t n)
{
    uint32_t acc = 0, counter = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v;
        switch (i % 3) { case 0: v = helperr(i, &counter) ^ 0x1234u; break; case 1: v = helperr(i + 1, &counter) >> 3; break;
        default: v = helperr(i, &counter) + helperr(i + 2, &counter); }
        acc += sw(i & 7, v);
    }
    return acc + counter;
}
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); static uint8_t obj[1 << 16]; size_t n = fread(obj, 1, sizeof obj, f); fclose(f);
    struct c2obj_info in; if (c2obj_info_get(obj, n, &in)) { puts("bad object"); return 1; }
    printf("image %u total %u relocs %u entries %u\n", in.image_size, in.total_size, in.n_relocs, in.n_entries);
    int fails = 0;
    uint8_t *buf = mmap(NULL, 4 << 20, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for (int k = 0; k < 4; k++) {
        uintptr_t base = ((uintptr_t)buf + 0xffff) & ~(uintptr_t)0xffff;
        base += (uintptr_t)k * 0x10000 * 3;                      /* different 64 KB aligned places */
        uint32_t ent[1];
        int r = c2obj_apply((void *)base, (uint32_t)base, obj, n, ent, 1);
        if (r) { printf("apply failed %d\n", r); return 1; }
        __builtin___clear_cache((char *)base, (char *)base + in.total_size);
        uint32_t (*fn)(uint32_t) = (uint32_t (*)(uint32_t))ent[0];
        for (uint32_t m = 1; m < 200; m += 37) {
            memset((void *)(base + in.image_size), 0, in.total_size - in.image_size);
            c2obj_apply((void *)base, (uint32_t)base, obj, n, ent, 1);   /* fresh state: counter = 0 */
            __builtin___clear_cache((char *)base, (char *)base + in.total_size);
            uint32_t got = fn(m), want = reloc_test_expected(m);
            if (got != want) { printf("base %lx m %u: got %08x want %08x\n", (unsigned long)base, m, got, want); fails++; }
        }
    }
    printf("%s\n", fails ? "FAILED" : "ALL PASSED");
    return fails != 0;
}
