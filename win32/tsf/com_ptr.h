#pragma once

#include <cstddef>
#include <utility>

// Minimal replacement for ATL's CComPtr, used by the TSF frontend. Only the
// operations needed by this project are provided.
template <typename T>
class CComPtr {
public:
    CComPtr() = default;
    CComPtr(std::nullptr_t) {}
    CComPtr(T *pointer) : pointer_(pointer) {
        if (pointer_) {
            pointer_->AddRef();
        }
    }

    CComPtr(const CComPtr &other) : pointer_(other.pointer_) {
        if (pointer_) {
            pointer_->AddRef();
        }
    }
    CComPtr(CComPtr &&other) noexcept : pointer_(other.Detach()) {}

    ~CComPtr() {
        if (pointer_) {
            pointer_->Release();
        }
    }

    CComPtr &operator=(const CComPtr &other) {
        if (this != &other) {
            T *pointer = other.pointer_;
            if (pointer) {
                pointer->AddRef();
            }
            Release();
            pointer_ = pointer;
        }
        return *this;
    }
    CComPtr &operator=(CComPtr &&other) noexcept {
        if (this != &other) {
            Release();
            pointer_ = other.Detach();
        }
        return *this;
    }
    CComPtr &operator=(T *pointer) {
        if (pointer_ != pointer) {
            if (pointer) {
                pointer->AddRef();
            }
            Release();
            pointer_ = pointer;
        }
        return *this;
    }

    T *operator->() const { return pointer_; }
    T *get() const { return pointer_; }
    operator T *() const { return pointer_; }
    bool operator!() const { return pointer_ == nullptr; }
    explicit operator bool() const { return pointer_ != nullptr; }

    T **operator&() {
        Release();
        return &pointer_;
    }

    T *Detach() {
        T *result = pointer_;
        pointer_ = nullptr;
        return result;
    }
    void Attach(T *pointer) {
        if (pointer_) {
            pointer_->Release();
        }
        pointer_ = pointer;
    }
    void Release() {
        if (pointer_) {
            pointer_->Release();
            pointer_ = nullptr;
        }
    }

    bool operator==(std::nullptr_t) const { return pointer_ == nullptr; }
    bool operator!=(std::nullptr_t) const { return pointer_ != nullptr; }
    bool operator==(T *pointer) const { return pointer_ == pointer; }
    bool operator!=(T *pointer) const { return pointer_ != pointer; }

private:
    T *pointer_ = nullptr;
};
