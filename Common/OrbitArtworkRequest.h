#pragma once

#include <cstdint>
#include <string>

namespace moonlight_xbox_dx {

inline std::wstring OrbitArtworkUri(const std::wstring& path, const std::wstring& localFolder) {
	if (!localFolder.empty() && path.compare(0, localFolder.size(), localFolder) == 0 &&
		path.size() > localFolder.size() && (path[localFolder.size()] == L'\\' || path[localFolder.size()] == L'/')) {
		std::wstring uri = L"ms-appdata:///local" + path.substr(localFolder.size());
		for (auto& c : uri) if (c == L'\\') c = L'/';
		return uri;
	}
	return path;
}

// An image decode may finish after focus changed or the page was left.
class OrbitArtworkRequest {
public:
	bool Begin(const std::wstring& path, uint64_t& request) {
		if (path.empty() || path == requestedPath) return false;
		requestedPath = path;
		request = ++generation;
		return true;
	}
	bool IsCurrent(uint64_t request) const { return request == generation; }
	void Invalidate() { ++generation; requestedPath.clear(); }

private:
	uint64_t generation = 0;
	std::wstring requestedPath;
};

} // namespace moonlight_xbox_dx
