/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2022  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include "Frontend/FullscreenUI.h"
#include "Frontend/ImGuiManager.h"
#include "Frontend/ImGuiFullscreen.h"
#include "Frontend/INISettingsInterface.h"
#include "Frontend/GameList.h"
#include "IconsFontAwesome5.h"

#include "common/FileSystem.h"
#include "common/Console.h"
#include "common/Image.h"
#include "common/Path.h"
#include "common/SettingsInterface.h"
#include "common/SettingsWrapper.h"
#include "common/StringUtil.h"
#include "common/Timer.h"

#include "GS.h"
#include "Host.h"
#include "HostDisplay.h"
#include "HostSettings.h"
#include "ps2/BiosTools.h"
#include "VMManager.h"

#include "imgui.h"
#include "imgui_internal.h"
//#include "imgui_stdlib.h"

#include "fmt/core.h"

#include <array>
#include <bitset>
#include <thread>

static constexpr float LAYOUT_MAIN_MENU_BAR_SIZE = 20.0f; // Should be DPI scaled, not layout scaled!
static constexpr s32 MAX_SAVE_STATE_SLOTS = 10;

using ImGuiFullscreen::g_large_font;
using ImGuiFullscreen::g_layout_padding_left;
using ImGuiFullscreen::g_layout_padding_top;
using ImGuiFullscreen::g_medium_font;
using ImGuiFullscreen::LAYOUT_LARGE_FONT_SIZE;
using ImGuiFullscreen::LAYOUT_MEDIUM_FONT_SIZE;
using ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT;
using ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY;
using ImGuiFullscreen::LAYOUT_MENU_BUTTON_X_PADDING;
using ImGuiFullscreen::LAYOUT_MENU_BUTTON_Y_PADDING;
using ImGuiFullscreen::LAYOUT_SCREEN_HEIGHT;
using ImGuiFullscreen::LAYOUT_SCREEN_WIDTH;

using ImGuiFullscreen::ActiveButton;
using ImGuiFullscreen::BeginFullscreenColumns;
using ImGuiFullscreen::BeginFullscreenColumnWindow;
using ImGuiFullscreen::BeginFullscreenWindow;
using ImGuiFullscreen::BeginMenuButtons;
using ImGuiFullscreen::BeginNavBar;
using ImGuiFullscreen::CloseChoiceDialog;
using ImGuiFullscreen::CloseFileSelector;
using ImGuiFullscreen::DPIScale;
using ImGuiFullscreen::EndFullscreenColumns;
using ImGuiFullscreen::EndFullscreenColumnWindow;
using ImGuiFullscreen::EndFullscreenWindow;
using ImGuiFullscreen::EndMenuButtons;
using ImGuiFullscreen::EndNavBar;
using ImGuiFullscreen::EnumChoiceButton;
using ImGuiFullscreen::FloatingButton;
using ImGuiFullscreen::GetCachedTexture;
using ImGuiFullscreen::LayoutScale;
using ImGuiFullscreen::MenuButton;
using ImGuiFullscreen::MenuButtonFrame;
using ImGuiFullscreen::MenuButtonWithValue;
using ImGuiFullscreen::MenuHeading;
using ImGuiFullscreen::MenuHeadingButton;
using ImGuiFullscreen::MenuImageButton;
using ImGuiFullscreen::NavButton;
using ImGuiFullscreen::NavTitle;
using ImGuiFullscreen::OpenChoiceDialog;
using ImGuiFullscreen::OpenFileSelector;
using ImGuiFullscreen::RangeButton;
using ImGuiFullscreen::RightAlignNavButtons;
using ImGuiFullscreen::ThreeWayToggleButton;
using ImGuiFullscreen::ToggleButton;

namespace FullscreenUI
{
	//////////////////////////////////////////////////////////////////////////
	// Utility
	//////////////////////////////////////////////////////////////////////////
	static std::string TimeToPrintableString(time_t t);

	//////////////////////////////////////////////////////////////////////////
	// Main
	//////////////////////////////////////////////////////////////////////////
	static void UpdateForcedVsync(bool should_force);
	static void ClearImGuiFocus();
	static void UpdateGameDetails(std::string path, std::string serial, std::string title, u32 crc);
	static void PauseForMenuOpen();
	static bool WantsToCloseMenu();
	static void ClosePauseMenu();
	static void ReturnToMainWindow();
	static void DrawLandingWindow();
	static void DrawQuickMenu(MainWindowType type);
	static void DrawAboutWindow();
	static void OpenAboutWindow();

	static MainWindowType s_current_main_window = MainWindowType::None;
	//static std::bitset<static_cast<u32>(FrontendCommon::ControllerNavigationButton::Count)> s_nav_input_values{};
	static bool s_initialized = false;
	static bool s_tried_to_initialize = false;
	static bool s_debug_menu_enabled = false;
	static bool s_debug_menu_allowed = false;
	static bool s_quick_menu_was_open = false;
	static bool s_was_paused_on_quick_menu_open = false;
	static bool s_about_window_open = false;
	static u32 s_close_button_state = 0;
	static std::optional<u32> s_open_leaderboard_id;

	// local copies of the currently-running game
	static std::string s_current_game_title;
	static std::string s_current_game_subtitle;
	static std::string s_current_game_serial;
	static std::string s_current_game_path;
	static u32 s_current_game_crc;

	//////////////////////////////////////////////////////////////////////////
	// Resources
	//////////////////////////////////////////////////////////////////////////
	static bool LoadResources();
	static void DestroyResources();

	static std::unique_ptr<HostDisplayTexture> LoadTextureCallback(const char* path);
	static std::unique_ptr<HostDisplayTexture> LoadTexture(const char* path, bool from_package);

	static std::unique_ptr<HostDisplayTexture> s_app_icon_texture;
	static std::unique_ptr<HostDisplayTexture> s_placeholder_texture;
	static std::array<std::unique_ptr<HostDisplayTexture>, static_cast<u32>(GameList::Region::Count)> s_disc_region_textures;
	static std::array<std::unique_ptr<HostDisplayTexture>, static_cast<u32>(GameDatabaseSchema::Compatibility::Perfect) + 1>
		s_game_compatibility_textures;
	static std::unique_ptr<HostDisplayTexture> s_fallback_disc_texture;
	static std::unique_ptr<HostDisplayTexture> s_fallback_exe_texture;
	static std::unique_ptr<HostDisplayTexture> s_fallback_playlist_texture;

	//////////////////////////////////////////////////////////////////////////
	// Landing
	//////////////////////////////////////////////////////////////////////////
	static void SwitchToLanding();
	static ImGuiFullscreen::FileSelectorFilters GetDiscImageFilters();
	static void DoStartPath(const std::string& path, bool allow_resume);
	static void DoStartFile();
	static void DoStartBIOS();
	static void DoToggleFrameLimit();
	static void DoShutdown();
	static void DoReset();
	static void DoChangeDiscFromFile();
	static void DoChangeDisc();
	static void DoRequestExit();
	static void DoToggleFullscreen();

	//////////////////////////////////////////////////////////////////////////
	// Settings
	//////////////////////////////////////////////////////////////////////////

	enum class InputBindingType
	{
		None,
		Button,
		Axis,
		HalfAxis,
		Rumble
	};

	static constexpr double INPUT_BINDING_TIMEOUT_SECONDS = 5.0;

	static void SwitchToSettings();
	static void DrawSettingsWindow();
	static void DrawInterfaceSettingsPage();
	static void DrawGameListSettingsPage();
	static void DrawBIOSSettingsPage();
	static void DrawEmulationSettingsPage();
	static void DrawSystemSettingsPage();
	static void DrawGraphicsSettingsPage();
	static void DrawAudioSettingsPage();
	static void DrawMemoryCardSettingsPage();
	static void DrawControllerSettingsPage();
	static void DrawHotkeySettingsPage();

	static bool IsEditingGameSettings();
	static SettingsInterface* GetEditingSettingsInterface();

	static bool DrawToggleSetting(const char* title, const char* summary, const char* section, const char* key, bool default_value,
		bool enabled = true, float height = ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT, ImFont* font = g_large_font,
		ImFont* summary_font = g_medium_font);
	static void DrawIntListSetting(const char* title, const char* summary, const char* section, const char* key, int default_value,
		const char* const* options, size_t option_count, int option_offset = 0, bool enabled = true,
		float height = ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT, ImFont* font = g_large_font, ImFont* summary_font = g_medium_font);
	static void DrawStringListSetting(const char* title, const char* summary, const char* section, const char* key,
		const char* default_value, const char* const* options, const char* const* option_values, size_t option_count, bool enabled = true,
		float height = ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT, ImFont* font = g_large_font, ImFont* summary_font = g_medium_font);
	static void PopulateGameListDirectoryCache(SettingsInterface* si);
	static ImGuiFullscreen::ChoiceDialogOptions GetGameListDirectoryOptions(bool recursive_as_checked);
	static void BeginInputBinding(
		InputBindingType type, const std::string_view& section, const std::string_view& key, const std::string_view& display_name);
	static void EndInputBinding();
	static void ClearInputBinding(const char* section, const char* key);
	static void DrawInputBindingWindow();
	static void DrawInputBindingButton(
		InputBindingType type, const char* section, const char* name, const char* display_name, bool show_type = true);
	static void ClearInputBindingVariables();

	static SettingsPage s_settings_page = SettingsPage::InterfaceSettings;
	static std::vector<std::pair<std::string, bool>> s_game_list_directories_cache;
	static std::unique_ptr<INISettingsInterface> s_game_settings_interface;
	static bool s_settings_changed = false;
	static InputBindingType s_input_binding_type = InputBindingType::None;
	static std::string s_input_binding_section;
	static std::string s_input_binding_key;
	static std::string s_input_binding_display_name;
	static bool s_input_binding_keyboard_pressed;
	static Common::Timer s_input_binding_timer;

	//////////////////////////////////////////////////////////////////////////
	// Save State List
	//////////////////////////////////////////////////////////////////////////
	struct SaveStateListEntry
	{
		std::string title;
		std::string summary;
		std::string path;
		std::unique_ptr<HostDisplayTexture> preview_texture;
		s32 slot;
	};

	static void InitializePlaceholderSaveStateListEntry(SaveStateListEntry* li, s32 slot);
	static bool InitializeSaveStateListEntry(SaveStateListEntry* li, s32 slot);
	static void PopulateSaveStateListEntries();
	static void OpenSaveStateSelector(bool is_loading);
	static void CloseSaveStateSelector();
	static void DrawSaveStateSelector(bool is_loading, bool fullscreen);

	static std::vector<SaveStateListEntry> s_save_state_selector_slots;
	static bool s_save_state_selector_open = false;
	static bool s_save_state_selector_loading = true;

	//////////////////////////////////////////////////////////////////////////
	// Game List
	//////////////////////////////////////////////////////////////////////////
	static void DrawGameListWindow();
	static void SwitchToGameList();
	static void PopulateGameListEntryList();
	static HostDisplayTexture* GetTextureForGameListEntryType(GameList::EntryType type);
	static HostDisplayTexture* GetGameListCover(const GameList::Entry* entry);
	static HostDisplayTexture* GetCoverForCurrentGame();

	// Lazily populated cover images.
	static std::unordered_map<std::string, std::string> s_cover_image_map;
	static std::vector<const GameList::Entry*> s_game_list_sorted_entries;
} // namespace FullscreenUI

//////////////////////////////////////////////////////////////////////////
// Utility
//////////////////////////////////////////////////////////////////////////

std::string FullscreenUI::TimeToPrintableString(time_t t)
{
	struct tm lt = {};
#ifdef _MSC_VER
	localtime_s(&lt, &t);
#else
	localtime_r(&t, &lt);
#endif

	char buf[256];
	//std::strftime(buf, sizeof(buf), "%Y/%m/%d, %H:%M:%S", &lt);
	std::strftime(buf, sizeof(buf), "%c", &lt);
	return std::string(buf);
}

//////////////////////////////////////////////////////////////////////////
// Main
//////////////////////////////////////////////////////////////////////////

bool FullscreenUI::Initialize()
{
	if (s_initialized)
		return true;

	if (s_tried_to_initialize)
		return false;

	// TODO: We can get initialized in the middle of rendering (through MTGS).
	// This causes issues with vulkan and resource uploads. Reset+restore API.

	ImGuiFullscreen::UpdateLayoutScale();
	ImGuiFullscreen::SetLoadTextureFunction(LoadTextureCallback);

	if (!ImGuiManager::AddFullscreenFontsIfMissing() || !LoadResources())
	{
		ImGuiFullscreen::ClearState();
		s_tried_to_initialize = true;
		return false;
	}

	GetMTGS().SetRunIdle(true);
	s_initialized = true;

	if (VMManager::HasValidVM())
	{
		UpdateGameDetails(VMManager::GetDiscPath(), VMManager::GetGameSerial(), VMManager::GetGameName(), VMManager::GetGameCRC());
	}
	else
	{
		SwitchToLanding();
	}

	// force vsync on so we don't run at thousands of fps
	// Initialize is called on the GS thread, so we can access the display directly.
	UpdateForcedVsync(VMManager::GetState() != VMState::Running);

	return true;
}

