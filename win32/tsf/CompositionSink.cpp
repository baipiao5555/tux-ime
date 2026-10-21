#include "tsf.h"

namespace fcitx {
STDMETHODIMP Tsf::OnCompositionTerminated(TfEditCookie ecWrite,
                                          ITfComposition *pComposition) {
    (void)ecWrite;
    (void)pComposition;
    composition_ = nullptr;
    compositionRange_ = nullptr;
    if (!endingComposition_) {
        clearCompositionState();
    }
    return S_OK;
}
} // namespace fcitx
