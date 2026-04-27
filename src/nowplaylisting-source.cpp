#include "nowplaylisting-source.hpp"

#include <obs-module.h>
#include <util/platform.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
constexpr const char *kPlaylistSelectedSetting = "playlist_selected";
constexpr const char *kPlaylistArtistEditSetting = "playlist_artist_edit";
constexpr const char *kPlaylistTitleEditSetting = "playlist_title_edit";
constexpr const char *kPlaylistShowTagsEditSetting = "playlist_show_tags_edit";
constexpr const char *kPlaylistSaveRecordButton = "playlist_save_record";
constexpr const char *kPlaylistMoveUpButton = "playlist_move_up";
constexpr const char *kPlaylistMoveDownButton = "playlist_move_down";
constexpr const char *kPlaylistExpandedViewSetting = "playlist_expanded_view";
constexpr const char *kPlaylistExpandedToggleButton = "playlist_expanded_toggle";
constexpr const char *kPlaylistPreviewTextSetting = "playlist_preview_text";
constexpr const char *kSavedPlaylistSelectSetting = "saved_playlist_select";
constexpr const char *kSavedPlaylistNameSetting = "saved_playlist_name";
constexpr const char *kSavedPlaylistNewButton = "saved_playlist_new";
constexpr const char *kSavedPlaylistSaveButton = "saved_playlist_save";
constexpr const char *kSavedPlaylistRenameButton = "saved_playlist_rename";
constexpr const char *kSavedPlaylistExportPathSetting = "saved_playlist_export_path";
constexpr const char *kSavedPlaylistExportButton = "saved_playlist_export";
constexpr const char *kShuffleSetting = "shuffle";
constexpr const char *kLoopSetting = "loop";
constexpr const char *kExportTagsEnabledSetting = "save_current_tags_to_files";
constexpr const char *kExportArtistPathSetting = "artist_output_file";
constexpr const char *kExportTitlePathSetting = "title_output_file";
constexpr const char *kBounceIntensitySetting = "bounce_intensity";
constexpr const char *kShakeIntensitySetting = "shake_intensity";
constexpr const char *kMediaBrightnessSetting = "media_brightness";
constexpr const char *kMediaContrastSetting = "media_contrast";
constexpr const char *kMediaSaturationSetting = "media_saturation";
constexpr const char *kMediaGlowColorSetting = "media_glow_color";
constexpr const char *kMediaGlowSizeSetting = "media_glow_size";
constexpr const char *kMediaGlowIntensitySetting = "media_glow_intensity";
constexpr const char *kMediaVignetteStrengthSetting = "media_vignette_strength";
constexpr const char *kMediaVignetteRoundnessSetting = "media_vignette_roundness";
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
constexpr int kDefaultBounceIntensity = 0;
constexpr int kDefaultShakeIntensity = 0;
constexpr int kDefaultMediaSaturation = 100;
constexpr uint32_t kDefaultMediaGlowColor = 0xFFFFFF;
constexpr int kDefaultMediaVignetteRoundness = 100;
constexpr size_t kDefaultPlaylistPreviewRows = 8;
constexpr size_t kExpandedPlaylistPreviewRows = 18;
constexpr const char *kSavedPlaylistsFile = "saved-playlists.json";
constexpr const char *kSavedPlaylistsRootKey = "playlists";
constexpr const char *kSavedPlaylistNameKey = "name";
constexpr const char *kSavedPlaylistItemsKey = "items";
constexpr const char *kMediaFileFilter =
	"Media Files (*.mp3 *.wav *.aiff *.aif *.mp4 *.mpg *.mpeg *.mkv *.avi);;All Files (*.*)";
constexpr const char *kTextFileFilter = "Text Files (*.txt);;All Files (*.*)";
constexpr const char *kEmbeddedMediaEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;

uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float2 texel_size;
uniform float glow_size;
uniform float glow_intensity;
uniform float4 glow_color;
uniform float vignette_strength;
uniform float vignette_roundness;

sampler_state textureSampler {
	Filter = Linear;
	AddressU = Clamp;
	AddressV = Clamp;
};

struct VertData {
	float4 pos : POSITION;
	float2 uv : TEXCOORD0;
};

VertData VSDefault(VertData vert_in)
{
	VertData vert_out;
	vert_out.pos = mul(float4(vert_in.pos.xyz, 1.0), ViewProj);
	vert_out.uv = vert_in.uv;
	return vert_out;
}

float sample_alpha(float2 uv)
{
	return image.Sample(textureSampler, uv).a;
}

float4 PSNowPlaylistingMedia(VertData vert_in) : TARGET
{
	float4 src = image.Sample(textureSampler, vert_in.uv);
	float alpha = src.a;
	float3 rgb = (alpha > 0.0001) ? (src.rgb / alpha) : float3(0.0, 0.0, 0.0);

	rgb = (rgb - 0.5.xxx) * (1.0 + contrast) + 0.5.xxx;
	rgb += brightness.xxx;

	float luma = dot(rgb, float3(0.2126, 0.7152, 0.0722));
	rgb = lerp(luma.xxx, rgb, max(saturation, 0.0));

	if (glow_intensity > 0.0001 && glow_size > 0.0001) {
		float2 radius = texel_size * glow_size;
		float alpha_sum = 0.0;
		alpha_sum += sample_alpha(vert_in.uv + float2(radius.x, 0.0));
		alpha_sum += sample_alpha(vert_in.uv + float2(-radius.x, 0.0));
		alpha_sum += sample_alpha(vert_in.uv + float2(0.0, radius.y));
		alpha_sum += sample_alpha(vert_in.uv + float2(0.0, -radius.y));
		alpha_sum += sample_alpha(vert_in.uv + float2(radius.x, radius.y));
		alpha_sum += sample_alpha(vert_in.uv + float2(radius.x, -radius.y));
		alpha_sum += sample_alpha(vert_in.uv + float2(-radius.x, radius.y));
		alpha_sum += sample_alpha(vert_in.uv + float2(-radius.x, -radius.y));
		float avg_alpha = alpha_sum / 8.0;
		float edge = saturate(avg_alpha - alpha);
		rgb += glow_color.rgb * edge * glow_intensity;
	}

	if (vignette_strength > 0.0001) {
		float2 p = vert_in.uv * 2.0 - 1.0;
		float roundness = max(vignette_roundness, 0.01);
		p.x /= roundness;
		float distance_from_center = length(p);
		float vignette_mask = smoothstep(0.35, 1.0, distance_from_center);
		rgb *= (1.0 - vignette_strength * vignette_mask);
	}

	rgb = saturate(rgb);
	return float4(rgb * alpha, alpha);
}

technique Draw
{
	pass
	{
		vertex_shader = VSDefault(vert_in);
		pixel_shader = PSNowPlaylistingMedia(vert_in);
	}
}
)";

enum class media_kind { unsupported, audio, video };

struct metadata_info {
	std::string artist;
	std::string title;
	bool show_tags = true;
};

struct playlist_item {
	std::string path;
	std::string artist;
	std::string title;
	bool show_tags = true;
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
	gs_texrender_t *media_texrender = nullptr;
	gs_effect_t *media_effect = nullptr;
	gs_eparam_t *media_param_image = nullptr;
	gs_eparam_t *media_param_brightness = nullptr;
	gs_eparam_t *media_param_contrast = nullptr;
	gs_eparam_t *media_param_saturation = nullptr;
	gs_eparam_t *media_param_texel_size = nullptr;
	gs_eparam_t *media_param_glow_size = nullptr;
	gs_eparam_t *media_param_glow_intensity = nullptr;
	gs_eparam_t *media_param_glow_color = nullptr;
	gs_eparam_t *media_param_vignette_strength = nullptr;
	gs_eparam_t *media_param_vignette_roundness = nullptr;
	bool media_effect_load_attempted = false;
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
	std::string current_artist_for_export;
	std::string current_title_for_export;
	bool current_show_tags = true;

