#include "nowplaylisting-source.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace Gdiplus;
namespace fs = std::filesystem;

namespace {
constexpr const char *kSourceId = "nowplaylisting_source";
constexpr const char *kPlaylistSetting = "playlist";
constexpr const char *kRecursiveSetting = "recursive";
constexpr const char *kShuffleSetting = "shuffle";
constexpr const char *kLoopSetting = "loop";
constexpr const char *kFontSetting = "font";
constexpr const char *kTextColorSetting = "text_color";
constexpr const char *kOutlineColorSetting = "glow_color";
constexpr const char *kOutlineSizeSetting = "glow_size";
constexpr const char *kGlowColorSetting = "diffuse_glow_color";
constexpr const char *kGlowSizeSetting = "diffuse_glow_size";
constexpr const char *kOffsetXSetting = "offset_x";
constexpr const char *kOffsetYSetting = "offset_y";

constexpr uint32_t kDefaultCanvasSize = 480;
constexpr uint32_t kDefaultTextColor = 0xFFFFFF;
constexpr uint32_t kDefaultOutlineColor = 0x000000;
constexpr int kDefaultOutlineSize = 4;
constexpr uint32_t kDefaultGlowColor = 0x000000;
constexpr int kDefaultGlowSize = 0;
constexpr int kDefaultGlowOpacity = 25;
constexpr int kDefaultOffset = 0;
constexpr int kDefaultFontSize = 96;
constexpr int kFontSizeScaleMultiplier = 3;
constexpr float kDefaultTitleGap = 12.0f;
constexpr const char *kMediaFileFilter =
	"Media Files (*.mp3 *.wav *.aiff *.aif *.mp4 *.mpg *.mpeg *.mkv *.avi);;All Files (*.*)";

enum class media_kind { unsupported, audio, video };

struct metadata_info {
	std::string artist;
	std::string title;
};

struct playlist_item {
	std::string path;
	std::string artist;
	std::string title;
};

struct nowplaylist_source {
	obs_source_t *source = nullptr;
	obs_source_t *media_source = nullptr;
	obs_source_t *album_art_source = nullptr;
	obs_source_t *artist_glow_source = nullptr;
	obs_source_t *artist_source = nullptr;
	obs_source_t *title_glow_source = nullptr;
	obs_source_t *title_source = nullptr;
	obs_data_t *font_settings = nullptr;
	uint32_t output_sample_rate = 48000;
	enum speaker_layout output_speakers = SPEAKERS_STEREO;

	std::vector<playlist_item> playlist_items;
	std::vector<std::string> playlist_entries;
	std::vector<std::string> expanded_files;
	std::vector<size_t> play_order;
	std::unordered_map<std::string, metadata_info> metadata_by_path;

	bool recursive = false;
	bool shuffle = false;
	bool loop = false;
	bool current_is_video = false;
	bool skip_next_media_ended = false;

	size_t order_pos = 0;

	std::string current_media_path;
	std::string current_album_art_path;
	std::string current_artist;
	std::string current_title;

	uint32_t text_color = kDefaultTextColor;
	uint32_t outline_color = kDefaultOutlineColor;
	int outline_size = kDefaultOutlineSize;
	uint32_t glow_color = kDefaultGlowColor;
	int glow_size = kDefaultGlowSize;
	int offset_x = kDefaultOffset;
	int offset_y = kDefaultOffset;

	std::mt19937 rng{std::random_device{}()};
};

void nowplaylist_update(void *data, obs_data_t *settings);

class scoped_com_init {
public:
	scoped_com_init()
	{
		hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	}

	~scoped_com_init()
	{
		if (SUCCEEDED(hr))
			CoUninitialize();
	}

	bool usable() const
	{
		return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
	}

private:
	HRESULT hr = E_FAIL;
};

ULONG_PTR g_gdiplus_token = 0;
bool g_gdiplus_initialized = false;
bool g_png_encoder_cached = false;
CLSID g_png_encoder_clsid{};

std::wstring utf8_to_wide(const std::string &value)
{
	if (value.empty())
		return {};

	const int required =
		MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
	if (required <= 0)
		return {};

	std::wstring out(static_cast<size_t>(required), L'\0');
	if (MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), required) <= 0)
		return {};

	return out;
}

std::string wide_to_utf8(const std::wstring &value)
{
	if (value.empty())
		return {};

	const int required =
		WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	if (required <= 0)
		return {};

	std::string out(static_cast<size_t>(required), '\0');
	if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), required, nullptr,
				nullptr) <= 0)
		return {};

	return out;
}

fs::path utf8_to_path(const std::string &value)
{
	return fs::path(utf8_to_wide(value));
}

std::string path_to_utf8(const fs::path &value)
{
	return wide_to_utf8(value.wstring());
}

std::wstring shell_path_wstring(const fs::path &value)
{
	fs::path normalized = value;
	normalized.make_preferred();
	return normalized.wstring();
}

std::string trim(std::string_view value)
{
	size_t start = 0;
	size_t end = value.size();

	while (start < end && std::isspace(static_cast<unsigned char>(value[start])))
		++start;
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
		--end;

	return std::string(value.substr(start, end - start));
}

std::string lower_ascii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

std::string extension_lower(const fs::path &path)
{
	return lower_ascii(path.extension().string());
}

std::string normalize_path_key(const std::string &path_utf8)
{
	fs::path normalized = utf8_to_path(path_utf8);
	normalized.make_preferred();
	std::string key = path_to_utf8(normalized);
	if (key.empty())
		key = path_utf8;

	std::replace(key.begin(), key.end(), '\\', '/');
	return lower_ascii(key);
}

media_kind classify_media_file(const fs::path &path)
{
	static const std::array<std::string, 4> audio_ext = {".mp3", ".wav", ".aiff", ".aif"};
	static const std::array<std::string, 5> video_ext = {".mp4", ".mpg", ".mpeg", ".mkv", ".avi"};
	const std::string ext = extension_lower(path);

	if (std::find(audio_ext.begin(), audio_ext.end(), ext) != audio_ext.end())
		return media_kind::audio;
	if (std::find(video_ext.begin(), video_ext.end(), ext) != video_ext.end())
		return media_kind::video;
	return media_kind::unsupported;
}

bool ensure_gdiplus()
{
	if (g_gdiplus_initialized)
		return true;

	GdiplusStartupInput input;
	if (GdiplusStartup(&g_gdiplus_token, &input, nullptr) != Ok)
		return false;

	g_gdiplus_initialized = true;
	return true;
}

