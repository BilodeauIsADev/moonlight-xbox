#include "../Common/PcPin.h"
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>

using namespace moonlight_xbox_dx::PcPin;
static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct Event { unsigned short key; bool down; char modifiers; int at; };

int main() {
    static_assert(!std::is_copy_constructible<Value>::value, "PIN buffers must never be copied");
    Check(IsRemoteInput(2147483506, L"Remote Input"), "recognize the advertised built-in");
    Check(IsRemoteInput(2147483516, L"   REMOTE INPUT "), "accept prioritized names");
    Check(IsRemoteInput(2147483606, L"Remote Input"), "accept the legacy built-in");
    Check(!IsRemoteInput(42, L"Remote Input") && !IsRemoteInput(2147483506, L"Desktop") &&
        !IsRemoteInput(2147483505, L"Remote Monitor"), "never substitute desktop, monitor or a renamed game");

    Value pin;
    Check(!pin.Assign(nullptr, 4) && !pin.Assign(L"123", 3), "reject empty and short PINs");
    Check(!pin.Assign(L"12a4", 4) && !pin.Assign(L"12\n4", 4) && !pin.Assign(L"\uFF11\uFF12\uFF13\uFF14", 4),
        "only literal digit keys are accepted");
    Check(!pin.Assign(L"1234", 128), "reject excessive lengths before reading the buffer");
    Check(pin.Assign(L"001204", 6) && pin[0] == L'0', "preserve leading zeroes");

    std::vector<Event> events;
    int now = 0;
    auto key = [&](unsigned short code, bool down, char modifiers) {
        events.push_back({code, down, modifiers, now}); return true;
    };
    auto wait = [&](int milliseconds) { now += milliseconds; return true; };
    Check(Send(pin, key, wait, [] { return false; }) == Delivery::Sent, "send exactly one PIN");
    Check(events[0].key == 0x20 && events[0].down && !events[1].down,
        "dismiss the lock-screen wallpaper with a complete space press");
    Check(events[2].at >= 1500 && events[2].key == 0x11 && events[2].down,
        "wait for Windows to reveal the field before clearing it");
    Check(events[3].key == 0x41 && events[3].modifiers == 0x02 &&
        events[5].key == 0x11 && !events[5].down && events[5].modifiers == 0 &&
        events[6].key == 0x08, "select all, release Control, and clear partial input");
    for (std::size_t i = 0; i < pin.Size(); ++i) {
        Check(events[8 + 2*i].key == pin[i] && events[8 + 2*i].down &&
            !events[9 + 2*i].down && events[8 + 2*i].modifiers == 0,
            "send each digit once with matching key-up and no modifier");
    }
    Check(events.size() == 22 && events[20].key == 0x0D && events[20].down &&
        !events[21].down && now - events.back().at >= 1000, "submit once and allow transport to drain");

    events.clear(); now = 0;
    bool cancelled = false;
    auto cancelBeforePin = [&](int milliseconds) { now += milliseconds; if (milliseconds == 1500) cancelled = true; return !cancelled; };
    Check(Send(pin, key, cancelBeforePin, [&] { return cancelled; }) == Delivery::Cancelled &&
        events.size() == 2, "cancelling during lock-screen dismissal sends no credential");

    events.clear(); now = 0; cancelled = false;
    auto cancelWithControlHeld = [&](unsigned short code, bool down, char modifiers) {
        key(code, down, modifiers);
        if (code == 0x41 && down) cancelled = true;
        return true;
    };
    Check(Send(pin, cancelWithControlHeld, wait, [&] { return cancelled; }) == Delivery::Cancelled &&
        events.back().key == 0x11 && !events.back().down, "cancellation always releases Control");

    events.clear(); now = 0; cancelled = false;
    auto failDigit = [&](unsigned short code, bool down, char modifiers) {
        key(code, down, modifiers); return !(code == L'1' && down);
    };
    Check(Send(pin, failDigit, wait, [] { return false; }) == Delivery::Failed,
        "a rejected key aborts delivery without a retry or Enter");
    for (const auto& event : events) Check(event.key != 0x0D, "failed delivery must never submit a partial PIN");

    events.clear(); now = 0; cancelled = false;
    auto cancelDuringPin = [&](unsigned short code, bool down, char modifiers) {
        key(code, down, modifiers);
        if (code == L'1' && down) cancelled = true;
        return true;
    };
    Check(Send(pin, cancelDuringPin, wait, [&] { return cancelled; }) == Delivery::Cancelled &&
        !events.back().down, "cancellation during a digit releases that key");
    for (const auto& event : events) Check(event.key != 0x0D, "cancellation must not submit partial input");

    events.clear(); now = 0;
    auto lostTransport = [&](int milliseconds) { now += milliseconds; return milliseconds != 1500; };
    Check(Send(pin, key, lostTransport, [] { return false; }) == Delivery::Failed && events.size() == 2,
        "transport loss before the PIN produces no digit or retry");

    pin.Clear();
    Check(pin.Size() == 0, "erase the PIN after use");
    for (std::size_t i = 0; i < Value::MaxLength; ++i) Check(pin[i] == 0, "overwrite the entire credential buffer");
    Check(Send(pin, key, wait, [] { return false; }) == Delivery::Failed, "an erased PIN cannot be resent");
    std::puts("PC PIN delivery tests passed");
}