	uint32_t text_color = kDefaultTextColor;
	uint32_t outline_color = kDefaultOutlineColor;
	int outline_size = kDefaultOutlineSize;
	uint32_t glow_color = kDefaultGlowColor;
	int glow_size = kDefaultGlowSize;
	int offset_x = kDefaultOffset;
	int offset_y = kDefaultOffset;
	bool export_tags_to_files = false;
	std::string artist_output_file;
	std::string title_output_file;
	int bounce_intensity = kDefaultBounceIntensity;
	int shake_intensity = kDefaultShakeIntensity;
	std::atomic<float> audio_peak{0.0f};
	float smoothed_peak = 0.0f;
	float media_brightness = 0.0f;
	float media_contrast = 0.0f;
	float media_saturation = 1.0f;
	uint32_t media_glow_color = kDefaultMediaGlowColor;
	float media_glow_size = 0.0f;
	float media_glow_intensity = 0.0f;
	float media_vignette_strength = 0.0f;
	float media_vignette_roundness = 1.0f;

	std::mt19937 rng{std::random_device{}()};
};

void nowplaylist_update(void *data, obs_data_t *settings);
std::vector<playlist_item> read_and_normalize_playlist_items(obs_data_t *settings, bool recursive_dirs);
std::string playlist_item_display_text(const playlist_item &item, size_t index);

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

std::string module_text_path(const char *filename)
{
	if (!filename || !*filename)
		return {};

	char *raw = obs_module_config_path(filename);
	if (!raw)
		return {};

	std::string out(raw);
	bfree(raw);
	return out;
}

obs_data_t *load_saved_playlists_store()
{
	const std::string store_path = module_text_path(kSavedPlaylistsFile);
	obs_data_t *store = nullptr;
	if (!store_path.empty())
		store = obs_data_create_from_json_file(store_path.c_str());
	if (!store)
		store = obs_data_create();

	obs_data_array_t *playlists = obs_data_get_array(store, kSavedPlaylistsRootKey);
	if (!playlists) {
		playlists = obs_data_array_create();
		obs_data_set_array(store, kSavedPlaylistsRootKey, playlists);
	}
	obs_data_array_release(playlists);
	return store;
}

bool save_saved_playlists_store(obs_data_t *store)
{
	if (!store)
		return false;

	const std::string store_path = module_text_path(kSavedPlaylistsFile);
	if (store_path.empty())
		return false;

	const fs::path output_path = utf8_to_path(store_path);
	std::error_code ec;
	const fs::path parent = output_path.parent_path();
	if (!parent.empty())
		fs::create_directories(parent, ec);

	return obs_data_save_json_pretty_safe(store, store_path.c_str(), "tmp", "bak");
}

int64_t find_saved_playlist_index(obs_data_array_t *playlists, const std::string &name)
{
	if (!playlists || name.empty())
		return -1;

	const size_t count = obs_data_array_count(playlists);
	for (size_t i = 0; i < count; ++i) {
		obs_data_t *entry = obs_data_array_item(playlists, i);
		if (!entry)
			continue;

		const char *stored_name_raw = obs_data_get_string(entry, kSavedPlaylistNameKey);
		const std::string stored_name = stored_name_raw ? trim(stored_name_raw) : std::string();
		obs_data_release(entry);
		if (stored_name == name)
			return static_cast<int64_t>(i);
	}

	return -1;
}

std::vector<std::string> read_saved_playlist_names(obs_data_t *store)
{
	std::vector<std::string> names;
	if (!store)
		return names;

	obs_data_array_t *playlists = obs_data_get_array(store, kSavedPlaylistsRootKey);
	if (!playlists)
		return names;

	const size_t count = obs_data_array_count(playlists);
	names.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		obs_data_t *entry = obs_data_array_item(playlists, i);
		if (!entry)
			continue;

		const char *name_raw = obs_data_get_string(entry, kSavedPlaylistNameKey);
		const std::string name = name_raw ? trim(name_raw) : std::string();
		if (!name.empty())
			names.emplace_back(name);
		obs_data_release(entry);
	}

	obs_data_array_release(playlists);
	return names;
}

std::string make_unique_saved_playlist_name(obs_data_t *store, const std::string &desired_name)
{
	std::string base_name = trim(desired_name);
	if (base_name.empty())
		base_name = "Playlist";

	obs_data_array_t *playlists = obs_data_get_array(store, kSavedPlaylistsRootKey);
	if (!playlists)
		return base_name;

	if (find_saved_playlist_index(playlists, base_name) < 0) {
		obs_data_array_release(playlists);
		return base_name;
	}

	for (int suffix = 2; suffix < 10000; ++suffix) {
		const std::string candidate = base_name + " (" + std::to_string(suffix) + ")";
		if (find_saved_playlist_index(playlists, candidate) < 0) {
			obs_data_array_release(playlists);
			return candidate;
		}
	}

	obs_data_array_release(playlists);
	return base_name;
}

bool write_current_playlist_to_store(obs_data_t *store, const std::string &name, obs_data_array_t *items,
				     bool create_if_missing)
{
	if (!store || name.empty())
		return false;

	obs_data_array_t *playlists = obs_data_get_array(store, kSavedPlaylistsRootKey);
	if (!playlists)
		return false;

	int64_t index = find_saved_playlist_index(playlists, name);
	if (index < 0) {
		if (!create_if_missing) {
			obs_data_array_release(playlists);
			return false;
		}

		obs_data_t *entry = obs_data_create();
		obs_data_set_string(entry, kSavedPlaylistNameKey, name.c_str());
		if (items)
			obs_data_set_array(entry, kSavedPlaylistItemsKey, items);
		else {
			obs_data_array_t *empty_items = obs_data_array_create();
			obs_data_set_array(entry, kSavedPlaylistItemsKey, empty_items);
			obs_data_array_release(empty_items);
		}
		obs_data_array_push_back(playlists, entry);
		obs_data_release(entry);
	} else {
		obs_data_t *entry = obs_data_array_item(playlists, static_cast<size_t>(index));
		if (entry) {
			obs_data_set_string(entry, kSavedPlaylistNameKey, name.c_str());
			if (items)
				obs_data_set_array(entry, kSavedPlaylistItemsKey, items);
			else {
				obs_data_array_t *empty_items = obs_data_array_create();
				obs_data_set_array(entry, kSavedPlaylistItemsKey, empty_items);
				obs_data_array_release(empty_items);
			}
			obs_data_release(entry);
		}
	}

	obs_data_set_array(store, kSavedPlaylistsRootKey, playlists);
	obs_data_array_release(playlists);
	return true;
}

bool load_saved_playlist_into_settings(obs_data_t *settings, const std::string &name)
{
	if (!settings || name.empty())
		return false;

	obs_data_t *store = load_saved_playlists_store();
	if (!store)
		return false;

	obs_data_array_t *playlists = obs_data_get_array(store, kSavedPlaylistsRootKey);
	if (!playlists) {
		obs_data_release(store);
		return false;
	}

	bool loaded = false;
	const int64_t index = find_saved_playlist_index(playlists, name);
	if (index >= 0) {
		obs_data_t *entry = obs_data_array_item(playlists, static_cast<size_t>(index));
		if (entry) {
			obs_data_array_t *items = obs_data_get_array(entry, kSavedPlaylistItemsKey);
			if (items) {
				obs_data_set_array(settings, kPlaylistSetting, items);
				obs_data_array_release(items);
				loaded = true;
			}
			obs_data_release(entry);
		}
	}

	obs_data_array_release(playlists);
	obs_data_release(store);
	return loaded;
}

