#pragma once

namespace moonlight_xbox_dx {
namespace OrbitNavigation {

// Tabs always follow the visual order: Hosts, Library, Settings.
inline int NextTab(int current, int direction, const bool (&available)[3]) {
	for (int step = 1; step <= 3; ++step) {
		int next = (current + direction * step + 3) % 3;
		if (available[next]) return next;
	}
	return current;
}

inline int ShelfPageTarget(int current, int count, int visibleCount, int direction) {
	if (count <= 0) return -1;
	if (current < 0 || current >= count) return direction < 0 ? count - 1 : 0;
	const int pageSize = visibleCount > 0 ? visibleCount : 1;
	if (direction < 0) return current < pageSize ? 0 : current - pageSize;
	return count - 1 - current < pageSize ? count - 1 : current + pageSize;
}

} // namespace OrbitNavigation
} // namespace moonlight_xbox_dx
