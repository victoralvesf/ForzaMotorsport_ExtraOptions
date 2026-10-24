/*
 * Testes de common/winrt.c: LocalSpaceStatics::GetForMachineAsync (slot 8) sobre um Azimuth falso
 * (as funcoes do Azimuth que ele chama viram jmps para versoes de teste).
 */
#include "../common/winrt.c"
#include "../fm5/winrt.c"
#include "../fm6/winrt.c"
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
static void *user_self, *user_arg, **user_out;
static int user_calls;
static long fake_user_async(void *self, void *user, void **out)
{
    user_calls++; user_self = self; user_arg = user; user_out = out;
    if (out) *out = (void *)0x5678;
    return S_OK;
}

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
    p = az + 0x189b90; memcpy(az + 0x2b6620, &p, 8);            /* slot 7 = stub fatal */
    p = az + 0x1899a0; memcpy(az + 0x2b6618, &p, 8);            /* slot 6 = GetForUserAsync */
    memcpy(az + 0x2b62c0, L"Windows.Xbox.Storage.ConnectedStorageSpace", 86);
    return az;
}

/* slots 7/8: so instala se slots 6/7/8 e o nome da classe conferem */
TEST(machine_storage_install_checks_slots)
{
    uint8_t *az = fake_azimuth(0);
    log_capture();
    CHECK(!install_machine_storage());
    CHECK(log_has("LocalSpaceStatics 0x2b6628: does not match"));
    CHECK(*(void **)(az + 0x2b6628) == NULL);
    az = fake_storage_azimuth();
    az[0x2b62c0] = 'w';                                          /* nome da classe diferente */
    CHECK(!install_machine_storage());
    CHECK(*(void **)(az + 0x2b6628) == az + 0x189bd0);
    az[0x2b62c0] = 'W';
    *(void **)(az + 0x2b6620) = az + 0x189b91;                   /* slot 7 nao e o stub */
    CHECK(!install_machine_storage());
    CHECK(*(void **)(az + 0x2b6628) == az + 0x189bd0);
    *(void **)(az + 0x2b6620) = az + 0x189b90;
    CHECK(install_machine_storage());
    CHECK(*(void **)(az + 0x2b6628) == (void *)local_machine_async);
    CHECK(*(void **)(az + 0x2b6620) == (void *)local_user_scid_async);
    CHECK(*(void **)(az + 0x2b6618) == az + 0x1899a0);           /* slot 6 intacto */
}

/* GetForUserAsync(user, serviceConfigId): slot 6 do Azimuth com o mesmo self/user/out, sem o service ID */
TEST(user_storage_scid_forwards_to_slot6)
{
    uint8_t *az = fake_storage_azimuth();
    void *out = NULL;
    jmp_to(az + 0x1899a0, (void *)fake_user_async);
    user_calls = 0;
    log_capture();
    CHECK_EQ(local_user_scid_async((void *)0x10, (void *)0x20, (void *)0x30, &out), S_OK);
    CHECK_EQ(user_calls, 1);
    CHECK(user_self == (void *)0x10 && user_arg == (void *)0x20 && user_out == &out);
    CHECK(out == (void *)0x5678);
    CHECK(log_has("service ID ignored"));
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
    CHECK(log_has("GetForMachineAsync -> LocalSpace at"));
    RemoveDirectoryW(machine);
    machine_space = NULL;
}

/* ---- GameChat do FM6: slots renumerados p/ o descritor do FH2 do Azimuth ---- */
static uint32_t disp_slot, disp_calls;
static uint64_t fake_dispatch(uint32_t slot, uint64_t *args, void *ret, double *fargs)
{
    (void)args; (void)ret; (void)fargs;
    disp_slot = slot; disp_calls++;
    return 0x77;
}

/* objeto de tabela {vtbl, classe, descritor, "TBLEFACE"} com descritor {.., +0x10 std::string, +0x30/+0x38} */
typedef struct { uint64_t pad[2]; const char *name; uint64_t pad2; size_t len, cap; uint8_t *mb, *me; } FakeDesc;
static uint8_t fake_methods[0xa8 * 40];
static void fake_table_obj(uint64_t obj[4], FakeDesc *d, const char *name, unsigned slots)
{
    memset(d, 0, sizeof *d);
    d->name = name; d->len = strlen(name); d->cap = 31;
    d->mb = fake_methods; d->me = fake_methods + 0xa8 * (slots - 6);
    obj[0] = 0; obj[1] = 0; obj[2] = (uint64_t)d; obj[3] = 0x45434146454c4254ull;
}

static uint32_t routed(uint64_t *obj, uint32_t slot)
{
    uint64_t args[4] = { (uint64_t)obj, 0, 0, 0 };
    disp_slot = 0xffff; disp_calls = 0;
    CHECK_EQ(gamechat_call(slot, args, NULL, NULL), 0x77);
    return disp_slot;
}

