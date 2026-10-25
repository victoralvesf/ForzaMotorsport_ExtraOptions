/*
 * Ativacao WinRT do FM6 (common/winrt.c): CoreApplicationContext.CurrentUser e o GameChat no layout do FM6.
 */
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../proxy.h"
#include "../common/winrt.h"
#include "fm6.h"

/* ------------------------------------------------------------------------ */
/* FM6: Windows.Xbox.ApplicationModel.Core.CoreApplicationContext.CurrentUser */
/* ------------------------------------------------------------------------ */
/*
 * Unico uso no FM6: game+0x57bb30 pega os statics (iid d8d4c59f-...), chama o slot 6 (get_CurrentUser) e devolve
 * o User (chamador game+0xc930f4, login com [this+0x3be] ligado; User NULL -> erro 0xc no fluxo de login).
 * O Azimuth nao tem a classe, mas tem o usuario local: LocalUserStatics (singleton 0x140041290, o provedor de
 * Windows.Xbox.System.User) guarda o LocalUser unico em [statics+0x18] (vtable 0x14029d718, IUser a2c2223e-...,
 * o mesmo IID que o FM6 usa).
 * => statics proprios cujo get_CurrentUser devolve esse LocalUser com AddRef.
 */
#define RVA_USER_STATICS    0x41290    /* LocalUserStatics singleton */
#define RVA_LOCAL_USER_VTBL 0x29d718
#define USER_STATICS_USER   0x18
static const wchar_t CORE_APP_CONTEXT[] = L"Windows.Xbox.ApplicationModel.Core.CoreApplicationContext";
static const GUID IID_ICoreApplicationContextStatics_ =
    { 0xd8d4c59f, 0xb9ac, 0x4d41, { 0x84, 0x2d, 0xa0, 0x1c, 0x59, 0xb6, 0x17, 0xe7 } };

typedef void *(*user_statics_fn)(void);

static long STDMETHODCALLTYPE cac_QueryInterface(StubFactory *self, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) ||
        IsEqualGUID(iid, &IID_IAgileObject_) || IsEqualGUID(iid, &IID_ICoreApplicationContextStatics_)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    wlog("winrt: CoreApplicationContext: QI %08lx-... refused", iid->Data1);
    return E_NOINTERFACE;
}

static long STDMETHODCALLTYPE cac_get_CurrentUser(StubFactory *self, void **out)
{
    /* sub rsp,0x28 ; mov ecx,0x10 ; mov rax,gs:[0x58] */
    static const char sig[] = "4883ec28" "b910000000" "65488b042558000000";
    uint8_t *statics;
    void ***user;
    (void)self;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!bytes_match(g_base + RVA_USER_STATICS, sig, 18)) {
        wlog("winrt: CoreApplicationContext.CurrentUser: %#x does not match -> NULL", RVA_USER_STATICS);
        return S_OK;
    }
    statics = ((user_statics_fn)(g_base + RVA_USER_STATICS))();
    user = statics ? *(void ****)(statics + USER_STATICS_USER) : NULL;
    if (!user || *user != (void **)(g_base + RVA_LOCAL_USER_VTBL)) {
        wlog("winrt: CoreApplicationContext.CurrentUser: no Azimuth LocalUser -> NULL");
        return S_OK;
    }
    ((ULONG (STDMETHODCALLTYPE *)(void *))(*user)[1])(user);
    *out = user;
    wlog("winrt: CoreApplicationContext.CurrentUser -> Azimuth LocalUser");
    return S_OK;
}

static void *const cac_vtbl[] = {
    (void *)cac_QueryInterface, (void *)sf_AddRef, (void *)sf_Release,
    (void *)sf_GetIids, (void *)sf_GetRuntimeClassName, (void *)sf_GetTrustLevel,
    (void *)cac_get_CurrentUser,
};
static StubFactory core_app_context = { cac_vtbl, CORE_APP_CONTEXT };

