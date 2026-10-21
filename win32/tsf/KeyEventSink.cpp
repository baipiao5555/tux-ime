#include "tsf.h"
#include <cwctype>

namespace fcitx {
bool Tsf::initKeyEventSink() {
    CComPtr<ITfKeystrokeMgr> keystrokeMgr;
    if (threadMgr_->QueryInterface(&keystrokeMgr) != S_OK) {
        return false;
    }
    return keystrokeMgr->AdviseKeyEventSink(clientId_, (ITfKeyEventSink *)this,
                                            TRUE) == S_OK;
}

void Tsf::uninitKeyEventSink() {
    CComPtr<ITfKeystrokeMgr> keystrokeMgr;
    if (threadMgr_->QueryInterface(&keystrokeMgr) != S_OK) {
        return;
    }
    keystrokeMgr->UnadviseKeyEventSink(clientId_);
}

bool Tsf::shouldHandleKey(WPARAM wParam, LPARAM) const {
    if (!textEditSinkContext_)
        return false;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        return false;
    if (GetKeyState(VK_MENU) & 0x8000)
        return false;

    if (wParam >= 'A' && wParam <= 'Z')
        return true;

    // Only consume navigation/editing keys while a composition is active.
    if (compositionText_.empty())
        return false;

    return wParam == VK_BACK || wParam == VK_ESCAPE ||
           wParam == VK_RETURN || wParam == VK_SPACE ||
           (wParam >= '0' && wParam <= '9');
}

bool Tsf::requestEditSession() {
    if (!textEditSinkContext_ || clientId_ == TF_CLIENTID_NULL)
        return false;

    HRESULT sessionResult = E_FAIL;
    return SUCCEEDED(textEditSinkContext_->RequestEditSession(
        clientId_, this, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
        &sessionResult));
}

void Tsf::handleKey(WPARAM wParam, LPARAM) {
    if (wParam >= 'A' && wParam <= 'Z') {
        wchar_t c = static_cast<wchar_t>(wParam);
        if (!(GetKeyState(VK_SHIFT) & 0x8000))
            c = static_cast<wchar_t>(std::towlower(c));
        compositionText_.push_back(c);
        pendingCommit_ = false;
        pendingCancel_ = false;
    } else if (wParam == VK_BACK) {
        if (!compositionText_.empty())
            compositionText_.pop_back();
        if (compositionText_.empty())
            pendingCancel_ = true;
    } else if (wParam == VK_ESCAPE) {
        pendingCancel_ = true;
    } else if (wParam == VK_RETURN) {
        pendingCommit_ = true;
    } else if (wParam == VK_SPACE) {
        compositionText_.push_back(L' ');
    } else if (wParam >= '0' && wParam <= '9') {
        compositionText_.push_back(static_cast<wchar_t>(wParam));
    }

    requestEditSession();
}

STDMETHODIMP Tsf::OnSetFocus(BOOL fForeground) { return S_OK; }

STDMETHODIMP Tsf::OnTestKeyDown(ITfContext *pContext, WPARAM wParam,
                                LPARAM lParam, BOOL *pfEaten) {
    *pfEaten = shouldHandleKey(wParam, lParam) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP Tsf::OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                            BOOL *pfEaten) {
    if (shouldHandleKey(wParam, lParam)) {
        handleKey(wParam, lParam);
        *pfEaten = TRUE;
    } else {
        *pfEaten = FALSE;
    }
    return S_OK;
}

STDMETHODIMP Tsf::OnTestKeyUp(ITfContext *pContext, WPARAM wParam,
                              LPARAM lParam, BOOL *pfEaten) {
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP Tsf::OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                          BOOL *pfEaten) {
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP Tsf::OnPreservedKey(ITfContext *pContext, REFGUID rguid,
                                 BOOL *pfEaten) {
    *pfEaten = FALSE;
    return S_OK;
}
} // namespace fcitx
