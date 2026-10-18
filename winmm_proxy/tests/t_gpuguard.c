/*
 * Testes de gpuguard_fm6.c: deteccao do veneno 0xdeadbeef (endereco de GPU e descritor) e bitmap de slots
 * gravados por heap de descritores.
 */
#include "../gpuguard_fm6.c"
#include "test.h"

TEST(gpuguard_va_poisoned)
{
    CHECK(gg_va_poisoned(0xdeadbeefdeadbeefull));    /* inteiro: fora de 48 bits */
    CHECK(gg_va_poisoned(0xbeefdeadbeefull));        /* truncado a 48 bits (DRED: pagina 0xbeefdeadb000) */
    CHECK(gg_va_poisoned(0xbeefdeaeb000ull));        /* + indice * stride */
    CHECK(!gg_va_poisoned(0x80d0d2800ull));          /* placement buffer real do FM6 */
    CHECK(!gg_va_poisoned(0x7ff812340000ull));
    CHECK(!gg_va_poisoned(0xbeef00000000ull));
    CHECK(!gg_va_poisoned(0));
}

TEST(gpuguard_desc_poisoned)
{
    uint32_t d[8] = { 0x1000, 0x20000, 0xdeadbeef, 0, 0xdeadbeef, 0xdeadbeef, 0, 0 };
    CHECK(gg_desc_poisoned(d, 8));
    CHECK(!gg_desc_poisoned(d, 5));     /* um 0xdeadbeef sozinho pode ser dado legitimo */
    d[5] = 0;
    CHECK(!gg_desc_poisoned(d, 8));
}

TEST(gpuguard_heap_slots)
{
    static uint8_t mem[64 * 32], mem2[16 * 32];
    uintptr_t a = (uintptr_t)mem;
    gg_heap_reset();
    CHECK_EQ(gg_written(a), (unsigned long long)-1);   /* fora de heap conhecido: sem opiniao */
    gg_heap_add(a, 64, 32);
    CHECK_EQ(gg_written(a + 5 * 32), 0);
    gg_mark(a + 4 * 32, 3);                             /* slots 4..6 */
    CHECK_EQ(gg_written(a + 3 * 32), 0);
    CHECK_EQ(gg_written(a + 4 * 32), 1);
    CHECK_EQ(gg_written(a + 6 * 32), 1);
    CHECK_EQ(gg_written(a + 7 * 32), 0);
    gg_mark(a + 62 * 32, 5);                            /* passa do fim: so 62..63 */
    CHECK_EQ(gg_written(a + 63 * 32), 1);
    CHECK_EQ(gg_written(a + 64 * 32), (unsigned long long)-1);
    gg_heap_add((uintptr_t)mem2, 16, 32);
    CHECK_EQ(gg_written(a + 4 * 32), 1);                /* outro heap nao mexe neste */
    gg_heap_add(a + 32 * 32, 8, 32);                    /* sobrepoe: memoria reaproveitada -> heap antigo sai */
    CHECK_EQ(gg_written(a + 4 * 32), (unsigned long long)-1);
    CHECK_EQ(gg_written(a + 32 * 32), 0);
    gg_heap_reset();
}
