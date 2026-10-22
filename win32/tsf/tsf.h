#pragma once

#include "com_ptr.h"
#include <msctf.h>
#include <memory>
#include <string>
#include <vector>

#include "CandidateWindow.h"
#include "ServerClient.h"

namespace fcitx {
class Tsf : public ITfTextInputProcessorEx,
            public ITfThreadMgrEventSink,
            public ITfTextEditSink,
            public ITfKeyEventSink,
            public ITfCompositionSink,
            public ITfEditSession {
  public:
    Tsf();
    ~Tsf();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr *pThreadMgr,
                          TfClientId tfClientId) override;
    STDMETHODIMP Deactivate() override;

    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId,
                            DWORD dwFlags) override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr *pDocMgr) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr *pDocMgr) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr *pDocMgrFocus,
                            ITfDocumentMgr *pDocMgrPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext *pContext) override;
    STDMETHODIMP OnPopContext(ITfContext *pContext) override;

    // ITfTextEditSink
    STDMETHODIMP OnEndEdit(ITfContext *pic, TfEditCookie ecReadOnly,
                           ITfEditRecord *pEditRecord) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext *pContext, WPARAM wParam,
                               LPARAM lParam, BOOL *pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                           BOOL *pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                             BOOL *pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam,
                         BOOL *pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext *pContext, REFGUID rguid,
                                BOOL *pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite,
                                         ITfComposition *pComposition) override;

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec) override;

  private:
    LONG refCount_ = 1;

    // ITfThreadMgrEventSink
    bool initThreadMgrEventSink();
    void uninitThreadMgrEventSink();
    CComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD threadMgrEventSinkCookie_ = TF_INVALID_COOKIE;

    // ITfTextEditSink
    bool initTextEditSink(CComPtr<ITfDocumentMgr> documentMgr);
    DWORD textEditSinkCookie_ = TF_INVALID_COOKIE;
    CComPtr<ITfContext> textEditSinkContext_;

    // ITfKeyEventSink
    bool initKeyEventSink();
    void uninitKeyEventSink();
    bool shouldHandleKey(WPARAM wParam, LPARAM lParam) const;
    void handleKey(WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    bool requestEditSession();
    void clearCompositionState();
    void applyServerResult(const ServerKeyResult &result);
    void updateCandidateWindow(const RECT &caret);
    void updateCaretFromRange(TfEditCookie ec);
    RECT caretRect() const;
    void connectServer();

    std::unique_ptr<ServerClient> server_;
    std::unique_ptr<CandidateWindow> candidateWindow_;
    std::wstring preedit_;
    std::wstring pendingCommit_;
    std::wstring status_;
    std::vector<ServerCandidate> candidates_;
    int selectedCandidate_ = 0;
    CComPtr<ITfComposition> composition_;
    CComPtr<ITfRange> compositionRange_;
    bool pendingCancel_ = false;
    bool endingComposition_ = false;
    bool pendingCandidateUpdate_ = false;
    RECT lastCaretRect_{};
    bool hasCaretRect_ = false;
};
} // namespace fcitx
