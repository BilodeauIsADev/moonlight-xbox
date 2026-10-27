#pragma once

// Pure logic behind the Library shelf: recently played ordering, relative
// "last played" labels and jump-to-letter navigation. Plain C++ so it can be
// unit tested without a UWP build (see tests/OrbitRecentsTests.cpp).

#include <cstdint>
#include <string>

namespace moonlight_xbox_dx {
namespace OrbitRecents {

	// Everything the shelf order needs to know about one poster.
	struct AppOrderKey {
		bool running = false;
		long long lastPlayed = 0; // Unix seconds; 0 means never played
	};

	// Ordering for a stable sort: the running app first, then most recently
	// played. Never-played apps compare equal, so std::stable_sort keeps the
	// alphabetical order the app list arrived in.
	inline bool AppOrderBefore(const AppOrderKey& a, const AppOrderKey& b) {
		if (a.running != b.running) return a.running;
		return a.lastPlayed > b.lastPlayed;
	}

	inline std::string CountedAgo(const char* unit, long long count) {
		std::string label = std::to_string(count) + ' ' + unit;
		if (count != 1) label += 's';
		return label + " ago";
	}

	// "Just now", "3 hours ago", "2 days ago" ... or "Never played".
	inline std::string RelativeTimeLabel(long long lastPlayed, long long now) {
		if (lastPlayed <= 0 || now < lastPlayed) return "Never played";
		const long long seconds = now - lastPlayed;
		if (seconds < 60) return "Just now";
		const long long minutes = seconds / 60;
		if (minutes < 60) return CountedAgo("minute", minutes);
		const long long hours = minutes / 60;
		if (hours < 24) return CountedAgo("hour", hours);
		const long long days = hours / 24;
		if (days < 30) return CountedAgo("day", days);
		const long long months = days / 30;
		if (months < 12) return CountedAgo("month", months);
		return CountedAgo("year", days / 365);
	}

	// Jump bucket for a title: its leading A-Z letter (upper case), or '#'
	// when it starts with anything else (digits, punctuation, other scripts).
	inline char JumpLetter(const std::string& name) {
		size_t first = 0;
		while (first < name.size() && name[first] == ' ') ++first;
		if (first < name.size()) {
			const char c = name[first];
			if (c >= 'a' && c <= 'z') return static_cast<char>(c - 'a' + 'A');
			if (c >= 'A' && c <= 'Z') return c;
		}
		return '#';
	}

} // namespace OrbitRecents
} // namespace moonlight_xbox_dx
