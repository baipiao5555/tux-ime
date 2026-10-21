#include "tsf.h"

namespace fcitx {
STDAPI Tsf::Activate(ITfThreadMgr *pThreadMgr, TfClientId tfClientId) {
    return ActivateEx(pThreadMgr, tfClientId, 0U);
}

STDAPI Tsf::Deactivate() {
    clearCompositionState();
    if (server_) {
        server_->focusOut();
        server_->disconnect();
        server_.reset();
    }
    if (candidateWindow_) {
        candidateWindow_->hide();
        candidateWindow_.reset();
    }
    initTextEditSink(CComPtr<ITfDocumentMgr>());
    uninitThreadMgrEventSink();
    uninitKeyEventSink();
    threadMgr_ = nullptr;
    clientId_ = TF_CLIENTID_NULL;
    return S_OK;
}

STDAPI Tsf::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId,
                       DWORD dwFlags) {
    CComPtr<ITfDocumentMgr> documentMgr;
    threadMgr_ = pThreadMgr;
    clientId_ = tfClientId;

    server_ = std::make_unique<ServerClient>();
    server_->connect();
    candidateWindow_ = std::make_unique<CandidateWindow>();
    candidateWindow_->setSelectCallback([this](int index) {
        if (!server_) {
            return;
        }
        const ServerKeyResult result = server_->selectCandidate(index);
        if (result.ok) {
            applyServerResult(result);
        }
    });

    if (!initThreadMgrEventSink()) {
        goto ActivateExError;
    }

    if ((threadMgr_->GetFocus(&documentMgr) == S_OK) &&
        (documentMgr != nullptr)) {
        initTextEditSink(documentMgr);
    }

    if (!initKeyEventSink()) {
        goto ActivateExError;
    }

    return S_OK;

ActivateExError:
    Deactivate();
    return E_FAIL;
}
} // namespace fcitx
