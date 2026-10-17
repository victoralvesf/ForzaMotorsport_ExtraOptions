/*
 * Mini framework dos testes do proxy (winmm_proxy/tests, build_tests.sh).
 * Cada t_*.c inclui UM fonte do proxy (acesso as funcoes static) e registra os testes com TEST(nome).
 */
#ifndef FM5_TEST_H
#define FM5_TEST_H
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void (*test_fn)(void);
void test_register(const char *name, const char *file, test_fn fn);
void test_fail(const char *file, int line, const char *fmt, ...);
void test_skip(const char *why);
extern int t_failed;   /* falhas do teste atual */

#define TEST(name) \
    static void name(void); \
    static void __attribute__((constructor)) reg_##name(void) { test_register(#name, __FILE__, name); } \
    static void name(void)

#define CHECK(c) do { if (!(c)) test_fail(__FILE__, __LINE__, "%s", #c); } while (0)
#define CHECK_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(a), b_ = (unsigned long long)(b); \
    if (a_ != b_) test_fail(__FILE__, __LINE__, "%s == %s (%#llx != %#llx)", #a, #b, a_, b_); } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); \
    if (!a_ || strcmp(a_, b_)) test_fail(__FILE__, __LINE__, "%s == \"%s\" (\"%s\")", #a, b_, a_ ? a_ : "(null)"); } while (0)
#define CHECK_HEX(p, hex) do { if (!t_hex_eq((const uint8_t *)(p), hex)) \
    test_fail(__FILE__, __LINE__, "bytes em %s != %s (%s)", #p, hex, t_hex((const uint8_t *)(p), strlen(hex) / 2)); } while (0)
#define REQUIRE(c) do { if (!(c)) { test_fail(__FILE__, __LINE__, "%s", #c); return; } } while (0)

/* utilidades (main.c) */
int t_hex_eq(const uint8_t *p, const char *hex);
const char *t_hex(const uint8_t *p, size_t n);
const char *t_root(void);                       /* raiz do projeto (E:\Azimuth For FM5) */
void *t_rwx(size_t n);                          /* memoria executavel zerada */
void *t_call_from(uint8_t *ret, void *fn);      /* stub que chama fn com o endereco de retorno = ret (escreve em ret-16) */

/* PE mapeado como o loader faria (secoes + relocacoes), RWX; NULL se o arquivo nao existir */
uint8_t *pe_map(const char *rel_path, uint32_t *size_of_image);
void pe_unmap(uint8_t *base);
int sha256_file(const char *rel_path, char out_hex[65], uint64_t *size);

/* log do proxy (wlog) e ini (cfg_int), em t_patch.c */
void log_capture(void);
const char *log_text(void);
int log_has(const char *s);
void ini_set(const char *body);                 /* "[FM5]\nSkipIntro=1\n..." ; NULL = sem ini */
uint8_t *fake_azimuth(int fm5);                 /* g_base falso (zerado), opcionalmente com o registro do FM5 */

/* binarios usados nos testes de assinatura (pulam se nao existirem) */
#define AZIMUTH_EXE "azimuth.exe"
#define FM5_EXE     "game_binaries\\forza_durango_release_final.exe"
#endif
