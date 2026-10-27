#include "pch.h"
#include "CoverArt.h"
#include "Utils.hpp"
#include <cwctype>
#include <fstream>
#include <vector>

// The key is baked in at build time and never committed. Copy SteamGridDbKey.h.example
// to SteamGridDbKey.h and fill it in; CI writes it from the STEAMGRIDDB_API_KEY secret.
#if __has_include("SteamGridDbKey.h")
#include "SteamGridDbKey.h"
#endif
#ifndef STEAMGRIDDB_API_KEY
#define STEAMGRIDDB_API_KEY L""
#endif

using namespace Platform;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Web::Http;
using namespace concurrency;

namespace moonlight_xbox_dx {
	namespace CoverArt {
		namespace {
			const wchar_t *kApi = L"https://www.steamgriddb.com/api/v2/";
			// A miss is retried after a week, in case someone uploads art for the game.
			const int64_t kMissRetry100ns = 7LL * 24 * 3600 * 10000000;

			enum class Result { Found, Missing, Failed };
			enum class Kind { Square, Logo, Hero };

			// "Hollow Knight: Voidheart Edition" -> "hollowknightvoidheartedition"
			std::wstring Normalize(const wchar_t *s) {
				std::wstring out;
				for (; *s; ++s) {
					if (iswalnum(*s)) out += towlower(*s);
				}
				return out;
			}

			HttpClient ^ ApiClient() {
				static HttpClient ^ client = [] {
					auto c = ref new HttpClient();
					c->DefaultRequestHeaders->Authorization =
						ref new Headers::HttpCredentialsHeaderValue(L"Bearer", ref new String(STEAMGRIDDB_API_KEY));
					return c;
				}();
				return client;
			}

			// GETs an API path and returns its "data" array, or nullptr on any failure.
			JsonArray ^ GetData(String ^ path) {
				auto response = create_task(ApiClient()->GetAsync(ref new Uri(ref new String(kApi) + path))).get();
				if (!response->IsSuccessStatusCode) {
					Utils::Logf("SteamGridDB: HTTP %d\n", (int)response->StatusCode);
					return nullptr;
				}
				auto text = create_task(response->Content->ReadAsStringAsync()).get();
				JsonObject ^ obj;
				if (!JsonObject::TryParse(text, &obj) || !obj->GetNamedBoolean(L"success", false)) return nullptr;
				return obj->GetNamedArray(L"data", nullptr);
			}

