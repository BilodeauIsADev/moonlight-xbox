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

			Result Download(String ^ name, String ^ path) {
				// Only an exact name match is used, so "Desktop" doesn't pick up some random game.
				auto games = GetData(L"search/autocomplete/" + Uri::EscapeComponent(name));
				if (games == nullptr) return Result::Failed;
				std::wstring want = Normalize(name->Data());
				double gameId = -1;
				for (unsigned i = 0; i < games->Size && gameId < 0; i++) {
					auto game = games->GetObjectAt(i);
					if (Normalize(game->GetNamedString(L"name", L"")->Data()) == want) gameId = game->GetNamedNumber(L"id", -1);
				}
				if (gameId < 0) return Result::Missing;

				// Results come sorted by community score, so the first grid is the best one.
				auto grids = GetData(L"grids/game/" + ref new String(std::to_wstring((int64_t)gameId).c_str()) +
				                     L"?dimensions=1024x1024,512x512&types=static&nsfw=false&humor=false");
				if (grids == nullptr) return Result::Failed;
				if (grids->Size == 0) return Result::Missing;
				auto url = grids->GetObjectAt(0)->GetNamedString(L"url", L"");
				if (url->IsEmpty()) return Result::Missing;

				// The CDN doesn't need the API key, so don't send it there.
				auto image = create_task((ref new HttpClient())->GetBufferAsync(ref new Uri(url))).get();
				if (image->Length == 0) return Result::Failed;
				std::vector<uint8_t> bytes(image->Length);
				Windows::Storage::Streams::DataReader::FromBuffer(image)->ReadBytes(
					ArrayReference<uint8_t>(bytes.data(), (unsigned)bytes.size()));

				// Write then rename, so an interrupted download never leaves a broken cover cached.
				String ^ part = path + L".part";
				{
					std::ofstream out(part->Data(), std::ios::binary | std::ios::trunc);
					out.write((const char *)bytes.data(), bytes.size());
					if (!out) return Result::Failed;
				}
				_wremove(path->Data());
				return _wrename(part->Data(), path->Data()) == 0 ? Result::Found : Result::Failed;
			}
		}

		String ^ FindSquareCover(String ^ appName, String ^ cacheFolder) {
			if (STEAMGRIDDB_API_KEY[0] == L'\0' || appName == nullptr) return nullptr;
			std::wstring key = Normalize(appName->Data());
			if (key.empty()) return nullptr;

			String ^ path = cacheFolder + L"sgdb_" + ref new String(key.c_str()) + L".img";
			String ^ missPath = path + L".miss";
			int64_t time;
			if (FileTime(path, &time)) return path;
			if (FileTime(missPath, &time) && Now() - time < kMissRetry100ns) return nullptr;

			Result result;
			try {
				result = Download(appName, path);
			} catch (Exception ^ e) {
				Utils::Logf("SteamGridDB: lookup failed: %S\n", e->Message->Data());
				result = Result::Failed;
			}

			if (result == Result::Found) return path;
			if (result == Result::Missing) std::ofstream(missPath->Data()).close();
			return nullptr;
		}
	}
}