bool FullscreenUI::IsInitialized()
{
	return s_initialized;
}

bool FullscreenUI::HasActiveWindow()
{
	return s_current_main_window != MainWindowType::None || s_save_state_selector_open || ImGuiFullscreen::IsChoiceDialogOpen() ||
		   ImGuiFullscreen::IsFileSelectorOpen();
}

void FullscreenUI::UpdateForcedVsync(bool should_force)
{
	// force vsync on so we don't run at thousands of fps
	const VsyncMode mode = EmuConfig.GetEffectiveVsyncMode();

	// toss it through regardless of the mode, because options can change it
	Host::GetHostDisplay()->SetVSync((should_force && mode == VsyncMode::Off) ? VsyncMode::On : mode);
}

void FullscreenUI::OnVMStarted()
{
	if (!IsInitialized())
		return;

	GetMTGS().RunOnGSThread([]() {
		if (!IsInitialized())
			return;

		s_current_main_window = MainWindowType::None;
		ClearImGuiFocus();
	});
}

void FullscreenUI::OnVMPaused()
{
	if (!IsInitialized())
		return;

	GetMTGS().RunOnGSThread([]() {
		if (!IsInitialized())
			return;

		UpdateForcedVsync(true);
	});
}

void FullscreenUI::OnVMResumed()
{
	if (!IsInitialized())
		return;

	GetMTGS().RunOnGSThread([]() {
		if (!IsInitialized())
			return;

		UpdateForcedVsync(false);
	});
}

void FullscreenUI::OnVMDestroyed()
{
	if (!IsInitialized())
		return;

	GetMTGS().RunOnGSThread([]() {
		if (!IsInitialized())
			return;

		s_quick_menu_was_open = false;
		SwitchToLanding();
		UpdateForcedVsync(true);
	});
}

void FullscreenUI::OnRunningGameChanged(std::string path, std::string serial, std::string title, u32 crc)
{
	if (!IsInitialized())
		return;

	GetMTGS().RunOnGSThread([path = std::move(path), serial = std::move(serial), title = std::move(title), crc]() {
		if (!IsInitialized())
			return;

		UpdateGameDetails(std::move(path), std::move(serial), std::move(title), crc);
	});
}

void FullscreenUI::UpdateGameDetails(std::string path, std::string serial, std::string title, u32 crc)
{
	if (!serial.empty())
		s_current_game_subtitle = fmt::format("{0} - {1}", serial, Path::GetFileName(path));
	else
		s_current_game_subtitle = {};

	s_current_game_title = std::move(title);
	s_current_game_serial = std::move(serial);
	s_current_game_path = std::move(path);
	s_current_game_crc = crc;
}

void FullscreenUI::PauseForMenuOpen()
{
	s_was_paused_on_quick_menu_open = (VMManager::GetState() == VMState::Paused);
	if (Host::GetBoolSettingValue("UI", "PauseOnMenu", true) && !s_was_paused_on_quick_menu_open)
		Host::RunOnCPUThread([]() { VMManager::SetPaused(true); });

	s_quick_menu_was_open = true;
}

bool FullscreenUI::WantsToCloseMenu()
{
	// Wait for the Close button to be released, THEN pressed
	if (s_close_button_state == 0)
	{
		if (!ImGuiFullscreen::IsCancelButtonPressed())
			s_close_button_state = 1;
	}
	else if (s_close_button_state == 1)
	{
		if (ImGuiFullscreen::IsCancelButtonPressed())
		{
			s_close_button_state = 0;
			return true;
		}
	}
	return false;
}

void FullscreenUI::OpenPauseMenu()
{
	if (!Initialize() || !VMManager::HasValidVM())
		return;

	GetMTGS().RunOnGSThread([]() {
		if (!IsInitialized() || s_current_main_window != MainWindowType::None)
			return;

		PauseForMenuOpen();
		s_current_main_window = MainWindowType::QuickMenu;
		ClearImGuiFocus();
	});
}

void FullscreenUI::ClosePauseMenu()
{
	if (!IsInitialized() || !VMManager::HasValidVM())
		return;

	if (VMManager::GetState() == VMState::Paused && !s_was_paused_on_quick_menu_open)
		Host::RunOnCPUThread([]() { VMManager::SetPaused(false); });

	s_current_main_window = MainWindowType::None;
	s_quick_menu_was_open = false;
	ClearImGuiFocus();
}

void FullscreenUI::Shutdown()
{
	CloseSaveStateSelector();
	s_cover_image_map.clear();
	s_game_list_sorted_entries = {};
	//s_nav_input_values = {};
	DestroyResources();
	ImGuiFullscreen::ClearState();
	s_initialized = false;
	s_tried_to_initialize = false;
}

void FullscreenUI::Render()
{
	if (!s_initialized)
		return;

	ImGuiFullscreen::BeginLayout();

	switch (s_current_main_window)
	{
		case MainWindowType::Landing:
			DrawLandingWindow();
			break;
		case MainWindowType::GameList:
			DrawGameListWindow();
			break;
		case MainWindowType::Settings:
			DrawSettingsWindow();
			break;
		case MainWindowType::QuickMenu:
			DrawQuickMenu(s_current_main_window);
			break;
		default:
			break;
	}

	if (s_save_state_selector_open)
		DrawSaveStateSelector(s_save_state_selector_loading, false);

	if (s_about_window_open)
		DrawAboutWindow();

#if 0
  if (s_input_binding_type != InputBindingType::None)
    DrawInputBindingWindow();
#endif

	ImGuiFullscreen::EndLayout();

	if (s_settings_changed)
	{
		s_settings_changed = false;

		auto lock = Host::GetSettingsLock();
		Host::Internal::GetBaseSettingsLayer()->Save();
		Host::RunOnCPUThread([]() { VMManager::ApplySettings(); });
	}
}

void FullscreenUI::ClearImGuiFocus()
{
	ImGui::SetWindowFocus(nullptr);
	s_close_button_state = 0;
}

void FullscreenUI::ReturnToMainWindow()
{
	if (s_quick_menu_was_open)
		ClosePauseMenu();

	s_current_main_window = VMManager::HasValidVM() ? MainWindowType::None : MainWindowType::Landing;
}

bool FullscreenUI::LoadResources()
{
	if (!(s_app_icon_texture = LoadTextureResource("logo.png", false)) ||
		!(s_placeholder_texture = LoadTextureResource("placeholder.png", false)))
	{
		return false;
	}

	if (!(s_disc_region_textures[static_cast<u32>(GameList::Region::NTSC_UC)] = LoadTextureResource("flag-uc.png")) ||
		!(s_disc_region_textures[static_cast<u32>(GameList::Region::NTSC_J)] = LoadTextureResource("flag-jp.png")) ||
		!(s_disc_region_textures[static_cast<u32>(GameList::Region::PAL)] = LoadTextureResource("flag-eu.png")) ||
		!(s_disc_region_textures[static_cast<u32>(GameList::Region::Other)] = LoadTextureResource("flag-eu.png")) ||
		!(s_fallback_disc_texture = LoadTextureResource("media-cdrom.png")) ||
		!(s_fallback_exe_texture = LoadTextureResource("applications-system.png")) ||
		!(s_fallback_playlist_texture = LoadTextureResource("address-book-new.png")))
	{
		return false;
	}

	for (u32 i = 0; i <= static_cast<u32>(GameDatabaseSchema::Compatibility::Perfect); i++)
	{
		if (!(s_game_compatibility_textures[i] = LoadTextureResource(fmt::format("star-{}.png", i).c_str())))
			return false;
	}

	return true;
}

void FullscreenUI::DestroyResources()
{
	s_app_icon_texture.reset();
	s_placeholder_texture.reset();
	s_fallback_playlist_texture.reset();
	s_fallback_exe_texture.reset();
	s_fallback_disc_texture.reset();
	for (auto& tex : s_game_compatibility_textures)
		tex.reset();
	for (auto& tex : s_disc_region_textures)
		tex.reset();
}

std::unique_ptr<HostDisplayTexture> FullscreenUI::LoadTexture(const char* path, bool from_package)
{
	std::optional<std::vector<u8>> data;
	if (from_package)
		data = Host::ReadResourceFile(path);
	else
		data = FileSystem::ReadBinaryFile(path);
	if (!data.has_value())
	{
		Console.Error("Failed to open texture resource '%s'", path);
		return {};
	}

	Common::RGBA8Image image;
	if (!image.LoadFromBuffer(path, data->data(), data->size()))
	{
		Console.Error("Failed to read texture resource '%s'", path);
		return {};
	}

	std::unique_ptr<HostDisplayTexture> texture =
		Host::GetHostDisplay()->CreateTexture(image.GetWidth(), image.GetHeight(), image.GetPixels(), image.GetByteStride());
	if (!texture)
	{
		Console.Error("failed to create %ux%u texture for resource", image.GetWidth(), image.GetHeight());
		return {};
	}

	DevCon.WriteLn("Uploaded texture resource '%s' (%ux%u)", path, image.GetWidth(), image.GetHeight());
	return texture;
}

std::unique_ptr<HostDisplayTexture> FullscreenUI::LoadTextureCallback(const char* path)
{
	return LoadTexture(path, false);
}

std::unique_ptr<HostDisplayTexture> FullscreenUI::LoadTextureResource(const char* name, bool allow_fallback /*= true*/)
{
	const std::string path(StringUtil::StdStringFromFormat("fullscreenui/%s", name));
	std::unique_ptr<HostDisplayTexture> texture = LoadTexture(path.c_str(), true);
	if (texture)
		return texture;

	if (!allow_fallback)
		return nullptr;

	Console.Error("Missing resource '%s', using fallback", name);
	return LoadTextureResource("fullscreenui/placeholder.png", false);
}

//////////////////////////////////////////////////////////////////////////
// Utility
//////////////////////////////////////////////////////////////////////////

ImGuiFullscreen::FileSelectorFilters FullscreenUI::GetDiscImageFilters()
{
	return {"*.bin", "*.iso", "*.cue", "*.chd", "*.cso", "*.gz", "*.elf", "*.irx", "*.m3u", "*.gs", "*.gs.xz", "*.gs.zst"};
}

void FullscreenUI::DoStartPath(const std::string& path, bool allow_resume)
{
	if (VMManager::IsElfFileName(path) || VMManager::IsGSDumpFileName(path))
		allow_resume = false;

	// TODO: Resume logic
	VMBootParameters params;
	params.filename = path;

	Host::RunOnCPUThread([params = std::move(params)]() {
		if (VMManager::HasValidVM())
			return;

		if (VMManager::Initialize(params))
			VMManager::SetState(VMState::Running);
	});
}

void FullscreenUI::DoStartFile()
{
	auto callback = [](const std::string& path) {
		if (!path.empty())
			DoStartPath(path, false);

		ClearImGuiFocus();
		CloseFileSelector();
	};

	OpenFileSelector(ICON_FA_COMPACT_DISC "  Select Disc Image", false, std::move(callback), GetDiscImageFilters());
}

void FullscreenUI::DoStartBIOS()
{
	Host::RunOnCPUThread([]() {
		if (VMManager::HasValidVM())
			return;

		VMBootParameters params;
		if (VMManager::Initialize(params))
			VMManager::SetState(VMState::Running);
	});
}

void FullscreenUI::DoToggleFrameLimit()
{
	Host::RunOnCPUThread([]() {
		if (!VMManager::HasValidVM())
			return;

		VMManager::SetLimiterMode(
			(EmuConfig.LimiterMode != LimiterModeType::Unlimited) ? LimiterModeType::Unlimited : LimiterModeType::Nominal);
	});
}

void FullscreenUI::DoShutdown()
{
	Host::RunOnCPUThread([]() { Host::RequestVMShutdown(EmuConfig.SaveStateOnShutdown); });
}

void FullscreenUI::DoReset()
{
	Host::RunOnCPUThread([]() {
		if (!VMManager::HasValidVM())
			return;

		VMManager::Reset();
	});
}

void FullscreenUI::DoChangeDiscFromFile()
{
	auto callback = [](const std::string& path) {
		if (!path.empty())
		{
			Host::RunOnCPUThread([path = path]() { VMManager::ChangeDisc(std::move(path)); });
		}

		ClearImGuiFocus();
		CloseFileSelector();
		ReturnToMainWindow();
	};

	OpenFileSelector(ICON_FA_COMPACT_DISC "  Select Disc Image", false, std::move(callback), GetDiscImageFilters(),
		std::string(Path::GetDirectory(s_current_game_path)));
}

