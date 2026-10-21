#include "tsf.h"

namespace fcitx {
bool Tsf::initThreadMgrEventSink() {
    CComPtr<ITfSource> source;
    if (threadMgr_->QueryInterface(IID_ITfSource, (void **)&source) != S_OK) {
        return false;
    }
    if (source->AdviseSink(IID_ITfThreadMgrEventSink,
                           (ITfThreadMgrEventSink *)this,
                           &threadMgrEventSinkCookie_) != S_OK) {
        threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
    }
    return threadMgrEventSinkCookie_ != TF_INVALID_COOKIE;
}

void Tsf::uninitThreadMgrEventSink() {
    CComPtr<ITfSource> source;
    if (threadMgrEventSinkCookie_ == TF_INVALID_COOKIE) {
        return;
    }
    if (SUCCEEDED(
            threadMgr_->QueryInterface(IID_ITfSource, (void **)&source))) {
        source->UnadviseSink(threadMgrEventSinkCookie_);
    }
    threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;
}

STDMETHODIMP Tsf::OnInitDocumentMgr(ITfDocumentMgr *pDocMgr) { return S_OK; }

STDMETHODIMP Tsf::OnUninitDocumentMgr(ITfDocumentMgr *pDocMgr) { return S_OK; }

STDMETHODIMP Tsf::OnSetFocus(ITfDocumentMgr *pDocMgrFocus,
                             ITfDocumentMgr *pDocMgrPrevFocus) {
    clearCompositionState();
    initTextEditSink(pDocMgrFocus);
    if (server_) {
        if (pDocMgrFocus) {
            server_->focusIn();
        } else {
            server_->focusOut();
        }
    }
    return S_OK;
}

STDMETHODIMP Tsf::OnPushContext(ITfContext *pContext) { return S_OK; }

STDMETHODIMP Tsf::OnPopContext(ITfContext *pContext) {
    if (textEditSinkContext_ && pContext == textEditSinkContext_) {
        clearCompositionState();
        if (server_) {
            server_->focusOut();
        }
    }
    return S_OK;
}
} // namespace fcitx