TEST(gamechat_manager_slots)
{
    uint64_t obj[4], args[4], tok1 = 0, tok2 = 0;
    FakeDesc d;
    orig_table_dispatch = fake_dispatch;
    log_capture();
    fake_table_obj(obj, &d, "__IChatManagerPublicNonVirtuals", 30);
    CHECK_EQ(routed(obj, 6), 6);       /* ChatSettings */
    CHECK_EQ(routed(obj, 7), 7);
    CHECK_EQ(routed(obj, 16), 16);     /* 5o evento (remove) */
    CHECK_EQ(routed(obj, 19), 17);
    CHECK_EQ(routed(obj, 20), 18);
    CHECK_EQ(routed(obj, 25), 23);
    CHECK_EQ(routed(obj, 29), 27);
    CHECK_EQ(routed(obj, 30), 22);     /* (Object) -> IAsyncAction */
    CHECK_EQ(routed(obj, 31), 28);
    CHECK_EQ(routed(obj, 32), 29);     /* ChatPerformanceCounters (game+0xcd20f8) */
    /* 6o evento: add devolve tokens distintos, remove S_OK, nada chega ao Azimuth */
    disp_calls = 0;
    args[0] = (uint64_t)obj; args[1] = 0x1000; args[2] = (uint64_t)&tok1;
    CHECK_EQ(gamechat_call(17, args, NULL, NULL), S_OK);
    args[2] = (uint64_t)&tok2;
    CHECK_EQ(gamechat_call(17, args, NULL, NULL), S_OK);
    CHECK(tok1 && tok2 && tok1 != tok2);
    args[2] = 0;
    CHECK_EQ(gamechat_call(17, args, NULL, NULL), E_POINTER);
    args[1] = tok1;
    CHECK_EQ(gamechat_call(18, args, NULL, NULL), S_OK);
    CHECK_EQ(disp_calls, 0);
    CHECK(log_has("__IChatManagerPublicNonVirtuals in the FM6 layout"));
}

TEST(gamechat_settings_counters_user)
{
    uint64_t obj[4], args[4];
    FakeDesc d;
    uint32_t u = 0; uint8_t b = 0; double dbl = 1.0, f[3] = { 0 }; float sf = 0;
    orig_table_dispatch = fake_dispatch;
    fake_table_obj(obj, &d, "__IChatManagerSettingsPublicNonVirtuals", 30);
    CHECK_EQ(routed(obj, 29), 29);
    args[0] = (uint64_t)obj; args[1] = 1234;
    CHECK_EQ(gamechat_call(31, args, NULL, NULL), S_OK);          /* put UInt32 */
    args[1] = (uint64_t)&u;
    CHECK_EQ(gamechat_call(30, args, NULL, NULL), S_OK);
    CHECK_EQ(u, 1234);
    args[1] = 0x101;                                              /* Boolean: so o byte baixo */
    CHECK_EQ(gamechat_call(33, args, NULL, NULL), S_OK);
    args[1] = (uint64_t)&b;
    CHECK_EQ(gamechat_call(32, args, NULL, NULL), S_OK);
    CHECK_EQ(b, 1);
    args[1] = 0;
    CHECK_EQ(gamechat_call(30, args, NULL, NULL), E_POINTER);

    fake_table_obj(obj, &d, "__IChatPerformanceCountersPublicNonVirtuals", 11);
    CHECK_EQ(routed(obj, 9), 9);                                  /* game+0xcd209c */
    CHECK_EQ(routed(obj, 10), 10);
    CHECK_EQ(routed(obj, 11), 10);                                /* 6o tempo */
    args[0] = (uint64_t)obj; args[1] = (uint64_t)&dbl;
    CHECK_EQ(gamechat_call(13, args, NULL, NULL), S_OK);
    CHECK(dbl == 0.0);

    fake_table_obj(obj, &d, "__IChatUserPublicNonVirtuals", 21);
    CHECK_EQ(routed(obj, 20), 20);
    args[0] = (uint64_t)obj; args[1] = (uint64_t)&sf;
    CHECK_EQ(gamechat_call(21, args, NULL, NULL), S_OK);
    CHECK(sf == 1.0f);                                            /* padrao */
    *(float *)&f[0] = 0.25f;                                      /* put Single: xmm1 salvo em fargs[0] */
    CHECK_EQ(gamechat_call(22, args, NULL, f), S_OK);
    CHECK_EQ(gamechat_call(21, args, NULL, NULL), S_OK);
    CHECK(sf == 0.25f);
}

/* outros objetos/interfaces, descritor com outro tamanho: slot intacto */
TEST(gamechat_passthrough)
{
    uint64_t obj[4], args[4] = { 0 };
    FakeDesc d;
    orig_table_dispatch = fake_dispatch;
    log_capture();
    fake_table_obj(obj, &d, "__IChatPerformanceTimePublicNonVirtuals", 12);
    CHECK_EQ(routed(obj, 7), 7);
    fake_table_obj(obj, &d, "__IChatManagerPublicNonVirtuals", 30);
    obj[3] = 0;                                                   /* sem "TBLEFACE" */
    CHECK_EQ(routed(obj, 32), 32);
    CHECK_EQ(gamechat_call(32, args, NULL, NULL), 0x77);          /* this NULL */
    fake_table_obj(obj, &d, "__IChatUserPublicNonVirtuals", 23);  /* ja no layout novo */
    gc_logged[GC_USER] = 0;
    CHECK_EQ(routed(obj, 22), 22);
    CHECK(log_has("__IChatUserPublicNonVirtuals with 23 slots (expected 21)"));
}