bool get_png_encoder_clsid(CLSID *out_clsid)
{
	if (g_png_encoder_cached) {
		*out_clsid = g_png_encoder_clsid;
		return true;
	}

	if (!ensure_gdiplus())
		return false;

	UINT num = 0;
	UINT size = 0;
	GetImageEncodersSize(&num, &size);
	if (!num || !size)
		return false;

	std::vector<BYTE> buffer(size);
	auto *encoders = reinterpret_cast<ImageCodecInfo *>(buffer.data());
	if (GetImageEncoders(num, size, encoders) != Ok)
		return false;

	for (UINT i = 0; i < num; ++i) {
		if (encoders[i].MimeType && wcscmp(encoders[i].MimeType, L"image/png") == 0) {
			g_png_encoder_clsid = encoders[i].Clsid;
			g_png_encoder_cached = true;
			*out_clsid = g_png_encoder_clsid;
			return true;
		}
	}

	return false;
}

bool save_stream_png(IStream *stream, const std::wstring &output_path)
{
	if (!stream || !ensure_gdiplus())
		return false;

	CLSID png_encoder{};
	if (!get_png_encoder_clsid(&png_encoder))
		return false;

	LARGE_INTEGER zero{};
	stream->Seek(zero, STREAM_SEEK_SET, nullptr);

	Bitmap bitmap(stream, false);
	if (bitmap.GetLastStatus() != Ok)
		return false;

	return bitmap.Save(output_path.c_str(), &png_encoder, nullptr) == Ok;
}

bool save_thumbnail_png(const std::wstring &input_path, const std::wstring &output_path)
{
	if (!ensure_gdiplus())
		return false;

	CLSID png_encoder{};
	if (!get_png_encoder_clsid(&png_encoder))
		return false;

	ComPtr<IShellItemImageFactory> image_factory;
	if (FAILED(SHCreateItemFromParsingName(input_path.c_str(), nullptr, IID_PPV_ARGS(&image_factory))))
		return false;

	HBITMAP bitmap = nullptr;
	SIZE requested = {1024, 1024};
	const HRESULT hr =
		image_factory->GetImage(requested, SIIGBF_BIGGERSIZEOK | SIIGBF_RESIZETOFIT, &bitmap);
	if (FAILED(hr) || !bitmap)
		return false;

	Bitmap image(bitmap, nullptr);
	DeleteObject(bitmap);
	if (image.GetLastStatus() != Ok)
		return false;

	return image.Save(output_path.c_str(), &png_encoder, nullptr) == Ok;
}

bool extract_album_art_png(const std::wstring &input_path, const std::wstring &output_path)
{
	ComPtr<IPropertyStore> property_store;
	if (SUCCEEDED(SHGetPropertyStoreFromParsingName(input_path.c_str(), nullptr, GPS_DEFAULT,
							IID_PPV_ARGS(&property_store)))) {
		PROPVARIANT var;
		PropVariantInit(&var);
		const HRESULT hr = property_store->GetValue(PKEY_ThumbnailStream, &var);
		if (SUCCEEDED(hr) && var.vt == VT_STREAM && var.pStream) {
			const bool ok = save_stream_png(var.pStream, output_path);
			PropVariantClear(&var);
			if (ok)
				return true;
		} else {
			PropVariantClear(&var);
		}
	}

	return save_thumbnail_png(input_path, output_path);
}

std::string property_to_utf8(IPropertyStore *property_store, REFPROPERTYKEY key)
{
	if (!property_store)
		return {};

	PROPVARIANT var;
	PropVariantInit(&var);
	std::string out;

	if (SUCCEEDED(property_store->GetValue(key, &var))) {
		PWSTR value = nullptr;
		if (SUCCEEDED(PropVariantToStringAlloc(var, &value)) && value) {
			out = trim(wide_to_utf8(value));
			CoTaskMemFree(value);
		}

		if (out.empty()) {
			const ULONG count = PropVariantGetElementCount(var);
			for (ULONG i = 0; i < count; ++i) {
				PWSTR element = nullptr;
				if (SUCCEEDED(PropVariantGetStringElem(var, i, &element)) && element) {
					out = trim(wide_to_utf8(element));
					CoTaskMemFree(element);
					if (!out.empty())
						break;
				}
			}
		}
	}

	PropVariantClear(&var);
	return out;
}

metadata_info read_metadata_for_file(const fs::path &path)
{
	metadata_info info{};

	const std::wstring shell_path = shell_path_wstring(path);
	ComPtr<IPropertyStore> property_store;
	const HRESULT hr =
		SHGetPropertyStoreFromParsingName(shell_path.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&property_store));
	if (FAILED(hr)) {
		blog(LOG_WARNING, "[nowplaylisting] SHGetPropertyStoreFromParsingName failed: hr=0x%08X path='%s'",
		     static_cast<unsigned>(hr), path_to_utf8(path).c_str());
		return info;
	}

	const std::string display_artist = property_to_utf8(property_store.Get(), PKEY_Music_DisplayArtist);
	const std::string contributing_artist = property_to_utf8(property_store.Get(), PKEY_Music_Artist);
	const std::string album_artist = property_to_utf8(property_store.Get(), PKEY_Music_AlbumArtist);
	const std::string author = property_to_utf8(property_store.Get(), PKEY_Author);

	info.artist = display_artist;
	if (info.artist.empty())
		info.artist = contributing_artist;
	if (info.artist.empty())
		info.artist = album_artist;
	if (info.artist.empty())
		info.artist = author;
	info.title = property_to_utf8(property_store.Get(), PKEY_Title);

	return info;
}

std::string build_album_art_cache_path(const std::string &media_path_utf8)
{
	char *cache_dir_raw = obs_module_config_path("album-art");
	if (!cache_dir_raw)
		return {};

	const std::string cache_dir_utf8(cache_dir_raw);
	bfree(cache_dir_raw);

	if (cache_dir_utf8.empty())
		return {};

	std::error_code ec;
	const fs::path cache_dir = utf8_to_path(cache_dir_utf8);
	fs::create_directories(cache_dir, ec);

	const size_t hash = std::hash<std::string>{}(lower_ascii(media_path_utf8));
	const fs::path output_path = cache_dir / (std::to_string(hash) + ".png");
	return path_to_utf8(output_path);
}

void append_directory_files(const fs::path &directory, bool recursive, std::vector<std::string> &out);

