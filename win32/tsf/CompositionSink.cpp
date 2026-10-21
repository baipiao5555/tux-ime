#include "tsf.h"

namespace fcitx {
STDMETHODIMP Tsf::OnCompositionTerminated(TfEditCookie ecWrite,
                                          ITfComposition *pComposition) {
    (void)ecWrite;
    (void)pComposition;
    clearCompositionState();
    return S_OK;
}
} // namespace fcitx