/* ---- FM6: CoreApplicationContext.CurrentUser = LocalUser do Azimuth ---- */
static int orig_factory_calls, user_addrefs;
static long fake_orig_factory(const wchar_t *cls, const GUID *iid, void **out)
{
    (void)cls; (void)iid;
    orig_factory_calls++;
    *out = NULL;
    return REGDB_E_CLASSNOTREG;
}
static ULONG STDMETHODCALLTYPE fake_user_addref(void *u) { (void)u; return ++user_addrefs; }
static uint64_t fake_user_statics[8];
static void *fake_get_user_statics(void) { return fake_user_statics; }

/* singleton com o prologo real (sub rsp,0x28 ; mov ecx,0x10 ; mov rax,gs:[0x58]) seguido de add rsp,0x28 + jmp */
static uint8_t *fake_user_azimuth(void **user)
{
    uint8_t *az = fake_azimuth(2);
    void *p = (void *)fake_user_addref;
    memcpy(az + 0x41290, "\x48\x83\xec\x28\xb9\x10\x00\x00\x00\x65\x48\x8b\x04\x25\x58\x00\x00\x00"
                         "\x48\x83\xc4\x28", 22);
    jmp_to(az + 0x41290 + 22, (void *)fake_get_user_statics);
    memcpy(az + 0x29d718 + 8, &p, 8);                            /* LocalUser slot 1 = AddRef */
    *user = az + 0x29d718;
    fake_user_statics[3] = (uint64_t)user;                       /* [statics+0x18] */
    return az;
}

TEST(core_app_context_current_user)
{
    static const GUID statics_iid =
        { 0xd8d4c59f, 0xb9ac, 0x4d41, { 0x84, 0x2d, 0xa0, 0x1c, 0x59, 0xb6, 0x17, 0xe7 } };
    static const GUID other_iid = { 0x12345678, 0, 0, { 0 } };
    void *user, *out = (void *)1, **f;
    uint8_t *az = fake_user_azimuth(&user);
    orig_get_factory = fake_orig_factory;
    orig_factory_calls = user_addrefs = 0;
    log_capture();
    CHECK_EQ(hook_get_factory(L"Windows.Xbox.ApplicationModel.Core.CoreApplicationContext", &statics_iid, &out),
             S_OK);
    REQUIRE(out != NULL);
    CHECK_EQ(orig_factory_calls, 0);
    f = *(void ***)out;
    CHECK_EQ(((long (*)(void *, void **))f[6])(out, NULL), E_POINTER);
    out = NULL;
    {
        void *u = NULL, *stat = NULL;
        hook_get_factory(L"Windows.Xbox.ApplicationModel.Core.CoreApplicationContext", &statics_iid, &stat);
        CHECK_EQ(((long (*)(void *, void **))f[6])(stat, &u), S_OK);   /* slot 6 = get_CurrentUser */
        CHECK(u == &user);
        CHECK_EQ(user_addrefs, 1);
        CHECK(log_has("CurrentUser -> Azimuth LocalUser"));
        /* LocalUser com outra vtable -> NULL sem AddRef */
        user = az + 0x29d720;
        CHECK_EQ(((long (*)(void *, void **))f[6])(stat, &u), S_OK);
        CHECK(u == NULL && user_addrefs == 1);
        /* singleton com outros bytes -> nao chama */
        user = az + 0x29d718;
        az[0x41294] = 0x90;
        CHECK_EQ(((long (*)(void *, void **))f[6])(stat, &u), S_OK);
        CHECK(u == NULL && user_addrefs == 1);
        CHECK(log_has("0x41290 does not match"));
    }
    /* outra interface: recusada sem chegar ao Azimuth */
    CHECK_EQ(hook_get_factory(L"Windows.Xbox.ApplicationModel.Core.CoreApplicationContext", &other_iid, &out),
             E_NOINTERFACE);
    CHECK(out == NULL && orig_factory_calls == 0);
    /* FM5 / outra classe: segue para o Azimuth */
    fake_azimuth(1);
    CHECK_EQ(hook_get_factory(L"Windows.Xbox.ApplicationModel.Core.CoreApplicationContext", &statics_iid, &out),
             REGDB_E_CLASSNOTREG);
    fake_azimuth(2);
    CHECK_EQ(hook_get_factory(L"Windows.Xbox.System.User", &statics_iid, &out), REGDB_E_CLASSNOTREG);
    CHECK_EQ(orig_factory_calls, 2);
}