std::vector<playlist_item> read_and_normalize_playlist_items(obs_data_t *settings, bool recursive_dirs)
{
	std::vector<playlist_item> items;
	obs_data_array_t *playlist = obs_data_get_array(settings, kPlaylistSetting);
	if (!playlist)
		return items;

	const size_t count = obs_data_array_count(playlist);
	items.reserve(count);

	obs_data_array_t *normalized = obs_data_array_create();
	bool changed = false;
	scoped_com_init com_init;
	const bool can_read_shell = com_init.usable();
	std::unordered_set<std::string> seen_paths;

	for (size_t i = 0; i < count; ++i) {
		obs_data_t *item = obs_data_array_item(playlist, i);
		if (!item)
			continue;

		const char *value_raw = obs_data_get_string(item, "value");
		const std::string path = value_raw ? trim(value_raw) : std::string();
		const char *artist_raw = obs_data_get_string(item, "artist");
		const char *title_raw = obs_data_get_string(item, "title");
		const std::string existing_artist = artist_raw ? trim(artist_raw) : std::string();
		const std::string existing_title = title_raw ? trim(title_raw) : std::string();

		if (path.empty()) {
			changed = true;
			obs_data_release(item);
			continue;
		}

		std::string artist = existing_artist;
		std::string title = existing_title;

		const fs::path entry_path = utf8_to_path(path);
		std::error_code ec;

		auto append_normalized_item = [&](const fs::path &media_path, const std::string &seed_artist,
						  const std::string &seed_title) {
			std::string out_path = path_to_utf8(media_path);
			const std::string out_key = normalize_path_key(out_path);
			if (!seen_paths.insert(out_key).second)
				return;

			std::string out_artist = seed_artist;
			std::string out_title = seed_title;

			if ((out_artist.empty() || out_title.empty()) && can_read_shell) {
				const metadata_info imported = read_metadata_for_file(media_path);
				if (out_artist.empty())
					out_artist = imported.artist;
				if (out_title.empty())
					out_title = imported.title;
			}

			if (out_title.empty())
				out_title = path_to_utf8(media_path.stem());

			playlist_item normalized_item;
			normalized_item.path = out_path;
			normalized_item.artist = out_artist;
			normalized_item.title = out_title;
			items.emplace_back(normalized_item);

			obs_data_t *normalized_data = obs_data_create();
			obs_data_set_string(normalized_data, "value", normalized_item.path.c_str());
			obs_data_set_string(normalized_data, "artist", normalized_item.artist.c_str());
			obs_data_set_string(normalized_data, "title", normalized_item.title.c_str());
			obs_data_array_push_back(normalized, normalized_data);
			obs_data_release(normalized_data);
		};

		if (fs::is_regular_file(entry_path, ec)) {
			if (classify_media_file(entry_path) == media_kind::unsupported) {
				changed = true;
				obs_data_release(item);
				continue;
			}

			append_normalized_item(entry_path, artist, title);
			if (path != (value_raw ? std::string(value_raw) : std::string()) || artist != existing_artist ||
			    title != existing_title) {
				changed = true;
			}
			obs_data_release(item);
			continue;
		}

		ec.clear();
		if (fs::is_directory(entry_path, ec)) {
			std::vector<std::string> expanded_paths;
			append_directory_files(entry_path, recursive_dirs, expanded_paths);
			for (const std::string &expanded_path : expanded_paths)
				append_normalized_item(utf8_to_path(expanded_path), std::string(), std::string());

			changed = true;
			obs_data_release(item);
			continue;
		}

		changed = true;
		obs_data_release(item);
	}

	obs_data_array_release(playlist);

	if (changed)
		obs_data_set_array(settings, kPlaylistSetting, normalized);

	obs_data_array_release(normalized);
	return items;
}

void rebuild_playlist_runtime_cache(nowplaylist_source *source)
{
	source->playlist_entries.clear();
	source->playlist_entries.reserve(source->playlist_items.size());
	source->metadata_by_path.clear();

	for (const playlist_item &item : source->playlist_items) {
		if (item.path.empty())
			continue;

		source->playlist_entries.push_back(item.path);
		if (item.artist.empty() && item.title.empty())
			continue;

		metadata_info metadata{};
		metadata.artist = item.artist;
		metadata.title = item.title;
		source->metadata_by_path[normalize_path_key(item.path)] = metadata;
	}
}

void append_directory_files(const fs::path &directory, bool recursive, std::vector<std::string> &out)
{
	std::vector<fs::path> files;
	const fs::directory_options options = fs::directory_options::skip_permission_denied;
	std::error_code ec;

	if (recursive) {
		for (fs::recursive_directory_iterator it(directory, options, ec), end; it != end; it.increment(ec)) {
			if (ec) {
				ec.clear();
				continue;
			}

			std::error_code status_ec;
			if (!it->is_regular_file(status_ec))
				continue;

			if (classify_media_file(it->path()) != media_kind::unsupported)
				files.push_back(it->path());
		}
	} else {
		for (fs::directory_iterator it(directory, options, ec), end; it != end; it.increment(ec)) {
			if (ec) {
				ec.clear();
				continue;
			}

			std::error_code status_ec;
			if (!it->is_regular_file(status_ec))
				continue;

			if (classify_media_file(it->path()) != media_kind::unsupported)
				files.push_back(it->path());
		}
	}

	std::sort(files.begin(), files.end());
	for (const auto &file : files)
		out.emplace_back(path_to_utf8(file));
}

std::vector<std::string> expand_playlist(const std::vector<std::string> &entries, bool recursive)
{
	std::vector<std::string> expanded;

	for (const std::string &entry : entries) {
		std::error_code ec;
		const fs::path path = utf8_to_path(entry);

		if (fs::is_regular_file(path, ec)) {
			if (classify_media_file(path) != media_kind::unsupported)
				expanded.emplace_back(path_to_utf8(path));
			continue;
		}

		ec.clear();
		if (fs::is_directory(path, ec)) {
			append_directory_files(path, recursive, expanded);
			continue;
		}
	}

	return expanded;
}

void build_play_order(nowplaylist_source *source, const std::string &preferred_path)
{
	source->play_order.clear();
	source->play_order.resize(source->expanded_files.size());
	std::iota(source->play_order.begin(), source->play_order.end(), 0);

	if (source->shuffle && source->play_order.size() > 1)
		std::shuffle(source->play_order.begin(), source->play_order.end(), source->rng);

	source->order_pos = 0;
	source->current_media_path.clear();

	if (source->play_order.empty() || preferred_path.empty())
		return;

	const auto file_it =
		std::find(source->expanded_files.begin(), source->expanded_files.end(), preferred_path);
	if (file_it == source->expanded_files.end())
		return;

	const size_t file_index = static_cast<size_t>(file_it - source->expanded_files.begin());
	const auto order_it = std::find(source->play_order.begin(), source->play_order.end(), file_index);
	if (order_it == source->play_order.end())
		return;

	source->order_pos = static_cast<size_t>(order_it - source->play_order.begin());
	source->current_media_path = preferred_path;
}