bool rename_saved_playlist(obs_data_t *store, const std::string &old_name, const std::string &new_name)
{
	if (!store || old_name.empty() || new_name.empty() || old_name == new_name)
		return false;

	obs_data_array_t *playlists = obs_data_get_array(store, kSavedPlaylistsRootKey);
	if (!playlists)
		return false;

	const int64_t old_index = find_saved_playlist_index(playlists, old_name);
	if (old_index < 0 || find_saved_playlist_index(playlists, new_name) >= 0) {
		obs_data_array_release(playlists);
		return false;
	}

	obs_data_t *entry = obs_data_array_item(playlists, static_cast<size_t>(old_index));
	if (!entry) {
		obs_data_array_release(playlists);
		return false;
	}

	obs_data_set_string(entry, kSavedPlaylistNameKey, new_name.c_str());
	obs_data_release(entry);

	obs_data_set_array(store, kSavedPlaylistsRootKey, playlists);
	obs_data_array_release(playlists);
	return true;
}

std::string build_playlist_preview_text(obs_data_t *settings)
{
	if (!settings)
		return {};

	const bool recursive_dirs = obs_data_get_bool(settings, kRecursiveSetting);
	const std::vector<playlist_item> items = read_and_normalize_playlist_items(settings, recursive_dirs);
	if (items.empty())
		return "(playlist is empty)";

	const bool expanded = obs_data_get_bool(settings, kPlaylistExpandedViewSetting);
	const size_t row_limit = expanded ? kExpandedPlaylistPreviewRows : kDefaultPlaylistPreviewRows;
	const size_t render_count = std::min(row_limit, items.size());

	std::string text;
	for (size_t i = 0; i < render_count; ++i) {
		text += playlist_item_display_text(items[i], i);
		text += '\n';
	}

	if (items.size() > render_count) {
		text += "... +" + std::to_string(items.size() - render_count) + " more";
	}

	return text;
}

void write_utf8_text_file(const std::string &path_utf8, const std::string &text_utf8)
{
	if (path_utf8.empty())
		return;

	const fs::path output_path = utf8_to_path(path_utf8);
	std::error_code ec;
	const fs::path parent = output_path.parent_path();
	if (!parent.empty())
		fs::create_directories(parent, ec);

	std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		blog(LOG_WARNING, "[nowplaylisting] failed to open text output file: '%s'", path_utf8.c_str());
		return;
	}

	out.write(text_utf8.data(), static_cast<std::streamsize>(text_utf8.size()));
}

bool read_show_tags_flag(obs_data_t *item)
{
	if (!item)
		return true;
	if (!obs_data_has_user_value(item, "show_tags"))
		return true;
	return obs_data_get_bool(item, "show_tags");
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
		const bool has_show_tags = obs_data_has_user_value(item, "show_tags");
		const bool show_tags = read_show_tags_flag(item);
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
						  const std::string &seed_title, const bool seed_show_tags) {
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
			normalized_item.show_tags = seed_show_tags;
			items.emplace_back(normalized_item);

			obs_data_t *normalized_data = obs_data_create();
			obs_data_set_string(normalized_data, "value", normalized_item.path.c_str());
			obs_data_set_string(normalized_data, "artist", normalized_item.artist.c_str());
			obs_data_set_string(normalized_data, "title", normalized_item.title.c_str());
			obs_data_set_bool(normalized_data, "show_tags", normalized_item.show_tags);
			obs_data_array_push_back(normalized, normalized_data);
			obs_data_release(normalized_data);
		};

		if (fs::is_regular_file(entry_path, ec)) {
			if (classify_media_file(entry_path) == media_kind::unsupported) {
				changed = true;
				obs_data_release(item);
				continue;
			}

			append_normalized_item(entry_path, artist, title, show_tags);
			if (path != (value_raw ? std::string(value_raw) : std::string()) || artist != existing_artist ||
			    title != existing_title || !has_show_tags) {
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
				append_normalized_item(utf8_to_path(expanded_path), std::string(), std::string(), true);

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
		metadata.show_tags = item.show_tags;
		source->metadata_by_path[normalize_path_key(item.path)] = metadata;
	}
}

std::string playlist_item_display_text(const playlist_item &item, size_t index)
{
	std::string filename = path_to_utf8(utf8_to_path(item.path).filename());
	if (filename.empty())
		filename = item.path;

	const std::string artist = item.artist.empty() ? "-" : item.artist;
	const std::string title = item.title.empty() ? "-" : item.title;
	const char *tag_state = item.show_tags ? "On" : "Off";

	char index_buf[32];
	snprintf(index_buf, sizeof(index_buf), "%zu", index + 1);

	return std::string(index_buf) + ". " + filename + " | Artist: " + artist + " | Title: " + title +
	       " | Tags: " + tag_state;
}

void set_playlist_editor_defaults(obs_data_t *settings)
{
	if (!settings)
		return;

	obs_data_set_int(settings, kPlaylistSelectedSetting, -1);
	obs_data_set_string(settings, kPlaylistArtistEditSetting, "");
	obs_data_set_string(settings, kPlaylistTitleEditSetting, "");
	obs_data_set_bool(settings, kPlaylistShowTagsEditSetting, true);
}

int64_t clamp_playlist_selected_index(obs_data_t *settings, size_t count)
{
	if (!settings || count == 0) {
		set_playlist_editor_defaults(settings);
		return -1;
	}

	int64_t selected = obs_data_get_int(settings, kPlaylistSelectedSetting);
	if (selected < 0)
		selected = 0;
	if (selected >= static_cast<int64_t>(count))
		selected = static_cast<int64_t>(count - 1);

	obs_data_set_int(settings, kPlaylistSelectedSetting, selected);
	return selected;
}

bool get_playlist_item_at(obs_data_t *settings, size_t index, playlist_item *out_item)
{
	if (!settings || !out_item)
		return false;

	obs_data_array_t *playlist = obs_data_get_array(settings, kPlaylistSetting);
	if (!playlist)
		return false;

	bool found = false;
	const size_t count = obs_data_array_count(playlist);
	if (index < count) {
		obs_data_t *item = obs_data_array_item(playlist, index);
		if (item) {
			const char *value_raw = obs_data_get_string(item, "value");
			const char *artist_raw = obs_data_get_string(item, "artist");
			const char *title_raw = obs_data_get_string(item, "title");
			out_item->path = value_raw ? trim(value_raw) : std::string();
			out_item->artist = artist_raw ? trim(artist_raw) : std::string();
			out_item->title = title_raw ? trim(title_raw) : std::string();
			out_item->show_tags = read_show_tags_flag(item);
			obs_data_release(item);
			found = true;
		}
	}

	obs_data_array_release(playlist);
	return found;
}

void sync_editor_from_selected_item(obs_data_t *settings)
{
	if (!settings)
		return;

	obs_data_array_t *playlist = obs_data_get_array(settings, kPlaylistSetting);
	if (!playlist) {
		set_playlist_editor_defaults(settings);
		return;
	}

	const size_t count = obs_data_array_count(playlist);
	obs_data_array_release(playlist);

	const int64_t selected = clamp_playlist_selected_index(settings, count);
	if (selected < 0)
		return;

	playlist_item selected_item{};
	if (!get_playlist_item_at(settings, static_cast<size_t>(selected), &selected_item))
		return;

	obs_data_set_string(settings, kPlaylistArtistEditSetting, selected_item.artist.c_str());
	obs_data_set_string(settings, kPlaylistTitleEditSetting, selected_item.title.c_str());
	obs_data_set_bool(settings, kPlaylistShowTagsEditSetting, selected_item.show_tags);
}

