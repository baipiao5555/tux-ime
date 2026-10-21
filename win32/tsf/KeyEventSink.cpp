#include "tsf.h"

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
    if (!textEditSinkContext_ || !server_ || !server_->connected()) {
        return false;
    }

    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (ctrl && wParam == VK_SPACE) {
        return true;
    }
    if (ctrl || alt) {
        return false;
    }

    if (wParam >= 'A' && wParam <= 'Z') {
        return true;
    }

    if (preedit_.empty() && candidates_.empty()) {
        return false;
    }

    switch (wParam) {
    case VK_BACK:
    case VK_ESCAPE:
    case VK_RETURN:
    case VK_SPACE:
    case VK_TAB:
    case VK_DELETE:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_OEM_COMMA:
    case VK_OEM_PERIOD:
    case VK_OEM_MINUS:
    case VK_OEM_PLUS:
        return true;
    default:
        return wParam >= '0' && wParam <= '9';
    }
}

bool Tsf::requestEditSession() {
    if (!textEditSinkContext_ || clientId_ == TF_CLIENTID_NULL) {
        return false;
    }

    HRESULT sessionResult = E_FAIL;
    return SUCCEEDED(textEditSinkContext_->RequestEditSession(
        clientId_, this, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
        &sessionResult));
}

RECT Tsf::caretRect() const {
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (GetGUIThreadInfo(GetCurrentThreadId(), &info) && info.hwndCaret) {
        POINT topLeft{info.rcCaret.left, info.rcCaret.top};
        POINT bottomRight{info.rcCaret.right, info.rcCaret.bottom};
        ClientToScreen(info.hwndCaret, &topLeft);
        ClientToScreen(info.hwndCaret, &bottomRight);
        return {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    }

    POINT point{0, 0};
    GetCaretPos(&point);
    HWND focus = GetFocus();
    if (focus) {
        ClientToScreen(focus, &point);
    }
    return {point.x, point.y, point.x + 1, point.y + 20};
}

void Tsf::updateCandidateWindow(const RECT &caret) {
    if (!candidateWindow_) {
        return;
    }
    if (candidates_.empty()) {
        candidateWindow_->hide();
        return;
    }
    candidateWindow_->update(candidates_, selectedCandidate_, caret);
}

void Tsf::applyServerResult(const ServerKeyResult &result) {
    if (!result.ok) {
        return;
    }
    if (!result.commit.empty()) {
        pendingCommit_ += result.commit;
    }
    preedit_ = result.preedit;
    candidates_ = result.candidates;
    selectedCandidate_ = result.selected;
    pendingCancel_ = false;

    const RECT caret = caretRect();
    if (server_) {
        server_->setCursor(caret);
    }
    requestEditSession();
    updateCandidateWindow(caret);
}

void Tsf::handleKey(WPARAM wParam, LPARAM lParam, BOOL *pfEaten) {
    *pfEaten = FALSE;
    if (!server_ || !server_->connect()) {
        return;
    }

    const RECT caret = caretRect();
    server_->setCursor(caret);

    const ServerKeyResult result = server_->sendKey(wParam, lParam, false);
    if (!result.ok) {
        return;
    }
    applyServerResult(result);
    *pfEaten = result.handled ? TRUE : FALSE;
}

STDMETHODIMP Tsf::OnSetFocus(BOOL fForeground) {
    if (server_) {
        if (fForeground) {
            server_->focusIn();
        } else {
            server_->focusOut();
        }
    }
    return S_OK;
}

STDMETHODIMP Tsf::OnTestKeyDown(ITfContext *pContext, WPARAM wParam,
                                LPARAM lParam, BOOL *pfEaten) {
    *pfEaten = shouldHandleKey(wParam, lParam) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP Tsf::OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                            BOOL *pfEaten) {
    if (shouldHandleKey(wParam, lParam)) {
        handleKey(wParam, lParam, pfEaten);
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