void update_album_art_source(nowplaylist_source *source)
{
	if (!source->album_art_source)
		return;

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", source->current_album_art_path.c_str());
	obs_data_set_bool(settings, "linear_alpha", false);
	obs_source_update(source->album_art_source, settings);
	obs_data_release(settings);

	const bool enabled = !source->current_is_video && !source->current_album_art_path.empty();
	obs_source_set_enabled(source->album_art_source, enabled);
}

obs_data_t *create_scaled_font_settings(const nowplaylist_source *source)
{
	if (!source || !source->font_settings)
		return nullptr;

	obs_data_t *font = obs_data_create();

	const char *face = obs_data_get_string(source->font_settings, "face");
	if (face && *face)
		obs_data_set_string(font, "face", face);

	const char *style = obs_data_get_string(source->font_settings, "style");
	if (style && *style)
		obs_data_set_string(font, "style", style);

	int64_t size = obs_data_get_int(source->font_settings, "size");
	if (size <= 0)
		size = kDefaultFontSize;
	size *= static_cast<int64_t>(kFontSizeScaleMultiplier);
	size = std::clamp<int64_t>(size, 1, 10000);
	obs_data_set_int(font, "size", size);

	const int64_t flags = obs_data_get_int(source->font_settings, "flags");
	obs_data_set_int(font, "flags", flags);

	return font;
}

void update_text_source_style(obs_source_t *text_source, const std::string &text, const nowplaylist_source *source)
{
	if (!text_source)
		return;

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.c_str());
	obs_data_t *scaled_font = create_scaled_font_settings(source);
	if (scaled_font) {
		obs_data_set_obj(settings, "font", scaled_font);
		obs_data_release(scaled_font);
	}
	obs_data_set_int(settings, "color", source->text_color);
	obs_data_set_int(settings, "opacity", 100);
	obs_data_set_bool(settings, "outline", source->outline_size > 0);
	obs_data_set_int(settings, "outline_size", std::max(source->outline_size, 0));
	obs_data_set_int(settings, "outline_color", source->outline_color);
	obs_data_set_int(settings, "outline_opacity", 100);
	obs_data_set_string(settings, "align", "center");
	obs_data_set_string(settings, "valign", "top");
	obs_source_update(text_source, settings);
	obs_data_release(settings);
}

void update_glow_source_style(obs_source_t *text_source, const std::string &text, const nowplaylist_source *source)
{
	if (!text_source)
		return;

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.c_str());
	obs_data_t *scaled_font = create_scaled_font_settings(source);
	if (scaled_font) {
		obs_data_set_obj(settings, "font", scaled_font);
		obs_data_release(scaled_font);
	}
	obs_data_set_int(settings, "color", source->glow_color);
	obs_data_set_int(settings, "opacity", kDefaultGlowOpacity);
	obs_data_set_bool(settings, "outline", true);
	obs_data_set_int(settings, "outline_size", std::max(source->outline_size, 0));
	obs_data_set_int(settings, "outline_color", source->glow_color);
	obs_data_set_int(settings, "outline_opacity", 100);
	obs_data_set_string(settings, "align", "center");
	obs_data_set_string(settings, "valign", "top");
	obs_source_update(text_source, settings);
	obs_data_release(settings);
}

void update_text_sources(nowplaylist_source *source)
{
	update_glow_source_style(source->artist_glow_source, source->current_artist, source);
	update_text_source_style(source->artist_source, source->current_artist, source);
	update_glow_source_style(source->title_glow_source, source->current_title, source);
	update_text_source_style(source->title_source, source->current_title, source);

	const bool artist_enabled = !source->current_is_video && !source->current_artist.empty();
	const bool title_enabled = !source->current_is_video && !source->current_title.empty();
	const bool artist_glow_enabled = artist_enabled && source->glow_size > 0;
	const bool title_glow_enabled = title_enabled && source->glow_size > 0;
	if (source->artist_glow_source)
		obs_source_set_enabled(source->artist_glow_source, artist_glow_enabled);
	if (source->artist_source)
		obs_source_set_enabled(source->artist_source, artist_enabled);
	if (source->title_glow_source)
		obs_source_set_enabled(source->title_glow_source, title_glow_enabled);
	if (source->title_source)
		obs_source_set_enabled(source->title_source, title_enabled);
}

void clear_visual_state(nowplaylist_source *source)
{
	source->current_is_video = false;
	source->current_artist.clear();
	source->current_title.clear();
	source->current_album_art_path.clear();
	update_text_sources(source);
	update_album_art_source(source);
}

void apply_track_visual_state(nowplaylist_source *source, const std::string &path_utf8)
{
	source->current_media_path = path_utf8;
	const fs::path media_path = utf8_to_path(path_utf8);
	const media_kind kind = classify_media_file(media_path);
	source->current_is_video = (kind == media_kind::video);

	if (source->current_is_video) {
		source->current_artist.clear();
		source->current_title.clear();
		source->current_album_art_path.clear();
		update_text_sources(source);
		update_album_art_source(source);
		return;
	}

	const std::string metadata_key = normalize_path_key(path_utf8);
	metadata_info metadata{};
	const auto saved_metadata_it = source->metadata_by_path.find(metadata_key);
	if (saved_metadata_it != source->metadata_by_path.end())
		metadata = saved_metadata_it->second;

	std::string album_art;
	scoped_com_init com_init;
	if (com_init.usable()) {
		const std::wstring shell_path = shell_path_wstring(media_path);
		if (metadata.artist.empty() || metadata.title.empty()) {
			const metadata_info imported = read_metadata_for_file(media_path);
			if (metadata.artist.empty())
				metadata.artist = imported.artist;
			if (metadata.title.empty())
				metadata.title = imported.title;
		}

		const std::string output_path = build_album_art_cache_path(path_utf8);
		if (!output_path.empty() &&
		    extract_album_art_png(shell_path, utf8_to_path(output_path).wstring())) {
			album_art = output_path;
		}
	}

	if (metadata.title.empty())
		metadata.title = path_to_utf8(media_path.stem());

	source->current_artist = metadata.artist;
	source->current_title = metadata.title;
	source->current_album_art_path = album_art;
	source->metadata_by_path[metadata_key] = metadata;
	update_text_sources(source);
	update_album_art_source(source);
}

