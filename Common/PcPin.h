#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace moonlight_xbox_dx { namespace PcPin {

inline bool IsRemoteInput(int id, const std::wstring& name) {
    // Vibeshine's current, prioritized, and legacy built-in identities.
    if (id != 2147483506 && id != 2147483516 && id != 2147483606) return false;
    const auto first = name.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return false;
    auto title = name.substr(first, name.find_last_not_of(L" \t\r\n") - first + 1);
    for (auto& c : title) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return title == L"remote input";
}

// One owned buffer, never a navigation parameter or persisted setting.
class Value {
public:
    static constexpr std::size_t MaxLength = 127;
    Value() = default;
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
    ~Value() { Clear(); }

    static bool IsValid(const wchar_t* text, std::size_t count) {
        if (text == nullptr || count < 4 || count > MaxLength) return false;
        for (std::size_t i = 0; i < count; ++i)
            if (text[i] < L'0' || text[i] > L'9') return false;
        return true;
    }
    bool Assign(const wchar_t* text, std::size_t count) {
        Clear();
        if (!IsValid(text, count)) return false;
        for (std::size_t i = 0; i < count; ++i) digits[i] = text[i];
        length = count;
        return true;
    }
    void Clear() {
        volatile wchar_t* memory = digits.data();
        for (std::size_t i = 0; i < digits.size(); ++i) memory[i] = 0;
        length = 0;
    }
    std::size_t Size() const { return length; }
    wchar_t operator[](std::size_t i) const { return digits[i]; }

private:
    std::array<wchar_t, MaxLength> digits{};
    std::size_t length = 0;
};

enum class Delivery { Sent, Cancelled, Failed };

// key returns whether the event was queued. wait must observe cancellation and
// connection loss. Never repeat a submission whose outcome cannot be seen.
template<typename Key, typename Wait, typename Cancelled>
Delivery Send(const Value& pin, Key key, Wait wait, Cancelled cancelled) {
    if (pin.Size() < 4) return Delivery::Failed;
    auto failed = [&]() { return cancelled() ? Delivery::Cancelled : Delivery::Failed; };
    auto press = [&](unsigned short code, char modifiers = 0) {
        if (cancelled() || !key(code, true, modifiers)) return false;
        const bool held = wait(30);
        const bool released = key(code, false, modifiers);
        return held && released && !cancelled();
    };

    // Space dismisses the lock-screen wallpaper. Clear any incidental space or
    // previous partial PIN only after Windows has had time to show the field.
    if (!press(0x20) || !wait(1500)) return failed();
    if (cancelled() || !key(0x11, true, 0x02)) return failed();
    const bool selected = press(0x41, 0x02);
    const bool releasedControl = key(0x11, false, 0);
    if (!selected || !releasedControl || !press(0x08) || !wait(100)) return failed();
    for (std::size_t i = 0; i < pin.Size(); ++i)
        if (!press(static_cast<unsigned short>(pin[i])) || !wait(30)) return failed();
    if (!press(0x0D) || !wait(1000)) return failed();
    return Delivery::Sent;
}

} }
