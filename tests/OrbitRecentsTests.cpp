#include "../Common/OrbitRecents.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace moonlight_xbox_dx::OrbitRecents;

static void Check(bool condition, const char* description) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", description);
		std::exit(1);
	}
}

int main() {
	const long long now = 1'700'000'000;
	Check(RelativeTimeLabel(0, now) == "Never played", "No stamp reads as never played");
	Check(RelativeTimeLabel(-1, now) == "Never played", "Negative stamps read as never played");
	Check(RelativeTimeLabel(now + 5, now) == "Never played", "Future stamps read as never played");
	Check(RelativeTimeLabel(now, now) == "Just now", "Same second reads as just now");
	Check(RelativeTimeLabel(now - 59, now) == "Just now", "Under a minute reads as just now");
	Check(RelativeTimeLabel(now - 60, now) == "1 minute ago", "Singular minute");
	Check(RelativeTimeLabel(now - 3 * 60, now) == "3 minutes ago", "Plural minutes");
	Check(RelativeTimeLabel(now - 90 * 60, now) == "1 hour ago", "Singular hour");
	Check(RelativeTimeLabel(now - 3 * 60 * 60, now) == "3 hours ago", "Plural hours");
	Check(RelativeTimeLabel(now - 2LL * 24 * 60 * 60, now) == "2 days ago", "Plural days");
	Check(RelativeTimeLabel(now - 45LL * 24 * 60 * 60, now) == "1 month ago", "Singular month");
	Check(RelativeTimeLabel(now - 400LL * 24 * 60 * 60, now) == "1 year ago", "Singular year");

	const AppOrderKey running{ true, 0 };
	const AppOrderKey recent{ false, now };
	const AppOrderKey older{ false, now - 60 };
	const AppOrderKey never{ false, 0 };
	Check(AppOrderBefore(running, recent), "A running app comes first even if played long ago");
	Check(AppOrderBefore(recent, older), "More recently played comes first");
	Check(AppOrderBefore(older, never), "Played apps outrank never-played ones");
	Check(AppOrderBefore(running, recent) && !AppOrderBefore(recent, running), "Running first is a strict order");
	const AppOrderKey neverAgain{ false, 0 };
	Check(!AppOrderBefore(never, neverAgain) && !AppOrderBefore(neverAgain, never), "Never-played ties are stable, not swapped");

	// A stable sort must keep never-played apps in the alphabetical order the
	// app list arrived in, lifting only played and running apps to the front.
	struct Entry { int id; AppOrderKey key; };
	std::vector<Entry> shelf = {
		{ 1, AppOrderKey{ false, 0 } },          // "Beta", never played
		{ 2, AppOrderKey{ false, now - 60 } },   // "Omega", played long ago
		{ 3, AppOrderKey{ false, 0 } },          // "Alpha", never played
		{ 4, AppOrderKey{ true, 0 } },           // "Gamma", running
		{ 5, AppOrderKey{ false, now } },        // "Zulu", just played
	};
	std::stable_sort(shelf.begin(), shelf.end(), [](const Entry& a, const Entry& b) {
		return AppOrderBefore(a.key, b.key);
	});
	Check(shelf[0].id == 4, "Running app first");
	Check(shelf[1].id == 5, "Just played second");
	Check(shelf[2].id == 2, "Older played third");
	Check(shelf[3].id == 1 && shelf[4].id == 3, "Never played keep the arrival order");

	Check(JumpLetter("Halo Reach") == 'H', "Leading letter jumps to its own row");
	Check(JumpLetter("steam link") == 'S', "Lower case titles still jump by letter");
	Check(JumpLetter("  Puzzle") == 'P', "Leading spaces do not change the letter");
	Check(JumpLetter("7 Days to Die") == '#', "Titles starting with a digit go under #");
	Check(JumpLetter("_hidden") == '#', "Underscore helper apps go under #");
	Check(JumpLetter("___") == '#', "No leading letter goes under #");
	Check(JumpLetter("") == '#', "Empty names go under #");

	std::puts("Orbit recents tests passed");
}