			bool FileTime(String ^ path, int64_t *time) {
				WIN32_FILE_ATTRIBUTE_DATA data;
				if (!GetFileAttributesEx(path->Data(), GetFileExInfoStandard, &data)) return false;
				*time = ((int64_t)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
				return true;
			}

			int64_t Now() {
				FILETIME ft;
				GetSystemTimeAsFileTime(&ft);
				return ((int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
			}

			Result ResolveGame(String^ name, int64_t& gameId) {
				// Only an exact name match is used, so "Desktop" doesn't pick up some random game.
				auto games = GetData(L"search/autocomplete/" + Uri::EscapeComponent(name));
				if (games == nullptr) return Result::Failed;
				std::wstring want = Normalize(name->Data());
				for (unsigned i = 0; i < games->Size && gameId < 0; i++) {
					auto game = games->GetObjectAt(i);
					if (Normalize(game->GetNamedString(L"name", L"")->Data()) == want) gameId = static_cast<int64_t>(game->GetNamedNumber(L"id", -1));
				}
				return gameId < 0 ? Result::Missing : Result::Found;
			}

			Result Download(int64_t gameId, Kind kind, String^ path) {
				// Request formats the UWP bitmap decoder supports, and only static art.
				String^ endpoint;
				String^ filters = L"?types=static&nsfw=false&humor=false&mimes=image/png,image/jpeg";
				switch (kind) {
				case Kind::Square:
					endpoint = L"grids/game/";
					filters += L"&dimensions=1024x1024,512x512";
					break;
				case Kind::Logo:
					endpoint = L"logos/game/";
					filters = L"?types=static&nsfw=false&humor=false&mimes=image/png&styles=official,white,custom";
					break;
				default:
					endpoint = L"heroes/game/";
					break;
				}
				auto artwork = GetData(endpoint + ref new String(std::to_wstring(gameId).c_str()) + filters);
				if (artwork == nullptr) return Result::Failed;
				if (artwork->Size == 0) return Result::Missing;
				auto url = artwork->GetObjectAt(0)->GetNamedString(L"url", L"");
				if (url->IsEmpty()) return Result::Missing;

				// The CDN doesn't need the API key, so don't send it there.
				auto cdn = ref new HttpClient();
				auto image = create_task(cdn->GetBufferAsync(ref new Uri(url))).get();
				if (image->Length == 0) return Result::Failed;
				std::vector<uint8_t> bytes(image->Length);
				Windows::Storage::Streams::DataReader::FromBuffer(image)->ReadBytes(
					ArrayReference<uint8_t>(bytes.data(), (unsigned)bytes.size()));

				// Separate temporary files also protect overlapping page/host requests
				// for the same game. A completed download wins without being replaced.
				String ^ part = path + L"." + ref new String(std::to_wstring(GetCurrentThreadId()).c_str()) + L".part";
				{
					std::ofstream out(part->Data(), std::ios::binary | std::ios::trunc);
					out.write((const char *)bytes.data(), bytes.size());
					if (!out) return Result::Failed;
				}
				if (_wrename(part->Data(), path->Data()) == 0) return Result::Found;
				int64_t time;
				const bool cached = FileTime(path, &time);
				_wremove(part->Data());
				return cached ? Result::Found : Result::Failed;
			}

			String^ FindArtwork(String^ appName, String^ cacheFolder, Kind kind, int64_t& gameId, Result& lookup) {
				if (STEAMGRIDDB_API_KEY[0] == L'\0' || appName == nullptr || cacheFolder == nullptr) return nullptr;
				std::wstring key = Normalize(appName->Data());
				if (key.empty()) return nullptr;

				// Preserve the existing square-cover cache when adding new artwork types.
				String^ prefix = kind == Kind::Square ? L"sgdb_" : kind == Kind::Logo ? L"sgdb_logo_" : L"sgdb_hero_";
				String ^ path = cacheFolder + prefix + ref new String(key.c_str()) + L".img";
				String ^ missPath = path + L".miss";
				int64_t time;
				if (FileTime(path, &time)) return path;
				if (FileTime(missPath, &time) && Now() - time < kMissRetry100ns) return nullptr;

				Result result;
				try {
					if (gameId < 0 && lookup != Result::Missing) lookup = ResolveGame(appName, gameId);
					result = lookup == Result::Found ? Download(gameId, kind, path) : lookup;
				} catch (Exception ^ e) {
					Utils::Logf("SteamGridDB: lookup failed: %S\n", e->Message->Data());
					result = Result::Failed;
				}

				if (result == Result::Found) return path;
				if (result == Result::Missing) std::ofstream(missPath->Data()).close();
				return nullptr;
			}
		}

		String^ FindSquareCover(String^ appName, String^ cacheFolder) {
			int64_t gameId = -1;
			auto lookup = Result::Failed;
			return FindArtwork(appName, cacheFolder, Kind::Square, gameId, lookup);
		}

		HeroArtwork FindHeroArtwork(String^ appName, String^ cacheFolder) {
			int64_t gameId = -1;
			auto lookup = Result::Failed;
			HeroArtwork artwork;
			artwork.LogoPath = FindArtwork(appName, cacheFolder, Kind::Logo, gameId, lookup);
			artwork.BackgroundPath = FindArtwork(appName, cacheFolder, Kind::Hero, gameId, lookup);
			return artwork;
		}
	}
}
