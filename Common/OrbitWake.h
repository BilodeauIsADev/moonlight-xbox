#pragma once

#include <string>

namespace moonlight_xbox_dx { namespace OrbitWake {

// A responding service must stay available before we hand it a stream.
enum class Result { Waiting, Ready, TimedOut, Cancelled };
class Attempt {
public:
    Result Observe(bool connected, int elapsedSeconds, bool cancelled = false) {
        if (result != Result::Waiting) return result;
        if (cancelled) return result = Result::Cancelled;
        if (elapsedSeconds >= 60) return result = Result::TimedOut;
        consecutiveSuccesses = connected ? consecutiveSuccesses + 1 : 0;
        if (consecutiveSuccesses >= 3) result = Result::Ready;
        return result;
    }
private:
    int consecutiveSuccesses = 0;
    Result result = Result::Waiting;
};

inline bool IsDesktop(const std::wstring& name) {
    const auto first = name.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return false;
    const auto last = name.find_last_not_of(L" \t\r\n");
    auto trimmed = name.substr(first, last - first + 1);
    for (auto& c : trimmed) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return trimmed == L"desktop";
}

// GameStream resumes an existing session regardless of the requested app ID.
// Keep that session intact; it can show the Windows lock screen too.
inline int SignInAppId(int desktopId, int runningId) {
    return runningId > 0 ? runningId : desktopId;
}

} }
