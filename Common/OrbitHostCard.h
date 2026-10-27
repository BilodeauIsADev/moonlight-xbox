#pragma once

// Pure logic behind the Hosts screen cards, kept UI-free so it can be unit tested
// on any machine (see tests/OrbitHostCardTests.cpp).
#include <cstdint>
#include <string>

namespace moonlight_xbox_dx {

namespace OrbitHostCard {

	// Each host card gets a stable accent derived from its name, so several PCs
	// on the shelf can be told apart without relying on the identical dark tile.
	inline constexpr int kPaletteCount = 8;
	// 0xAARRGGBB muted colours tuned for the dark Orbit canvas; they act as the
	// first gradient stop over the near-black second stop of the card.
	inline constexpr uint32_t kPalette[kPaletteCount] = {
		0xFF4C6EA8, // steel blue
		0xFF3F8C7A, // teal
		0xFF8F6F3F, // amber
		0xFF8F4A3F, // rust
		0xFF6B5A9E, // violet
		0xFF3F6E8C, // slate cyan
		0xFF8C5A7A, // rose
		0xFF6E8C3F, // olive
	};

	// FNV-1a over the UTF-16 code units: stable across runs for the same name.
	inline uint32_t Hash(const std::wstring& name) {
		uint32_t hash = 2166136261u;
		for (wchar_t c : name) {
			hash ^= (uint32_t)c;
			hash *= 16777619u;
		}
		return hash;
	}

	inline int PaletteIndex(const std::wstring& name) {
		return (int)(Hash(name) % kPaletteCount);
	}

	inline uint32_t AccentArgb(const std::wstring& name) {
		return kPalette[PaletteIndex(name)];
	}

	// The four waiting states a host card can show, one line of copy each.
	enum class HostCardStatus { Probing, Waking, WakeTimedOut, Offline, Online };

	// wakeSeconds only matters for Waking; it may be negative from a clock,
	// so clamp instead of printing a countdown that can go backwards.
	inline std::wstring StatusText(HostCardStatus status, int wakeSeconds) {
		switch (status) {
		case HostCardStatus::Probing:
			return L"Checking\u2026";
		case HostCardStatus::Waking:
			return L"Waking\u2026 " + std::to_wstring(wakeSeconds > 0 ? wakeSeconds : 0) + L"s";
		case HostCardStatus::WakeTimedOut:
			return L"Couldn't reach \u00B7 X to retry";
		case HostCardStatus::Offline:
			return L"Offline \u00B7 X to wake";
		case HostCardStatus::Online:
			return L"Online";
		}
		return L"";
	}

} // namespace OrbitHostCard

} // namespace moonlight_xbox_dx
