/*
 * Ativacao WinRT do FM5 (common/winrt.c): classes do runtime do console xdk-nov-2012 que o Azimuth nao tem.
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
#include "../proxy.h"
#include "../common/winrt.h"
#include "fm5.h"

/* classes do FM5 sem equivalente no Azimuth -> fabrica stub */
static StubFactory stub_factories[] = {
    { sf_vtbl, L"Windows.Xbox.Speech.Recognition.SpeechRecognizer" },
};

long fm5_winrt_factory(const wchar_t *cls, const GUID *iid, void **out, int *handled)
{
    size_t i;
    for (i = 0; i < sizeof stub_factories / sizeof stub_factories[0]; i++)
        if (!wcscmp(cls, stub_factories[i].cls)) {
            long hr = sf_QueryInterface(&stub_factories[i], iid, out);
            wlog("winrt: activation %ls iid=%08lx-... -> stub factory hr=%#lx", cls, iid->Data1, hr);
            *handled = 1;
            return hr;
        }
    return 0;
}

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
#define RVA_GLOBAL_TEMPLATE  0x31aaa8  /* std::wstring {buf/ptr, +0x10 size, +0x18 capacity} */
#define RVA_HSTRING_ALLOC    0xb2e10   /* HSTRING local do Azimuth com len chars; texto em [h+0x10] */
#define TEMPLATE_SLOTS       22
#define TEMPLATE_NAME_SLOT   0xc

typedef uint8_t *(*hstring_alloc_fn)(uint32_t len);

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

long fm5_winrt_template(void *self, const void *name_, void **out, int *handled)
{
    const LocalHString *name = name_;
    {
        size_t i;
        for (i = 0; i < N_TEMPLATES; i++) {
            const wchar_t *t = fm5_templates[i];
            if (wcslen(t) == name->len && !memcmp(t, name->buf, name->len * sizeof(wchar_t))) {
                const uint8_t *g = g_base + RVA_GLOBAL_TEMPLATE;
                LocalHString global = { HSTRING_MAGIC, (uint32_t)*(const uint64_t *)(g + 0x10), 0,
                                        *(const uint64_t *)(g + 0x18) > 7 ? *(const wchar_t *const *)g
                                                                          : (const wchar_t *)g };
                long hr = winrt_template_orig(self, &global, out);
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
                wlog("winrt: GetTemplateByName(%ls) -> offline template (base %ls) hr=%#lx",
                     t, global.buf, hr);
                *handled = 1;
                return hr;
            }
        }
    }
    return 0;
}