void sync_playlist_preview_text(obs_data_t *settings)
{
	if (!settings)
		return;

	const std::string preview = build_playlist_preview_text(settings);
	obs_data_set_string(settings, kPlaylistPreviewTextSetting, preview.c_str());
}

void rebuild_saved_playlists_property(obs_properties_t *props, obs_data_t *settings)
{
	if (!props || !settings)
		return;

	obs_property_t *saved_playlist_prop = obs_properties_get(props, kSavedPlaylistSelectSetting);
	if (!saved_playlist_prop)
		return;

	obs_data_t *store = load_saved_playlists_store();
	std::vector<std::string> names = read_saved_playlist_names(store);
	obs_data_release(store);

	obs_property_list_clear(saved_playlist_prop);
	obs_property_list_add_string(saved_playlist_prop, "(none)", "");
	for (const std::string &name : names)
		obs_property_list_add_string(saved_playlist_prop, name.c_str(), name.c_str());

	const char *selected_raw = obs_data_get_string(settings, kSavedPlaylistSelectSetting);
	std::string selected = selected_raw ? trim(selected_raw) : std::string();
	if (!selected.empty()) {
		const bool exists = std::find(names.begin(), names.end(), selected) != names.end();
		if (!exists)
			selected.clear();
	}

	obs_data_set_string(settings, kSavedPlaylistSelectSetting, selected.c_str());
	if (!selected.empty())
		obs_data_set_string(settings, kSavedPlaylistNameSetting, selected.c_str());
}

void rebuild_playlist_selector_property(obs_properties_t *props, obs_data_t *settings)
{
	if (!props || !settings)
		return;

	obs_property_t *selector = obs_properties_get(props, kPlaylistSelectedSetting);
	if (!selector)
		return;

	const bool recursive_dirs = obs_data_get_bool(settings, kRecursiveSetting);
	const std::vector<playlist_item> items = read_and_normalize_playlist_items(settings, recursive_dirs);

	obs_property_list_clear(selector);
	for (size_t i = 0; i < items.size(); ++i) {
		const std::string label = playlist_item_display_text(items[i], i);
		obs_property_list_add_int(selector, label.c_str(), static_cast<long long>(i));
	}

	if (items.empty()) {
		set_playlist_editor_defaults(settings);
		sync_playlist_preview_text(settings);
		return;
	}

	clamp_playlist_selected_index(settings, items.size());
	sync_editor_from_selected_item(settings);
	sync_playlist_preview_text(settings);
}

bool update_selected_playlist_item_metadata(obs_data_t *settings)
{
	if (!settings)
		return false;

	obs_data_array_t *playlist = obs_data_get_array(settings, kPlaylistSetting);
	if (!playlist)
		return false;

	const size_t count = obs_data_array_count(playlist);
	const int64_t selected = clamp_playlist_selected_index(settings, count);
	if (selected < 0 || selected >= static_cast<int64_t>(count)) {
		obs_data_array_release(playlist);
		return false;
	}

	obs_data_t *item = obs_data_array_item(playlist, static_cast<size_t>(selected));
	if (!item) {
		obs_data_array_release(playlist);
		return false;
	}

	const char *artist_raw = obs_data_get_string(settings, kPlaylistArtistEditSetting);
	const char *title_raw = obs_data_get_string(settings, kPlaylistTitleEditSetting);
	const std::string artist = artist_raw ? trim(artist_raw) : std::string();
	const std::string title = title_raw ? trim(title_raw) : std::string();
	const bool show_tags = obs_data_get_bool(settings, kPlaylistShowTagsEditSetting);

	obs_data_set_string(item, "artist", artist.c_str());
	obs_data_set_string(item, "title", title.c_str());
	obs_data_set_bool(item, "show_tags", show_tags);
	obs_data_release(item);

	obs_data_set_array(settings, kPlaylistSetting, playlist);
	obs_data_array_release(playlist);
	return true;
}

bool move_selected_playlist_item(obs_data_t *settings, int direction, int64_t *out_selected)
{
	if (!settings)
		return false;

	obs_data_array_t *playlist = obs_data_get_array(settings, kPlaylistSetting);
	if (!playlist)
		return false;

	const size_t count = obs_data_array_count(playlist);
	const int64_t selected = clamp_playlist_selected_index(settings, count);
	if (selected < 0 || selected >= static_cast<int64_t>(count)) {
		obs_data_array_release(playlist);
		return false;
	}

	const int64_t destination = selected + static_cast<int64_t>(direction);
	if (destination < 0 || destination >= static_cast<int64_t>(count)) {
		obs_data_array_release(playlist);
		return false;
	}

	obs_data_t *item = obs_data_array_item(playlist, static_cast<size_t>(selected));
	if (!item) {
		obs_data_array_release(playlist);
		return false;
	}

	obs_data_array_erase(playlist, static_cast<size_t>(selected));
	obs_data_array_insert(playlist, static_cast<size_t>(destination), item);
	obs_data_release(item);

	obs_data_set_array(settings, kPlaylistSetting, playlist);
	obs_data_array_release(playlist);

	obs_data_set_int(settings, kPlaylistSelectedSetting, destination);
	sync_editor_from_selected_item(settings);
	if (out_selected)
		*out_selected = destination;
	return true;
}

void sync_current_tag_output_files(nowplaylist_source *source)
{
	if (!source || !source->export_tags_to_files)
		return;

	write_utf8_text_file(source->artist_output_file, source->current_artist_for_export);
	write_utf8_text_file(source->title_output_file, source->current_title_for_export);
}

float normalize_slider(int value, int min_value, int max_value)
{
	if (max_value <= min_value)
		return 0.0f;

	const int clamped = std::clamp(value, min_value, max_value);
	return static_cast<float>(clamped - min_value) / static_cast<float>(max_value - min_value);
}

float normalize_signed_slider(int value, int magnitude)
{
	if (magnitude <= 0)
		return 0.0f;
	const int clamped = std::clamp(value, -magnitude, magnitude);
	return static_cast<float>(clamped) / static_cast<float>(magnitude);
}

float map_media_brightness(int slider_value)
{
	const float normalized = normalize_signed_slider(slider_value, 100);
	return normalized * 0.45f;
}

float map_media_contrast(int slider_value)
{
	const float normalized = normalize_signed_slider(slider_value, 100);
	const float magnitude = std::pow(std::fabs(normalized), 1.35f);
	return std::copysign(magnitude * 0.85f, normalized);
}

float map_media_saturation(int slider_value)
{
	const int clamped = std::clamp(slider_value, 0, 300);
	if (clamped <= 100)
		return static_cast<float>(clamped) / 100.0f;

	const float t = static_cast<float>(clamped - 100) / 200.0f;
	return 1.0f + std::pow(t, 1.2f) * 1.2f;
}

float map_media_glow_size(int slider_value)
{
	const float normalized = normalize_slider(slider_value, 0, 48);
	return std::pow(normalized, 1.6f) * 28.0f;
}

float map_media_glow_intensity(int slider_value)
{
	const float normalized = normalize_slider(slider_value, 0, 200);
	return std::pow(normalized, 1.35f) * 1.6f;
}

float map_media_vignette_strength(int slider_value)
{
	const float normalized = normalize_slider(slider_value, 0, 100);
	return std::pow(normalized, 1.5f) * 0.95f;
}