/* ------------------------------------------------------------------------ */
/* FM6: teclado virtual (Windows.Xbox.UI.KeyboardOptions + SystemUI 01f75706) */
/* ------------------------------------------------------------------------ */
/*
 * Nomear um tune/setup (game+0xcaf7d0): ref new KeyboardOptions (fabrica + ActivateInstance, o objeto e usado como
 * IKeyboardOptions sem QI), 7 setters (slot 7 = texto padrao, o nome atual do jogo em [this+0x460]; 9/11 = strings
 * de titulo/descricao; 13/17/19 = inteiros; 15 = Boolean), depois os statics de SystemUI com o iid 01f75706 (o FH2
 * so usa o 1a0dd80e), slot 6 (options) -> IAsyncOperation<String>. O jogo guarda a operacao e consulta por
 * polling (game+0xcb1f74): QI IAsyncInfo, get_Status 1 -> GetResults vira o nome; 2 = cancelado; 3 = erro.
 * O Azimuth nao tem KeyboardOptions nem essa interface -> STOPPED "activation Windows.Xbox.UI.KeyboardOptions".
 * => objetos proprios: as opcoes guardam o texto padrao; o teclado "fecha na hora" (como o Azimuth faz com as
 *    telas do sistema no FH2) aceitando o texto padrao (Completed); sem texto padrao = Canceled.
 */
static const wchar_t KEYBOARD_OPTIONS[] = L"Windows.Xbox.UI.KeyboardOptions";
static const wchar_t SYSTEM_UI[] = L"Windows.Xbox.UI.SystemUI";
static const GUID IID_ISystemUIStatics_Kbd_ =
    { 0x01f75706, 0x9df0, 0x44d8, { 0x83, 0x7c, 0xa9, 0x30, 0xda, 0x05, 0xa7, 0x4a } };
static const GUID IID_IAsyncInfo_ = { 0x00000036, 0x0000, 0x0000, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IAsyncOperation_String_ =
    { 0x3e1fe603, 0xf897, 0x5263, { 0xb3, 0x28, 0x08, 0x06, 0x42, 0x6b, 0x8a, 0x79 } };
#define RVA_HSTRING_ALLOC   0xb2e10    /* (len) -> HSTRING local do Azimuth; texto em [h+0x10] */
#define KBD_OPTION_SLOTS    20
#define KBD_DEFAULT_TEXT    7
enum { ASYNC_STARTED, ASYNC_COMPLETED, ASYNC_CANCELED, ASYNC_ERROR };
#ifndef E_ILLEGAL_METHOD_CALL
#define E_ILLEGAL_METHOD_CALL ((long)0x8000000eL)
#endif

typedef uint8_t *(*hstring_alloc_fn)(uint32_t len);

/* texto de um HSTRING do Azimuth (copia em malloc, NULL = vazio) */
static wchar_t *hstring_dup(const LocalHString *h, uint32_t *len)
{
    wchar_t *s;
    *len = 0;
    if (!h) return NULL;
    if (h->magic != HSTRING_MAGIC) { wlog("winrt: keyboard: foreign HSTRING ignored"); return NULL; }
    if (!h->len || !(s = malloc((h->len + 1) * sizeof(wchar_t)))) return NULL;
    memcpy(s, h->buf, h->len * sizeof(wchar_t));
    s[h->len] = 0;
    *len = h->len;
    return s;
}

static long hstring_new(const wchar_t *s, uint32_t len, void **out)
{
    uint8_t *h;
    *out = NULL;
    if (!len) return S_OK;
    /* mov [rsp+8],rbx ; push rdi ; sub rsp,0x20 ; mov edi,ecx */
    if (!bytes_match(g_base + RVA_HSTRING_ALLOC, "48895c2408" "57" "4883ec20" "8bf9", 12)) {
        wlog("winrt: keyboard: %#x does not match", RVA_HSTRING_ALLOC);
        return E_FAIL;
    }
    h = ((hstring_alloc_fn)(g_base + RVA_HSTRING_ALLOC))(len);
    if (!h) return E_OUTOFMEMORY;
    memcpy(*(void **)(h + 0x10), s, len * sizeof(wchar_t));
    *out = h;
    return S_OK;
}

/* --- KeyboardOptions --- */
typedef struct { void *const *vtbl; volatile LONG refs; uint32_t len; wchar_t *text; } KbdOptions;

static long STDMETHODCALLTYPE ko_QueryInterface(KbdOptions *self, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) ||
        IsEqualGUID(iid, &IID_IAgileObject_)) {
        InterlockedIncrement(&self->refs);
        *out = self;
        return S_OK;
    }
    *out = NULL;
    wlog("winrt: KeyboardOptions: QI %08lx-... refused", iid->Data1);
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ko_AddRef(KbdOptions *self) { return (ULONG)InterlockedIncrement(&self->refs); }
static ULONG STDMETHODCALLTYPE ko_Release(KbdOptions *self)
{
    LONG n = InterlockedDecrement(&self->refs);
    if (!n) { free(self->text); free(self); }
    return (ULONG)n;
}
static long STDMETHODCALLTYPE ko_get_DefaultText(KbdOptions *self, void **out)
{
    if (!out) return E_POINTER;
    return hstring_new(self->text, self->len, out);
}
static long STDMETHODCALLTYPE ko_put_DefaultText(KbdOptions *self, const LocalHString *h)
{
    uint32_t len;
    wchar_t *s = hstring_dup(h, &len);
    free(self->text);
    self->text = s;
    self->len = s ? len : 0;
    return S_OK;
}
static long STDMETHODCALLTYPE ko_get_String(KbdOptions *self, void **out)
{
    (void)self;
    if (!out) return E_POINTER;
    *out = NULL;
    return S_OK;
}
static long STDMETHODCALLTYPE ko_get_UInt32(KbdOptions *self, uint32_t *out)
{
    (void)self;
    if (!out) return E_POINTER;
    *out = 0;
    return S_OK;
}
static long STDMETHODCALLTYPE ko_get_Boolean(KbdOptions *self, uint8_t *out)
{
    (void)self;
    if (!out) return E_POINTER;
    *out = 0;
    return S_OK;
}
static long STDMETHODCALLTYPE ko_put_ignored(KbdOptions *self, uint64_t v) { (void)self; (void)v; return S_OK; }