void FullscreenUI::DoChangeDisc()
{
	const bool has_playlist = false;
	if (!has_playlist)
	{
		DoChangeDiscFromFile();
		return;
	}

#if 0
	// This will be for when we have playlists...
	const u32 current_index = System::GetMediaSubImageIndex();
	const u32 count = System::GetMediaSubImageCount();
	ImGuiFullscreen::ChoiceDialogOptions options;
	options.reserve(count + 1);
	options.emplace_back("From File...", false);

	for (u32 i = 0; i < count; i++)
		options.emplace_back(System::GetMediaSubImageTitle(i), i == current_index);

	auto callback = [](s32 index, const std::string& title, bool checked) {
		if (index == 0)
		{
			CloseChoiceDialog();
			DoChangeDiscFromFile();
			return;
		}
		else if (index > 0)
		{
			System::SwitchMediaSubImage(static_cast<u32>(index - 1));
		}

		ClearImGuiFocus();
		CloseChoiceDialog();
		ReturnToMainWindow();
	};

	OpenChoiceDialog(ICON_FA_COMPACT_DISC "  Select Disc Image", true, std::move(options), std::move(callback));
#endif
}

void FullscreenUI::DoRequestExit()
{
	Host::RunOnCPUThread([]() { Host::RequestExit(EmuConfig.SaveStateOnShutdown); });
}

void FullscreenUI::DoToggleFullscreen()
{
	Host::RunOnCPUThread([]() { Host::SetFullscreen(!Host::IsFullscreen()); });
}

//////////////////////////////////////////////////////////////////////////
// Landing Window
//////////////////////////////////////////////////////////////////////////

void FullscreenUI::SwitchToLanding()
{
	s_current_main_window = MainWindowType::Landing;
	ClearImGuiFocus();
}

void FullscreenUI::DrawLandingWindow()
{
	BeginFullscreenColumns();

	if (BeginFullscreenColumnWindow(0.0f, 570.0f, "logo", ImVec4(0.11f, 0.15f, 0.17f, 1.00f)))
	{
		const float image_size = LayoutScale(380.f);
		ImGui::SetCursorPos(
			ImVec2((ImGui::GetWindowWidth() * 0.5f) - (image_size * 0.5f), (ImGui::GetWindowHeight() * 0.5f) - (image_size * 0.5f)));
		ImGui::Image(s_app_icon_texture->GetHandle(), ImVec2(image_size, image_size));
	}
	EndFullscreenColumnWindow();

	if (BeginFullscreenColumnWindow(570.0f, LAYOUT_SCREEN_WIDTH, "menu"))
	{
		BeginMenuButtons(5, 0.5f);

		if (MenuButton(" " ICON_FA_FOLDER_OPEN "  Start File", "Launch a game by selecting a file/disc image."))
		{
			DoStartFile();
		}

		if (MenuButton(" " ICON_FA_TOOLBOX "  Start BIOS", "Start the console without any disc inserted."))
		{
			DoStartBIOS();
		}

		if (MenuButton(" " ICON_FA_LIST "  Open Game List", "Launch a game from images scanned from your game directories."))
		{
			SwitchToGameList();
		}

		if (MenuButton(" " ICON_FA_SLIDERS_H "  Settings", "Change settings for the emulator."))
			SwitchToSettings();

		if (MenuButton(" " ICON_FA_SIGN_OUT_ALT "  Exit", "Exits the program."))
		{
			DoRequestExit();
		}

		{
			ImVec2 fullscreen_pos;
			if (FloatingButton(ICON_FA_WINDOW_CLOSE, 0.0f, 0.0f, -1.0f, -1.0f, 1.0f, 0.0f, true, g_large_font, &fullscreen_pos))
			{
				DoRequestExit();
			}

			if (FloatingButton(ICON_FA_EXPAND, fullscreen_pos.x, 0.0f, -1.0f, -1.0f, -1.0f, 0.0f, true, g_large_font, &fullscreen_pos))
			{
				DoToggleFullscreen();
			}

			if (FloatingButton(ICON_FA_QUESTION_CIRCLE, fullscreen_pos.x, 0.0f, -1.0f, -1.0f, -1.0f, 0.0f))
				OpenAboutWindow();
		}

		EndMenuButtons();
	}

	EndFullscreenColumnWindow();

	EndFullscreenColumns();
}

bool FullscreenUI::IsEditingGameSettings()
{
	return static_cast<bool>(s_game_settings_interface);
}

SettingsInterface* FullscreenUI::GetEditingSettingsInterface()
{
	return s_game_settings_interface ? s_game_settings_interface.get() : Host::Internal::GetBaseSettingsLayer();
}

#if 0
void FullscreenUI::DrawInputBindingButton(InputBindingType type, const char* section, const char* name, const char* display_name, bool show_type = true)
{
  TinyString title;
  title.Format("%s/%s", section, name);

  ImRect bb;
  bool visible, hovered, clicked;
  clicked =
    MenuButtonFrame(title, true, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT, &visible, &hovered, &bb.Min, &bb.Max);
  if (!visible)
    return;

  const float midpoint = bb.Min.y + g_large_font->FontSize + LayoutScale(4.0f);
  const ImRect title_bb(bb.Min, ImVec2(bb.Max.x, midpoint));
  const ImRect summary_bb(ImVec2(bb.Min.x, midpoint), bb.Max);

  if (show_type)
  {
    switch (type)
    {
      case InputBindingType::Button:
        title.Format(ICON_FA_CIRCLE "  %s Button", display_name);
        break;
      case InputBindingType::Axis:
        title.Format(ICON_FA_BULLSEYE "  %s Axis", display_name);
        break;
      case InputBindingType::HalfAxis:
        title.Format(ICON_FA_SLIDERS_H "  %s Half-Axis", display_name);
        break;
      case InputBindingType::Rumble:
        title.Format(ICON_FA_BELL "  %s", display_name);
        break;
      default:
        title = display_name;
        break;
    }
  }

  ImGui::PushFont(g_large_font);
  ImGui::RenderTextClipped(title_bb.Min, title_bb.Max, show_type ? title.GetCharArray() : display_name, nullptr,
                           nullptr, ImVec2(0.0f, 0.0f), &title_bb);
  ImGui::PopFont();

  // eek, potential heap allocation :/
  const std::string value = s_host_interface->GetSettingsInterface()->GetStringValue(section, name);
  ImGui::PushFont(g_medium_font);
  ImGui::RenderTextClipped(summary_bb.Min, summary_bb.Max, value.empty() ? "(No Binding)" : value.c_str(), nullptr,
                           nullptr, ImVec2(0.0f, 0.0f), &summary_bb);
  ImGui::PopFont();

  if (clicked)
    BeginInputBinding(type, section, name, display_name);
  else if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    ClearInputBinding(section, name);
}

void FullscreenUI::ClearInputBindingVariables()
{
  s_input_binding_type = InputBindingType::None;
  s_input_binding_section.Clear();
  s_input_binding_key.Clear();
  s_input_binding_display_name.Clear();
}

bool FullscreenUI::IsBindingInput()
{
  return s_input_binding_type != InputBindingType::None;
}

bool FullscreenUI::HandleKeyboardBinding(const char* keyName, bool pressed)
{
  if (s_input_binding_type == InputBindingType::None)
    return false;

  if (pressed)
  {
    s_input_binding_keyboard_pressed = true;
    return true;
  }

  if (!s_input_binding_keyboard_pressed)
    return false;

  TinyString value;
  value.Format("Keyboard/%s", keyName);

  {
    auto lock = s_host_interface->GetSettingsLock();
    s_host_interface->GetSettingsInterface()->SetStringValue(s_input_binding_section, s_input_binding_key, value);
  }

  EndInputBinding();
  s_host_interface->RunLater(SaveAndApplySettings);
  return true;
}

void FullscreenUI::BeginInputBinding(InputBindingType type, const std::string_view& section, const std::string_view& key,
                       const std::string_view& display_name)
{
  s_input_binding_type = type;
  s_input_binding_section = section;
  s_input_binding_key = key;
  s_input_binding_display_name = display_name;
  s_input_binding_timer.Reset();

  ControllerInterface* ci = s_host_interface->GetControllerInterface();
  if (ci)
  {
    auto callback = [](const ControllerInterface::Hook& hook) -> ControllerInterface::Hook::CallbackResult {
      // ignore if axis isn't at least halfway
      if (hook.type == ControllerInterface::Hook::Type::Axis && std::abs(std::get<float>(hook.value)) < 0.5f)
        return ControllerInterface::Hook::CallbackResult::ContinueMonitoring;

      TinyString value;
      switch (s_input_binding_type)
      {
        case InputBindingType::Axis:
        {
          if (hook.type == ControllerInterface::Hook::Type::Axis)
            value.Format("Controller%d/Axis%d", hook.controller_index, hook.button_or_axis_number);
        }
        break;

        case InputBindingType::HalfAxis:
        {
          if (hook.type == ControllerInterface::Hook::Type::Axis)
          {
            value.Format("Controller%d/%cAxis%d", hook.controller_index,
                         (std::get<float>(hook.value) < 0.0f) ? '-' : '+', hook.button_or_axis_number);
          }
        }
        break;

        case InputBindingType::Button:
        {
          if (hook.type == ControllerInterface::Hook::Type::Axis)
            value.Format("Controller%d/+Axis%d", hook.controller_index, hook.button_or_axis_number);
          else if (hook.type == ControllerInterface::Hook::Type::Button && std::get<float>(hook.value) > 0.0f)
            value.Format("Controller%d/Button%d", hook.controller_index, hook.button_or_axis_number);
        }
        break;

        case InputBindingType::Rumble:
        {
          if (hook.type == ControllerInterface::Hook::Type::Button && std::get<float>(hook.value) > 0.0f)
            value.Format("Controller%d", hook.controller_index);
        }
        break;

        default:
          break;
      }

      if (value.IsEmpty())
        return ControllerInterface::Hook::CallbackResult::ContinueMonitoring;

      {
        auto lock = s_host_interface->GetSettingsLock();
        s_host_interface->GetSettingsInterface()->SetStringValue(s_input_binding_section, s_input_binding_key, value);
      }

      ClearInputBindingVariables();
      s_host_interface->RunLater(SaveAndApplySettings);

      return ControllerInterface::Hook::CallbackResult::StopMonitoring;
    };
    ci->SetHook(std::move(callback));
  }
}

void FullscreenUI::EndInputBinding()
{
  ClearInputBindingVariables();

  ControllerInterface* ci = s_host_interface->GetControllerInterface();
  if (ci)
    ci->ClearHook();
}

void FullscreenUI::ClearInputBinding(const char* section, const char* key)
{
  {
    auto lock = s_host_interface->GetSettingsLock();
    s_host_interface->GetSettingsInterface()->DeleteValue(section, key);
  }

  s_host_interface->RunLater(SaveAndApplySettings);
}

void FullscreenUI::DrawInputBindingWindow()
{
  DebugAssert(s_input_binding_type != InputBindingType::None);

  const double time_remaining = INPUT_BINDING_TIMEOUT_SECONDS - s_input_binding_timer.GetTimeSeconds();
  if (time_remaining <= 0.0)
  {
    EndInputBinding();
    return;
  }

  const char* title = ICON_FA_GAMEPAD "  Set Input Binding";
  ImGui::SetNextWindowSize(LayoutScale(500.0f, 0.0f));
  ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::OpenPopup(title);

  ImGui::PushFont(g_large_font);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(10.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, LayoutScale(ImGuiFullscreen::LAYOUT_MENU_BUTTON_X_PADDING,
                                                              ImGuiFullscreen::LAYOUT_MENU_BUTTON_Y_PADDING));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, LayoutScale(10.0f, 10.0f));

  if (ImGui::BeginPopupModal(title, nullptr,
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs))
  {
    ImGui::TextWrapped("Setting %s binding %s.", s_input_binding_section.GetCharArray(),
                       s_input_binding_display_name.GetCharArray());
    ImGui::TextUnformatted("Push a controller button or axis now.");
    ImGui::NewLine();
    ImGui::Text("Timing out in %.0f seconds...", time_remaining);
    ImGui::EndPopup();
  }

  ImGui::PopStyleVar(3);
  ImGui::PopFont();
}

#endif

bool FullscreenUI::DrawToggleSetting(const char* title, const char* summary, const char* section, const char* key, bool default_value,
	bool enabled, float height, ImFont* font, ImFont* summary_font)
{
	SettingsInterface* bsi = GetEditingSettingsInterface();
	if (!IsEditingGameSettings())
	{
		bool value = bsi->GetBoolValue(section, key, default_value);
		if (!ToggleButton(title, summary, &value, enabled, height, font, summary_font))
			return false;

		bsi->SetBoolValue(section, key, value);
	}
	else
	{
		std::optional<bool> value(false);
		if (!bsi->GetBoolValue(section, key, &value.value()))
			value.reset();
		if (!ThreeWayToggleButton(title, summary, &value, enabled, height, font, summary_font))
			return false;

		if (value.has_value())
			bsi->SetBoolValue(section, key, value.value());
		else
			bsi->DeleteValue(section, key);
	}

	s_settings_changed = true;
	return true;
}

