#pragma once

namespace moonlight_xbox_dx {
	namespace CoverArt {
		struct HeroArtwork {
			Platform::String^ LogoPath = nullptr;
			Platform::String^ BackgroundPath = nullptr;
		};

		// Looks up a square cover for appName on SteamGridDB and caches it in cacheFolder
		// (which must end with a backslash). Returns the cached file path, or nullptr when
		// there is no API key, no exact match, or the request failed. Blocks on network I/O,
		// so call it from a background thread.
		Platform::String ^ FindSquareCover(Platform::String ^ appName, Platform::String ^ cacheFolder);
		// Uses the same exact-name lookup, with separate caches and miss markers for
		// logos and wide hero artwork. Call from a background thread.
		HeroArtwork FindHeroArtwork(Platform::String^ appName, Platform::String^ cacheFolder);
	}
}