float map_media_vignette_roundness(int slider_value)
{
	const float normalized = normalize_slider(slider_value, 25, 200);
	return 0.65f + normalized * 1.55f;
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
	obs_data_set_int(settings, "opacity", 100);
	obs_data_set_bool(settings, "outline", true);
	obs_data_set_int(settings, "outline_size", std::max(source->glow_size, 1));
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
	source->current_artist_for_export.clear();
	source->current_title_for_export.clear();
	source->current_album_art_path.clear();
	source->current_show_tags = true;
	update_text_sources(source);
	update_album_art_source(source);
	sync_current_tag_output_files(source);
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
		source->current_artist_for_export.clear();
		source->current_title_for_export.clear();
		source->current_album_art_path.clear();
		source->current_show_tags = true;
		update_text_sources(source);
		update_album_art_source(source);
		sync_current_tag_output_files(source);
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

	source->current_show_tags = metadata.show_tags;
	source->current_artist_for_export = metadata.artist;
	source->current_title_for_export = metadata.title;
	source->current_artist = metadata.show_tags ? metadata.artist : std::string();
	source->current_title = metadata.show_tags ? metadata.title : std::string();
	source->current_album_art_path = album_art;
	source->metadata_by_path[metadata_key] = metadata;
	update_text_sources(source);
	update_album_art_source(source);
	sync_current_tag_output_files(source);
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

struct motion_state {
	float scale = 1.0f;
	float shake_x = 0.0f;
	float shake_y = 0.0f;
};

motion_state compute_audio_motion(nowplaylist_source *source)
{
	motion_state motion{};
	if (!source)
		return motion;

	const float peak = std::clamp(source->audio_peak.load(std::memory_order_relaxed), 0.0f, 1.5f);
	source->smoothed_peak = source->smoothed_peak * 0.85f + peak * 0.15f;
	const float transient = std::max(0.0f, peak - source->smoothed_peak);

	if (source->bounce_intensity > 0) {
		const float intensity = static_cast<float>(source->bounce_intensity) / 100.0f;
		motion.scale += transient * intensity * 0.32f;
	}

	if (source->shake_intensity > 0) {
		const float intensity = static_cast<float>(source->shake_intensity) / 100.0f;
		const float shake_pixels = std::clamp(peak, 0.0f, 1.0f) * intensity * 18.0f;
		if (shake_pixels > 0.01f) {
			std::uniform_real_distribution<float> random_offset(-1.0f, 1.0f);
			motion.shake_x = random_offset(source->rng) * shake_pixels;
			motion.shake_y = random_offset(source->rng) * shake_pixels;
		}
	}

	return motion;
}

void render_media_block(nowplaylist_source *source, uint32_t target_width, uint32_t target_height)
{
	if (!source)
		return;

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
}

void ensure_media_render_resources(nowplaylist_source *source)
{
	if (!source)
		return;

	if (!source->media_texrender)
		source->media_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);

	if (source->media_effect_load_attempted)
		return;

	source->media_effect_load_attempted = true;
	char *effect_path = obs_module_file("effects/nowplaylisting_media.effect");
	if (!effect_path) {
		blog(LOG_WARNING, "[nowplaylisting] media effect path unavailable");
		return;
	}

	char *error_string = nullptr;
	source->media_effect = gs_effect_create_from_file(effect_path, &error_string);
	if (!source->media_effect) {
		blog(LOG_WARNING, "[nowplaylisting] failed to load media effect '%s'%s%s", effect_path,
		     error_string ? ": " : "", error_string ? error_string : "");
		if (error_string) {
			bfree(error_string);
			error_string = nullptr;
		}

		source->media_effect =
			gs_effect_create(kEmbeddedMediaEffect, "nowplaylisting_media_embedded.effect", &error_string);
		if (!source->media_effect) {
			blog(LOG_WARNING, "[nowplaylisting] failed to load embedded media effect%s%s",
			     error_string ? ": " : "", error_string ? error_string : "");
		}
	} else {
		source->media_param_image = gs_effect_get_param_by_name(source->media_effect, "image");
		source->media_param_brightness = gs_effect_get_param_by_name(source->media_effect, "brightness");
		source->media_param_contrast = gs_effect_get_param_by_name(source->media_effect, "contrast");
		source->media_param_saturation = gs_effect_get_param_by_name(source->media_effect, "saturation");
		source->media_param_texel_size = gs_effect_get_param_by_name(source->media_effect, "texel_size");
		source->media_param_glow_size = gs_effect_get_param_by_name(source->media_effect, "glow_size");
		source->media_param_glow_intensity = gs_effect_get_param_by_name(source->media_effect, "glow_intensity");
		source->media_param_glow_color = gs_effect_get_param_by_name(source->media_effect, "glow_color");
		source->media_param_vignette_strength =
			gs_effect_get_param_by_name(source->media_effect, "vignette_strength");
		source->media_param_vignette_roundness =
			gs_effect_get_param_by_name(source->media_effect, "vignette_roundness");
	}

	if (source->media_effect && !source->media_param_image) {
		source->media_param_image = gs_effect_get_param_by_name(source->media_effect, "image");
		source->media_param_brightness = gs_effect_get_param_by_name(source->media_effect, "brightness");
		source->media_param_contrast = gs_effect_get_param_by_name(source->media_effect, "contrast");
		source->media_param_saturation = gs_effect_get_param_by_name(source->media_effect, "saturation");
		source->media_param_texel_size = gs_effect_get_param_by_name(source->media_effect, "texel_size");
		source->media_param_glow_size = gs_effect_get_param_by_name(source->media_effect, "glow_size");
		source->media_param_glow_intensity = gs_effect_get_param_by_name(source->media_effect, "glow_intensity");
		source->media_param_glow_color = gs_effect_get_param_by_name(source->media_effect, "glow_color");
		source->media_param_vignette_strength =
			gs_effect_get_param_by_name(source->media_effect, "vignette_strength");
		source->media_param_vignette_roundness =
			gs_effect_get_param_by_name(source->media_effect, "vignette_roundness");
	}

	if (error_string)
		bfree(error_string);
	bfree(effect_path);
}

vec4 media_glow_vec4(uint32_t obs_color)
{
	const float r = static_cast<float>(obs_color & 0xFF) / 255.0f;
	const float g = static_cast<float>((obs_color >> 8) & 0xFF) / 255.0f;
	const float b = static_cast<float>((obs_color >> 16) & 0xFF) / 255.0f;
	return vec4{r, g, b, 1.0f};
}

bool render_media_to_texture(nowplaylist_source *source, uint32_t target_width, uint32_t target_height,
			     const motion_state &motion)
{
	if (!source || !source->media_texrender || !target_width || !target_height)
		return false;

	gs_texrender_reset(source->media_texrender);
	if (!gs_texrender_begin(source->media_texrender, target_width, target_height))
		return false;

	vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, static_cast<float>(target_width), 0.0f, static_cast<float>(target_height), -100.0f, 100.0f);

	const bool apply_motion = (motion.scale > 1.001f || std::fabs(motion.shake_x) > 0.01f ||
				   std::fabs(motion.shake_y) > 0.01f);
	if (apply_motion) {
		const float center_x = static_cast<float>(target_width) * 0.5f;
		const float center_y = static_cast<float>(target_height) * 0.5f;
		gs_matrix_push();
		gs_matrix_translate3f(center_x + motion.shake_x, center_y + motion.shake_y, 0.0f);
		gs_matrix_scale3f(motion.scale, motion.scale, 1.0f);
		gs_matrix_translate3f(-center_x, -center_y, 0.0f);
		render_media_block(source, target_width, target_height);
		gs_matrix_pop();
	} else {
		render_media_block(source, target_width, target_height);
	}

	gs_texrender_end(source->media_texrender);
	return true;
}