void FullscreenUI::DrawIntListSetting(const char* title, const char* summary, const char* section, const char* key, int default_value,
	const char* const* options, size_t option_count, int option_offset, bool enabled, float height, ImFont* font, ImFont* summary_font)
{
	SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
	const int value = bsi->GetIntValue(section, key, default_value);
	const int index = value - option_offset;
	const char* value_text = (index < 0 || static_cast<size_t>(index) >= option_count) ? "Unknown" : options[index];

	if (MenuButtonWithValue(title, summary, value_text, enabled, height, font, summary_font))
	{
		ImGuiFullscreen::ChoiceDialogOptions cd_options;
		cd_options.reserve(option_count);
		for (size_t i = 0; i < option_count; i++)
			cd_options.emplace_back(options[i], (i == static_cast<int>(index)));
		OpenChoiceDialog(
			title, false, std::move(cd_options), [section, key, option_offset](s32 index, const std::string& title, bool checked) {
				if (index >= 0)
				{
					auto lock = Host::GetSettingsLock();
					SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
					bsi->SetIntValue(section, key, index + option_offset);
					s_settings_changed = true;
				}

				CloseChoiceDialog();
			});
	}
}

void FullscreenUI::DrawStringListSetting(const char* title, const char* summary, const char* section, const char* key,
	const char* default_value, const char* const* options, const char* const* option_values, size_t option_count, bool enabled,
	float height, ImFont* font, ImFont* summary_font)
{
	SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
	const std::string value(bsi->GetStringValue(section, key, default_value));

	if (option_count == 0)
	{
		// select from null entry
		while (options && options[option_count] != nullptr)
			option_count++;
	}

	size_t index = option_count;
	for (size_t i = 0; i < option_count; i++)
	{
		if (value == option_values[i])
		{
			index = i;
			break;
		}
	}

	if (MenuButtonWithValue(title, summary, (index < option_count) ? options[index] : "Unknown", enabled, height, font, summary_font))
	{
		ImGuiFullscreen::ChoiceDialogOptions cd_options;
		cd_options.reserve(option_count);
		for (size_t i = 0; i < option_count; i++)
			cd_options.emplace_back(options[i], (i == static_cast<int>(index)));
		OpenChoiceDialog(
			title, false, std::move(cd_options), [section, key, option_values](s32 index, const std::string& title, bool checked) {
				if (index >= 0)
				{
					auto lock = Host::GetSettingsLock();
					SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
					bsi->SetStringValue(section, key, option_values[index]);
					s_settings_changed = true;
				}

				CloseChoiceDialog();
			});
	}
}

void FullscreenUI::SwitchToSettings()
{
	// populate the cache with all settings from ini
	auto lock = Host::GetSettingsLock();
	SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();

	PopulateGameListDirectoryCache(bsi);

	s_current_main_window = MainWindowType::Settings;
}

void FullscreenUI::PopulateGameListDirectoryCache(SettingsInterface* si)
{
	s_game_list_directories_cache.clear();
	for (std::string& dir : si->GetStringList("GameList", "Paths"))
		s_game_list_directories_cache.emplace_back(std::move(dir), false);
	for (std::string& dir : si->GetStringList("GameList", "RecursivePaths"))
		s_game_list_directories_cache.emplace_back(std::move(dir), true);
}

ImGuiFullscreen::ChoiceDialogOptions FullscreenUI::GetGameListDirectoryOptions(bool recursive_as_checked)
{
	ImGuiFullscreen::ChoiceDialogOptions options;
	for (const auto& it : s_game_list_directories_cache)
		options.emplace_back(it.first, it.second && recursive_as_checked);
	return options;
}

void FullscreenUI::DrawSettingsWindow()
{
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 heading_size =
		ImVec2(io.DisplaySize.x, LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY + LAYOUT_MENU_BUTTON_Y_PADDING * 2.0f + 2.0f));

	if (BeginFullscreenWindow(
			ImVec2(0.0f, ImGuiFullscreen::g_menu_bar_size), heading_size, "settings_category", ImVec4(0.18f, 0.18f, 0.18f, 1.00f)))
	{
		static constexpr float ITEM_WIDTH = 22.0f;

		static constexpr std::array<const char*, static_cast<u32>(SettingsPage::Count)> icons = {
			{ICON_FA_WINDOW_MAXIMIZE, ICON_FA_LIST, ICON_FA_MICROCHIP, ICON_FA_SLIDERS_H, ICON_FA_HDD, ICON_FA_MAGIC, ICON_FA_HEADPHONES,
				ICON_FA_SD_CARD, ICON_FA_GAMEPAD, ICON_FA_KEYBOARD, ICON_FA_TROPHY}};

		static constexpr std::array<const char*, static_cast<u32>(SettingsPage::Count)> titles = {
			{"Interface Settings", "Game List Settings", "BIOS Settings", "Emulation Settings", "System Settings", "Graphics Settings",
				"Audio Settings", "Memory Card Settings", "Controller Settings", "Hotkey Settings", "Achievements Settings"}};

		BeginNavBar();

		if (ImGui::IsNavInputTest(ImGuiNavInput_FocusPrev, ImGuiNavReadMode_Pressed))
		{
			s_settings_page =
				static_cast<SettingsPage>((s_settings_page == static_cast<SettingsPage>(0)) ? (static_cast<u32>(SettingsPage::Count) - 1) :
                                                                                              (static_cast<u32>(s_settings_page) - 1));
		}
		else if (ImGui::IsNavInputTest(ImGuiNavInput_FocusNext, ImGuiNavReadMode_Pressed))
		{
			s_settings_page = static_cast<SettingsPage>((static_cast<u32>(s_settings_page) + 1) % static_cast<u32>(SettingsPage::Count));
		}

		if (NavButton(ICON_FA_BACKWARD, false, true))
			ReturnToMainWindow();

		NavTitle(titles[static_cast<u32>(s_settings_page)]);

		RightAlignNavButtons(static_cast<u32>(titles.size()), ITEM_WIDTH, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);

		for (u32 i = 0; i < static_cast<u32>(titles.size()); i++)
		{
			if (NavButton(
					icons[i], s_settings_page == static_cast<SettingsPage>(i), true, ITEM_WIDTH, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY))
			{
				s_settings_page = static_cast<SettingsPage>(i);
			}
		}

		EndNavBar();
	}

	EndFullscreenWindow();

	if (BeginFullscreenWindow(ImVec2(0.0f, ImGuiFullscreen::g_menu_bar_size + heading_size.y),
			ImVec2(io.DisplaySize.x, io.DisplaySize.y - heading_size.y - ImGuiFullscreen::g_menu_bar_size), "settings_parent"))
	{
		if (ImGui::IsNavInputTest(ImGuiNavInput_Cancel, ImGuiNavReadMode_Pressed))
		{
			if (ImGui::IsWindowFocused())
				ReturnToMainWindow();
		}

		auto lock = Host::GetSettingsLock();

		switch (s_settings_page)
		{
			case SettingsPage::InterfaceSettings:
				DrawInterfaceSettingsPage();
				break;

			case SettingsPage::GameListSettings:
				DrawGameListSettingsPage();
				break;

			case SettingsPage::BIOSSettings:
				DrawBIOSSettingsPage();
				break;

			case SettingsPage::EmulationSettings:
				DrawEmulationSettingsPage();
				break;

			case SettingsPage::SystemSettings:
				DrawSystemSettingsPage();
				break;

			case SettingsPage::GraphicsSettings:
				DrawGraphicsSettingsPage();
				break;

			case SettingsPage::AudioSettings:
				DrawAudioSettingsPage();
				break;

			case SettingsPage::MemoryCardSettings:
				DrawMemoryCardSettingsPage();
				break;

			case SettingsPage::ControllerSettings:
				DrawControllerSettingsPage();
				break;

			case SettingsPage::HotkeySettings:
				DrawHotkeySettingsPage();
				break;

			default:
				break;
		}
	}

	EndFullscreenWindow();
}

void FullscreenUI::DrawInterfaceSettingsPage()
{
	BeginMenuButtons();

	MenuHeading("Behaviour");

	DrawToggleSetting("Inhibit Screensaver",
		"Prevents the screen saver from activating and the host from sleeping while emulation is running.", "UI", "InhibitScreensaver",
		true);
#ifdef WITH_DISCORD_PRESENCE
	DrawToggleSetting("Enable Discord Presence", "Shows the game you are currently playing as part of your profile on Discord.", "UI",
		"DiscordPresence", false);
#endif
	DrawToggleSetting("Pause On Start", "Pauses the emulator when a game is started.", "UI", "StartPaused", false);
	DrawToggleSetting("Pause On Focus Loss",
		"Pauses the emulator when you minimize the window or switch to another "
		"application, and unpauses when you switch back.",
		"UI", "PauseOnFocusLoss", false);
	DrawToggleSetting(
		"Pause On Menu", "Pauses the emulator when you open the quick menu, and unpauses when you close it.", "UI", "PauseOnMenu", true);
	DrawToggleSetting("Confirm Shutdown",
		"Determines whether a prompt will be displayed to confirm shutting down the emulator/game "
		"when the hotkey is pressed.",
		"UI", "ConfirmShutdown", true);
	DrawToggleSetting("Save State On Shutdown",
		"Automatically saves the emulator state when powering down or exiting. You can then "
		"resume directly from where you left off next time.",
		"EmuCore", "SaveStateOnShutdown", false);

	MenuHeading("Game Display");
	DrawToggleSetting(
		"Start Fullscreen", "Automatically switches to fullscreen mode when the program is started.", "UI", "StartFullscreen", false);
	DrawToggleSetting("Double-Click Toggles Fullscreen", "Switches between full screen and windowed when the window is double-clicked.",
		"UI", "DoubleClickTogglesFullscreen", true);
	DrawToggleSetting("Hide Cursor In Fullscreen", "Hides the mouse pointer/cursor when the emulator is in fullscreen mode.", "UI",
		"HideMouseCursor", false);

	EndMenuButtons();
}

void FullscreenUI::DrawGameListSettingsPage()
{
	BeginMenuButtons();

	MenuHeading("Game List");

	if (MenuButton(ICON_FA_FOLDER_PLUS "  Add Search Directory", "Adds a new directory to the game search list."))
	{
		OpenFileSelector(ICON_FA_FOLDER_PLUS "  Add Search Directory", true, [](const std::string& dir) {
			if (!dir.empty())
			{
				auto lock = Host::GetSettingsLock();
				SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();

				bsi->AddToStringList("GameList", "RecursivePaths", dir.c_str());
				bsi->RemoveFromStringList("GameList", "Paths", dir.c_str());
				bsi->Save();
				PopulateGameListDirectoryCache(bsi);
				Host::RefreshGameListAsync(false);
			}

			CloseFileSelector();
		});
	}

	if (MenuButton(
			ICON_FA_FOLDER_OPEN "  Change Recursive Directories", "Sets whether subdirectories are searched for each game directory"))
	{
		OpenChoiceDialog(ICON_FA_FOLDER_OPEN "  Change Recursive Directories", true, GetGameListDirectoryOptions(true),
			[](s32 index, const std::string& title, bool checked) {
				if (index < 0)
					return;

				auto lock = Host::GetSettingsLock();
				SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
				if (checked)
				{
					bsi->RemoveFromStringList("GameList", "Paths", title.c_str());
					bsi->AddToStringList("GameList", "RecursivePaths", title.c_str());
				}
				else
				{
					bsi->RemoveFromStringList("GameList", "RecursivePaths", title.c_str());
					bsi->AddToStringList("GameList", "Paths", title.c_str());
				}

				bsi->Save();
				PopulateGameListDirectoryCache(bsi);
				Host::RefreshGameListAsync(false);
			});
	}

	if (MenuButton(ICON_FA_FOLDER_MINUS "  Remove Search Directory", "Removes a directory from the game search list."))
	{
		OpenChoiceDialog(ICON_FA_FOLDER_MINUS "  Remove Search Directory", false, GetGameListDirectoryOptions(false),
			[](s32 index, const std::string& title, bool checked) {
				if (index < 0)
					return;

				auto lock = Host::GetSettingsLock();
				SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
				bsi->RemoveFromStringList("GameList", "Paths", title.c_str());
				bsi->RemoveFromStringList("GameList", "RecursivePaths", title.c_str());
				bsi->Save();
				PopulateGameListDirectoryCache(bsi);
				Host::RefreshGameListAsync(false);
				CloseChoiceDialog();
			});
	}

	MenuHeading("Search Directories");
	for (const auto& it : s_game_list_directories_cache)
		MenuButton(it.first.c_str(), it.second ? "Scanning Subdirectories" : "Not Scanning Subdirectories", false);

	EndMenuButtons();
}

