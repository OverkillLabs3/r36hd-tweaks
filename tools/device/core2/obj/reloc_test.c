/* Test job: several functions, static tables, a jump table (switch), function pointers, a global. No libc. */
#include <stdint.h>
static const uint16_t tab[8] = { 3, 1, 4, 1, 5, 9, 2, 6 };
static uint32_t counter;
static uint32_t helper(uint32_t x) { counter++; return x * 2654435761u + tab[x & 7]; }
static uint32_t op0(uint32_t a) { return helper(a) ^ 0x1234u; }
static uint32_t op1(uint32_t a) { return helper(a + 1) >> 3; }
static uint32_t op2(uint32_t a) { return helper(a) + helper(a + 2); }
typedef uint32_t (*fn_t)(uint32_t);
static const fn_t ops[3] = { op0, op1, op2 };
static uint32_t sw(uint32_t k, uint32_t a)
{
    switch (k) {
    case 0: return a + 10; case 1: return a * 3; case 2: return a ^ 0xff; case 3: return a - 7;
    case 4: return a << 2; case 5: return a >> 1; case 6: return ~a; default: return a;
    }
}
uint32_t reloc_test_entry(uint32_t n)
{
    uint32_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc += sw(i & 7, ops[i % 3](i));
    return acc + counter;
}
uint32_t reloc_test_expected(uint32_t n);   /* host reference, defined in the test driver */
