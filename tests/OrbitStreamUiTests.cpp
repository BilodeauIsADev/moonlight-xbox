#include "../Common/OrbitStreamUi.h"
#include <cstdio>
#include <cstdlib>

using namespace moonlight_xbox_dx::OrbitStreamUi;

static void Check(bool condition, const char* description) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", description);
		std::exit(1);
	}
}

static void CheckText(const std::string& actual, const char* expected, const char* description) {
	if (actual != expected) {
		std::fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n",
		             description, expected, actual.c_str());
		std::exit(1);
	}
}

int main() {
	// Known host-side setup problems read as instructions, not codes
	CheckText(DescribeStartFailure(StartErrorGsNotSupported4K, "raw"),
	          "The host can't stream at 4K. Lower the resolution in the stream settings.",
	          "gs_start_app 4K error maps to plain words");
	CheckText(DescribeStartFailure(StartErrorGsNotSupportedMode, "raw"),
	          "The host doesn't support the selected combination of codec, resolution or frame rate.",
	          "Unsupported codec or resolution maps to plain words");
	CheckText(DescribeStartFailure(StartErrorNoVideoTraffic, "raw"),
	          "The host was reachable but never sent video. Check its firewall and port forwarding rules.",
	          "No video traffic maps to plain words");
	CheckText(DescribeStartFailure(StartErrorProtectedContent, "raw"),
	          "The host can't capture protected content. Close any DRM-protected video playing on it.",
	          "Protected content maps to plain words");
	CheckText(DescribeStartFailure(StartErrorSocketTimeout, "raw"),
	          "The connection to the host timed out. Make sure the host is awake and the address is right.",
	          "Socket timeout maps to plain words");
	CheckText(DescribeStartFailure(StartErrorConnectionRefused, "raw"),
	          "The host refused the connection. Make sure its streaming software is running.",
	          "Connection refused maps to plain words");

	// Unknown codes fall back to the raw message, trimmed of its log newline
	CheckText(DescribeStartFailure(4242, "Stage 4: 'RTSP' - Failed with error: 4242.\n"),
	          "Stage 4: 'RTSP' - Failed with error: 4242.",
	          "Unknown code falls back to the trimmed raw message");
	CheckText(DescribeStartFailure(4242, "   \n\t"),
	          "The stream couldn't be started. Try again.",
	          "Empty raw message falls back to a generic line");
	CheckText(DescribeStartFailure(StartErrorGracefulTermination, ""),
	          "The stream couldn't be started. Try again.",
	          "Graceful termination is not a start failure message");

	CheckText(FormatPlayedDuration(6120), "1h 42m", "Over an hour drops the seconds");
	CheckText(FormatPlayedDuration(3600), "1h", "Exactly an hour shows no minutes");
	CheckText(FormatPlayedDuration(300), "5m 00s", "Whole minutes keep padded seconds");
	CheckText(FormatPlayedDuration(42), "42s", "Under a minute shows seconds only");
	CheckText(FormatPlayedDuration(0), "0s", "An empty session still shows a length");
	CheckText(FormatPlayedDuration(-5), "0s", "Negative durations clamp to zero");

	Check(ShouldShowSessionSummary(10, true), "A ten second session shows the summary");
	Check(ShouldShowSessionSummary(3720, true), "A long clean session shows the summary");
	Check(!ShouldShowSessionSummary(9, true), "Sessions under ten seconds skip the summary");
	Check(!ShouldShowSessionSummary(3720, false), "Error disconnects skip the summary");
	Check(!ShouldShowSessionSummary(9, false), "Short error disconnects skip the summary");

	std::puts("Orbit stream UI tests passed");
}