void render_media_texture_with_effect(nowplaylist_source *source, uint32_t target_width, uint32_t target_height)
{
	if (!source || !source->media_texrender)
		return;

	gs_texture_t *texture = gs_texrender_get_texture(source->media_texrender);
	if (!texture)
		return;

	if (!source->media_effect || !source->media_param_image) {
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
		if (!effect || !image)
			return;
		gs_effect_set_texture_srgb(image, texture);
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(texture, 0, target_width, target_height);
		return;
	}

	const vec4 glow_color = media_glow_vec4(source->media_glow_color);
	const vec2 texel_size = vec2{1.0f / static_cast<float>(std::max<uint32_t>(target_width, 1)),
				     1.0f / static_cast<float>(std::max<uint32_t>(target_height, 1))};
	gs_effect_set_texture_srgb(source->media_param_image, texture);
	gs_effect_set_float(source->media_param_brightness, source->media_brightness);
	gs_effect_set_float(source->media_param_contrast, source->media_contrast);
	gs_effect_set_float(source->media_param_saturation, source->media_saturation);
	gs_effect_set_vec2(source->media_param_texel_size, &texel_size);
	gs_effect_set_float(source->media_param_glow_size, source->media_glow_size);
	gs_effect_set_float(source->media_param_glow_intensity, source->media_glow_intensity);
	gs_effect_set_vec4(source->media_param_glow_color, &glow_color);
	gs_effect_set_float(source->media_param_vignette_strength, source->media_vignette_strength);
	gs_effect_set_float(source->media_param_vignette_roundness, source->media_vignette_roundness);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(source->media_effect, "Draw"))
		gs_draw_sprite(texture, 0, target_width, target_height);
	gs_blend_state_pop();
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

bool on_playlist_property_modified(void *, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
	rebuild_playlist_selector_property(props, settings);
	rebuild_saved_playlists_property(props, settings);
	return true;
}

bool on_playlist_selected_modified(void *, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	sync_editor_from_selected_item(settings);
	sync_playlist_preview_text(settings);
	return true;
}

bool on_saved_playlist_selected_modified(void *, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);

	const char *selected_raw = obs_data_get_string(settings, kSavedPlaylistSelectSetting);
	const std::string selected = selected_raw ? trim(selected_raw) : std::string();
	if (selected.empty())
		return false;

	if (load_saved_playlist_into_settings(settings, selected)) {
		obs_data_set_string(settings, kSavedPlaylistNameSetting, selected.c_str());
		rebuild_playlist_selector_property(props, settings);
		return true;
	}

	return false;
}

bool move_playlist_button_clicked(obs_properties_t *props, void *data, int direction)
{
	auto *source = static_cast<nowplaylist_source *>(data);
	if (!source || !source->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(source->source);
	if (!settings)
		return false;

	int64_t selected_after_move = -1;
	const bool moved = move_selected_playlist_item(settings, direction, &selected_after_move);
	if (moved) {
		obs_source_update(source->source, settings);
		rebuild_playlist_selector_property(props, settings);
		rebuild_saved_playlists_property(props, settings);
		obs_data_set_int(settings, kPlaylistSelectedSetting, selected_after_move);
		sync_editor_from_selected_item(settings);
	}

	obs_data_release(settings);
	return moved;
}

bool on_playlist_move_up_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(property);
	return move_playlist_button_clicked(props, data, -1);
}

bool on_playlist_move_down_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(property);
	return move_playlist_button_clicked(props, data, 1);
}

bool on_playlist_save_record_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(property);

	auto *source = static_cast<nowplaylist_source *>(data);
	if (!source || !source->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(source->source);
	if (!settings)
		return false;

	const bool changed = update_selected_playlist_item_metadata(settings);
	if (changed) {
		obs_source_update(source->source, settings);
		rebuild_playlist_selector_property(props, settings);
		rebuild_saved_playlists_property(props, settings);
	}

	obs_data_release(settings);
	return changed;
}

bool on_playlist_expanded_toggle_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(property);

	auto *source = static_cast<nowplaylist_source *>(data);
	if (!source || !source->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(source->source);
	if (!settings)
		return false;

	const bool expanded = obs_data_get_bool(settings, kPlaylistExpandedViewSetting);
	obs_data_set_bool(settings, kPlaylistExpandedViewSetting, !expanded);
	sync_playlist_preview_text(settings);
	rebuild_playlist_selector_property(props, settings);
	rebuild_saved_playlists_property(props, settings);

	obs_data_release(settings);
	return true;
}

bool on_saved_playlist_new_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(property);

	auto *source = static_cast<nowplaylist_source *>(data);
	if (!source || !source->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(source->source);
	if (!settings)
		return false;

	const char *name_raw = obs_data_get_string(settings, kSavedPlaylistNameSetting);
	const std::string requested_name = name_raw ? trim(name_raw) : std::string();
	obs_data_t *store = load_saved_playlists_store();
	const std::string final_name = make_unique_saved_playlist_name(store, requested_name);

	obs_data_array_t *playlist = obs_data_get_array(settings, kPlaylistSetting);
	const bool changed = write_current_playlist_to_store(store, final_name, playlist, true);
	if (playlist)
		obs_data_array_release(playlist);

	if (changed && save_saved_playlists_store(store)) {
		obs_data_set_string(settings, kSavedPlaylistSelectSetting, final_name.c_str());
		obs_data_set_string(settings, kSavedPlaylistNameSetting, final_name.c_str());
		rebuild_saved_playlists_property(props, settings);
	}

	obs_data_release(store);
	obs_data_release(settings);
	return changed;
}

bool on_saved_playlist_save_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(property);

	auto *source = static_cast<nowplaylist_source *>(data);
	if (!source || !source->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(source->source);
	if (!settings)
		return false;

	const char *selected_raw = obs_data_get_string(settings, kSavedPlaylistSelectSetting);
	const char *name_raw = obs_data_get_string(settings, kSavedPlaylistNameSetting);
	std::string target_name = selected_raw ? trim(selected_raw) : std::string();
	if (target_name.empty())
		target_name = name_raw ? trim(name_raw) : std::string();

	if (target_name.empty()) {
		obs_data_release(settings);
		return false;
	}

	obs_data_t *store = load_saved_playlists_store();
	obs_data_array_t *playlist = obs_data_get_array(settings, kPlaylistSetting);
	const bool changed = write_current_playlist_to_store(store, target_name, playlist, true);
	if (playlist)
		obs_data_array_release(playlist);

	if (changed && save_saved_playlists_store(store)) {
		obs_data_set_string(settings, kSavedPlaylistSelectSetting, target_name.c_str());
		obs_data_set_string(settings, kSavedPlaylistNameSetting, target_name.c_str());
		rebuild_saved_playlists_property(props, settings);
	}

	obs_data_release(store);
	obs_data_release(settings);
	return changed;
}

bool on_saved_playlist_rename_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(property);

	auto *source = static_cast<nowplaylist_source *>(data);
	if (!source || !source->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(source->source);
	if (!settings)
		return false;

	const char *selected_raw = obs_data_get_string(settings, kSavedPlaylistSelectSetting);
	const char *name_raw = obs_data_get_string(settings, kSavedPlaylistNameSetting);
	const std::string old_name = selected_raw ? trim(selected_raw) : std::string();
	std::string new_name = name_raw ? trim(name_raw) : std::string();
	if (old_name.empty() || new_name.empty()) {
		obs_data_release(settings);
		return false;
	}
	if (new_name == old_name) {
		obs_data_release(settings);
		return false;
	}

	obs_data_t *store = load_saved_playlists_store();
	new_name = make_unique_saved_playlist_name(store, new_name);
	const bool renamed = rename_saved_playlist(store, old_name, new_name);
	if (renamed && save_saved_playlists_store(store)) {
		obs_data_set_string(settings, kSavedPlaylistSelectSetting, new_name.c_str());
		obs_data_set_string(settings, kSavedPlaylistNameSetting, new_name.c_str());
		rebuild_saved_playlists_property(props, settings);
	}

	obs_data_release(store);
	obs_data_release(settings);
	return renamed;
}

