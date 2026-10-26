#pragma once

#include <atomic>
#include <mutex>
#include <utility>

namespace moonlight_xbox_dx {

// One instance per streaming client. Cancellation must be visible before its
// queued startup runs, and teardown must wait for in-progress startup to finish.
class StreamSession {
  public:
	bool IsTerminated() const {
		return m_terminated.load(std::memory_order_acquire);
	}

	void Terminate() {
		m_terminated.store(true, std::memory_order_release);
	}

	template <typename StartFunction>
	int Start(StartFunction&& start) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (IsTerminated()) {
			return -1;
		}
		return std::forward<StartFunction>(start)();
	}

	template <typename StopFunction>
	void Stop(StopFunction&& stop) {
		Terminate();
		std::lock_guard<std::mutex> lock(m_mutex);
		std::forward<StopFunction>(stop)();
	}

  private:
	std::atomic<bool> m_terminated{false};
	std::mutex m_mutex;
};

} // namespace moonlight_xbox_dx