std::optional<std::string> current_track_path(const nowplaylist_source *source)
{
	if (source->play_order.empty() || source->order_pos >= source->play_order.size())
		return std::nullopt;

	const size_t index = source->play_order[source->order_pos];
	if (index >= source->expanded_files.size())
		return std::nullopt;

	return source->expanded_files[index];
}

bool start_current_track(nowplaylist_source *source)
{
	const std::optional<std::string> current = current_track_path(source);
	if (!current || !source->media_source)
		return false;

	const enum obs_media_state previous_state = obs_source_media_get_state(source->media_source);
	source->skip_next_media_ended =
		(previous_state == OBS_MEDIA_STATE_PLAYING || previous_state == OBS_MEDIA_STATE_PAUSED ||
		 previous_state == OBS_MEDIA_STATE_OPENING || previous_state == OBS_MEDIA_STATE_BUFFERING);

	apply_track_visual_state(source, *current);

	obs_data_t *media_settings = obs_data_create();
	obs_data_set_bool(media_settings, "is_local_file", true);
	obs_data_set_string(media_settings, "local_file", current->c_str());
	obs_data_set_bool(media_settings, "looping", false);
	obs_data_set_bool(media_settings, "restart_on_activate", false);
	obs_data_set_bool(media_settings, "close_when_inactive", false);
	obs_data_set_bool(media_settings, "clear_on_media_end", false);
	obs_data_set_bool(media_settings, "hw_decode", true);
	obs_source_update(source->media_source, media_settings);
	obs_data_release(media_settings);

	obs_source_media_restart(source->media_source);
	return true;
}

bool go_to_next_track(nowplaylist_source *source)
{
	if (source->play_order.empty())
		return false;

	if (source->order_pos + 1 < source->play_order.size()) {
		++source->order_pos;
		return start_current_track(source);
	}

	if (!source->loop)
		return false;

	if (source->shuffle && source->play_order.size() > 1) {
		const size_t last_played = source->play_order[source->order_pos];
		std::shuffle(source->play_order.begin(), source->play_order.end(), source->rng);
		if (source->play_order.front() == last_played)
			std::swap(source->play_order.front(), source->play_order[1]);
	}

	source->order_pos = 0;
	return start_current_track(source);
}

bool go_to_previous_track(nowplaylist_source *source)
{
	if (source->play_order.empty())
		return false;

	if (source->order_pos > 0) {
		--source->order_pos;
		return start_current_track(source);
	}

	if (!source->loop)
		return false;

	source->order_pos = source->play_order.size() - 1;
	return start_current_track(source);
}

void render_source_fit_centered(obs_source_t *child, uint32_t target_width, uint32_t target_height)
{
	if (!child)
		return;

	const uint32_t child_width = obs_source_get_width(child);
	const uint32_t child_height = obs_source_get_height(child);
	if (!child_width || !child_height || !target_width || !target_height) {
		obs_source_video_render(child);
		return;
	}

	const float scale_x = static_cast<float>(target_width) / static_cast<float>(child_width);
	const float scale_y = static_cast<float>(target_height) / static_cast<float>(child_height);
	const float scale = std::min(scale_x, scale_y);
	const float render_width = static_cast<float>(child_width) * scale;
	const float render_height = static_cast<float>(child_height) * scale;
	const float offset_x = (static_cast<float>(target_width) - render_width) * 0.5f;
	const float offset_y = (static_cast<float>(target_height) - render_height) * 0.5f;

	gs_matrix_push();
	gs_matrix_translate3f(offset_x, offset_y, 0.0f);
	gs_matrix_scale3f(scale, scale, 1.0f);
	obs_source_video_render(child);
	gs_matrix_pop();
}

void render_source_at(obs_source_t *child, float x, float y)
{
	if (!child)
		return;

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	obs_source_video_render(child);
	gs_matrix_pop();
}

void render_diffuse_glow(obs_source_t *glow_source, float x, float y, int glow_size)
{
	if (!glow_source || glow_size <= 0)
		return;

	const int radius = std::clamp(glow_size, 1, 12);
	const int step = radius > 8 ? 2 : 1;

	for (int dy = -radius; dy <= radius; dy += step) {
		for (int dx = -radius; dx <= radius; dx += step) {
			if (dx == 0 && dy == 0)
				continue;

			const int dist_sq = dx * dx + dy * dy;
			if (dist_sq > radius * radius)
				continue;

			render_source_at(glow_source, x + static_cast<float>(dx), y + static_cast<float>(dy));
		}
	}

	render_source_at(glow_source, x, y);
}

void render_centered_text(nowplaylist_source *source, uint32_t target_width, uint32_t target_height)
{
	struct line_info {
		obs_source_t *glow_source;
		obs_source_t *text_source;
		uint32_t glow_width;
		uint32_t glow_height;
		uint32_t text_width;
		uint32_t text_height;
	};

	std::vector<line_info> lines;
	if (source->artist_source && !source->current_artist.empty()) {
		const uint32_t text_width = obs_source_get_width(source->artist_source);
		const uint32_t text_height = obs_source_get_height(source->artist_source);
		const uint32_t glow_width =
			source->artist_glow_source ? obs_source_get_width(source->artist_glow_source) : text_width;
		const uint32_t glow_height =
			source->artist_glow_source ? obs_source_get_height(source->artist_glow_source) : text_height;
		lines.push_back(
			{source->artist_glow_source, source->artist_source, glow_width, glow_height, text_width, text_height});
	}

	if (source->title_source && !source->current_title.empty()) {
		const uint32_t text_width = obs_source_get_width(source->title_source);
		const uint32_t text_height = obs_source_get_height(source->title_source);
		const uint32_t glow_width =
			source->title_glow_source ? obs_source_get_width(source->title_glow_source) : text_width;
		const uint32_t glow_height =
			source->title_glow_source ? obs_source_get_height(source->title_glow_source) : text_height;
		lines.push_back(
			{source->title_glow_source, source->title_source, glow_width, glow_height, text_width, text_height});
	}

	if (lines.empty())
		return;

	const int text_effect_size = std::max(source->outline_size, source->glow_size);
	const float gap = lines.size() > 1 ? std::max(kDefaultTitleGap, static_cast<float>(text_effect_size + 2)) : 0.0f;

	float total_height = 0.0f;
	for (const auto &line : lines)
		total_height += static_cast<float>(line.text_height);
	total_height += gap * static_cast<float>(lines.size() - 1);

	float y = (static_cast<float>(target_height) - total_height) * 0.5f + static_cast<float>(source->offset_y);
	for (const auto &line : lines) {
		const float text_x = (static_cast<float>(target_width) - static_cast<float>(line.text_width)) * 0.5f +
				     static_cast<float>(source->offset_x);
		const float text_y = y;
		const float glow_x =
			text_x + (static_cast<float>(line.text_width) - static_cast<float>(line.glow_width)) * 0.5f;
		const float glow_y =
			text_y + (static_cast<float>(line.text_height) - static_cast<float>(line.glow_height)) * 0.5f;
		if (source->glow_size > 0 && line.glow_source)
			render_diffuse_glow(line.glow_source, glow_x, glow_y, source->glow_size);
		render_source_at(line.text_source, text_x, text_y);
		y += static_cast<float>(line.text_height) + gap;
	}
}

