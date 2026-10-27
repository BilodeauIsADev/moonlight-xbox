#include "../Common/OrbitHostCard.h"
#include <cstdio>
#include <cstdlib>

using namespace moonlight_xbox_dx;
using namespace moonlight_xbox_dx::OrbitHostCard;

static void Check(bool condition, const char* description) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", description);
		std::exit(1);
	}
}

int main() {
	// Stable per-PC accents.
	Check(PaletteIndex(L"HOLLOW-COURSE-DESKTOP") == PaletteIndex(L"HOLLOW-COURSE-DESKTOP"),
		"The same computer name always maps onto the same accent");
	Check(Hash(L"DESKTOP") != Hash(L"DESKTOP ") && Hash(L"B") != Hash(L"A"),
		"Different names are not trivially collapsed to one accent");
	int index = PaletteIndex(L"Living Room PC");
	Check(0 <= index && index < kPaletteCount, "An accent index stays inside the palette");
	Check(0 <= PaletteIndex(L"") && PaletteIndex(L"") < kPaletteCount, "An unknown name still lands inside the palette");
	Check(AccentArgb(L"Living Room PC") == kPalette[PaletteIndex(L"Living Room PC")],
		"The accent colour comes from the mapped palette entry");
	for (const wchar_t* name : { L"CORY-DESKTOP", L"desk", L"living-room", L"hyperv", L"my-game-pc" }) {
		int i = PaletteIndex(name);
		Check(0 <= i && i < kPaletteCount, "Probed names never fall outside the palette");
	}
	int distinct = 0;
	for (int i = 0; i < kPaletteCount; ++i) {
		for (int j = i + 1; j < kPaletteCount; ++j) {
			if (kPalette[i] != kPalette[j]) ++distinct;
		}
	}
	Check(distinct == kPaletteCount * (kPaletteCount - 1) / 2, "The palette has no duplicate colours");

	// One line of copy per waiting state.
	Check(StatusText(HostCardStatus::Probing, 0) == std::wstring(L"Checking\u2026"), "Probing shows Checking");
	Check(StatusText(HostCardStatus::Waking, 12) == std::wstring(L"Waking\u2026 12s"), "Waking counts up in seconds");
	Check(StatusText(HostCardStatus::Waking, 0) == std::wstring(L"Waking\u2026 0s"), "Waking still reads when the clock has not started");
	Check(StatusText(HostCardStatus::Waking, -3) == std::wstring(L"Waking\u2026 0s"), "A negative wake clock does not leak into the text");
	Check(StatusText(HostCardStatus::WakeTimedOut, 0) == std::wstring(L"Couldn't reach \u00B7 X to retry"),
		"Timed out offers the wake retry");
	Check(StatusText(HostCardStatus::Offline, 0) == std::wstring(L"Offline \u00B7 X to wake"), "Offline offers the wake shortcut");
	Check(StatusText(HostCardStatus::Online, 0) == std::wstring(L"Online"), "Online stays plain");
	std::puts("Orbit host card tests passed");
}
