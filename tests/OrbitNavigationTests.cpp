#include "../Common/OrbitNavigation.h"
#include <cstdio>
#include <cstdlib>

using namespace moonlight_xbox_dx::OrbitNavigation;

static void Check(bool condition, const char* description) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", description);
		std::exit(1);
	}
}

int main() {
	const bool allTabs[] = { true, true, true };
	Check(NextTab(0, 1, allTabs) == 1, "Hosts -> Library");
	Check(NextTab(1, 1, allTabs) == 2, "Library -> Settings");
	Check(NextTab(2, 1, allTabs) == 0, "Settings wraps to Hosts");
	Check(NextTab(0, -1, allTabs) == 2, "Hosts wraps back to Settings");
	const bool noLibrary[] = { true, false, true };
	Check(NextTab(2, -1, noLibrary) == 0, "App settings skips the hidden Library tab");
	Check(NextTab(0, 1, noLibrary) == 2, "Skip an unavailable Library");
	const bool onlyHosts[] = { true, false, false };
	Check(NextTab(0, 1, onlyHosts) == 0, "One available tab stays selected");
	const bool noTabs[] = { false, false, false };
	Check(NextTab(1, -1, noTabs) == 1, "No available tabs stays selected");

	Check(ShelfPageTarget(-1, 0, 6, 1) == -1, "Empty library has no target");
	Check(ShelfPageTarget(-1, 20, 6, 1) == 0, "Enter shelf at its first game");
	Check(ShelfPageTarget(-1, 20, 6, -1) == 19, "Enter shelf backwards at its last game");
	Check(ShelfPageTarget(2, 20, 6, 1) == 8, "Right trigger advances a visible page");
	Check(ShelfPageTarget(8, 20, 6, -1) == 2, "Left trigger retreats a visible page");
	Check(ShelfPageTarget(19, 20, 6, 1) == 19, "Last game does not wrap");
	Check(ShelfPageTarget(0, 20, 6, -1) == 0, "First game does not wrap");
	Check(ShelfPageTarget(4, 3, 6, 1) == 0, "Recover when a refresh removes the current game");
	Check(ShelfPageTarget(0, 2, 0, 1) == 1, "Unmeasured viewport still advances");
	for (int count = 1; count <= 50; ++count) {
		for (int page = 1; page <= count + 2; ++page) {
			for (int current = 0; current < count; ++current) {
				const int next = ShelfPageTarget(current, count, page, 1);
				const int previous = ShelfPageTarget(current, count, page, -1);
				Check(current <= next && next < count, "Forward paging stays in range and monotonic");
				Check(0 <= previous && previous <= current, "Backward paging stays in range and monotonic");
			}
		}
	}
	std::puts("Orbit navigation tests passed");
}
