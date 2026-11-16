/* c2obj: place a relocatable second-core object (.c2o, made by tools/device/core2/c2link.py) at a 64 KB aligned address.
 *   c2obj_apply(dst, base, obj, len, entries, max)  copies the image to dst (which the caller allocated, total_size bytes),
 *   applies the relocations for the second-core address `base` (a multiple of 0x10000) and returns 0 or a negative code.
 * `dst` and `base` are separate so the same code runs on the PC/QEMU (dst == base) and on the device (dst = Linux pointer,
 * base = second-core address of the same memory). */
#ifndef C2OBJ_H
#define C2OBJ_H
#include <stdint.h>
#include <string.h>

struct c2obj_info { uint32_t image_size, total_size, n_relocs, n_entries; };

static inline int c2obj_info_get(const void *obj, size_t len, struct c2obj_info *i)
{
    const uint32_t *h = (const uint32_t *)obj;
    if (len < 20 || memcmp(obj, "C2O1", 4)) return -1;
    i->image_size = h[1]; i->total_size = h[2]; i->n_relocs = h[3]; i->n_entries = h[4];
    if (20 + 4ull * (i->n_entries + i->n_relocs) + i->image_size > len) return -2;
    return 0;
}

static inline int c2obj_apply(void *dst, uint32_t base, const void *obj, size_t len, uint32_t *entries, unsigned max_entries)
{
    struct c2obj_info in;
    int r = c2obj_info_get(obj, len, &in);
    if (r) return r;
    if (base & 0xffffu) return -3;
    const uint32_t *h = (const uint32_t *)obj;
    const uint32_t *ent = h + 5, *rel = ent + in.n_entries;
    const uint8_t *img = (const uint8_t *)(rel + in.n_relocs);
    uint8_t *d = (uint8_t *)dst;
    memcpy(d, img, in.image_size);
    memset(d + in.image_size, 0, in.total_size - in.image_size);
    for (uint32_t i = 0; i < in.n_relocs; i++) {
        uint32_t off = rel[i] >> 3, kind = rel[i] & 7;
        if (off + 4 > in.image_size) return -4;
        uint32_t w;
        memcpy(&w, d + off, 4);
        if (kind == 0) w += base;
        else if (kind == 1) w = (w & 0xfc000000u) | ((w + (base >> 2)) & 0x03ffffffu);
        else if (kind == 2) w = (w & 0xffff0000u) | ((w + (base >> 16)) & 0xffffu);
        else return -5;
        memcpy(d + off, &w, 4);
    }
    for (unsigned i = 0; i < in.n_entries && i < max_entries; i++) entries[i] = base + ent[i];
    return 0;
}
#endif
