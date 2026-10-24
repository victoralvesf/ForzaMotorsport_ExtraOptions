/*
 * Ativacao WinRT no Azimuth (FM5 e FM6): fabrica stub, hooks de GetActivationFactoryByPCWSTR, GetTemplateByName e
 * do despacho dos objetos de tabela (GameChat), cada um repassado ao jogo carregado (fm5/winrt.c, fm6/winrt.c), e o
 * armazenamento local da maquina (stubs fatais do Azimuth, o FH2 nao usa).
 *
 * 0x14009a680 = HLE de vccorlib110!GetActivationFactoryByPCWSTR(PCWSTR classe, Guid& iid, void** out):
 * percorre 7 provedores; se todos devolvem REGDB_E_CLASSNOTREG -> STOPPED "activation <classe> iid=...".
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../proxy.h"
#include "winrt.h"

#define RVA_GET_FACTORY  0x9a680   /* GetActivationFactoryByPCWSTR HLE */
#define GET_FACTORY_STOLEN 15      /* mov [rsp+0x20],rbx ; push rbp/rsi/rdi ; sub rsp,0x120 */

typedef long (*get_factory_fn)(const wchar_t *cls, const GUID *iid, void **out);
static get_factory_fn orig_get_factory;

const GUID IID_IUnknown_ = { 0x00000000, 0x0000, 0x0000, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
const GUID IID_IActivationFactory_ = { 0x00000035, 0x0000, 0x0000, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
const GUID IID_IInspectable_ = { 0xaf86e2e0, 0xb12d, 0x4c6a, { 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90 } };
const GUID IID_IAgileObject_ = { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };

/* ------------------------------------------------------------------------ */
/* Fabrica stub (estatica, sem contagem de referencias)                       */
/* ------------------------------------------------------------------------ */
long STDMETHODCALLTYPE sf_QueryInterface(StubFactory *self, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) ||
        IsEqualGUID(iid, &IID_IActivationFactory_) || IsEqualGUID(iid, &IID_IAgileObject_)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    wlog("winrt: stub factory %ls: QI %08lx-... refused", self->cls, iid->Data1);
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE sf_AddRef(StubFactory *self) { (void)self; return 2; }
ULONG STDMETHODCALLTYPE sf_Release(StubFactory *self) { (void)self; return 1; }
long STDMETHODCALLTYPE sf_GetIids(StubFactory *self, ULONG *n, GUID **iids)
{
    (void)self;
    if (n) *n = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
long STDMETHODCALLTYPE sf_GetRuntimeClassName(StubFactory *self, void **name)
{
    (void)self;
    if (name) *name = NULL;
    return E_NOTIMPL;
}
long STDMETHODCALLTYPE sf_GetTrustLevel(StubFactory *self, int *level)
{
    (void)self;
    if (level) *level = 0;  /* BaseTrust */
    return S_OK;
}
static long STDMETHODCALLTYPE sf_ActivateInstance(StubFactory *self, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    wlog("winrt: %ls ActivateInstance -> NULL (not supported)", self->cls);
    return S_OK;
}

void *const sf_vtbl[] = {
    (void *)sf_QueryInterface, (void *)sf_AddRef, (void *)sf_Release,
    (void *)sf_GetIids, (void *)sf_GetRuntimeClassName, (void *)sf_GetTrustLevel,
    (void *)sf_ActivateInstance,
};

static long hook_get_factory(const wchar_t *cls, const GUID *iid, void **out)
{
    const game *g = game_current();
    if (cls && out && g && g->winrt_factory) {
        int handled = 0;
        long hr = g->winrt_factory(cls, iid, out, &handled);
        if (handled) return hr;
    }
    return orig_get_factory(cls, iid, out);
}

/* ------------------------------------------------------------------------ */
/* SecureDeviceAssociationTemplate.GetTemplateByName                          */
/* ------------------------------------------------------------------------ */
/*
 * 0x140208790 = statics slot 6 GetTemplateByName(self, HSTRING nome, out): compara o nome com o UNICO template
 * guardado da validacao do manifesto (std::wstring global 0x14031aaa8; no FM5 = "PeerTraffic", o 1o, por causa do
 * patch jne->jb em 0x85c8b) e da STOPPED "secure template name=" se diferir. O objeto devolvido
 * (LocalSecureTemplate, vtable 0x1402bd378, 22 slots, 0x18 bytes {vtbl, ref, classe}) e "offline": so Name
 * (slot 0xc, le o nome global) e Initiator/AcceptorSocketDescription (0xd/0xe, genericos) funcionam; o resto
 * (associacoes, certificados, AllowedUsages...) da STOP "offline secure ...". QI/Release nao olham a vtable.
 * O FM5 (manifesto com 5 templates) pede "SecurePeerTraffic" em game+0x4d1170.
 * => para os 5 nomes do manifesto do FM5: chamar o original com o nome global e trocar a vtable do objeto por
 *    uma copia por template, com Name devolvendo o nome pedido.
 */
#define RVA_TEMPLATE_BY_NAME 0x208790
#define TEMPLATE_STOLEN      20        /* 13 bytes + mov rax,[rip+cookie] (re-codificado no trampolim) */
#define RVA_COOKIE           0x318240
#define RVA_GLOBAL_TEMPLATE  0x31aaa8  /* std::wstring {buf/ptr, +0x10 size, +0x18 capacity} */
#define RVA_HSTRING_ALLOC    0xb2e10   /* HSTRING local do Azimuth com len chars; texto em [h+0x10] */
#define TEMPLATE_SLOTS       22
#define TEMPLATE_NAME_SLOT   0xc

typedef long (*template_by_name_fn)(void *self, const LocalHString *name, void **out);
typedef uint8_t *(*hstring_alloc_fn)(uint32_t len);
static template_by_name_fn orig_template_by_name;

long winrt_template_orig(void *self, const LocalHString *name, void **out)
{
    return orig_template_by_name(self, name, out);
}

static long hook_template_by_name(void *self, const LocalHString *name, void **out)
{
    const game *g = game_current();
    if (name && out && name->magic == HSTRING_MAGIC && g && g->winrt_template) {
        int handled = 0;
        long hr = g->winrt_template(self, name, out, &handled);
        if (handled) return hr;
    }
    return orig_template_by_name(self, name, out);
}

static int install_template_hook(void)
{
    uint8_t *at = g_base + RVA_TEMPLATE_BY_NAME, *tramp, *p;
    const void *cookie = g_base + RVA_COOKIE, *back = at + TEMPLATE_STOLEN;
    /* mov [rsp+8],rbx ; push rdi ; sub rsp,0x90 ; mov rax,[rip+0x10fa9c] (cookie) */
    if (!bytes_match(at, "48895c240857" "4881ec90000000" "488b059cfa1000", TEMPLATE_STOLEN)) {
        wlog("hook %#x: bytes do not match", RVA_TEMPLATE_BY_NAME);
        return 0;
    }
    tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return 0;
    p = tramp;
    memcpy(p, at, 13); p += 13;
    *p++ = 0x48; *p++ = 0xa1; memcpy(p, &cookie, 8); p += 8;           /* mov rax,[moffs64] */
    *p++ = 0xff; *p++ = 0x25; memset(p, 0, 4); p += 4; memcpy(p, &back, 8);  /* jmp [rip+0] */
    orig_template_by_name = (template_by_name_fn)tramp;
    abs_jmp(at, (const void *)hook_template_by_name, TEMPLATE_STOLEN);
    wlog("hook %#x installed", RVA_TEMPLATE_BY_NAME);
    return 1;
}

/* ------------------------------------------------------------------------ */
/* ConnectedStorageSpace.GetForMachineAsync() (FM6: game+0x22a045, na init)   */
/* LocalSpaceStatics do Azimuth (vtable 0x1402b65e8): slot 6 = GetForUserAsync(user) -> LocalSpace    */
/* unico em saves\player-1; slots 7-9 = stubs fatais (o FH2 nao usa); slot 8 (so o out) = "local     */
/* machine storage" -> STOPPED. Aqui: o mesmo LocalSpace do Azimuth (vtable/layout que o slot 6       */
/* monta), unico, em saves\machine, e a operacao assincrona do proprio Azimuth.                        */
/* Slot 7 = GetForUserAsync(user, serviceConfigId) (FM6: game+0x54e0fc) -> o slot 6 com o mesmo user  */
/* (o service ID e ignorado: o mesmo LocalSpace de saves\player-1).                                   */
/* ------------------------------------------------------------------------ */
#define RVA_STATICS_SLOT6   0x2b6618   /* GetForUserAsync = 0x1899a0 (conferido) */
#define RVA_USER_ASYNC      0x1899a0
#define RVA_STATICS_SCID    0x2b6620   /* slot 7 */
#define RVA_SCID_STUB       0x189b90
#define RVA_STATICS_MACHINE 0x2b6628   /* slot 8 */
#define RVA_MACHINE_STUB    0x189bd0
#define RVA_PLAYER_DIR      0x187040   /* (wstring *out) -> ...\saves\player-1, cria as pastas */
#define RVA_WSTR_INIT       0x008310   /* (wstring *s, ptr, len) */
#define RVA_WSTR_FREE       0x008d50
#define RVA_MALLOC          0x22f664
#define RVA_LOCAL_SPACE_VT  0x2b6830
#define RVA_LOCAL_SPACE_CLS 0x2b62c0   /* L"Windows.Xbox.Storage.ConnectedStorageSpace" */
#define RVA_LOCAL_SPACE_OP  0x18aa20   /* (space) -> IAsyncOperation (Azimuth.LocalStorageOperation) */

typedef struct { union { wchar_t buf[8]; wchar_t *ptr; }; size_t size, cap; } MsvcWstr;
typedef struct { const void *vtbl; uint32_t refs, pad; const wchar_t *cls; MsvcWstr path; } LocalSpace;

static LocalSpace *machine_space;
static SRWLOCK machine_lock = SRWLOCK_INIT;

static LocalSpace *machine_space_create(void)
{
    MsvcWstr player;
    wchar_t dir[MAX_PATH];
    const wchar_t *p;
    size_t n;
    LocalSpace *s;
    memset(&player, 0, sizeof player);
    ((void (*)(MsvcWstr *))(g_base + RVA_PLAYER_DIR))(&player);
    p = player.cap > 7 ? player.ptr : player.buf;
    for (n = player.size; n && p[n - 1] != L'\\'; n--) ;
    if (n && n + 8 <= MAX_PATH) { memcpy(dir, p, n * 2); wcscpy(dir + n, L"machine"); }
    ((void (*)(MsvcWstr *))(g_base + RVA_WSTR_FREE))(&player);
    if (!n || n + 8 > MAX_PATH) { wlog("storage: unexpected player folder"); return NULL; }
    if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        wlog("storage: CreateDirectory %ls failed (%lu)", dir, GetLastError());
        return NULL;
    }
    s = ((void *(*)(size_t))(g_base + RVA_MALLOC))(sizeof *s);
    if (!s) return NULL;
    s->vtbl = g_base + RVA_LOCAL_SPACE_VT;
    s->refs = 1;
    s->pad = 0;
    s->cls = (const wchar_t *)(g_base + RVA_LOCAL_SPACE_CLS);
    memset(&s->path, 0, sizeof s->path);
    ((void (*)(MsvcWstr *, const wchar_t *, size_t))(g_base + RVA_WSTR_INIT))(&s->path, dir, wcslen(dir));
    wlog("storage: GetForMachineAsync -> LocalSpace at %ls", dir);
    return s;
}

static long STDMETHODCALLTYPE local_machine_async(void *self, void **out)
{
    long hr = S_OK;
    (void)self;
    if (!out) return E_POINTER;
    *out = NULL;
    AcquireSRWLockExclusive(&machine_lock);
    if (!machine_space) machine_space = machine_space_create();
    if (machine_space) *out = ((void *(*)(void *))(g_base + RVA_LOCAL_SPACE_OP))(machine_space);
    else hr = E_FAIL;
    ReleaseSRWLockExclusive(&machine_lock);
    return hr;
}

static long STDMETHODCALLTYPE local_user_scid_async(void *self, void *user, void *scid, void **out)
{
    static LONG logged;
    (void)scid;
    if (!InterlockedExchange(&logged, 1))
        wlog("storage: GetForUserAsync(user, serviceConfigId) -> GetForUserAsync(user), service ID ignored");
    return ((long (*)(void *, void *, void **))(g_base + RVA_USER_ASYNC))(self, user, out);
}

static int install_machine_storage(void)
{
    void *fn = (void *)local_machine_async, *scid_fn = (void *)local_user_scid_async;
    if (*(void **)(g_base + RVA_STATICS_MACHINE) != g_base + RVA_MACHINE_STUB ||
        *(void **)(g_base + RVA_STATICS_SCID) != g_base + RVA_SCID_STUB ||
        *(void **)(g_base + RVA_STATICS_SLOT6) != g_base + RVA_USER_ASYNC ||
        !bytes_match(g_base + RVA_LOCAL_SPACE_CLS, "570069006e0064006f0077007300", 14)) {
        wlog("LocalSpaceStatics %#x: does not match", RVA_STATICS_MACHINE);
        return 0;
    }
    write_mem(g_base + RVA_STATICS_MACHINE, &fn, sizeof fn);
    write_mem(g_base + RVA_STATICS_SCID, &scid_fn, sizeof scid_fn);
    wlog("patch %#x: LocalSpaceStatics::GetForMachineAsync -> saves\\machine", RVA_STATICS_MACHINE);
    wlog("patch %#x: LocalSpaceStatics::GetForUserAsync(user, scid) -> slot 6", RVA_STATICS_SCID);
    return 1;
}

/* ------------------------------------------------------------------------ */
/* Microsoft.Xbox.GameChat do FM6 (DLL mais nova que a do FH2)                 */
/* ------------------------------------------------------------------------ */
/*
 * O Azimuth serve o GameChat com objetos de tabela: [obj+0x10] = descritor da interface ({+0x10 std::string
 * nome, +0x30/+0x38 vetor de metodos de 0xa8 bytes}), [obj+0x18] = "TBLEFACE"; a vtable e um banco de stubs
 * (slot s -> r10d = s) que cai em 0x203b60(slot, &args salvos {this, rdx, r8, r9, pilha...}, retorno,
 * &xmm1..3 salvos). O descritor segue a DLL do FH2; a do FM6 tem slots a mais (comparacao dos thunks __abi das
 * duas DLLs, tools\gc_slots.py / gc_fields.py):
 *   ChatManager (30 -> 33): 17/18 = add/remove de um 6o evento (AudioHardwareFailedHandler, so no FM6);
 *     19..29 = 17..27 do FH2; 30 = metodo novo (Object) -> IAsyncAction, mesma forma do 22 do FH2; 31/32 = 28/29.
 *   ChatManagerSettings (30 -> 34): 30/31 get/put UInt32, 32/33 get/put Boolean, novos no fim.
 *   ChatPerformanceCounters (11 -> 14): 6 tempos (ChatPerformanceTime) em vez de 5 (o 11 vira o 10) e
 *     12/13 = getters Double novos.
 *   ChatUser (21 -> 23): 21/22 get/put Single novos.
 * => no FM6: slots renumerados para o descritor do FH2; os novos respondidos aqui (valores guardados).
 */
#define RVA_TABLE_DISPATCH  0x203b60
#define TABLE_DISPATCH_STOLEN 15   /* push rbx/rbp/rsi/rdi/r12/r14/r15 ; sub rsp,0x60 */
#define TABLE_MAGIC         0x45434146454c4254ull   /* "TBLEFACE" */
#define TABLE_METHOD_SIZE   0xa8

typedef uint64_t (*table_dispatch_fn)(uint32_t slot, uint64_t *args, void *ret, double *fargs);
static table_dispatch_fn orig_table_dispatch;

uint64_t winrt_table_orig(uint32_t slot, uint64_t *args, void *ret, double *fargs)
{
    return orig_table_dispatch(slot, args, ret, fargs);
}

static uint64_t hook_table_dispatch(uint32_t slot, uint64_t *args, void *ret, double *fargs)
{
    const game *g = game_current();
    if (g && g->winrt_table_call) {
        int handled = 0;
        uint64_t r = g->winrt_table_call(slot, args, ret, fargs, &handled);
        if (handled) return r;
    }
    return orig_table_dispatch(slot, args, ret, fargs);
}

int winrt_install(void)
{
    return hook_function(RVA_GET_FACTORY, "48895c2420555657" "4881ec20010000", GET_FACTORY_STOLEN,
                         (void *)hook_get_factory, (void **)&orig_get_factory) &&
           install_template_hook() && install_machine_storage() &&
           hook_function(RVA_TABLE_DISPATCH, "40535556574154415641574883ec60", TABLE_DISPATCH_STOLEN,
                         (void *)hook_table_dispatch, (void **)&orig_table_dispatch);
}
