#include "../Common/OrbitArtworkRequest.h"
#include <cstdio>
#include <cstdlib>

using namespace moonlight_xbox_dx;

static void Check(bool condition, const char* description) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", description);
		std::exit(1);
	}
}

int main() {
	OrbitArtworkRequest requests;
	uint64_t first = 0, second = 0, repeated = 0;
	Check(!requests.Begin(L"", first), "Ignore an empty artwork path");
	Check(requests.Begin(L"first.png", first), "Start the first decode");
	Check(requests.IsCurrent(first), "First decode can be presented");
	Check(!requests.Begin(L"first.png", repeated), "Repeated notifications do not restart the fade");
	Check(requests.IsCurrent(first), "A duplicate notification does not cancel the pending decode");
	Check(requests.Begin(L"second.png", second), "Changing focus starts a new decode");
	Check(!requests.IsCurrent(first), "A slower old decode cannot replace the focused game");
	Check(requests.IsCurrent(second), "Only the latest decode can be presented");
	requests.Invalidate();
	Check(!requests.IsCurrent(second), "Leaving the page cancels pending presentation");
	Check(requests.Begin(L"second.png", repeated), "Re-entering the page can load the same artwork again");
	Check(repeated != second && !requests.IsCurrent(second), "An old callback stays invalid after re-entry");
	Check(requests.Begin(L"ms-appx:///Assets/gamepad.svg", first), "A failed cover can request the SVG fallback");
	Check(!requests.Begin(L"ms-appx:///Assets/gamepad.svg", second), "A failed fallback does not recurse");

	const std::wstring folder = L"C:\\Users\\Xbox\\LocalState";
	Check(OrbitArtworkUri(folder + L"\\images\\12.png", folder) == L"ms-appdata:///local/images/12.png", "Map the existing Windows cover cache to an app-data URI");
	Check(OrbitArtworkUri(L"ms-appx:///Assets/gamepad.svg", folder) == L"ms-appx:///Assets/gamepad.svg", "Keep packaged SVG URIs unchanged");
	Check(OrbitArtworkUri(L"https://example.com/cover.png", folder) == L"https://example.com/cover.png", "Keep absolute URIs unchanged");
	Check(OrbitArtworkUri(folder + L"Other\\cover.png", folder) == folder + L"Other\\cover.png", "A shared path prefix is not a folder match");
	Check(OrbitArtworkUri(L"cover.png", L"") == L"cover.png", "An empty local folder does not rewrite paths");
	std::puts("Orbit artwork tests passed");
}