void set_font_settings(nowplaylist_source *source, obs_data_t *settings)
{
	obs_data_t *font = obs_data_get_obj(settings, kFontSetting);
	if (!font)
		font = obs_data_create();

	const char *face = obs_data_get_string(font, "face");
	if (!face || !*face)
		obs_data_set_string(font, "face", "Segoe UI");

	const char *style = obs_data_get_string(font, "style");
	if (!style || !*style)
		obs_data_set_string(font, "style", "Regular");

	const int64_t size = obs_data_get_int(font, "size");
	if (size <= 0 || size > 1000)
		obs_data_set_int(font, "size", kDefaultFontSize);

	const int64_t flags = obs_data_get_int(font, "flags");
	if (flags < 0 || flags > 0xF)
		obs_data_set_int(font, "flags", 0);

	if (source->font_settings)
		obs_data_release(source->font_settings);

	source->font_settings = font;
}

void sync_playlist_and_state(nowplaylist_source *source)
{
	const std::string previous_path = source->current_media_path;
	source->expanded_files = expand_playlist(source->playlist_entries, source->recursive);
	build_play_order(source, previous_path);

	if (source->expanded_files.empty()) {
		source->current_media_path.clear();
		clear_visual_state(source);
		if (source->media_source)
			obs_source_media_stop(source->media_source);
		return;
	}

	if (source->current_media_path.empty()) {
		start_current_track(source);
		return;
	}

	apply_track_visual_state(source, source->current_media_path);
}

std::string make_child_name(const nowplaylist_source *source, const char *suffix)
{
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "%p", static_cast<const void *>(source));
	return std::string("nowplaylisting.") + suffix + "." + buffer;
}

const char *tr_text(const char *key, const char *fallback)
{
	const char *translated = nullptr;
	if (obs_module_get_string(key, &translated) && translated && *translated)
		return translated;
	return fallback;
}

void increment_child_showing(obs_source_t *child)
{
	if (child)
		obs_source_inc_showing(child);
}

void decrement_child_showing(obs_source_t *child)
{
	if (child)
		obs_source_dec_showing(child);
}

void increment_child_active(obs_source_t *child)
{
	if (child)
		obs_source_inc_active(child);
}

void decrement_child_active(obs_source_t *child)
{
	if (child)
		obs_source_dec_active(child);
}

void nowplaylist_show(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	increment_child_showing(source->media_source);
	increment_child_showing(source->album_art_source);
	increment_child_showing(source->artist_glow_source);
	increment_child_showing(source->artist_source);
	increment_child_showing(source->title_glow_source);
	increment_child_showing(source->title_source);
}

void nowplaylist_hide(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	decrement_child_showing(source->media_source);
	decrement_child_showing(source->album_art_source);
	decrement_child_showing(source->artist_glow_source);
	decrement_child_showing(source->artist_source);
	decrement_child_showing(source->title_glow_source);
	decrement_child_showing(source->title_source);
}

void nowplaylist_activate(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	increment_child_active(source->media_source);
	increment_child_active(source->album_art_source);
	increment_child_active(source->artist_glow_source);
	increment_child_active(source->artist_source);
	increment_child_active(source->title_glow_source);
	increment_child_active(source->title_source);
}

void nowplaylist_deactivate(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	decrement_child_active(source->media_source);
	decrement_child_active(source->album_art_source);
	decrement_child_active(source->artist_glow_source);
	decrement_child_active(source->artist_source);
	decrement_child_active(source->title_glow_source);
	decrement_child_active(source->title_source);
}

void on_child_media_started(void *param, calldata_t *data)
{
	UNUSED_PARAMETER(data);
	auto *source = static_cast<nowplaylist_source *>(param);
	source->skip_next_media_ended = false;
	obs_source_media_started(source->source);
}

void on_child_media_ended(void *param, calldata_t *data)
{
	UNUSED_PARAMETER(data);
	auto *source = static_cast<nowplaylist_source *>(param);
	if (source->skip_next_media_ended) {
		source->skip_next_media_ended = false;
		return;
	}

	if (!go_to_next_track(source))
		obs_source_media_ended(source->source);
}

void on_child_audio_capture(void *param, obs_source_t *child, const struct audio_data *audio_data, bool muted)
{
	UNUSED_PARAMETER(child);
	auto *source = static_cast<nowplaylist_source *>(param);
	if (!source || !source->source || !audio_data || !audio_data->frames || muted)
		return;

	struct obs_source_audio audio = {};
	audio.frames = static_cast<uint32_t>(audio_data->frames);
	audio.speakers = source->output_speakers;
	audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
	audio.samples_per_sec = source->output_sample_rate;
	audio.timestamp = audio_data->timestamp;

	for (size_t i = 0; i < MAX_AV_PLANES; ++i)
		audio.data[i] = reinterpret_cast<const uint8_t *>(audio_data->data[i]);

	obs_source_output_audio(source->source, &audio);
}

const char *nowplaylist_get_name(void *)
{
	return tr_text("NowPlaylisting.SourceName", "NowPlaylisting");
}

