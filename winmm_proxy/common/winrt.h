/* common/winrt.c: fabrica stub e hooks de ativacao WinRT do Azimuth (o jogo carregado responde pelo descritor) */
#ifndef COMMON_WINRT_H
#define COMMON_WINRT_H
#include <windows.h>
#include <stdint.h>

extern const GUID IID_IUnknown_, IID_IActivationFactory_, IID_IInspectable_, IID_IAgileObject_;

/* fabrica estatica, sem contagem de referencias; sf_vtbl: ActivateInstance devolve S_OK com objeto NULL */
typedef struct { void *const *vtbl; const wchar_t *cls; } StubFactory;
extern void *const sf_vtbl[];
long STDMETHODCALLTYPE sf_QueryInterface(StubFactory *self, const GUID *iid, void **out);
ULONG STDMETHODCALLTYPE sf_AddRef(StubFactory *self);
ULONG STDMETHODCALLTYPE sf_Release(StubFactory *self);
long STDMETHODCALLTYPE sf_GetIids(StubFactory *self, ULONG *n, GUID **iids);
long STDMETHODCALLTYPE sf_GetRuntimeClassName(StubFactory *self, void **name);
long STDMETHODCALLTYPE sf_GetTrustLevel(StubFactory *self, int *level);

/* HSTRING local do Azimuth (GetTemplateByName) */
#define HSTRING_MAGIC        0x32485346u  /* "FSH2" */
typedef struct { uint32_t magic, len; uint64_t pad; const wchar_t *buf; } LocalHString;
long winrt_template_orig(void *self, const LocalHString *name, void **out);
uint64_t winrt_table_orig(uint32_t slot, uint64_t *args, void *ret, double *fargs);
#endif
