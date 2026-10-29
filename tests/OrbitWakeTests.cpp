#include "../Common/OrbitWake.h"
#include <cstdio>
#include <cstdlib>

using namespace moonlight_xbox_dx::OrbitWake;

static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    Attempt ready;
    Check(ready.Observe(false, 0) == Result::Waiting, "offline hosts keep waiting");
    Check(ready.Observe(true, 1) == Result::Waiting, "one response is not readiness");
    Check(ready.Observe(true, 2) == Result::Waiting, "two responses are not readiness");
    Check(ready.Observe(true, 3) == Result::Ready, "three consecutive responses open sign-in");
    Check(ready.Observe(false, 61) == Result::Ready, "a completed attempt cannot later time out");

    Attempt flapping;
    flapping.Observe(true, 1);
    flapping.Observe(true, 2);
    Check(flapping.Observe(false, 3) == Result::Waiting, "a failed probe resets readiness");
    Check(flapping.Observe(true, 4) == Result::Waiting, "successes before a failure do not count");
    Check(flapping.Observe(true, 5) == Result::Waiting, "need three new consecutive responses");
    Check(flapping.Observe(true, 6) == Result::Ready, "recovered service becomes ready");

    Attempt timeout;
    Check(timeout.Observe(false, 59) == Result::Waiting, "keep waiting before the deadline");
    Check(timeout.Observe(true, 60) == Result::TimedOut, "real elapsed time bounds slow probes");
    Check(timeout.Observe(true, 61) == Result::TimedOut, "late results cannot reopen an expired attempt");
    Attempt lateReady;
    lateReady.Observe(true, 58);
    lateReady.Observe(true, 59);
    Check(lateReady.Observe(true, 60) == Result::TimedOut, "the final probe cannot bypass the deadline");

    Attempt cancelled;
    cancelled.Observe(true, 1);
    cancelled.Observe(true, 2);
    Check(cancelled.Observe(true, 3, true) == Result::Cancelled, "leaving the picker cancels a pending launch");
    Check(cancelled.Observe(true, 4) == Result::Cancelled, "cancelled attempts stay cancelled");
    Attempt retry;
    Check(retry.Observe(false, 0) == Result::Waiting, "a retry has its own state and deadline");

    Check(IsDesktop(L"Desktop") && IsDesktop(L" DESKTOP\t"), "desktop matching ignores case and surrounding space");
    Check(!IsDesktop(L"") && !IsDesktop(L" ") && !IsDesktop(L"Desktop Game") &&
        !IsDesktop(L"Steam") && !IsDesktop(L"Remote Desktop"), "never guess a game is the desktop");
    Check(SignInAppId(42, 0) == 42, "use the advertised desktop ID, never a hardcoded ID");
    Check(SignInAppId(42, 99) == 99, "resume a running app without closing it");
    Check(SignInAppId(-1, 99) == 99, "an existing session can sign in without a desktop entry");
    Check(SignInAppId(-1, 0) == -1, "missing desktop has no unsafe fallback");
    std::puts("Orbit wake and sign-in tests passed");
}