void FullscreenUI::DrawBIOSSettingsPage()
{
	BeginMenuButtons();

	MenuHeading("BIOS Configuration");

	if (MenuButton(ICON_FA_FOLDER_OPEN "  Change Search Directory", EmuFolders::Bios.c_str()))
	{
		OpenFileSelector(ICON_FA_FOLDER_OPEN "  Change Search Directory", true, [](const std::string& dir) {
			if (dir.empty())
				return;

			auto lock = Host::GetSettingsLock();
			std::string relative_path(Path::MakeRelative(dir, EmuFolders::DataRoot));
			GetEditingSettingsInterface()->SetStringValue("Folders", "Bios", relative_path.c_str());
			s_settings_changed = true;

			// TODO: This has to be set manually, because setting reload doesn't apply to folders.
			EmuFolders::Bios = dir;

			CloseFileSelector();
		});
	}

	const std::string bios_selection(GetEditingSettingsInterface()->GetStringValue("Filenames", "BIOS", ""));
	if (MenuButtonWithValue("BIOS Selection", "Changes the BIOS image used to start future sessions.",
			bios_selection.empty() ? "Automatic" : bios_selection.c_str()))
	{
		ImGuiFullscreen::ChoiceDialogOptions choices;
		choices.emplace_back("Automatic", bios_selection.empty());

		std::vector<std::string> values;
		values.push_back("");

		FileSystem::FindResultsArray results;
		FileSystem::FindFiles(EmuFolders::Bios.c_str(), "*", FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_HIDDEN_FILES, &results);
		for (const FILESYSTEM_FIND_DATA& fd : results)
		{
			u32 version, region;
			std::string description, zone;
			if (!IsBIOS(fd.FileName.c_str(), version, description, region, zone))
				continue;

			const std::string_view filename(Path::GetFileName(fd.FileName));
			choices.emplace_back(fmt::format("{} ({})", description, filename), bios_selection == filename);
			values.emplace_back(filename);
		}

		OpenChoiceDialog(
			"BIOS Selection", false, std::move(choices), [values = std::move(values)](s32 index, const std::string& title, bool checked) {
				if (index < 0)
					return;

				auto lock = Host::GetSettingsLock();
				GetEditingSettingsInterface()->SetStringValue("Filenames", "BIOS", values[index].c_str());
				s_settings_changed = true;
				CloseChoiceDialog();
			});
	}

	MenuHeading("Options and Patches");
	DrawToggleSetting("Fast Boot", "Skips the intro screen, and bypasses region checks.", "EmuCore", "EnableFastBoot", true);

	EndMenuButtons();
}

void FullscreenUI::DrawEmulationSettingsPage()
{
	BeginMenuButtons();

	MenuHeading("Frame Pacing/Latency Control");

	bool optimal_frame_pacing = (GetEditingSettingsInterface()->GetIntValue("EmuCore/GS", "VsyncQueueSize", 2) == 0);
	if (ToggleButton("Optimal Frame Pacing",
			"Synchronize EE and GS threads after each frame. Lowest input latency, but increases system requirements.",
			&optimal_frame_pacing))
	{
		GetEditingSettingsInterface()->SetIntValue("EmuCore/GS", "VsyncQueueSize", optimal_frame_pacing ? 2 : 0);
		s_settings_changed = true;
	}

	DrawToggleSetting("Adjust To Host Refresh Rate", "Speeds up emulation so that the guest refresh rate matches the host.", "EmuCore/GS",
		"SyncToHostRefreshRate", false);

	EndMenuButtons();
}

void FullscreenUI::DrawSystemSettingsPage()
{
	static constexpr const char* ee_cycle_rate_settings[] = {
		"50% Speed", "60% Speed", "75% Speed", "100% Speed (Default)", "130% Speed", "180% Speed", "300% Speed"};
	static constexpr const char* ee_cycle_skip_settings[] = {
		"Normal (Default)", "Mild Underclock", "Moderate Overclock", "Maximum Overclock"};
	static constexpr const char* ee_rounding_mode_settings[] = {"Nearest", "Negative", "Positive", "Chop/Zero (Default)"};
	static constexpr const char* ee_clamping_mode_settings[] = {"None", "Normal (Default)", "Extra + Preserve Sign", "Full"};
	static constexpr const char* vu_clamping_mode_settings[] = {"None", "Normal (Default)", "Extra", "Extra + Preserve Sign"};

	BeginMenuButtons();

	MenuHeading("Emotion Engine (MIPS-III/MIPS-IV)");
	DrawIntListSetting("Cycle Rate", "Underclocks or overclocks the emulated Emotion Engine CPU.", "EmuCore/Speedhacks", "EECycleRate", 0,
		ee_cycle_rate_settings, std::size(ee_cycle_rate_settings), -3);
	DrawIntListSetting("Cycle Skip", "Adds a penalty to the Emulated Emotion Engine for executing VU programs.", "EmuCore/Speedhacks",
		"EECycleSkip", 0, ee_cycle_skip_settings, std::size(ee_cycle_skip_settings));
	DrawIntListSetting(
		"Rounding Mode", "TODO", "EmuCore/CPU", "FPU.Roundmode", 3, ee_rounding_mode_settings, std::size(ee_rounding_mode_settings));
	DrawIntListSetting(
		"Clamping Mode", "TODO", "EmuCore/CPU", "FPU.Clampmode", 1, ee_clamping_mode_settings, std::size(ee_clamping_mode_settings));
	DrawToggleSetting("Enable Recompiler", "Performs just-in-time binary translation of 64-bit MIPS-IV machine code to native code.",
		"EmuCore/CPU/Recompiler", "EnableEE", true);
	DrawToggleSetting("Enable Cache", "Enables simulation of the EE's cache. Slow.", "EmuCore/CPU/Recompiler", "EnableEECache", false);
	DrawToggleSetting("INTC Spin Detection", "TODO.", "EmuCore/Speedhacks", "IntcStat", true);
	DrawToggleSetting("Wait Loop Detection", "TODO.", "EmuCore/Speedhacks", "WaitLoop", true);

	MenuHeading("Vector Units");
	DrawIntListSetting(
		"Rounding Mode", "TODO", "EmuCore/CPU", "VU.Roundmode", 3, ee_rounding_mode_settings, std::size(ee_rounding_mode_settings));
	DrawIntListSetting(
		"Clamping Mode", "TODO", "EmuCore/CPU", "FPU.Clampmode", 1, vu_clamping_mode_settings, std::size(vu_clamping_mode_settings));
	DrawToggleSetting("Enable MTVU (Multi-Threaded VU1)", "Uses a second thread for VU1 micro programs. Sizable speed boost.",
		"EmuCore/Speedhacks", "vuThread", false);
	DrawToggleSetting("Enable Instant VU1", "TODO.", "EmuCore/Speedhacks", "vu1Instant", true);
	DrawToggleSetting("Enable VU0 Recompiler (Micro Mode)", "New Vector Unit recompiler with much improved compatibility. Recommended.",
		"EmuCore/CPU/Recompiler", "EnableVU0", true);
	DrawToggleSetting("Enable VU1 Recompiler", "New Vector Unit recompiler with much improved compatibility. Recommended.",
		"EmuCore/CPU/Recompiler", "EnableVU1", true);
	DrawToggleSetting("VU Flag Optimization", "TODO.", "EmuCore/Speedhacks", "vuFlagHack", true);

	MenuHeading("I/O Processor (MIPS-I)");
	DrawToggleSetting(
		"Enable Fast CDVD", "Fast disc access, less loading times. Not recommended.", "EmuCore/Speedhacks", "fastCDVD", false);
	DrawToggleSetting("Enable Recompiler", "Performs just-in-time binary translation of 32-bit MIPS-I machine code to native code.",
		"EmuCore/CPU/Recompiler", "EnableIOP", true);

	EndMenuButtons();
}

