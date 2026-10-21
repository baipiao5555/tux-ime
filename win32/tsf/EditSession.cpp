#include "tsf.h"

namespace fcitx {
namespace {

HRESULT startComposition(ITfContext *context, TfEditCookie ec,
                         ITfCompositionSink *sink,
                         ITfComposition **composition, ITfRange **range) {
    CComPtr<ITfInsertAtSelection> insertAtSelection;
    if (context->QueryInterface(IID_ITfInsertAtSelection,
                                reinterpret_cast<void **>(&insertAtSelection)) !=
        S_OK) {
        return E_FAIL;
    }

    CComPtr<ITfRange> insertionRange;
    if (insertAtSelection->InsertTextAtSelection(
            ec, TF_IAS_QUERYONLY, nullptr, 0, &insertionRange) != S_OK) {
        return E_FAIL;
    }

    CComPtr<ITfContextComposition> contextComposition;
    if (context->QueryInterface(IID_ITfContextComposition,
                                reinterpret_cast<void **>(
                                    &contextComposition)) != S_OK) {
        return E_FAIL;
    }

    CComPtr<ITfComposition> newComposition;
    if (contextComposition->StartComposition(ec, insertionRange, sink,
                                             &newComposition) != S_OK ||
        !newComposition) {
        return E_FAIL;
    }

    *composition = newComposition.Detach();
    *range = insertionRange.Detach();
    return S_OK;
}

HRESULT insertText(ITfContext *context, TfEditCookie ec,
                   const std::wstring &text) {
    CComPtr<ITfInsertAtSelection> insertAtSelection;
    if (context->QueryInterface(IID_ITfInsertAtSelection,
                                reinterpret_cast<void **>(&insertAtSelection)) !=
        S_OK) {
        return E_FAIL;
    }
    CComPtr<ITfRange> range;
    return insertAtSelection->InsertTextAtSelection(
        ec, 0, text.c_str(), static_cast<ULONG>(text.size()), &range);
}

} // namespace

void Tsf::clearCompositionState() {
    composition_ = nullptr;
    compositionRange_ = nullptr;
    preedit_.clear();
    pendingCommit_.clear();
    candidates_.clear();
    selectedCandidate_ = 0;
    pendingCancel_ = false;
    if (candidateWindow_) {
        candidateWindow_->hide();
    }
}

STDMETHODIMP Tsf::DoEditSession(TfEditCookie ec) {
    if (!textEditSinkContext_) {
        return E_FAIL;
    }

    if (!pendingCommit_.empty()) {
        if (composition_ && compositionRange_) {
            endingComposition_ = true;
            compositionRange_->SetText(
                ec, 0, pendingCommit_.c_str(),
                static_cast<ULONG>(pendingCommit_.size()));
            composition_->EndComposition(ec);
            endingComposition_ = false;
            composition_ = nullptr;
            compositionRange_ = nullptr;
        } else {
            insertText(textEditSinkContext_, ec, pendingCommit_);
        }
        pendingCommit_.clear();
    }

    if (pendingCancel_ || preedit_.empty()) {
        if (composition_ && compositionRange_) {
            endingComposition_ = true;
            compositionRange_->SetText(ec, 0, L"", 0);
            composition_->EndComposition(ec);
            endingComposition_ = false;
        }
        composition_ = nullptr;
        compositionRange_ = nullptr;
        if (pendingCancel_) {
            preedit_.clear();
            candidates_.clear();
            selectedCandidate_ = 0;
            pendingCancel_ = false;
            if (candidateWindow_) {
                candidateWindow_->hide();
            }
        }
        return S_OK;
    }

    if (!composition_) {
        CComPtr<ITfComposition> composition;
        CComPtr<ITfRange> range;
        if (startComposition(textEditSinkContext_, ec, this, &composition,
                             &range) != S_OK) {
            return E_FAIL;
        }
        composition_ = composition;
        compositionRange_ = range;
    }

    if (!compositionRange_ || !composition_) {
        return E_FAIL;
    }

    return compositionRange_->SetText(
               ec, 0, preedit_.c_str(),
               static_cast<ULONG>(preedit_.size())) == S_OK
               ? S_OK
               : E_FAIL;
}
} // namespace fcitx
