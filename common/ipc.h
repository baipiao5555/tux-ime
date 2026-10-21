#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace tuxime::ipc {

constexpr uint32_t kProtocolVersion = 1;
constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\tuxime-";

enum class Type : uint32_t {
    Hello = 1,
    HelloAck,
    FocusIn,
    FocusOut,
    Cursor,
    Key,
    KeyResult,
    Reset,
    ShowSettings,
    ToggleAddon,
    SwitchInputMethod,
    SelectCandidate,
};

enum KeyFlag : uint32_t {
    KeyUp = 1U << 0,
};

enum Modifier : uint32_t {
    Shift = 1U << 0,
    Ctrl = 1U << 1,
    Alt = 1U << 2,
    Win = 1U << 3,
};

enum ResultFlag : uint32_t {
    HasPreedit = 1U << 0,
    HasCommit = 1U << 1,
    HasAux = 1U << 2,
    HasCandidates = 1U << 3,
    HasStatus = 1U << 4,
};

#pragma pack(push, 1)
struct Header {
    uint32_t size;
    uint32_t type;
    uint64_t request;
};

struct Hello {
    uint32_t pid;
    uint32_t version;
};

struct HelloAck {
    uint32_t session;
    uint32_t version;
};

struct Cursor {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

struct Key {
    uint32_t vk;
    uint32_t scan;
    uint32_t modifiers;
    uint32_t flags;
};

struct KeyResult {
    uint32_t handled;
    uint32_t flags;
    int32_t selected;
    uint32_t candidateCount;
};
#pragma pack(pop)

class Writer {
public:
    void u32(uint32_t value) {
        data_.insert(data_.end(), reinterpret_cast<const uint8_t *>(&value),
                     reinterpret_cast<const uint8_t *>(&value) + sizeof(value));
    }
    void i32(int32_t value) { u32(static_cast<uint32_t>(value)); }
    void u64(uint64_t value) {
        data_.insert(data_.end(), reinterpret_cast<const uint8_t *>(&value),
                     reinterpret_cast<const uint8_t *>(&value) + sizeof(value));
    }
    void bytes(const void *data, size_t size) {
        if (size == 0) {
            return;
        }
        const auto *p = static_cast<const uint8_t *>(data);
        data_.insert(data_.end(), p, p + size);
    }
    void str(const std::string &value) {
        u32(static_cast<uint32_t>(value.size()));
        bytes(value.data(), value.size());
    }

    std::vector<uint8_t> take() { return std::move(data_); }
    const std::vector<uint8_t> &data() const { return data_; }

private:
    std::vector<uint8_t> data_;
};

class Reader {
public:
    Reader(const uint8_t *data, size_t size) : data_(data), size_(size) {}

    uint32_t u32() {
        uint32_t value = 0;
        read(&value, sizeof(value));
        return value;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    uint64_t u64() {
        uint64_t value = 0;
        read(&value, sizeof(value));
        return value;
    }
    std::string str() {
        const uint32_t length = u32();
        if (!ok_ || length > size_ - offset_) {
            ok_ = false;
            return {};
        }
        std::string value(reinterpret_cast<const char *>(data_ + offset_),
                          length);
        offset_ += length;
        return value;
    }

    bool ok() const { return ok_; }

private:
    void read(void *out, size_t size) {
        if (!ok_ || size > size_ - offset_) {
            ok_ = false;
            return;
        }
        std::memcpy(out, data_ + offset_, size);
        offset_ += size;
    }

    const uint8_t *data_;
    size_t size_;
    size_t offset_ = 0;
    bool ok_ = true;
};

inline std::vector<uint8_t> makeMessage(Type type, uint64_t request,
                                        const std::vector<uint8_t> &payload) {
    Writer writer;
    writer.u32(static_cast<uint32_t>(sizeof(Header) + payload.size()));
    writer.u32(static_cast<uint32_t>(type));
    writer.u64(request);
    writer.bytes(payload.data(), payload.size());
    return writer.take();
}

} // namespace tuxime::ipc