void FullscreenUI::DrawGraphicsSettingsPage()
{
	static constexpr const char* s_renderer_names[] = {"Automatic",
#ifdef _WIN32
		"Direct3D 11", "Direct3D 12",
#endif
#ifdef ENABLE_OPENGL
		"OpenGL",
#endif
#ifdef ENABLE_VULKAN
		"Vulkan",
#endif
#ifdef __APPLE__
		"Metal",
#endif
		"Software", "Null"};
	static constexpr const char* s_renderer_values[] = {
		"-1", //GSRendererType::Auto,
#ifdef _WIN32
		"3", //GSRendererType::DX11,
		"15", //GSRendererType::DX12,
#endif
#ifdef ENABLE_OPENGL
		"12", //GSRendererType::OGL,
#endif
#ifdef ENABLE_VULKAN
		"14", //GSRendererType::VK,
#endif
#ifdef __APPLE__
		"17", //GSRendererType::Metal,
#endif
		"13", //GSRendererType::SW,
		"11", //GSRendererType::Null
	};
	static constexpr const char* s_deinterlacing_options[] = {"None", "Weave (Top Field First, Sawtooth)",
		"Weave (Bottom Field First, Sawtooth)", "Bob (Top Field First)", "Bob (Bottom Field First)", "Blend (Top Field First, Half FPS)",
		"Blend (Bottom Field First, Half FPS)", "Automatic (Default)"};
	static constexpr const char* s_resolution_options[] = {
		"Native (PS2)",
		"2x Native (~720p)",
		"3x Native (~1080p)",
		"4x Native (~1440p/2K)",
		"5x Native (~1620p)",
		"6x Native (~2160p/4K)",
		"7x Native (~2520p)",
		"8x Native (~2880p)",
	};
	static constexpr const char* s_mipmapping_options[] = {"Automatic (Default)", "Off", "Basic (Generated Mipmaps)", "Full (PS2 Mipmaps)"};
	static constexpr const char* s_bilinear_options[] = {
		"Nearest", "Bilinear (Forced)", "Bilinear (PS2)", "Bilinear (Forced excluding sprite)"};
	static constexpr const char* s_trilinear_options[] = {"Automatic (Default)", "Off (None)", "Trilinear (PS2)", "Trilinear (Forced)"};
	static constexpr const char* s_dithering_options[] = {"Off", "Scaled", "Unscaled (Default)"};
	static constexpr const char* s_crc_fix_options[] = {
		"Automatic (Default)", "None (Debug)", "Minimum (Debug)", "Partial (OpenGL)", "Full (Direct3D)", "Aggressive"};
	static constexpr const char* s_blending_options[] = {
		"Minimum", "Basic (Recommended)", "Medium", "High", "Full (Slow)", "Maximum (Very Slow)"};
	static constexpr const char* s_anisotropic_filtering_entries[] = {"Off (Default)", "2x", "4x", "8x", "16x"};
	static constexpr const char* s_anisotropic_filtering_values[] = {"0", "2", "4", "8", "16"};
	static constexpr const char* s_preloading_options[] = {"None", "Partial", "Full (Hash Cache)"};

	SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();

	const GSRendererType renderer =
		static_cast<GSRendererType>(bsi->GetIntValue("EmuCore/GS", "Renderer", static_cast<int>(GSRendererType::Auto)));
	const bool is_hardware = (renderer == GSRendererType::DX11 || renderer == GSRendererType::DX12 || renderer == GSRendererType::OGL ||
							  renderer == GSRendererType::VK || renderer == GSRendererType::Metal);
	const bool is_software = (renderer == GSRendererType::SW);

	BeginMenuButtons();

	MenuHeading("Renderer");
	DrawStringListSetting("Renderer", "Selects the API used to render the emulated GS.", "EmuCore/GS", "Renderer", "-1", s_renderer_names,
		s_renderer_values, std::size(s_renderer_names));
	DrawToggleSetting(
		"Sync To Host Refresh (VSync)", "Synchronizes frame presentation with host refresh.", "EmuCore/GS", "VsyncEnable", false);

	MenuHeading("Display");
	DrawStringListSetting("Aspect Ratio", "Selects the aspect ratio to display the game content at.", "EmuCore/GS", "AspectRatio",
		"Auto 4:3/3:2", Pcsx2Config::GSOptions::AspectRatioNames, Pcsx2Config::GSOptions::AspectRatioNames, 0);
	DrawStringListSetting("FMV Aspect Ratio", "Selects the aspect ratio for display when a FMV is detected as playing.", "EmuCore/GS",
		"FMVAspectRatioSwitch", "Auto 4:3/3:2", Pcsx2Config::GSOptions::FMVAspectRatioSwitchNames,
		Pcsx2Config::GSOptions::FMVAspectRatioSwitchNames, 0);
	DrawIntListSetting("Deinterlacing", "Selects the algorithm used to convert the PS2's interlaced output to progressive for display.",
		"EmuCore/GS", "deinterlace", static_cast<int>(GSInterlaceMode::Automatic), s_deinterlacing_options,
		std::size(s_deinterlacing_options));
	// Zoom
	// Stretch
	// Offset
	DrawToggleSetting(
		"Bilinear Filtering", "Smooths out the image when upscaling the console to the screen.", "EmuCore/GS", "linear_present", true);
	DrawToggleSetting("Integer Upscaling",
		"Adds padding to the display area to ensure that the ratio between pixels on the host to pixels in the console is an integer "
		"number. May result in a sharper image in some 2D games.",
		"EmuCore/GS", "IntegerScaling", false);
	DrawToggleSetting("Internal Resolution Screenshots", "Save screenshots at the full render resolution, rather than display resolution.",
		"EmuCore/GS", "InternalResolutionScreenshots", false);
	DrawToggleSetting("Screen Offsets", "Simulates the border area of typical CRTs.", "EmuCore/GS", "pcrtc_offsets", false);

	MenuHeading("Rendering");
	if (is_hardware)
	{
		DrawIntListSetting("Internal Resolution", "Multiplies the render resolution by the specified factor (upscaling).", "EmuCore/GS",
			"upscale_multiplier", 1, s_resolution_options, std::size(s_resolution_options), 1);
		DrawIntListSetting("Mipmapping", "Determines how mipmaps are used when rendering textures.", "EmuCore/GS", "mipmap_hw",
			static_cast<int>(HWMipmapLevel::Automatic), s_mipmapping_options, std::size(s_mipmapping_options), -1);
		DrawIntListSetting("Bilinear Filtering", "Selects where bilinear filtering is utilized when rendering textures.", "EmuCore/GS",
			"filter", static_cast<int>(BiFiltering::PS2), s_bilinear_options, std::size(s_bilinear_options));
		DrawIntListSetting("Trilinear Filtering", "Selects where trilinear filtering is utilized when rendering textures.", "EmuCore/GS",
			"UserHacks_TriFilter", static_cast<int>(TriFiltering::Automatic), s_trilinear_options, std::size(s_trilinear_options), -1);
		DrawStringListSetting("Anisotropic Filtering", "Selects where anistropic filtering is utilized when rendering textures.",
			"EmuCore/GS", "MaxAnisotropy", "0", s_anisotropic_filtering_entries, s_anisotropic_filtering_values,
			std::size(s_anisotropic_filtering_entries));
		DrawIntListSetting("Dithering", "Selects the type of dithering applies when the game requests it.", "EmuCore/GS", "dithering_ps2",
			2, s_dithering_options, std::size(s_dithering_options));
		DrawIntListSetting("CRC Fix Level", "TODO", "EmuCore/GS", "crc_hack_level", static_cast<int>(CRCHackLevel::Automatic),
			s_crc_fix_options, std::size(s_crc_fix_options), -1);
		DrawIntListSetting("Blending Accuracy", "TODO", "EmuCore/GS", "accurate_blending_unit", static_cast<int>(AccBlendLevel::Basic),
			s_blending_options, std::size(s_blending_options));
		DrawIntListSetting("Texture Preloading",
			"Uploads full textures to the GPU on use, rather than only the utilized regions. Can improve performance in some games.",
			"EmuCore/GS", "texture_preloading", static_cast<int>(TexturePreloadingLevel::Off), s_preloading_options,
			std::size(s_preloading_options));
		DrawToggleSetting("Accurate Destination Alpha Test", "Implement a more accurate algorithm to compute GS destination alpha testing.",
			"EmuCore/GS", "accurate_date", true);
		DrawToggleSetting("Conservative Buffer Allocation",
			"Uses a smaller framebuffer where possible to reduce VRAM bandwidth and usage. May need to be disabled to prevent FMV flicker.",
			"EmuCore/GS", "conservative_framebuffer", true);
		DrawToggleSetting("GPU Palette Conversion",
			"Applies palettes to textures on the GPU instead of the CPU. Can result in speed improvements in some games.", "EmuCore/GS",
			"paltex", false);
	}
	else
	{
	}

	if (is_hardware)
	{
		MenuHeading("Hardware Fixes");
		DrawToggleSetting("Manual Hardware Fixes", "TODO", "EmuCore/GS", "UserHacks", false);

		const bool manual_hw_fixes = bsi->GetBoolValue("EmuCore/GS", "UserHacks", false);
		if (manual_hw_fixes)
		{
			// Half Bottom Override
			// Skip Draw
			DrawToggleSetting("Auto Flush (Hardware)", "Force a primitive flush when a framebuffer is also an input texture.", "EmuCore/GS",
				"UserHacks_AutoFlush", false, manual_hw_fixes);
			DrawToggleSetting("CPU Framebuffer Conversion", "Convert 4-bit and 8-bit frame buffer on the CPU instead of the GPU.",
				"EmuCore/GS", "UserHacks_CPU_FB_Conversion", false, manual_hw_fixes);
			DrawToggleSetting("Disable Depth Support", "Disable the support of depth buffer in the texture cache.", "EmuCore/GS",
				"UserHacks_DisableDepthSupport", false, manual_hw_fixes);
			DrawToggleSetting(
				"Wrap GS Memory", "Emulates GS memory wrapping accurately.", "EmuCore/GS", "wrap_gs_mem", false, manual_hw_fixes);
			DrawToggleSetting("Disable Safe Features", "This option disables multiple safe features.", "EmuCore/GS",
				"UserHacks_Disable_Safe_Features", false, manual_hw_fixes);
			DrawToggleSetting("Preload Frame", "Uploads GS data when rendering a new frame to reproduce some effects accurately.",
				"EmuCore/GS", "preload_frame_with_gs_data", false, manual_hw_fixes);
			DrawToggleSetting("Disable Partial Invalidation",
				"Removes texture cache entries when there is any intersection, rather than only the intersected areas.", "EmuCore/GS",
				"UserHacks_DisablePartialInvalidation", false, manual_hw_fixes);
			DrawToggleSetting("Texture Inside Render Target", "TODO", "EmuCore/GS", "UserHacks_TextureInsideRt", false, manual_hw_fixes);

			MenuHeading("Upscaling Fixes");
			// Half Pixel Offset
			// Round Sprite
			// TC Offset X/Y
			DrawToggleSetting("Align Sprite", "Fixes issues with upscaling (vertical lines) in some games.", "EmuCore/GS",
				"UserHacks_align_sprite_X", false, manual_hw_fixes);
			DrawToggleSetting("Merge Sprite", "Replaces multiple post-processing sprites with a larger single sprite.", "EmuCore/GS",
				"UserHacks_merge_pp_sprite", false, manual_hw_fixes);
			DrawToggleSetting("Wild Arms Hack", "TODO", "EmuCore/GS", "UserHacks_WildHack", false, manual_hw_fixes);
		}
	}
	else
	{
		// extrathreads
		DrawToggleSetting("Auto Flush (Software)", "Force a primitive flush when a framebuffer is also an input texture.", "EmuCore/GS",
			"autoflush_sw", true);
		DrawToggleSetting("Edge AA (AA1)", "Enables emulation of the GS's edge anti-aliasing (AA1).", "EmuCore/GS", "aa1", true);
		DrawToggleSetting("Mipmapping", "Enables emulation of the GS's texture mipmapping.", "EmuCore/GS", "mipmap", true);
	}


	MenuHeading("On-Screen Display");
	// OSD scale
	DrawToggleSetting("Show Messages",
		"Shows on-screen-display messages when events occur such as save states being created/loaded, screenshots being taken, etc.",
		"EmuCore/GS", "OsdShowMessages", true);
	DrawToggleSetting("Show Speed",
		"Shows the current emulation speed of the system in the top-right corner of the display as a percentage.", "EmuCore/GS",
		"OsdShowSpeed", false);
	DrawToggleSetting("Show FPS",
		"Shows the number of video frames (or v-syncs) displayed per second by the system in the top-right corner of the display.",
		"EmuCore/GS", "OsdShowFPS", false);
	DrawToggleSetting("Show CPU Usage", "Shows the CPU usage based on threads in the top-right corner of the display.", "EmuCore/GS",
		"OsdShowCPU", false);
	DrawToggleSetting(
		"Show GPU Usage", "Shows the host's GPU usage in the top-right corner of the display.", "EmuCore/GS", "OsdShowGPU", false);
	DrawToggleSetting("Show Resolution", "Shows the resolution the game is rendering at in the top-right corner of the display.",
		"EmuCore/GS", "OsdShowResolution", false);
	DrawToggleSetting("Show GS Statistics", "Shows statistics about GS (primitives, draw calls) in the top-right corner of the display.",
		"EmuCore/GS", "OsdShowGSStats", false);
	DrawToggleSetting("Show Status Indicators", "Shows indicators when fast forwarding, pausing, and other abnormal states are active.",
		"EmuCore/GS", "OsdShowIndicators", true);

	MenuHeading("Advanced");


	EndMenuButtons();
}

void FullscreenUI::DrawAudioSettingsPage()
{
}

void FullscreenUI::DrawMemoryCardSettingsPage()
{
}

void FullscreenUI::DrawControllerSettingsPage()
{
}

void FullscreenUI::DrawHotkeySettingsPage()
{
}

void FullscreenUI::DrawQuickMenu(MainWindowType type)
{
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImVec2 display_size(ImGui::GetIO().DisplaySize);
	dl->AddRectFilled(ImVec2(0.0f, 0.0f), display_size, IM_COL32(0x21, 0x21, 0x21, 200));

	// title info
	{
		const ImVec2 title_size(
			g_large_font->CalcTextSizeA(g_large_font->FontSize, std::numeric_limits<float>::max(), -1.0f, s_current_game_title.c_str()));
		const ImVec2 subtitle_size(g_medium_font->CalcTextSizeA(
			g_medium_font->FontSize, std::numeric_limits<float>::max(), -1.0f, s_current_game_subtitle.c_str()));

		ImVec2 title_pos(display_size.x - LayoutScale(20.0f + 50.0f + 20.0f) - title_size.x, display_size.y - LayoutScale(20.0f + 50.0f));
		ImVec2 subtitle_pos(display_size.x - LayoutScale(20.0f + 50.0f + 20.0f) - subtitle_size.x,
			title_pos.y + g_large_font->FontSize + LayoutScale(4.0f));
		float rp_height = 0.0f;

		dl->AddText(g_large_font, g_large_font->FontSize, title_pos, IM_COL32(255, 255, 255, 255), s_current_game_title.c_str());
		dl->AddText(g_medium_font, g_medium_font->FontSize, subtitle_pos, IM_COL32(255, 255, 255, 255), s_current_game_subtitle.c_str());

		const ImVec2 image_min(
			display_size.x - LayoutScale(20.0f + 50.0f) - rp_height, display_size.y - LayoutScale(20.0f + 50.0f) - rp_height);
		const ImVec2 image_max(image_min.x + LayoutScale(50.0f) + rp_height, image_min.y + LayoutScale(50.0f) + rp_height);
		dl->AddImage(GetCoverForCurrentGame()->GetHandle(), image_min, image_max);
	}

	const ImVec2 window_size(LayoutScale(500.0f, LAYOUT_SCREEN_HEIGHT));
	const ImVec2 window_pos(0.0f, display_size.y - window_size.y);
	if (BeginFullscreenWindow(
			window_pos, window_size, "pause_menu", ImVec4(0.0f, 0.0f, 0.0f, 0.0f), 0.0f, 10.0f, ImGuiWindowFlags_NoBackground))
	{
		BeginMenuButtons(11, 1.0f, ImGuiFullscreen::LAYOUT_MENU_BUTTON_X_PADDING, ImGuiFullscreen::LAYOUT_MENU_BUTTON_Y_PADDING,
			ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);

		// NOTE: Menu close must come first, because otherwise VM destruction options will race.

		if (ActiveButton(ICON_FA_PLAY "  Resume Game", false) || WantsToCloseMenu())
			ClosePauseMenu();

		if (ActiveButton(ICON_FA_FAST_FORWARD "  Toggle Frame Limit", false))
		{
			ClosePauseMenu();
			DoToggleFrameLimit();
		}

		ActiveButton(ICON_FA_TROPHY "  Achievements", false, false);
		ActiveButton(ICON_FA_STOPWATCH "  Leaderboards", false, false);

		if (ActiveButton(ICON_FA_CAMERA "  Save Screenshot", false))
		{
			GSQueueSnapshot(std::string());
			ClosePauseMenu();
		}

		const bool can_load_or_save_state = !s_current_game_serial.empty();

		if (ActiveButton(ICON_FA_UNDO "  Load State", false, can_load_or_save_state))
		{
			s_current_main_window = MainWindowType::None;
			OpenSaveStateSelector(true);
		}

		if (ActiveButton(ICON_FA_SAVE "  Save State", false, can_load_or_save_state))
		{
			s_current_main_window = MainWindowType::None;
			OpenSaveStateSelector(false);
		}

		if (ActiveButton(ICON_FA_COMPACT_DISC "  Change Disc", false))
		{
			s_current_main_window = MainWindowType::None;
			DoChangeDisc();
		}

		if (ActiveButton(ICON_FA_SLIDERS_H "  Settings", false))
			SwitchToSettings();

		if (ActiveButton(ICON_FA_SYNC "  Reset System", false))
		{
			ClosePauseMenu();
			DoReset();
		}

		if (ActiveButton(ICON_FA_POWER_OFF "  Exit Game", false))
		{
			DoShutdown();
		}

		EndMenuButtons();

		EndFullscreenWindow();
	}
}

void FullscreenUI::InitializePlaceholderSaveStateListEntry(SaveStateListEntry* li, s32 slot)
{
	li->title = fmt::format("{0} Slot {1}##game_slot_{1}", s_current_game_title, slot);
	li->summary = "No Save State";
	li->path = {};
	li->slot = slot;
	li->preview_texture = {};
}