static void *const kbd_options_vtbl[KBD_OPTION_SLOTS] = {
    (void *)ko_QueryInterface, (void *)ko_AddRef, (void *)ko_Release,
    (void *)sf_GetIids, (void *)sf_GetRuntimeClassName, (void *)sf_GetTrustLevel,
    (void *)ko_get_DefaultText, (void *)ko_put_DefaultText,      /* 6/7 */
    (void *)ko_get_String, (void *)ko_put_ignored,               /* 8/9 */
    (void *)ko_get_String, (void *)ko_put_ignored,               /* 10/11 */
    (void *)ko_get_UInt32, (void *)ko_put_ignored,               /* 12/13 */
    (void *)ko_get_Boolean, (void *)ko_put_ignored,              /* 14/15 */
    (void *)ko_get_UInt32, (void *)ko_put_ignored,               /* 16/17 */
    (void *)ko_get_UInt32, (void *)ko_put_ignored,               /* 18/19 */
};

static long STDMETHODCALLTYPE ko_ActivateInstance(StubFactory *self, void **out)
{
    KbdOptions *o;
    (void)self;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(o = calloc(1, sizeof *o))) return E_OUTOFMEMORY;
    o->vtbl = kbd_options_vtbl;
    o->refs = 1;
    *out = o;
    return S_OK;
}
static void *const ko_factory_vtbl[] = {
    (void *)sf_QueryInterface, (void *)sf_AddRef, (void *)sf_Release,
    (void *)sf_GetIids, (void *)sf_GetRuntimeClassName, (void *)sf_GetTrustLevel,
    (void *)ko_ActivateInstance,
};
static StubFactory kbd_options_factory = { ko_factory_vtbl, KEYBOARD_OPTIONS };

/* --- IAsyncOperation<String> ja concluida (op em +0, IAsyncInfo em +8) --- */
typedef struct {
    void *const *vtbl, *const *info_vtbl;
    volatile LONG refs;
    uint32_t status, len;
    wchar_t *text;
} KbdOp;
#define OP_OF_INFO(p) ((KbdOp *)((uint8_t *)(p) - offsetof(KbdOp, info_vtbl)))