void *nowplaylist_create(obs_data_t *settings, obs_source_t *source_ref)
{
	auto *source = new nowplaylist_source;
	source->source = source_ref;

	struct obs_audio_info oai;
	if (obs_get_audio_info(&oai)) {
		source->output_sample_rate = oai.samples_per_sec;
		source->output_speakers = oai.speakers;
	}

	obs_data_t *media_settings = obs_data_create();
	obs_data_set_bool(media_settings, "is_local_file", true);
	obs_data_set_bool(media_settings, "looping", false);
	obs_data_set_bool(media_settings, "restart_on_activate", false);
	obs_data_set_bool(media_settings, "close_when_inactive", false);
	obs_data_set_bool(media_settings, "clear_on_media_end", false);
	obs_data_set_bool(media_settings, "hw_decode", true);
	source->media_source = obs_source_create_private("ffmpeg_source", make_child_name(source, "media").c_str(),
							 media_settings);
	obs_data_release(media_settings);

	obs_data_t *art_settings = obs_data_create();
	obs_data_set_string(art_settings, "file", "");
	source->album_art_source =
		obs_source_create_private("image_source", make_child_name(source, "album_art").c_str(), art_settings);
	obs_data_release(art_settings);

	obs_data_t *artist_glow_settings = obs_data_create();
	obs_data_set_string(artist_glow_settings, "text", "");
	obs_data_set_string(artist_glow_settings, "align", "center");
	obs_data_set_string(artist_glow_settings, "valign", "top");
	source->artist_glow_source =
		obs_source_create_private("text_gdiplus", make_child_name(source, "artist_glow").c_str(),
					 artist_glow_settings);
	obs_data_release(artist_glow_settings);

	obs_data_t *artist_text_settings = obs_data_create();
	obs_data_set_string(artist_text_settings, "text", "");
	obs_data_set_string(artist_text_settings, "align", "center");
	obs_data_set_string(artist_text_settings, "valign", "top");
	source->artist_source = obs_source_create_private("text_gdiplus", make_child_name(source, "artist").c_str(),
							 artist_text_settings);
	obs_data_release(artist_text_settings);

	obs_data_t *title_glow_settings = obs_data_create();
	obs_data_set_string(title_glow_settings, "text", "");
	obs_data_set_string(title_glow_settings, "align", "center");
	obs_data_set_string(title_glow_settings, "valign", "top");
	source->title_glow_source =
		obs_source_create_private("text_gdiplus", make_child_name(source, "title_glow").c_str(),
					 title_glow_settings);
	obs_data_release(title_glow_settings);

	obs_data_t *title_text_settings = obs_data_create();
	obs_data_set_string(title_text_settings, "text", "");
	obs_data_set_string(title_text_settings, "align", "center");
	obs_data_set_string(title_text_settings, "valign", "top");
	source->title_source = obs_source_create_private("text_gdiplus", make_child_name(source, "title").c_str(),
							title_text_settings);
	obs_data_release(title_text_settings);

	if (source->media_source) {
		signal_handler_t *handler = obs_source_get_signal_handler(source->media_source);
		if (handler) {
			signal_handler_connect(handler, "media_started", on_child_media_started, source);
			signal_handler_connect(handler, "media_ended", on_child_media_ended, source);
		}
		obs_source_add_audio_capture_callback(source->media_source, on_child_audio_capture, source);
	}

	if (settings)
		nowplaylist_update(source, settings);

	return source;
}

void nowplaylist_destroy(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);

	if (source->media_source) {
		obs_source_remove_audio_capture_callback(source->media_source, on_child_audio_capture, source);
		signal_handler_t *handler = obs_source_get_signal_handler(source->media_source);
		if (handler) {
			signal_handler_disconnect(handler, "media_started", on_child_media_started, source);
			signal_handler_disconnect(handler, "media_ended", on_child_media_ended, source);
		}
	}

	if (obs_source_active(source->source))
		nowplaylist_deactivate(source);
	if (obs_source_showing(source->source))
		nowplaylist_hide(source);

	if (source->media_source)
		obs_source_release(source->media_source);
	if (source->album_art_source)
		obs_source_release(source->album_art_source);
	if (source->artist_glow_source)
		obs_source_release(source->artist_glow_source);
	if (source->artist_source)
		obs_source_release(source->artist_source);
	if (source->title_glow_source)
		obs_source_release(source->title_glow_source);
	if (source->title_source)
		obs_source_release(source->title_source);
	if (source->font_settings)
		obs_data_release(source->font_settings);

	delete source;
}

uint32_t nowplaylist_get_width(void *data)
{
	UNUSED_PARAMETER(data);
	return kDefaultCanvasSize;
}

uint32_t nowplaylist_get_height(void *data)
{
	UNUSED_PARAMETER(data);
	return kDefaultCanvasSize;
}

void nowplaylist_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	auto *source = static_cast<nowplaylist_source *>(data);

	const uint32_t target_width = nowplaylist_get_width(data);
	const uint32_t target_height = nowplaylist_get_height(data);

	if (source->current_is_video) {
		render_source_fit_centered(source->media_source, target_width, target_height);
		return;
	}

	bool drew_background = false;
	if (source->album_art_source && obs_source_enabled(source->album_art_source)) {
		render_source_fit_centered(source->album_art_source, target_width, target_height);
		drew_background = true;
	}

	if (!drew_background)
		render_source_fit_centered(source->media_source, target_width, target_height);

	render_centered_text(source, target_width, target_height);
}

void nowplaylist_enum_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	if (source->media_source)
		enum_callback(source->source, source->media_source, param);
	if (source->album_art_source)
		enum_callback(source->source, source->album_art_source, param);
	if (source->artist_glow_source)
		enum_callback(source->source, source->artist_glow_source, param);
	if (source->artist_source)
		enum_callback(source->source, source->artist_source, param);
	if (source->title_glow_source)
		enum_callback(source->source, source->title_glow_source, param);
	if (source->title_source)
		enum_callback(source->source, source->title_source, param);
}

void nowplaylist_update(void *data, obs_data_t *settings)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	source->recursive = obs_data_get_bool(settings, kRecursiveSetting);
	source->shuffle = obs_data_get_bool(settings, kShuffleSetting);
	source->loop = obs_data_get_bool(settings, kLoopSetting);
	source->text_color = static_cast<uint32_t>(obs_data_get_int(settings, kTextColorSetting));
	source->outline_color = static_cast<uint32_t>(obs_data_get_int(settings, kOutlineColorSetting));
	source->outline_size = static_cast<int>(obs_data_get_int(settings, kOutlineSizeSetting));
	source->glow_color = static_cast<uint32_t>(obs_data_get_int(settings, kGlowColorSetting));
	source->glow_size = static_cast<int>(obs_data_get_int(settings, kGlowSizeSetting));
	source->offset_x = static_cast<int>(obs_data_get_int(settings, kOffsetXSetting));
	source->offset_y = static_cast<int>(obs_data_get_int(settings, kOffsetYSetting));
	source->playlist_items = read_and_normalize_playlist_items(settings, source->recursive);
	rebuild_playlist_runtime_cache(source);

	set_font_settings(source, settings);
	sync_playlist_and_state(source);
}