bool FullscreenUI::InitializeSaveStateListEntry(SaveStateListEntry* li, s32 slot)
{
	std::string filename(VMManager::GetSaveStateFileName(s_current_game_serial.c_str(), s_current_game_crc, slot));
	FILESYSTEM_STAT_DATA sd;
	if (filename.empty() || !FileSystem::StatFile(filename.c_str(), &sd))
	{
		InitializePlaceholderSaveStateListEntry(li, slot);
		return false;
	}

	li->title = fmt::format("{0} Slot {1}##game_slot_{1}", s_current_game_title, slot);
	li->summary = fmt::format("{0} - Saved {1}", s_current_game_serial, TimeToPrintableString(sd.ModificationTime));
	li->slot = slot;
	li->path = std::move(filename);

	li->preview_texture.reset();

	u32 screenshot_width, screenshot_height;
	std::vector<u32> screenshot_pixels;
	if (SaveState_ReadScreenshot(li->path, &screenshot_width, &screenshot_height, &screenshot_pixels))
	{
		li->preview_texture = Host::GetHostDisplay()->CreateTexture(
			screenshot_width, screenshot_height, screenshot_pixels.data(), sizeof(u32) * screenshot_width, false);
		if (!li->preview_texture)
			Console.Error("Failed to upload save state image to GPU");
	}

	return true;
}

void FullscreenUI::PopulateSaveStateListEntries()
{
	s_save_state_selector_slots.clear();

	for (s32 i = 0; i <= MAX_SAVE_STATE_SLOTS; i++)
	{
		SaveStateListEntry li;
		if (InitializeSaveStateListEntry(&li, i) || !s_save_state_selector_loading)
			s_save_state_selector_slots.push_back(std::move(li));
	}
}

void FullscreenUI::OpenSaveStateSelector(bool is_loading)
{
	s_save_state_selector_loading = is_loading;
	s_save_state_selector_open = true;
	s_save_state_selector_slots.clear();
	PopulateSaveStateListEntries();
}

void FullscreenUI::CloseSaveStateSelector()
{
	s_save_state_selector_slots.clear();
	s_save_state_selector_open = false;
	ReturnToMainWindow();
}

#if 0
void FullscreenUI::DrawSaveStateSelector(bool is_loading, bool fullscreen)
{
	const HostDisplayTexture* selected_texture = s_placeholder_texture.get();
	if (!BeginFullscreenColumns())
	{
		EndFullscreenColumns();
		return;
	}

	// drawn back the front so the hover changes the image
	if (BeginFullscreenColumnWindow(570.0f, LAYOUT_SCREEN_WIDTH, "save_state_selector_slots"))
	{
		BeginMenuButtons(static_cast<u32>(s_save_state_selector_slots.size()), true);

		for (const SaveStateListEntry& entry : s_save_state_selector_slots)
		{
			if (MenuButton(entry.title.c_str(), entry.summary.c_str()))
			{
				Host::RunOnCPUThread([path = entry.path]() {
					VMManager::LoadState(path.c_str());
				});
			}

			if (ImGui::IsItemHovered())
				selected_texture = entry.preview_texture.get();
		}

		EndMenuButtons();
	}
	EndFullscreenColumnWindow();

	if (BeginFullscreenColumnWindow(0.0f, 570.0f, "save_state_selector_preview", ImVec4(0.11f, 0.15f, 0.17f, 1.00f)))
	{
		ImGui::SetCursorPos(LayoutScale(20.0f, 20.0f));
		ImGui::PushFont(g_large_font);
		ImGui::TextUnformatted(is_loading ? ICON_FA_FOLDER_OPEN "  Load State" : ICON_FA_SAVE "  Save State");
		ImGui::PopFont();

		ImGui::SetCursorPos(LayoutScale(ImVec2(85.0f, 160.0f)));
		ImGui::Image(selected_texture ? selected_texture->GetHandle() : s_placeholder_texture->GetHandle(),
			LayoutScale(ImVec2(400.0f, 400.0f)));

		ImGui::SetCursorPosY(LayoutScale(670.0f));
		BeginMenuButtons(1, false);
		if (ActiveButton(ICON_FA_BACKWARD "  Back", false))
			ReturnToMainWindow();
		EndMenuButtons();
	}
	EndFullscreenColumnWindow();

	EndFullscreenColumns();
}
#else

void FullscreenUI::DrawSaveStateSelector(bool is_loading, bool fullscreen)
{
	if (fullscreen)
	{
		if (!BeginFullscreenColumns())
		{
			EndFullscreenColumns();
			return;
		}

		if (!BeginFullscreenColumnWindow(0.0f, LAYOUT_SCREEN_WIDTH, "save_state_selector_slots"))
		{
			EndFullscreenColumnWindow();
			EndFullscreenColumns();
			return;
		}
	}
	else
	{
		const char* window_title = is_loading ? "Load State" : "Save State";

		ImGui::PushFont(g_large_font);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(10.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
			LayoutScale(ImGuiFullscreen::LAYOUT_MENU_BUTTON_X_PADDING, ImGuiFullscreen::LAYOUT_MENU_BUTTON_Y_PADDING));

		ImGui::SetNextWindowSize(LayoutScale(1000.0f, 680.0f));
		ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::OpenPopup(window_title);
		bool is_open = !WantsToCloseMenu();
		if (!ImGui::BeginPopupModal(
				window_title, &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove) ||
			!is_open)
		{
			ImGui::PopStyleVar(2);
			ImGui::PopFont();
			CloseSaveStateSelector();
			return;
		}
	}

	BeginMenuButtons();

	constexpr float padding = 10.0f;
	constexpr float button_height = 96.0f;
	constexpr float max_image_width = 96.0f;
	constexpr float max_image_height = 96.0f;

	for (const SaveStateListEntry& entry : s_save_state_selector_slots)
	{
		ImRect bb;
		bool visible, hovered;
		bool pressed = MenuButtonFrame(entry.title.c_str(), true, button_height, &visible, &hovered, &bb.Min, &bb.Max);
		if (!visible)
			continue;

		ImVec2 pos(bb.Min);

		// use aspect ratio of screenshot to determine height
		const HostDisplayTexture* image = entry.preview_texture ? entry.preview_texture.get() : s_placeholder_texture.get();
		const float image_height = max_image_width / (static_cast<float>(image->GetWidth()) / static_cast<float>(image->GetHeight()));
		const float image_margin = (max_image_height - image_height) / 2.0f;
		const ImRect image_bb(
			ImVec2(pos.x, pos.y + LayoutScale(image_margin)), pos + LayoutScale(max_image_width, image_margin + image_height));
		pos.x += LayoutScale(max_image_width + padding);

		ImRect text_bb(pos, ImVec2(bb.Max.x, pos.y + g_large_font->FontSize));
		ImGui::PushFont(g_large_font);
		ImGui::RenderTextClipped(text_bb.Min, text_bb.Max, entry.title.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f), &text_bb);
		ImGui::PopFont();

		ImGui::PushFont(g_medium_font);

		if (!entry.summary.empty())
		{
			text_bb.Min.y = text_bb.Max.y + LayoutScale(4.0f);
			text_bb.Max.y = text_bb.Min.y + g_medium_font->FontSize;
			ImGui::RenderTextClipped(text_bb.Min, text_bb.Max, entry.summary.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f), &text_bb);
		}

		if (!entry.path.empty())
		{
			text_bb.Min.y = text_bb.Max.y + LayoutScale(4.0f);
			text_bb.Max.y = text_bb.Min.y + g_medium_font->FontSize;
			ImGui::RenderTextClipped(text_bb.Min, text_bb.Max, entry.path.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f), &text_bb);
		}

#if 0
    if (!entry.media_path.empty())
    {
      text_bb.Min.y = text_bb.Max.y + LayoutScale(4.0f);
      text_bb.Max.y = text_bb.Min.y + g_medium_font->FontSize;
      ImGui::RenderTextClipped(text_bb.Min, text_bb.Max, entry.media_path.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f),
        &text_bb);
    }
#endif

		ImGui::PopFont();

		ImGui::GetWindowDrawList()->AddImage(
			static_cast<ImTextureID>(entry.preview_texture ? entry.preview_texture->GetHandle() : s_placeholder_texture->GetHandle()),
			image_bb.Min, image_bb.Max);

		if (pressed)
		{
			if (is_loading)
			{
				Host::RunOnCPUThread([path = entry.path]() { VMManager::LoadState(path.c_str()); });
				CloseSaveStateSelector();
			}
			else
			{
				Host::RunOnCPUThread([slot = entry.slot]() { VMManager::SaveStateToSlot(slot); });
				CloseSaveStateSelector();
			}
		}
	}

	EndMenuButtons();

	if (fullscreen)
	{
		EndFullscreenColumnWindow();
		EndFullscreenColumns();
	}
	else
	{
		ImGui::EndPopup();
		ImGui::PopStyleVar(2);
		ImGui::PopFont();
	}
}
#endif

void FullscreenUI::PopulateGameListEntryList()
{
	const u32 count = GameList::GetEntryCount();
	s_game_list_sorted_entries.resize(count);
	for (u32 i = 0; i < count; i++)
		s_game_list_sorted_entries[i] = GameList::GetEntryByIndex(i);

	// TODO: Custom sort types
	std::sort(s_game_list_sorted_entries.begin(), s_game_list_sorted_entries.end(),
		[](const GameList::Entry* lhs, const GameList::Entry* rhs) { return lhs->title < rhs->title; });
}

void FullscreenUI::DrawGameListWindow()
{
	if (!BeginFullscreenColumns())
	{
		EndFullscreenColumns();
		return;
	}

	auto game_list_lock = GameList::GetLock();
	const GameList::Entry* selected_entry = nullptr;
	PopulateGameListEntryList();

	if (BeginFullscreenColumnWindow(450.0f, LAYOUT_SCREEN_WIDTH, "game_list_entries"))
	{
		const ImVec2 image_size(LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT, LAYOUT_MENU_BUTTON_HEIGHT));

		BeginMenuButtons();

		// TODO: replace with something not heap alllocating
		std::string summary;

		for (const GameList::Entry* entry : s_game_list_sorted_entries)
		{
			ImRect bb;
			bool visible, hovered;
			bool pressed = MenuButtonFrame(entry->path.c_str(), true, LAYOUT_MENU_BUTTON_HEIGHT, &visible, &hovered, &bb.Min, &bb.Max);
			if (!visible)
				continue;

			HostDisplayTexture* cover_texture = GetGameListCover(entry);

			summary.clear();
			if (entry->serial.empty())
				fmt::format_to(std::back_inserter(summary), "{} - ", GameList::RegionToString(entry->region));
			else
				fmt::format_to(std::back_inserter(summary), "{} - {} - ", entry->serial, GameList::RegionToString(entry->region));

			const std::string_view filename(Path::GetFileName(entry->path));
			summary.append(filename);

			ImGui::GetWindowDrawList()->AddImage(cover_texture->GetHandle(), bb.Min, bb.Min + image_size, ImVec2(0.0f, 0.0f),
				ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));

			const float midpoint = bb.Min.y + g_large_font->FontSize + LayoutScale(4.0f);
			const float text_start_x = bb.Min.x + image_size.x + LayoutScale(15.0f);
			const ImRect title_bb(ImVec2(text_start_x, bb.Min.y), ImVec2(bb.Max.x, midpoint));
			const ImRect summary_bb(ImVec2(text_start_x, midpoint), bb.Max);

			ImGui::PushFont(g_large_font);
			ImGui::RenderTextClipped(title_bb.Min, title_bb.Max, entry->title.c_str(), entry->title.c_str() + entry->title.size(), nullptr,
				ImVec2(0.0f, 0.0f), &title_bb);
			ImGui::PopFont();

			if (!summary.empty())
			{
				ImGui::PushFont(g_medium_font);
				ImGui::RenderTextClipped(
					summary_bb.Min, summary_bb.Max, summary.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f), &summary_bb);
				ImGui::PopFont();
			}

			if (pressed)
			{
				// launch game
				DoStartPath(entry->path, true);
			}

			if (hovered)
				selected_entry = entry;
		}

		EndMenuButtons();
	}
	EndFullscreenColumnWindow();

	if (BeginFullscreenColumnWindow(0.0f, 450.0f, "game_list_info", ImVec4(0.11f, 0.15f, 0.17f, 1.00f)))
	{
		ImGui::SetCursorPos(LayoutScale(ImVec2(50.0f, 50.0f)));
		ImGui::Image(selected_entry ? GetGameListCover(selected_entry)->GetHandle() :
                                      GetTextureForGameListEntryType(GameList::EntryType::Count)->GetHandle(),
			LayoutScale(ImVec2(350.0f, 350.0f)));

		const float work_width = ImGui::GetCurrentWindow()->WorkRect.GetWidth();
		constexpr float field_margin_y = 10.0f;
		constexpr float start_x = 50.0f;
		float text_y = 425.0f;
		float text_width;

		ImGui::SetCursorPos(LayoutScale(start_x, text_y));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, field_margin_y));
		ImGui::BeginGroup();

		if (selected_entry)
		{
			// title
			ImGui::PushFont(g_large_font);
			text_width = ImGui::CalcTextSize(selected_entry->title.c_str(), nullptr, false, work_width).x;
			ImGui::SetCursorPosX((work_width - text_width) / 2.0f);
			ImGui::TextWrapped("%s", selected_entry->title.c_str());
			ImGui::PopFont();

			ImGui::PushFont(g_medium_font);

			// developer
			const char* developer = "Unknown Developer";
			if (true)
			{
				text_width = ImGui::CalcTextSize(developer, nullptr, false, work_width).x;
				ImGui::SetCursorPosX((work_width - text_width) / 2.0f);
				ImGui::TextWrapped("%s", developer);
			}

			// code
			text_width = ImGui::CalcTextSize(selected_entry->serial.c_str(), nullptr, false, work_width).x;
			ImGui::SetCursorPosX((work_width - text_width) / 2.0f);
			ImGui::TextWrapped("%s", selected_entry->serial.c_str());
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15.0f);

			// region
			ImGui::TextUnformatted("Region: ");
			ImGui::SameLine();
			ImGui::Image(s_disc_region_textures[static_cast<u32>(selected_entry->region)]->GetHandle(), LayoutScale(23.0f, 16.0f));
			ImGui::SameLine();
			ImGui::Text(" (%s)", GameList::RegionToString(selected_entry->region));

			// genre
			const char* genre = "Unknown";
			ImGui::Text("Genre: %s", genre);

			// release date
			const char* release_date_str = "Unknown";
			ImGui::Text("Release Date: %s", release_date_str);

			// compatibility
			ImGui::TextUnformatted("Compatibility: ");
			ImGui::SameLine();
			ImGui::Image(s_game_compatibility_textures[static_cast<u32>(selected_entry->compatibility_rating)]->GetHandle(),
				LayoutScale(64.0f, 16.0f));
			ImGui::SameLine();
			ImGui::Text(" (%s)", GameList::EntryCompatibilityRatingToString(selected_entry->compatibility_rating));

			// size
			ImGui::Text("Size: %.2f MB", static_cast<float>(selected_entry->total_size) / 1048576.0f);

			// game settings
			const u32 user_setting_count = 0; // FIXME
			if (user_setting_count > 0)
				ImGui::Text("%u Per-Game Settings Set", user_setting_count);
			else
				ImGui::TextUnformatted("No Per-Game Settings Set");

			ImGui::PopFont();
		}
		else
		{
			// title
			const char* title = "No Game Selected";
			ImGui::PushFont(g_large_font);
			text_width = ImGui::CalcTextSize(title, nullptr, false, work_width).x;
			ImGui::SetCursorPosX((work_width - text_width) / 2.0f);
			ImGui::TextWrapped("%s", title);
			ImGui::PopFont();
		}

		ImGui::EndGroup();
		ImGui::PopStyleVar();

		ImGui::SetCursorPosY(ImGui::GetWindowHeight() - LayoutScale(50.0f));
		BeginMenuButtons();
		if (ActiveButton(ICON_FA_BACKWARD "  Back", false))
			ReturnToMainWindow();
		EndMenuButtons();
	}
	EndFullscreenColumnWindow();

	EndFullscreenColumns();
}