static long STDMETHODCALLTYPE op_QueryInterface(KbdOp *self, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) ||
        IsEqualGUID(iid, &IID_IAgileObject_) || IsEqualGUID(iid, &IID_IAsyncOperation_String_))
        *out = self;
    else if (IsEqualGUID(iid, &IID_IAsyncInfo_))
        *out = &self->info_vtbl;
    else {
        *out = NULL;
        wlog("winrt: keyboard operation: QI %08lx-... refused", iid->Data1);
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&self->refs);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE op_AddRef(KbdOp *self) { return (ULONG)InterlockedIncrement(&self->refs); }
static ULONG STDMETHODCALLTYPE op_Release(KbdOp *self)
{
    LONG n = InterlockedDecrement(&self->refs);
    if (!n) { free(self->text); free(self); }
    return (ULONG)n;
}
/* ja concluida: o handler roda na hora (Invoke = slot 3 do delegate) */
static long STDMETHODCALLTYPE op_put_Completed(KbdOp *self, void ***handler)
{
    if (handler) ((long (STDMETHODCALLTYPE *)(void *, void *, int))(*handler)[3])(handler, self, (int)self->status);
    return S_OK;
}
static long STDMETHODCALLTYPE op_get_Completed(KbdOp *self, void **out)
{
    (void)self;
    if (!out) return E_POINTER;
    *out = NULL;
    return S_OK;
}
static long STDMETHODCALLTYPE op_GetResults(KbdOp *self, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (self->status != ASYNC_COMPLETED) return E_ILLEGAL_METHOD_CALL;
    return hstring_new(self->text, self->len, out);
}

static void *const kbd_op_vtbl[] = {
    (void *)op_QueryInterface, (void *)op_AddRef, (void *)op_Release,
    (void *)sf_GetIids, (void *)sf_GetRuntimeClassName, (void *)sf_GetTrustLevel,
    (void *)op_put_Completed, (void *)op_get_Completed, (void *)op_GetResults,
};

static long STDMETHODCALLTYPE info_QueryInterface(void *self, const GUID *iid, void **out)
{
    return op_QueryInterface(OP_OF_INFO(self), iid, out);
}
static ULONG STDMETHODCALLTYPE info_AddRef(void *self) { return op_AddRef(OP_OF_INFO(self)); }
static ULONG STDMETHODCALLTYPE info_Release(void *self) { return op_Release(OP_OF_INFO(self)); }
static long STDMETHODCALLTYPE info_get_Id(void *self, uint32_t *out)
{
    (void)self;
    if (!out) return E_POINTER;
    *out = 1;
    return S_OK;
}
static long STDMETHODCALLTYPE info_get_Status(void *self, uint32_t *out)
{
    if (!out) return E_POINTER;
    *out = OP_OF_INFO(self)->status;
    return S_OK;
}
static long STDMETHODCALLTYPE info_get_ErrorCode(void *self, long *out)
{
    (void)self;
    if (!out) return E_POINTER;
    *out = S_OK;
    return S_OK;
}
static long STDMETHODCALLTYPE info_Cancel(void *self) { (void)self; return S_OK; }
static long STDMETHODCALLTYPE info_Close(void *self) { (void)self; return S_OK; }

static void *const kbd_info_vtbl[] = {
    (void *)info_QueryInterface, (void *)info_AddRef, (void *)info_Release,
    (void *)sf_GetIids, (void *)sf_GetRuntimeClassName, (void *)sf_GetTrustLevel,
    (void *)info_get_Id, (void *)info_get_Status, (void *)info_get_ErrorCode, (void *)info_Cancel,
    (void *)info_Close,
};

/* --- SystemUI statics 01f75706 --- */
static long STDMETHODCALLTYPE kui_QueryInterface(StubFactory *self, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) ||
        IsEqualGUID(iid, &IID_IAgileObject_) || IsEqualGUID(iid, &IID_ISystemUIStatics_Kbd_)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    wlog("winrt: SystemUI (keyboard statics): QI %08lx-... refused", iid->Data1);
    return E_NOINTERFACE;
}

