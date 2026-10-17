/*
 * Testes de winrt_fm5.c: LocalSpaceStatics::GetForMachineAsync (slot 8) sobre um Azimuth falso
 * (as funcoes do Azimuth que ele chama viram jmps para versoes de teste).
 */
#include "../winrt_fm5.c"
#include "test.h"
#include <stdlib.h>

static wchar_t saves_dir[MAX_PATH];
static int wstr_inits, wstr_frees, op_calls;
static void *op_space;

static void fake_wstr_set(MsvcWstr *s, const wchar_t *p, size_t n)
{
    wchar_t *d = s->buf;
    if (n > 7) d = s->ptr = malloc((n + 1) * 2);
    memcpy(d, p, n * 2); d[n] = 0;
    s->size = n; s->cap = n > 7 ? n : 7;
}

static void fake_player_dir(MsvcWstr *out)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\player-1", saves_dir);
    fake_wstr_set(out, p, wcslen(p));
}
static void fake_wstr_init(MsvcWstr *s, const wchar_t *p, size_t n) { wstr_inits++; fake_wstr_set(s, p, n); }
static void fake_wstr_free(MsvcWstr *s)
{
    wstr_frees++;
    if (s->cap > 7) free(s->ptr);
    s->size = 0; s->cap = 7; s->buf[0] = 0;
}
static void *fake_op(void *space) { op_calls++; op_space = space; return (void *)0x1234; }

static void jmp_to(uint8_t *at, const void *fn) { memcpy(at, "\xff\x25\0\0\0\0", 6); memcpy(at + 6, &fn, 8); }

static uint8_t *fake_storage_azimuth(void)
{
    uint8_t *az = fake_azimuth(0);
    void *p;
    jmp_to(az + RVA_PLAYER_DIR, (void *)fake_player_dir);
    jmp_to(az + RVA_WSTR_INIT, (void *)fake_wstr_init);
    jmp_to(az + RVA_WSTR_FREE, (void *)fake_wstr_free);
    jmp_to(az + RVA_MALLOC, (void *)malloc);
    jmp_to(az + RVA_LOCAL_SPACE_OP, (void *)fake_op);
    p = az + 0x189bd0; memcpy(az + 0x2b6628, &p, 8);            /* slot 8 = stub fatal */
    p = az + 0x1899a0; memcpy(az + 0x2b6618, &p, 8);            /* slot 6 = GetForUserAsync */
    memcpy(az + 0x2b62c0, L"Windows.Xbox.Storage.ConnectedStorageSpace", 86);
    return az;
}

/* slot 8: so instala se slot 6/8 e o nome da classe conferem */
TEST(machine_storage_install_checks_slots)
{
    uint8_t *az = fake_azimuth(0);
    log_capture();
    CHECK(!install_machine_storage());
    CHECK(log_has("LocalSpaceStatics 0x2b6628: nao confere"));
    CHECK(*(void **)(az + 0x2b6628) == NULL);
    az = fake_storage_azimuth();
    az[0x2b62c0] = 'w';                                          /* nome da classe diferente */
    CHECK(!install_machine_storage());
    CHECK(*(void **)(az + 0x2b6628) == az + 0x189bd0);
    az[0x2b62c0] = 'W';
    CHECK(install_machine_storage());
    CHECK(*(void **)(az + 0x2b6628) == (void *)local_machine_async);
}

/* GetForMachineAsync: LocalSpace do Azimuth em saves\machine (irma de player-1), criado uma vez */
TEST(machine_storage_local_space)
{
    uint8_t *az = fake_storage_azimuth();
    void *out = (void *)1;
    wchar_t machine[MAX_PATH];
    const wchar_t *path;
    swprintf(saves_dir, MAX_PATH, L"%hs\\winmm_proxy\\build\\test_saves", t_root());
    swprintf(machine, MAX_PATH, L"%ls\\machine", saves_dir);
    CreateDirectoryW(saves_dir, NULL);
    RemoveDirectoryW(machine);
    machine_space = NULL;
    wstr_inits = wstr_frees = op_calls = 0;
    log_capture();
    CHECK_EQ(local_machine_async(NULL, NULL), E_POINTER);
    CHECK_EQ(local_machine_async(NULL, &out), S_OK);
    CHECK(out == (void *)0x1234);
    REQUIRE(machine_space && op_space == machine_space);
    CHECK(machine_space->vtbl == az + 0x2b6830);
    CHECK_EQ(machine_space->refs, 1);
    CHECK(machine_space->cls == (const wchar_t *)(az + 0x2b62c0));
    path = machine_space->path.cap > 7 ? machine_space->path.ptr : machine_space->path.buf;
    CHECK(!wcscmp(path, machine));
    CHECK_EQ(machine_space->path.size, wcslen(machine));
    CHECK(GetFileAttributesW(machine) & FILE_ATTRIBUTE_DIRECTORY);
    CHECK(GetFileAttributesW(machine) != INVALID_FILE_ATTRIBUTES);
    CHECK_EQ(wstr_frees, 1);                                     /* wstring do player-1 liberado */
    /* 2a chamada: mesmo objeto, nova operacao */
    out = NULL;
    CHECK_EQ(local_machine_async(NULL, &out), S_OK);
    CHECK(out == (void *)0x1234 && op_space == machine_space);
    CHECK_EQ(op_calls, 2);
    CHECK_EQ(wstr_inits, 1);
    CHECK(log_has("GetForMachineAsync -> LocalSpace em"));
    RemoveDirectoryW(machine);
    machine_space = NULL;
}
