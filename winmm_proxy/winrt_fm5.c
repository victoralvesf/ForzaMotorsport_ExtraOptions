/*
 * Ativacao WinRT do FM5: classes do runtime do console xdk-nov-2012 que o Azimuth nao tem.
 *
 * 0x14009a680 = HLE de vccorlib110!GetActivationFactoryByPCWSTR(PCWSTR classe, Guid& iid, void** out):
 * percorre 7 provedores; se todos devolvem REGDB_E_CLASSNOTREG -> STOPPED "activation <classe> iid=...".
 *
 * Windows.Xbox.Speech.Recognition.SpeechRecognizer (Kinect/voz): o Azimuth so conhece o nome do XDK novo
 * (...SpeechRecognizerFactory, statics 9d99295c-...); o FM5 usa o nome antigo e QI p/ 3a3aa19c-... (FM5 RVA
 * 0x1cc0e60) -> ABI diferente, nao da para reaproveitar, e a voz nao e simulavel. Uso no FM5:
 *   0x7e9d00: fabrica = GetActivationFactoryByPCWSTR(...) (falha -> excecao)
 *             inst = fabrica->ActivateInstance (0x14cda0; falha -> excecao, inst NULL tolerado)
 *             reconhecedor = inst ? QI(inst, 3a3aa19c) : NULL
 *   0x7e7220 (unico chamador, init 0x13a0b0): reconhecedor NULL -> pula toda a configuracao de voz
 *             (grammar, ForzaHyperlinks_*.cfg); 0x7e7730 e 0x7e7160 tambem toleram NULL.
 * => fabrica stub cujo ActivateInstance devolve S_OK com objeto NULL = "sem reconhecedor de voz".
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>

extern uint8_t *g_base;
extern void wlog(const char *fmt, ...);
extern int is_fm5(void);
extern int hook_function(uint32_t rva, const char *orig_hex, size_t stolen, void *hook, void **tramp);
extern int bytes_match(const uint8_t *p, const char *hex, size_t len);
extern void abs_jmp(uint8_t *at, const void *to, size_t len);

#define RVA_GET_FACTORY  0x9a680   /* GetActivationFactoryByPCWSTR HLE */
#define GET_FACTORY_STOLEN 15      /* mov [rsp+0x20],rbx ; push rbp/rsi/rdi ; sub rsp,0x120 */

typedef long (*get_factory_fn)(const wchar_t *cls, const GUID *iid, void **out);
static get_factory_fn orig_get_factory;