void nowplaylist_media_play_pause(void *data, bool pause)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	if (source->media_source)
		obs_source_media_play_pause(source->media_source, pause);
}

void nowplaylist_media_restart(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	if (!source->media_source)
		return;

	if (source->current_media_path.empty()) {
		start_current_track(source);
		return;
	}

	obs_source_media_restart(source->media_source);
}

void nowplaylist_media_stop(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	if (source->media_source)
		obs_source_media_stop(source->media_source);
}

void nowplaylist_media_next(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	if (!go_to_next_track(source) && source->media_source)
		obs_source_media_stop(source->media_source);
}

void nowplaylist_media_previous(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	if (!go_to_previous_track(source) && source->media_source)
		obs_source_media_restart(source->media_source);
}

int64_t nowplaylist_media_get_duration(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	return source->media_source ? obs_source_media_get_duration(source->media_source) : 0;
}

int64_t nowplaylist_media_get_time(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	return source->media_source ? obs_source_media_get_time(source->media_source) : 0;
}

void nowplaylist_media_set_time(void *data, int64_t milliseconds)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	if (source->media_source)
		obs_source_media_set_time(source->media_source, milliseconds);
}

enum obs_media_state nowplaylist_media_get_state(void *data)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	return source->media_source ? obs_source_media_get_state(source->media_source) : OBS_MEDIA_STATE_NONE;
}

obs_properties_t *nowplaylist_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *playlist = obs_properties_add_editable_list(
		props, kPlaylistSetting, tr_text("NowPlaylisting.Playlist", "Playlist"), OBS_EDITABLE_LIST_TYPE_FILES,
		kMediaFileFilter, nullptr);
	obs_property_set_long_description(
		playlist, tr_text("NowPlaylisting.PlaylistHint",
				  "Use Add -> Add Files or Add -> Add Folder to build the playlist."));

	obs_properties_add_bool(props, kRecursiveSetting, tr_text("NowPlaylisting.Recursive", "Scan folders recursively"));
	obs_properties_add_bool(props, kShuffleSetting, tr_text("NowPlaylisting.Shuffle", "Shuffle"));
	obs_properties_add_bool(props, kLoopSetting, tr_text("NowPlaylisting.Loop", "Loop"));

	obs_properties_add_font(props, kFontSetting, tr_text("NowPlaylisting.Font", "Text Font"));
	obs_properties_add_color(props, kTextColorSetting, tr_text("NowPlaylisting.TextColor", "Text Color"));
	obs_properties_add_color(props, kOutlineColorSetting, tr_text("NowPlaylisting.OutlineColor", "Outline Color"));
	obs_properties_add_int_slider(props, kOutlineSizeSetting, tr_text("NowPlaylisting.OutlineSize", "Outline Size"),
				      0, 20, 1);
	obs_properties_add_color(props, kGlowColorSetting, tr_text("NowPlaylisting.ShadowColor", "Shadow Color"));
	obs_properties_add_int_slider(props, kGlowSizeSetting, tr_text("NowPlaylisting.ShadowSize", "Shadow Size"), 0, 20,
				      1);
	obs_properties_add_int(props, kOffsetXSetting, tr_text("NowPlaylisting.OffsetX", "Text Offset X"), -4096, 4096,
			       1);
	obs_properties_add_int(props, kOffsetYSetting, tr_text("NowPlaylisting.OffsetY", "Text Offset Y"), -4096, 4096,
			       1);

	return props;
}

void nowplaylist_defaults(obs_data_t *settings)
{
	obs_data_array_t *playlist = obs_data_array_create();
	obs_data_set_default_array(settings, kPlaylistSetting, playlist);
	obs_data_array_release(playlist);

	obs_data_set_default_bool(settings, kRecursiveSetting, false);
	obs_data_set_default_bool(settings, kShuffleSetting, false);
	obs_data_set_default_bool(settings, kLoopSetting, false);
	obs_data_set_default_int(settings, kTextColorSetting, kDefaultTextColor);
	obs_data_set_default_int(settings, kOutlineColorSetting, kDefaultOutlineColor);
	obs_data_set_default_int(settings, kOutlineSizeSetting, kDefaultOutlineSize);
	obs_data_set_default_int(settings, kGlowColorSetting, kDefaultGlowColor);
	obs_data_set_default_int(settings, kGlowSizeSetting, kDefaultGlowSize);
	obs_data_set_default_int(settings, kOffsetXSetting, kDefaultOffset);
	obs_data_set_default_int(settings, kOffsetYSetting, kDefaultOffset);

	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", "Segoe UI");
	obs_data_set_string(font, "style", "Regular");
	obs_data_set_int(font, "size", kDefaultFontSize);
	obs_data_set_int(font, "flags", 0);
	obs_data_set_default_obj(settings, kFontSetting, font);
	obs_data_release(font);
}
} // namespace

void register_nowplaylisting_source()
{
	obs_source_info info = {};
	info.id = kSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags =
		OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_CONTROLLABLE_MEDIA |
		OBS_SOURCE_SRGB;
	info.get_name = nowplaylist_get_name;
	info.create = nowplaylist_create;
	info.destroy = nowplaylist_destroy;
	info.get_width = nowplaylist_get_width;
	info.get_height = nowplaylist_get_height;
	info.video_render = nowplaylist_video_render;
	info.activate = nowplaylist_activate;
	info.deactivate = nowplaylist_deactivate;
	info.show = nowplaylist_show;
	info.hide = nowplaylist_hide;
	info.get_defaults = nowplaylist_defaults;
	info.get_properties = nowplaylist_properties;
	info.update = nowplaylist_update;
	info.media_play_pause = nowplaylist_media_play_pause;
	info.media_restart = nowplaylist_media_restart;
	info.media_stop = nowplaylist_media_stop;
	info.media_next = nowplaylist_media_next;
	info.media_previous = nowplaylist_media_previous;
	info.media_get_duration = nowplaylist_media_get_duration;
	info.media_get_time = nowplaylist_media_get_time;
	info.media_set_time = nowplaylist_media_set_time;
	info.media_get_state = nowplaylist_media_get_state;
	info.icon_type = OBS_ICON_TYPE_MEDIA;

	obs_register_source(&info);
}

void nowplaylist_shutdown()
{
	if (g_gdiplus_initialized) {
		GdiplusShutdown(g_gdiplus_token);
		g_gdiplus_initialized = false;
		g_png_encoder_cached = false;
		g_gdiplus_token = 0;
	}
}