bool on_saved_playlist_export_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	auto *source = static_cast<nowplaylist_source *>(data);
	if (!source || !source->source)
		return false;

	obs_data_t *settings = obs_source_get_settings(source->source);
	if (!settings)
		return false;

	const char *export_path_raw = obs_data_get_string(settings, kSavedPlaylistExportPathSetting);
	std::string export_path = export_path_raw ? trim(export_path_raw) : std::string();
	if (export_path.empty())
		export_path = module_text_path("playlist-export.json");

	obs_data_t *export_root = obs_data_create();
	const char *selected_raw = obs_data_get_string(settings, kSavedPlaylistSelectSetting);
	const std::string selected_name = selected_raw ? trim(selected_raw) : std::string();
	if (!selected_name.empty())
		obs_data_set_string(export_root, kSavedPlaylistNameKey, selected_name.c_str());

	obs_data_array_t *playlist = obs_data_get_array(settings, kPlaylistSetting);
	if (playlist) {
		obs_data_set_array(export_root, kSavedPlaylistItemsKey, playlist);
		obs_data_array_release(playlist);
	}

	const bool exported = obs_data_save_json_pretty_safe(export_root, export_path.c_str(), "tmp", "bak");
	obs_data_release(export_root);
	obs_data_release(settings);
	return exported;
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

	float peak = 0.0f;
	const uint32_t channels = get_audio_channels(source->output_speakers);
	for (uint32_t channel = 0; channel < channels && channel < MAX_AV_PLANES; ++channel) {
		if (!audio_data->data[channel])
			continue;

		const float *samples = reinterpret_cast<const float *>(audio_data->data[channel]);
		for (size_t frame = 0; frame < audio_data->frames; ++frame)
			peak = std::max(peak, std::fabs(samples[frame]));
	}
	source->audio_peak.store(peak, std::memory_order_relaxed);

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
	static std::string display_name_with_version;
	if (display_name_with_version.empty()) {
		display_name_with_version = std::string(tr_text("NowPlaylisting.SourceName", "NowPlaylisting")) + " v" +
					    NOWPLAYLISTING_VERSION;
	}

	return display_name_with_version.c_str();
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

	obs_enter_graphics();
	if (source->media_effect)
		gs_effect_destroy(source->media_effect);
	if (source->media_texrender)
		gs_texrender_destroy(source->media_texrender);
	obs_leave_graphics();

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

	const motion_state motion = compute_audio_motion(source);
	ensure_media_render_resources(source);

	if (!render_media_to_texture(source, target_width, target_height, motion)) {
		render_media_block(source, target_width, target_height);
	} else {
		render_media_texture_with_effect(source, target_width, target_height);
	}

	if (!source->current_is_video)
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
	source->export_tags_to_files = obs_data_get_bool(settings, kExportTagsEnabledSetting);
	const char *artist_output_raw = obs_data_get_string(settings, kExportArtistPathSetting);
	const char *title_output_raw = obs_data_get_string(settings, kExportTitlePathSetting);
	source->artist_output_file = artist_output_raw ? trim(artist_output_raw) : std::string();
	source->title_output_file = title_output_raw ? trim(title_output_raw) : std::string();
	if (source->artist_output_file.empty())
		source->artist_output_file = module_text_path("current-artist.txt");
	if (source->title_output_file.empty())
		source->title_output_file = module_text_path("current-title.txt");
	source->bounce_intensity =
		std::clamp(static_cast<int>(obs_data_get_int(settings, kBounceIntensitySetting)), 0, 100);
	source->shake_intensity =
		std::clamp(static_cast<int>(obs_data_get_int(settings, kShakeIntensitySetting)), 0, 100);
	source->media_brightness =
		map_media_brightness(static_cast<int>(obs_data_get_int(settings, kMediaBrightnessSetting)));
	source->media_contrast =
		map_media_contrast(static_cast<int>(obs_data_get_int(settings, kMediaContrastSetting)));
	source->media_saturation =
		map_media_saturation(static_cast<int>(obs_data_get_int(settings, kMediaSaturationSetting)));
	source->media_glow_color = static_cast<uint32_t>(obs_data_get_int(settings, kMediaGlowColorSetting));
	source->media_glow_size =
		map_media_glow_size(static_cast<int>(obs_data_get_int(settings, kMediaGlowSizeSetting)));
	source->media_glow_intensity =
		map_media_glow_intensity(static_cast<int>(obs_data_get_int(settings, kMediaGlowIntensitySetting)));
	source->media_vignette_strength =
		map_media_vignette_strength(static_cast<int>(obs_data_get_int(settings, kMediaVignetteStrengthSetting)));
	source->media_vignette_roundness =
		map_media_vignette_roundness(static_cast<int>(obs_data_get_int(settings, kMediaVignetteRoundnessSetting)));

	source->playlist_items = read_and_normalize_playlist_items(settings, source->recursive);
	rebuild_playlist_runtime_cache(source);

	set_font_settings(source, settings);
	sync_playlist_and_state(source);
	update_text_sources(source);
	sync_current_tag_output_files(source);
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