void FullscreenUI::SwitchToGameList()
{
	s_current_main_window = MainWindowType::GameList;
}

HostDisplayTexture* FullscreenUI::GetGameListCover(const GameList::Entry* entry)
{
	// lookup and grab cover image
	auto cover_it = s_cover_image_map.find(entry->path);
	if (cover_it == s_cover_image_map.end())
	{
		std::string cover_path(GameList::GetCoverImagePathForEntry(entry));
		cover_it = s_cover_image_map.emplace(entry->path, std::move(cover_path)).first;
	}

	if (!cover_it->second.empty())
		return GetCachedTexture(cover_it->second);

	return GetTextureForGameListEntryType(entry->type);
}

HostDisplayTexture* FullscreenUI::GetTextureForGameListEntryType(GameList::EntryType type)
{
	switch (type)
	{
		case GameList::EntryType::ELF:
			return s_fallback_exe_texture.get();

		case GameList::EntryType::Playlist:
			return s_fallback_playlist_texture.get();

		case GameList::EntryType::PS1Disc:
		case GameList::EntryType::PS2Disc:
		default:
			return s_fallback_disc_texture.get();
	}
}

HostDisplayTexture* FullscreenUI::GetCoverForCurrentGame()
{
	auto lock = GameList::GetLock();

	const GameList::Entry* entry = GameList::GetEntryForPath(s_current_game_path.c_str());
	if (!entry)
		return s_fallback_disc_texture.get();

	return GetGameListCover(entry);
}

//////////////////////////////////////////////////////////////////////////
// Overlays
//////////////////////////////////////////////////////////////////////////

void FullscreenUI::OpenAboutWindow()
{
	s_about_window_open = true;
}

void FullscreenUI::DrawAboutWindow()
{
	ImGui::SetNextWindowSize(LayoutScale(1000.0f, 500.0f));
	ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::OpenPopup("About PCSX2");

	ImGui::PushFont(g_large_font);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(10.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, LayoutScale(10.0f, 10.0f));

	if (ImGui::BeginPopupModal("About PCSX2", &s_about_window_open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
	{
		ImGui::TextWrapped("TODO: Complete me...");

		ImGui::NewLine();

		BeginMenuButtons();
		if (ActiveButton(ICON_FA_WINDOW_CLOSE "  Close", false))
		{
			ImGui::CloseCurrentPopup();
			s_about_window_open = false;
		}
		EndMenuButtons();

		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(2);
	ImGui::PopFont();
}

bool FullscreenUI::DrawErrorWindow(const char* message)
{
	bool is_open = true;

	ImGuiFullscreen::BeginLayout();

	ImGui::SetNextWindowSize(LayoutScale(500.0f, 0.0f));
	ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::OpenPopup("ReportError");

	ImGui::PushFont(g_large_font);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(10.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, LayoutScale(10.0f, 10.0f));

	if (ImGui::BeginPopupModal("ReportError", &is_open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
	{
		ImGui::SetCursorPos(LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING, LAYOUT_MENU_BUTTON_Y_PADDING));
		ImGui::TextWrapped("%s", message);
		ImGui::GetCurrentWindow()->DC.CursorPos.y += LayoutScale(5.0f);

		BeginMenuButtons();

		if (ActiveButton(ICON_FA_WINDOW_CLOSE "  Close", false))
		{
			ImGui::CloseCurrentPopup();
			is_open = false;
		}
		EndMenuButtons();

		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(2);
	ImGui::PopFont();

	ImGuiFullscreen::EndLayout();
	return !is_open;
}

bool FullscreenUI::DrawConfirmWindow(const char* message, bool* result)
{
	bool is_open = true;

	ImGuiFullscreen::BeginLayout();

	ImGui::SetNextWindowSize(LayoutScale(500.0f, 0.0f));
	ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::OpenPopup("ConfirmMessage");

	ImGui::PushFont(g_large_font);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(10.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, LayoutScale(10.0f, 10.0f));

	if (ImGui::BeginPopupModal("ConfirmMessage", &is_open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
	{
		ImGui::SetCursorPos(LayoutScale(LAYOUT_MENU_BUTTON_X_PADDING, LAYOUT_MENU_BUTTON_Y_PADDING));
		ImGui::TextWrapped("%s", message);
		ImGui::GetCurrentWindow()->DC.CursorPos.y += LayoutScale(5.0f);

		BeginMenuButtons();

		bool done = false;

		if (ActiveButton(ICON_FA_CHECK "  Yes", false))
		{
			*result = true;
			done = true;
		}

		if (ActiveButton(ICON_FA_TIMES "  No", false))
		{
			*result = false;
			done = true;
		}
		if (done)
		{
			ImGui::CloseCurrentPopup();
			is_open = false;
		}

		EndMenuButtons();

		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(2);
	ImGui::PopFont();

	ImGuiFullscreen::EndLayout();
	return !is_open;
}

#if 0
bool SetControllerNavInput(FrontendCommon::ControllerNavigationButton button, bool value)
{
  s_nav_input_values[static_cast<u32>(button)] = value;
  if (!HasActiveWindow())
    return false;

#if 0
  // This is a bit hacky..
  ImGuiIO& io = ImGui::GetIO();

#define MAP_KEY(nbutton, imkey) \
	if (button == nbutton) \
	{ \
		io.KeysDown[io.KeyMap[imkey]] = value; \
	}

  MAP_KEY(FrontendCommon::ControllerNavigationButton::LeftTrigger, ImGuiKey_PageUp);
  MAP_KEY(FrontendCommon::ControllerNavigationButton::RightTrigger, ImGuiKey_PageDown);

#undef MAP_KEY
#endif

  return true;
}

void SetImGuiNavInputs()
{
  if (!HasActiveWindow())
    return;

  ImGuiIO& io = ImGui::GetIO();

#define MAP_BUTTON(button, imbutton) io.NavInputs[imbutton] = s_nav_input_values[static_cast<u32>(button)] ? 1.0f : 0.0f

  MAP_BUTTON(FrontendCommon::ControllerNavigationButton::Activate, ImGuiNavInput_Activate);
  MAP_BUTTON(FrontendCommon::ControllerNavigationButton::Cancel, ImGuiNavInput_Cancel);
  MAP_BUTTON(FrontendCommon::ControllerNavigationButton::DPadLeft, ImGuiNavInput_DpadLeft);
  MAP_BUTTON(FrontendCommon::ControllerNavigationButton::DPadRight, ImGuiNavInput_DpadRight);
  MAP_BUTTON(FrontendCommon::ControllerNavigationButton::DPadUp, ImGuiNavInput_DpadUp);
  MAP_BUTTON(FrontendCommon::ControllerNavigationButton::DPadDown, ImGuiNavInput_DpadDown);
  MAP_BUTTON(FrontendCommon::ControllerNavigationButton::LeftShoulder, ImGuiNavInput_FocusPrev);
  MAP_BUTTON(FrontendCommon::ControllerNavigationButton::RightShoulder, ImGuiNavInput_FocusNext);

#undef MAP_BUTTON
}
#endif

FullscreenUI::ProgressCallback::ProgressCallback(std::string name)
	: BaseProgressCallback()
	, m_name(std::move(name))
{
	ImGuiFullscreen::OpenBackgroundProgressDialog(m_name.c_str(), "", 0, 100, 0);
}

FullscreenUI::ProgressCallback::~ProgressCallback()
{
	ImGuiFullscreen::CloseBackgroundProgressDialog(m_name.c_str());
}

void FullscreenUI::ProgressCallback::PushState()
{
	BaseProgressCallback::PushState();
}

void FullscreenUI::ProgressCallback::PopState()
{
	BaseProgressCallback::PopState();
	Redraw(true);
}

void FullscreenUI::ProgressCallback::SetCancellable(bool cancellable)
{
	BaseProgressCallback::SetCancellable(cancellable);
	Redraw(true);
}

void FullscreenUI::ProgressCallback::SetTitle(const char* title)
{
	// todo?
}

void FullscreenUI::ProgressCallback::SetStatusText(const char* text)
{
	BaseProgressCallback::SetStatusText(text);
	Redraw(true);
}

void FullscreenUI::ProgressCallback::SetProgressRange(u32 range)
{
	u32 last_range = m_progress_range;

	BaseProgressCallback::SetProgressRange(range);

	if (m_progress_range != last_range)
		Redraw(false);
}

void FullscreenUI::ProgressCallback::SetProgressValue(u32 value)
{
	u32 lastValue = m_progress_value;

	BaseProgressCallback::SetProgressValue(value);

	if (m_progress_value != lastValue)
		Redraw(false);
}

void FullscreenUI::ProgressCallback::Redraw(bool force)
{
	const int percent = static_cast<int>((static_cast<float>(m_progress_value) / static_cast<float>(m_progress_range)) * 100.0f);
	if (percent == m_last_progress_percent && !force)
		return;

	m_last_progress_percent = percent;
	ImGuiFullscreen::UpdateBackgroundProgressDialog(m_name.c_str(), m_status_text.c_str(), 0, 100, percent);
}

void FullscreenUI::ProgressCallback::DisplayError(const char* message)
{
	Console.Error(message);
	Host::ReportErrorAsync("Error", message);
}

void FullscreenUI::ProgressCallback::DisplayWarning(const char* message)
{
	Console.Warning(message);
}

void FullscreenUI::ProgressCallback::DisplayInformation(const char* message)
{
	Console.WriteLn(message);
}

void FullscreenUI::ProgressCallback::DisplayDebugMessage(const char* message)
{
	DevCon.WriteLn(message);
}

void FullscreenUI::ProgressCallback::ModalError(const char* message)
{
	Console.Error(message);
	Host::ReportErrorAsync("Error", message);
}

bool FullscreenUI::ProgressCallback::ModalConfirmation(const char* message)
{
	return false;
}

void FullscreenUI::ProgressCallback::ModalInformation(const char* message)
{
	Console.WriteLn(message);
}

void FullscreenUI::ProgressCallback::SetCancelled()
{
	if (m_cancellable)
		m_cancelled = true;
}
