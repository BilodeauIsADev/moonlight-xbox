#include "../State/StreamSession.h"

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using moonlight_xbox_dx::StreamSession;
using namespace std::chrono_literals;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void testReconnect()
{
    // The previous global flag made the render worker exit before the queued
    // startup of the next session could clear it.
    StreamSession previous;
    previous.Terminate();
    for (int attempt = 0; attempt < 25; ++attempt) {
        StreamSession next;
        check(!next.IsTerminated(), "a new session inherited a previous disconnect");
        check(next.Start([] { return 0; }) == 0, "reconnect did not start");
        next.Stop([] {});
        check(next.IsTerminated(), "stop did not cancel the current session");
    }
    check(previous.IsTerminated(), "a new session resurrected the previous session");
}

void testCancelBeforeQueuedStartup()
{
    StreamSession session;
    bool started = false;
    session.Stop([] {});
    check(session.Start([&] { started = true; return 0; }) != 0,
          "startup succeeded after cancellation");
    check(!started && session.IsTerminated(), "queued startup overwrote cancellation");
}

void testStopDuringStartup()
{
    StreamSession session;
    std::promise<void> entered, release;
    auto enteredFuture = entered.get_future();
    auto releaseFuture = release.get_future();
    bool startupFinished = false;
    auto start = std::async(std::launch::async, [&] {
        return session.Start([&] {
            entered.set_value();
            releaseFuture.wait();
            startupFinished = true;
            return 0;
        });
    });
    const bool didEnter = enteredFuture.wait_for(2s) == std::future_status::ready;
    if (!didEnter) {
        release.set_value();
        start.get();
        throw std::runtime_error("startup worker did not run");
    }
    auto stop = std::async(std::launch::async, [&] {
        session.Stop([&] {
            check(startupFinished, "teardown raced in-progress connection startup");
        });
    });
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!session.IsTerminated() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool cancelledImmediately = session.IsTerminated();
    const bool stoppedEarly = stop.wait_for(0s) == std::future_status::ready;
    release.set_value();
    check(start.get() == 0, "in-progress startup did not finish");
    stop.get();
    check(cancelledImmediately, "stop delayed cancellation until startup finished");
    check(!stoppedEarly, "teardown returned while startup was still running");
    check(session.IsTerminated(), "startup completion erased cancellation");
}

void testFailedStartupCanStop()
{
    StreamSession session;
    try {
        session.Start([]() -> int { throw std::runtime_error("graphics device removed"); });
        throw std::logic_error("expected startup failure");
    }
    catch (const std::runtime_error&) {
    }
    bool cleaned = false;
    session.Stop([&] { cleaned = true; });
    check(cleaned && session.IsTerminated(), "startup failure prevented teardown");
}
} // namespace

int main()
{
    try {
        testReconnect();
        testCancelBeforeQueuedStartup();
        testStopDuringStartup();
        testFailedStartupCanStop();
        std::cout << "Stream session reconnect and cancellation tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