static const GUID IID_IUnknown_ = { 0x00000000, 0x0000, 0x0000, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IActivationFactory_ = { 0x00000035, 0x0000, 0x0000, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_ = { 0xaf86e2e0, 0xb12d, 0x4c6a, { 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90 } };
static const GUID IID_IAgileObject_ = { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };

/* ------------------------------------------------------------------------ */
/* Fabrica stub (estatica, sem contagem de referencias)                       */
/* ------------------------------------------------------------------------ */
typedef struct { void *const *vtbl; const wchar_t *cls; } StubFactory;

static long STDMETHODCALLTYPE sf_QueryInterface(StubFactory *self, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) ||
        IsEqualGUID(iid, &IID_IActivationFactory_) || IsEqualGUID(iid, &IID_IAgileObject_)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    wlog("winrt: fabrica stub %ls: QI %08lx-... recusado", self->cls, iid->Data1);
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE sf_AddRef(StubFactory *self) { (void)self; return 2; }
static ULONG STDMETHODCALLTYPE sf_Release(StubFactory *self) { (void)self; return 1; }
static long STDMETHODCALLTYPE sf_GetIids(StubFactory *self, ULONG *n, GUID **iids)
{
    (void)self;
    if (n) *n = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static long STDMETHODCALLTYPE sf_GetRuntimeClassName(StubFactory *self, void **name)
{
    (void)self;
    if (name) *name = NULL;
    return E_NOTIMPL;
}
static long STDMETHODCALLTYPE sf_GetTrustLevel(StubFactory *self, int *level)
{
    (void)self;
    if (level) *level = 0;  /* BaseTrust */
    return S_OK;
}
static long STDMETHODCALLTYPE sf_ActivateInstance(StubFactory *self, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    wlog("winrt: %ls ActivateInstance -> NULL (sem suporte)", self->cls);
    return S_OK;
}

static void *const sf_vtbl[] = {
    (void *)sf_QueryInterface, (void *)sf_AddRef, (void *)sf_Release,
    (void *)sf_GetIids, (void *)sf_GetRuntimeClassName, (void *)sf_GetTrustLevel,
    (void *)sf_ActivateInstance,
};

/* classes do FM5 sem equivalente no Azimuth -> fabrica stub */
static StubFactory stub_factories[] = {
    { sf_vtbl, L"Windows.Xbox.Speech.Recognition.SpeechRecognizer" },
};

static long hook_get_factory(const wchar_t *cls, const GUID *iid, void **out)
{
    if (cls && out && is_fm5()) {
        size_t i;
        for (i = 0; i < sizeof stub_factories / sizeof stub_factories[0]; i++)
            if (!wcscmp(cls, stub_factories[i].cls)) {
                long hr = sf_QueryInterface(&stub_factories[i], iid, out);
                wlog("winrt: activation %ls iid=%08lx-... -> fabrica stub hr=%#lx", cls, iid->Data1, hr);
                return hr;
            }
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
#define HSTRING_MAGIC        0x32485346u  /* "FSH2" */

typedef struct { uint32_t magic, len; uint64_t pad; const wchar_t *buf; } LocalHString;
typedef long (*template_by_name_fn)(void *self, const LocalHString *name, void **out);
typedef uint8_t *(*hstring_alloc_fn)(uint32_t len);
static template_by_name_fn orig_template_by_name;

static const wchar_t *const fm5_templates[] = {
    L"PeerTraffic", L"SecurePeerTraffic", L"DebugTraffic", L"TicketTrackerTraffic", L"FmodProfilerSocket" };
#define N_TEMPLATES (sizeof fm5_templates / sizeof fm5_templates[0])
static void *tmpl_vtbls[N_TEMPLATES][TEMPLATE_SLOTS];
static void *const *tmpl_orig_vtbl;

static long tmpl_get_Name(void **self, uint8_t **out)
{
    size_t i = (size_t)((void **)*self - &tmpl_vtbls[0][0]) / TEMPLATE_SLOTS;
    const wchar_t *n = fm5_templates[i];
    uint32_t len = (uint32_t)wcslen(n);
    uint8_t *h;
    if (!out) return E_INVALIDARG;
    *out = NULL;
    h = ((hstring_alloc_fn)(g_base + RVA_HSTRING_ALLOC))(len);
    if (!h) return E_OUTOFMEMORY;
    memcpy(*(void **)(h + 0x10), n, len * sizeof(wchar_t));
    *out = h;
    return S_OK;
}

static long hook_template_by_name(void *self, const LocalHString *name, void **out)
{
    if (name && out && name->magic == HSTRING_MAGIC && is_fm5()) {
        size_t i;
        for (i = 0; i < N_TEMPLATES; i++) {
            const wchar_t *t = fm5_templates[i];
            if (wcslen(t) == name->len && !memcmp(t, name->buf, name->len * sizeof(wchar_t))) {
                const uint8_t *g = g_base + RVA_GLOBAL_TEMPLATE;
                LocalHString global = { HSTRING_MAGIC, (uint32_t)*(const uint64_t *)(g + 0x10), 0,
                                        *(const uint64_t *)(g + 0x18) > 7 ? *(const wchar_t *const *)g
                                                                          : (const wchar_t *)g };
                long hr = orig_template_by_name(self, &global, out);
                if (hr >= 0 && *out) {
                    void ***obj = (void ***)*out;
                    if (!tmpl_orig_vtbl) {
                        size_t k, s;
                        tmpl_orig_vtbl = *obj;
                        for (k = 0; k < N_TEMPLATES; k++)
                            for (s = 0; s < TEMPLATE_SLOTS; s++)
                                tmpl_vtbls[k][s] = s == TEMPLATE_NAME_SLOT ? (void *)tmpl_get_Name
                                                                            : tmpl_orig_vtbl[s];
                    }
                    if (*obj == tmpl_orig_vtbl) *obj = tmpl_vtbls[i];
                }
                wlog("winrt: GetTemplateByName(%ls) -> template offline (base %ls) hr=%#lx",
                     t, global.buf, hr);
                return hr;
            }
        }
    }
    return orig_template_by_name(self, name, out);
}

static int install_template_hook(void)
{
    uint8_t *at = g_base + RVA_TEMPLATE_BY_NAME, *tramp, *p;
    const void *cookie = g_base + RVA_COOKIE, *back = at + TEMPLATE_STOLEN;
    /* mov [rsp+8],rbx ; push rdi ; sub rsp,0x90 ; mov rax,[rip+0x10fa9c] (cookie) */
    if (!bytes_match(at, "48895c240857" "4881ec90000000" "488b059cfa1000", TEMPLATE_STOLEN)) {
        wlog("hook %#x: bytes nao conferem", RVA_TEMPLATE_BY_NAME);
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
    wlog("hook %#x instalado", RVA_TEMPLATE_BY_NAME);
    return 1;
}

/* ------------------------------------------------------------------------ */
/* ConnectedStorageSpace.GetForMachineAsync() (FM6: game+0x22a045, na init)   */
/* LocalSpaceStatics do Azimuth (vtable 0x1402b65e8): slot 6 = GetForUserAsync(user) -> LocalSpace    */
/* unico em saves\player-1; slots 7-9 = stubs fatais (o FH2 nao usa); slot 8 (so o out) = "local     */
/* machine storage" -> STOPPED. Aqui: o mesmo LocalSpace do Azimuth (vtable/layout que o slot 6       */
/* monta), unico, em saves\machine, e a operacao assincrona do proprio Azimuth.                        */
/* ------------------------------------------------------------------------ */
#define RVA_STATICS_SLOT6   0x2b6618   /* GetForUserAsync = 0x1899a0 (conferido) */
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

extern void write_mem(void *dst, const void *src, size_t len);

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
    if (!n || n + 8 > MAX_PATH) { wlog("storage: pasta do jogador inesperada"); return NULL; }
    if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        wlog("storage: CreateDirectory %ls falhou (%lu)", dir, GetLastError());
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
    wlog("storage: GetForMachineAsync -> LocalSpace em %ls", dir);
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

static int install_machine_storage(void)
{
    void *fn = (void *)local_machine_async;
    if (*(void **)(g_base + RVA_STATICS_MACHINE) != g_base + RVA_MACHINE_STUB ||
        *(void **)(g_base + RVA_STATICS_SLOT6) != g_base + 0x1899a0 ||
        !bytes_match(g_base + RVA_LOCAL_SPACE_CLS, "570069006e0064006f0077007300", 14)) {
        wlog("LocalSpaceStatics %#x: nao confere", RVA_STATICS_MACHINE);
        return 0;
    }
    write_mem(g_base + RVA_STATICS_MACHINE, &fn, sizeof fn);
    wlog("patch %#x: LocalSpaceStatics::GetForMachineAsync -> saves\\machine", RVA_STATICS_MACHINE);
    return 1;
}

int winrt_install(void)
{
    return hook_function(RVA_GET_FACTORY, "48895c2420555657" "4881ec20010000", GET_FACTORY_STOLEN,
                         (void *)hook_get_factory, (void **)&orig_get_factory) &&
           install_template_hook() && install_machine_storage();
}
