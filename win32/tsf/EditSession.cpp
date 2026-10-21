#include "tsf.h"

namespace fcitx {
void Tsf::clearCompositionState() {
    composition_ = nullptr;
    compositionRange_ = nullptr;
    compositionText_.clear();
    pendingCommit_ = false;
    pendingCancel_ = false;
}

STDMETHODIMP Tsf::DoEditSession(TfEditCookie ec) {
    if (!textEditSinkContext_)
        return E_FAIL;

    if (pendingCancel_ || compositionText_.empty()) {
        if (composition_ && compositionRange_) {
            compositionRange_->SetText(ec, 0, L"", 0);
            composition_->EndComposition(ec);
        }
        clearCompositionState();
        return S_OK;
    }

    if (!composition_) {
        CComPtr<ITfInsertAtSelection> insertAtSelection;
        if (textEditSinkContext_->QueryInterface(
                IID_ITfInsertAtSelection,
                reinterpret_cast<void **>(&insertAtSelection)) != S_OK) {
            return E_FAIL;
        }

        CComPtr<ITfRange> range;
        if (insertAtSelection->InsertTextAtSelection(
                ec, TF_IAS_QUERYONLY, nullptr, 0, &range) != S_OK) {
            return E_FAIL;
        }

        CComPtr<ITfContextComposition> contextComposition;
        if (textEditSinkContext_->QueryInterface(
                IID_ITfContextComposition,
                reinterpret_cast<void **>(&contextComposition)) != S_OK) {
            return E_FAIL;
        }

        CComPtr<ITfComposition> composition;
        if (contextComposition->StartComposition(ec, range, this,
                                                 &composition) != S_OK ||
            !composition) {
            return E_FAIL;
        }

        composition_ = composition;
        compositionRange_ = range;
    }

    if (!compositionRange_ || !composition_)
        return E_FAIL;

    if (compositionRange_->SetText(
            ec, 0, compositionText_.c_str(),
            static_cast<ULONG>(compositionText_.size())) != S_OK) {
        return E_FAIL;
    }

    if (pendingCommit_) {
        composition_->EndComposition(ec);
        clearCompositionState();
    }

    return S_OK;
}
} // namespace fcitx
