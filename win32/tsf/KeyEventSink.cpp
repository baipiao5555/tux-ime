#include "tsf.h"

namespace {

class CaretSession final : public ITfEditSession {
public:
    CaretSession(ITfContext *context, ITfComposition *composition)
        : context_(context), composition_(composition) {}

    STDMETHODIMP QueryInterface(REFIID riid, void **object) override {
        if (!object) {
            return E_INVALIDARG;
        }
        *object = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ITfEditSession)) {
            *object = static_cast<ITfEditSession *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refCount_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG count = --refCount_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        CComPtr<ITfContextView> view;
        if (FAILED(context_->GetActiveView(&view)) || !view) {
            return E_FAIL;
        }

        CComPtr<ITfRange> range;
        if (composition_) {
            if (FAILED(composition_->GetRange(&range)) || !range) {
                return E_FAIL;
            }
            range->Collapse(ec, TF_ANCHOR_START);
        } else {
            TF_SELECTION selection{};
            ULONG fetched = 0;
            if (FAILED(context_->GetSelection(ec, TF_DEFAULT_SELECTION, 1,
                                              &selection, &fetched)) ||
                fetched == 0 || !selection.range) {
                return E_FAIL;
            }
            range.Attach(selection.range);
        }

        RECT rect{};
        BOOL clipped = FALSE;
        const HRESULT result = view->GetTextExt(ec, range, &rect, &clipped);
        if (FAILED(result) || (rect.left == 0 && rect.top == 0)) {
            return E_FAIL;
        }
        if (rect.bottom <= rect.top) {
            rect.bottom = rect.top + 1;
        }
        if (rect.right < rect.left) {
            rect.right = rect.left;
        }

        HWND foreground = GetForegroundWindow();
        RECT windowRect{};
        if (foreground && GetWindowRect(foreground, &windowRect) &&
            (rect.left < windowRect.left || rect.left > windowRect.right ||
             rect.top < windowRect.top || rect.top > windowRect.bottom)) {
            POINT caret{};
            if (GetCaretPos(&caret)) {
                const int offsetX = windowRect.left - rect.left + caret.x;
                const int offsetY = windowRect.top - rect.top + caret.y;
                rect.left += offsetX;
                rect.right += offsetX;
                rect.top += offsetY;
                rect.bottom += offsetY;
            }
        }

        rect_ = rect;
        valid_ = true;
        return S_OK;
    }

    bool valid() const { return valid_; }
    const RECT &rect() const { return rect_; }

private:
    ~CaretSession() = default;
    LONG refCount_ = 1;
    CComPtr<ITfContext> context_;
    CComPtr<ITfComposition> composition_;
    RECT rect_{};
    bool valid_ = false;
};

} // namespace

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
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (ctrl && wParam == VK_SPACE) {
        return true;
    }
    if (ctrl && !alt && wParam == VK_OEM_PERIOD) {
        return true;
    }
    if (ctrl && shift && !alt && wParam == 'F') {
        return true;
    }
    if (ctrl && alt && shift && wParam == 'U') {
        return true;
    }
    if (shift && !ctrl && !alt && wParam == VK_SPACE) {
        return true;
    }
    if (ctrl || alt) {
        return false;
    }

    if (wParam >= 'A' && wParam <= 'Z') {
        return true;
    }
    if (wParam == VK_OEM_1) {
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
    if (textEditSinkContext_ && clientId_ != TF_CLIENTID_NULL) {
        CComPtr<CaretSession> session;
        session.Attach(new CaretSession(textEditSinkContext_, composition_));
        HRESULT sessionResult = E_FAIL;
        if (SUCCEEDED(textEditSinkContext_->RequestEditSession(
                clientId_, session, TF_ES_SYNC | TF_ES_READ,
                &sessionResult)) &&
            SUCCEEDED(sessionResult) && session->valid()) {
            return session->rect();
        }
    }

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
    candidateWindow_->update(candidates_, selectedCandidate_, caret,
                             status_);
}

void Tsf::applyServerResult(const ServerKeyResult &result) {
    if (!result.ok) {
        return;
    }
    if (!result.commit.empty()) {
        pendingCommit_ += result.commit;
    }
    preedit_ = result.preedit;
    status_ = result.status;
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