/* slot 6: ShowVirtualKeyboard com KeyboardOptions -> operacao concluida com o texto padrao */
static long STDMETHODCALLTYPE kui_show_keyboard(StubFactory *self, const KbdOptions *opts, void **out)
{
    KbdOp *op;
    (void)self;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(op = calloc(1, sizeof *op))) return E_OUTOFMEMORY;
    op->vtbl = kbd_op_vtbl;
    op->info_vtbl = kbd_info_vtbl;
    op->refs = 1;
    op->status = ASYNC_CANCELED;
    if (opts && opts->vtbl == kbd_options_vtbl && opts->len && (op->text = malloc(opts->len * sizeof(wchar_t)))) {
        memcpy(op->text, opts->text, opts->len * sizeof(wchar_t));
        op->len = opts->len;
        op->status = ASYNC_COMPLETED;
    }
    if (op->status == ASYNC_COMPLETED)
        wlog("winrt: SystemUI virtual keyboard -> closed at once with the default text \"%.*ls\"", (int)op->len,
             op->text);
    else
        wlog("winrt: SystemUI virtual keyboard -> closed at once (canceled, no default text)");
    *out = op;
    return S_OK;
}

static long STDMETHODCALLTYPE kui_unsupported(StubFactory *self)
{
    (void)self;
    wlog("winrt: SystemUI (keyboard statics) slot 7 not supported");
    return E_NOTIMPL;
}

static void *const kui_vtbl[] = {
    (void *)kui_QueryInterface, (void *)sf_AddRef, (void *)sf_Release,
    (void *)sf_GetIids, (void *)sf_GetRuntimeClassName, (void *)sf_GetTrustLevel,
    (void *)kui_show_keyboard, (void *)kui_unsupported,
};
static StubFactory keyboard_ui = { kui_vtbl, SYSTEM_UI };