obs_properties_t *nowplaylist_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	auto *source = static_cast<nowplaylist_source *>(data);
	obs_data_t *settings = nullptr;
	if (source && source->source)
		settings = obs_source_get_settings(source->source);

	const bool expanded_preview = settings && obs_data_get_bool(settings, kPlaylistExpandedViewSetting);

	obs_property_t *saved_playlist_select =
		obs_properties_add_list(props, kSavedPlaylistSelectSetting,
					tr_text("NowPlaylisting.SavedPlaylist", "Saved Playlist"),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback2(saved_playlist_select, on_saved_playlist_selected_modified, nullptr);
	obs_properties_add_text(props, kSavedPlaylistNameSetting,
				tr_text("NowPlaylisting.SavedPlaylistName", "Playlist Name"), OBS_TEXT_DEFAULT);
	obs_properties_add_button2(props, kSavedPlaylistNewButton, tr_text("NowPlaylisting.New", "New"),
				   on_saved_playlist_new_clicked, source);
	obs_properties_add_button2(props, kSavedPlaylistSaveButton, tr_text("NowPlaylisting.Save", "Save"),
				   on_saved_playlist_save_clicked, source);
	obs_properties_add_button2(props, kSavedPlaylistRenameButton, tr_text("NowPlaylisting.Rename", "Rename"),
				   on_saved_playlist_rename_clicked, source);
	obs_properties_add_path(props, kSavedPlaylistExportPathSetting,
				tr_text("NowPlaylisting.ExportPath", "Export Path"), OBS_PATH_FILE_SAVE,
				"JSON Files (*.json);;All Files (*.*)", nullptr);
	obs_properties_add_button2(props, kSavedPlaylistExportButton, tr_text("NowPlaylisting.Export", "Export"),
				   on_saved_playlist_export_clicked, source);

	obs_properties_add_button2(
		props, kPlaylistExpandedToggleButton,
		expanded_preview ? tr_text("NowPlaylisting.CollapseRows", "Collapse Extra Rows")
				  : tr_text("NowPlaylisting.ExpandRows", "Expand Extra Rows"),
		on_playlist_expanded_toggle_clicked, source);

	obs_property_t *playlist = obs_properties_add_editable_list(
		props, kPlaylistSetting, tr_text("NowPlaylisting.Playlist", "Playlist"), OBS_EDITABLE_LIST_TYPE_FILES,
		kMediaFileFilter, nullptr);
	obs_property_set_long_description(
		playlist, tr_text("NowPlaylisting.PlaylistHint",
				  "Use Add -> Add Files or Add -> Add Folder to build the playlist."));
	obs_property_set_modified_callback2(playlist, on_playlist_property_modified, nullptr);

	obs_properties_add_button2(props, kPlaylistMoveUpButton, tr_text("NowPlaylisting.MoveUp", "Move Up"),
				   on_playlist_move_up_clicked, source);
	obs_properties_add_button2(props, kPlaylistMoveDownButton, tr_text("NowPlaylisting.MoveDown", "Move Down"),
				   on_playlist_move_down_clicked, source);

	obs_property_t *playlist_selected = obs_properties_add_list(
		props, kPlaylistSelectedSetting, tr_text("NowPlaylisting.PlaylistSelected", "Selected Track"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_set_modified_callback2(playlist_selected, on_playlist_selected_modified, nullptr);

	obs_properties_add_text(props, kPlaylistArtistEditSetting,
				tr_text("NowPlaylisting.PlaylistArtist", "Artist"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, kPlaylistTitleEditSetting,
				tr_text("NowPlaylisting.PlaylistTitle", "Title"), OBS_TEXT_DEFAULT);
	obs_properties_add_bool(
		props, kPlaylistShowTagsEditSetting, tr_text("NowPlaylisting.PlaylistShowTags", "Show Tags"));
	obs_properties_add_button2(props, kPlaylistSaveRecordButton,
				   tr_text("NowPlaylisting.SaveTrack", "Save Selected Track"),
				   on_playlist_save_record_clicked, source);

	obs_property_t *playlist_preview = obs_properties_add_text(
		props, kPlaylistPreviewTextSetting, tr_text("NowPlaylisting.PlaylistPreview", "Playlist Preview"),
		OBS_TEXT_INFO);
	obs_property_text_set_info_type(playlist_preview, OBS_TEXT_INFO_NORMAL);
	obs_property_text_set_info_word_wrap(playlist_preview, false);

	obs_property_t *recursive = obs_properties_add_bool(
		props, kRecursiveSetting, tr_text("NowPlaylisting.Recursive", "Scan folders recursively"));
	obs_property_set_modified_callback2(recursive, on_playlist_property_modified, nullptr);
	obs_properties_add_bool(props, kShuffleSetting, tr_text("NowPlaylisting.Shuffle", "Shuffle"));
	obs_properties_add_bool(props, kLoopSetting, tr_text("NowPlaylisting.Loop", "Loop"));
	obs_properties_add_bool(props, kExportTagsEnabledSetting,
				tr_text("NowPlaylisting.ExportTagsToFiles", "Save Current Artist/Title To Text Files"));
	obs_properties_add_path(props, kExportArtistPathSetting,
				tr_text("NowPlaylisting.ArtistOutputFile", "Artist Output File"), OBS_PATH_FILE_SAVE,
				kTextFileFilter, nullptr);
	obs_properties_add_path(props, kExportTitlePathSetting,
				tr_text("NowPlaylisting.TitleOutputFile", "Title Output File"), OBS_PATH_FILE_SAVE,
				kTextFileFilter, nullptr);
	obs_properties_add_int_slider(props, kBounceIntensitySetting,
				      tr_text("NowPlaylisting.BounceIntensity", "Bounce Intensity"), 0, 100, 1);
	obs_properties_add_int_slider(props, kShakeIntensitySetting,
				      tr_text("NowPlaylisting.CameraShakeIntensity", "Camera Shake Intensity"), 0, 100,
				      1);
	obs_properties_add_int_slider(props, kMediaBrightnessSetting,
				      tr_text("NowPlaylisting.MediaBrightness", "Media Brightness"), -100, 100, 1);
	obs_properties_add_int_slider(props, kMediaContrastSetting,
				      tr_text("NowPlaylisting.MediaContrast", "Media Contrast"), -100, 100, 1);
	obs_properties_add_int_slider(props, kMediaSaturationSetting,
				      tr_text("NowPlaylisting.MediaSaturation", "Media Saturation"), 0, 300, 1);
	obs_properties_add_color(props, kMediaGlowColorSetting,
				 tr_text("NowPlaylisting.MediaGlowColor", "Media Glow Color"));
	obs_properties_add_int_slider(props, kMediaGlowSizeSetting,
				      tr_text("NowPlaylisting.MediaGlowSize", "Media Glow Size"), 0, 48, 1);
	obs_properties_add_int_slider(props, kMediaGlowIntensitySetting,
				      tr_text("NowPlaylisting.MediaGlowIntensity", "Media Glow Intensity"), 0, 200,
				      1);
	obs_properties_add_int_slider(props, kMediaVignetteStrengthSetting,
				      tr_text("NowPlaylisting.MediaVignetteStrength", "Media Vignette"), 0, 100, 1);
	obs_properties_add_int_slider(props, kMediaVignetteRoundnessSetting,
				      tr_text("NowPlaylisting.MediaVignetteRoundness", "Media Vignette Roundness"),
				      25, 200, 1);

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

	if (settings) {
		rebuild_saved_playlists_property(props, settings);
		rebuild_playlist_selector_property(props, settings);
		sync_playlist_preview_text(settings);
		obs_data_release(settings);
	}

	return props;
}

void nowplaylist_defaults(obs_data_t *settings)
{
	obs_data_array_t *playlist = obs_data_array_create();
	obs_data_set_default_array(settings, kPlaylistSetting, playlist);
	obs_data_array_release(playlist);

	obs_data_set_default_bool(settings, kRecursiveSetting, false);
	obs_data_set_default_int(settings, kPlaylistSelectedSetting, -1);
	obs_data_set_default_string(settings, kPlaylistArtistEditSetting, "");
	obs_data_set_default_string(settings, kPlaylistTitleEditSetting, "");
	obs_data_set_default_bool(settings, kPlaylistShowTagsEditSetting, true);
	obs_data_set_default_bool(settings, kPlaylistExpandedViewSetting, false);
	obs_data_set_default_string(settings, kPlaylistPreviewTextSetting, "(playlist is empty)");
	obs_data_set_default_string(settings, kSavedPlaylistSelectSetting, "");
	obs_data_set_default_string(settings, kSavedPlaylistNameSetting, "");
	const std::string default_playlist_export = module_text_path("playlist-export.json");
	if (!default_playlist_export.empty())
		obs_data_set_default_string(settings, kSavedPlaylistExportPathSetting, default_playlist_export.c_str());
	obs_data_set_default_bool(settings, kShuffleSetting, false);
	obs_data_set_default_bool(settings, kLoopSetting, false);
	obs_data_set_default_bool(settings, kExportTagsEnabledSetting, false);
	const std::string default_artist_output = module_text_path("current-artist.txt");
	const std::string default_title_output = module_text_path("current-title.txt");
	if (!default_artist_output.empty())
		obs_data_set_default_string(settings, kExportArtistPathSetting, default_artist_output.c_str());
	if (!default_title_output.empty())
		obs_data_set_default_string(settings, kExportTitlePathSetting, default_title_output.c_str());
	obs_data_set_default_int(settings, kBounceIntensitySetting, kDefaultBounceIntensity);
	obs_data_set_default_int(settings, kShakeIntensitySetting, kDefaultShakeIntensity);
	obs_data_set_default_int(settings, kMediaBrightnessSetting, 0);
	obs_data_set_default_int(settings, kMediaContrastSetting, 0);
	obs_data_set_default_int(settings, kMediaSaturationSetting, kDefaultMediaSaturation);
	obs_data_set_default_int(settings, kMediaGlowColorSetting, kDefaultMediaGlowColor);
	obs_data_set_default_int(settings, kMediaGlowSizeSetting, 0);
	obs_data_set_default_int(settings, kMediaGlowIntensitySetting, 0);
	obs_data_set_default_int(settings, kMediaVignetteStrengthSetting, 0);
	obs_data_set_default_int(settings, kMediaVignetteRoundnessSetting, kDefaultMediaVignetteRoundness);
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