long fm6_winrt_factory(const wchar_t *cls, const GUID *iid, void **out, int *handled)
{
    long hr;
    if (!wcscmp(cls, CORE_APP_CONTEXT))
        hr = cac_QueryInterface(&core_app_context, iid, out);
    else if (!wcscmp(cls, KEYBOARD_OPTIONS))
        hr = sf_QueryInterface(&kbd_options_factory, iid, out);
    else if (!wcscmp(cls, SYSTEM_UI) && IsEqualGUID(iid, &IID_ISystemUIStatics_Kbd_))
        hr = kui_QueryInterface(&keyboard_ui, iid, out);
    else
        return 0;
    wlog("winrt: activation %ls iid=%08lx-... -> proxy statics hr=%#lx", cls, iid->Data1, hr);
    *handled = 1;
    return hr;
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
#define TABLE_MAGIC         0x45434146454c4254ull   /* "TBLEFACE" */
#define TABLE_METHOD_SIZE   0xa8

enum { GC_MANAGER, GC_SETTINGS, GC_COUNTERS, GC_USER, GC_N };
static const struct { const char *name; uint32_t fh2_slots; } gc_ifaces[GC_N] = {
    { "__IChatManagerPublicNonVirtuals", 30 },
    { "__IChatManagerSettingsPublicNonVirtuals", 30 },
    { "__IChatPerformanceCountersPublicNonVirtuals", 11 },
    { "__IChatUserPublicNonVirtuals", 21 },
};

static volatile LONG64 gc_next_token;
static volatile LONG gc_settings_u32, gc_settings_bool, gc_user_single = 0x3f800000;  /* Single 1.0 */
static volatile LONG gc_logged[GC_N];

/* interface do GameChat (GC_*) do objeto de tabela, -1 se outro; *slots = slots do descritor */
static int gc_iface(const uint8_t *obj, uint32_t *slots)
{
    const uint8_t *desc;
    const char *name;
    size_t len;
    int i;
    if (!obj || *(const uint64_t *)(obj + 0x18) != TABLE_MAGIC) return -1;
    desc = *(const uint8_t *const *)(obj + 0x10);
    if (!desc) return -1;
    len = *(const size_t *)(desc + 0x20);
    name = *(const size_t *)(desc + 0x28) > 15 ? *(const char *const *)(desc + 0x10) : (const char *)(desc + 0x10);
    for (i = 0; i < GC_N; i++)
        if (strlen(gc_ifaces[i].name) == len && !memcmp(gc_ifaces[i].name, name, len)) {
            *slots = (uint32_t)((*(const uint64_t *)(desc + 0x38) - *(const uint64_t *)(desc + 0x30)) /
                                TABLE_METHOD_SIZE) + 6;
            return i;
        }
    return -1;
}

/* slot do FM6 -> slot do descritor do FH2; -1 = slot novo, respondido em gc_own() */
static int gc_route(int iface, uint32_t slot)
{
    switch (iface) {
    case GC_MANAGER:
        if (slot < 17) return slot;
        if (slot < 19) return -1;
        if (slot < 30) return slot - 2;
        if (slot == 30) return 22;
        return slot - 3;
    case GC_SETTINGS: return slot < 30 ? (int)slot : -1;
    case GC_COUNTERS: return slot < 11 ? (int)slot : slot == 11 ? 10 : -1;
    case GC_USER:     return slot < 21 ? (int)slot : -1;
    }
    return slot;
}

static uint64_t gc_own(int iface, uint32_t slot, const uint64_t *args, const double *fargs)
{
    void *out = (void *)args[1];
    switch (iface * 64 + slot) {
    case GC_MANAGER * 64 + 17:                                   /* add_X(handler, out token) */
        if (!args[2]) return E_POINTER;
        *(int64_t *)args[2] = InterlockedIncrement64(&gc_next_token);
        return S_OK;
    case GC_MANAGER * 64 + 18: return S_OK;                     /* remove_X(token) */
    case GC_SETTINGS * 64 + 30: if (!out) return E_POINTER; *(uint32_t *)out = (uint32_t)gc_settings_u32; return S_OK;
    case GC_SETTINGS * 64 + 31: gc_settings_u32 = (LONG)(uint32_t)args[1]; return S_OK;
    case GC_SETTINGS * 64 + 32: if (!out) return E_POINTER; *(uint8_t *)out = (uint8_t)gc_settings_bool; return S_OK;
    case GC_SETTINGS * 64 + 33: gc_settings_bool = (uint8_t)args[1]; return S_OK;
    case GC_COUNTERS * 64 + 12:
    case GC_COUNTERS * 64 + 13: if (!out) return E_POINTER; *(double *)out = 0.0; return S_OK;
    case GC_USER * 64 + 21: if (!out) return E_POINTER; *(LONG *)out = gc_user_single; return S_OK;
    case GC_USER * 64 + 22: gc_user_single = *(const LONG *)&fargs[0]; return S_OK;   /* Single em xmm1 */
    }
    return E_NOTIMPL;
}

static uint64_t gamechat_call(uint32_t slot, uint64_t *args, void *ret, double *fargs)
{
    uint32_t slots = 0;
    int iface = gc_iface((const uint8_t *)args[0], &slots), to;
    if (iface < 0) return winrt_table_orig(slot, args, ret, fargs);
    if (slots != gc_ifaces[iface].fh2_slots) {                  /* descritor inesperado: nao mexe */
        if (!InterlockedExchange(&gc_logged[iface], 1))
            wlog("gamechat: %s with %u slots (expected %u), not renumbered", gc_ifaces[iface].name, slots,
                 gc_ifaces[iface].fh2_slots);
        return winrt_table_orig(slot, args, ret, fargs);
    }
    if (!InterlockedExchange(&gc_logged[iface], 1))
        wlog("gamechat: %s in the FM6 layout (slots renumbered to the %u of FH2)", gc_ifaces[iface].name, slots);
    to = gc_route(iface, slot);
    if (to < 0) return gc_own(iface, slot, args, fargs);
    return winrt_table_orig((uint32_t)to, args, ret, fargs);
}

uint64_t fm6_winrt_table_call(uint32_t slot, uint64_t *args, void *ret, double *fargs, int *handled)
{
    *handled = 1;
    return gamechat_call(slot, args, ret, fargs);
}
