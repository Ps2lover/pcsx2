#include "PrecompiledHeader.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include "Frontend/FullscreenUI.h"
#include "Frontend/ImGuiManager.h"
#include "Frontend/ImGuiFullscreen.h"
#include "Frontend/GameList.h"
#include "IconsFontAwesome5.h"

#include "common/FileSystem.h"
#include "common/Console.h"
#include "common/Image.h"
#include "common/SettingsInterface.h"
#include "common/SettingsWrapper.h"
#include "common/StringUtil.h"
#include "common/Timer.h"

#include "GS.h"
#include "Host.h"
#include "HostDisplay.h"
#include "HostSettings.h"
#include "VMManager.h"

#include "imgui.h"
#include "imgui_internal.h"
//#include "imgui_stdlib.h"

#include "fmt/core.h"

#include <array>
#include <bitset>
#include <thread>

#ifdef ENABLE_ACHIEVEMENTS
#include "Frontend/Achievements.h"
#endif

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
	static void LoadSettings();
	static void ClearImGuiFocus();
	static void PauseForMenuOpen();
	static bool WantsToCloseMenu();
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
	static std::array<std::unique_ptr<HostDisplayTexture>, static_cast<u32>(GameDatabaseSchema::Compatibility::Perfect) + 1> s_game_compatibility_textures;
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
	static bool ToggleButtonForNonSetting(const char* title, const char* summary, const char* section, const char* key,
		bool default_value, bool enabled = true,
		float height = ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT,
		ImFont* font = g_large_font, ImFont* summary_font = g_medium_font);
	static void SettingsToggleButton(const char* title, const char* summary, bool* v, bool enabled = true,
		float height = LAYOUT_MENU_BUTTON_HEIGHT, ImFont* font = g_large_font,
		ImFont* summary_font = g_medium_font);
	static bool BitfieldToggleButton(const char* title, const char* summary, bool v, bool enabled = true,
		float height = LAYOUT_MENU_BUTTON_HEIGHT, ImFont* font = g_large_font,
		ImFont* summary_font = g_medium_font);
	static void PopulateGameListDirectoryCache(SettingsInterface* si);
	static ImGuiFullscreen::ChoiceDialogOptions GetGameListDirectoryOptions(bool recursive_as_checked);
	static void BeginInputBinding(InputBindingType type, const std::string_view& section, const std::string_view& key,
		const std::string_view& display_name);
	static void EndInputBinding();
	static void ClearInputBinding(const char* section, const char* key);
	static void DrawInputBindingWindow();
	static void DrawInputBindingButton(InputBindingType type, const char* section, const char* name, const char* display_name, bool show_type = true);
	static void ClearInputBindingVariables();

	static SettingsPage s_settings_page = SettingsPage::InterfaceSettings;
	static Pcsx2Config s_settings_cache;
	static std::vector<std::pair<std::string, bool>> s_game_list_directories_cache;
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
	static HostDisplayTexture* GetTextureForGameListEntryType(GameList::EntryType type);
	static HostDisplayTexture* GetGameListCover(const GameList::Entry* entry);
	static HostDisplayTexture* GetCoverForCurrentGame();

	// Lazily populated cover images.
	static std::unordered_map<std::string, std::string> s_cover_image_map;
	static std::vector<const GameList::Entry*> s_game_list_sorted_entries;
	static std::thread s_game_list_load_thread;
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

	ImGuiFullscreen::UpdateLayoutScale();
	ImGuiFullscreen::SetLoadTextureFunction(LoadTextureCallback);

	if (!ImGuiManager::AddFullscreenFontsIfMissing() || !LoadResources())
	{
		ImGuiFullscreen::ClearState();
		s_tried_to_initialize = true;
		return false;
	}

	LoadSettings();
#if 0
  UpdateDebugMenuVisibility();
#endif

	GetMTGS().SetRunIdle(true);
	s_initialized = true;

	if (VMManager::HasValidVM())
	{
		OnVMStarted();
		OnRunningGameChanged(VMManager::GetDiscPath(), VMManager::GetGameSerial(), VMManager::GetGameName(), VMManager::GetGameCRC());
	}
	else
	{
		SwitchToLanding();
	}

	return true;
}

bool FullscreenUI::IsInitialized()
{
	return s_initialized;
}

bool FullscreenUI::HasActiveWindow()
{
	return s_current_main_window != MainWindowType::None || s_save_state_selector_open ||
		   ImGuiFullscreen::IsChoiceDialogOpen() || ImGuiFullscreen::IsFileSelectorOpen();
}

void FullscreenUI::LoadSettings()
{
}

void FullscreenUI::UpdateSettings()
{
	LoadSettings();
}

void FullscreenUI::OnVMStarted()
{
	if (!IsInitialized())
		return;

	s_current_main_window = MainWindowType::None;
	ClearImGuiFocus();
}

void FullscreenUI::OnVMDestroyed()
{
	if (!IsInitialized())
		return;

	s_quick_menu_was_open = false;
	SwitchToLanding();
}

void FullscreenUI::OnRunningGameChanged(std::string path, std::string serial, std::string title, u32 crc)
{
	if (!IsInitialized())
		return;

	if (!serial.empty())
		s_current_game_subtitle = fmt::format("{0} - {1}", serial, FileSystem::GetFileNameFromPath(path));
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
	if (/*s_settings_copy.pause_on_menu && */ !s_was_paused_on_quick_menu_open)
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
	if (!Initialize() || !VMManager::HasValidVM() || s_current_main_window != MainWindowType::None)
		return;

	PauseForMenuOpen();

	s_current_main_window = MainWindowType::QuickMenu;
	ClearImGuiFocus();
}

void FullscreenUI::CloseQuickMenu()
{
	if (!IsInitialized() || !VMManager::HasValidVM())
		return;

	if (VMManager::GetState() == VMState::Paused && !s_was_paused_on_quick_menu_open)
		Host::RunOnCPUThread([]() { VMManager::SetPaused(false); });

	s_current_main_window = MainWindowType::None;
	s_quick_menu_was_open = false;
	ClearImGuiFocus();
}

#ifdef ENABLE_ACHIEVEMENTS

bool FullscreenUI::OpenAchievementsWindow()
{
	const bool achievements_enabled = Achievements::HasActiveGame() && (Achievements::GetAchievementCount() > 0);
	if (!achievements_enabled)
		return false;

	if (!s_quick_menu_was_open)
		PauseForMenuOpen();

	s_current_main_window = MainWindowType::Achievements;
	return true;
}

bool FullscreenUI::OpenLeaderboardsWindow()
{
	const bool leaderboards_enabled = Achievements::HasActiveGame() && (Achievements::GetLeaderboardCount() > 0);
	if (!leaderboards_enabled)
		return false;

	if (!s_quick_menu_was_open)
		PauseForMenuOpen();

	s_current_main_window = MainWindowType::Leaderboards;
	s_open_leaderboard_id.reset();
	return true;
}

#endif

void FullscreenUI::Shutdown()
{
	if (s_game_list_load_thread.joinable())
		s_game_list_load_thread.join();

	CloseSaveStateSelector();
	s_cover_image_map.clear();
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

#if 0
  if (s_debug_menu_enabled)
    DrawDebugMenu();
#endif

#if 0
  if (System::IsValid())
  {
    if (!s_debug_menu_enabled)
      s_host_interface->DrawStatsOverlay();

    if (!IsAchievementsHardcoreModeActive())
      s_host_interface->DrawDebugWindows();
  }
#endif

	ImGuiFullscreen::BeginLayout();

	switch (s_current_main_window)
	{
		case MainWindowType::Landing:
			DrawLandingWindow();
			break;
		case MainWindowType::GameList:
#if 0
      DrawGameListWindow();
#endif
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
}

void FullscreenUI::ClearImGuiFocus()
{
	ImGui::SetWindowFocus(nullptr);
	s_close_button_state = 0;
}

void FullscreenUI::ReturnToMainWindow()
{
	if (s_quick_menu_was_open)
		CloseQuickMenu();

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

	std::unique_ptr<HostDisplayTexture> texture = Host::GetHostDisplay()->CreateTexture(
		image.GetWidth(), image.GetHeight(), image.GetPixels(), image.GetByteStride());
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

#if 0
  texture = g_host_interface->GetDisplay()->CreateTexture(PLACEHOLDER_ICON_WIDTH, PLACEHOLDER_ICON_HEIGHT, 1, 1, 1,
                                                          HostDisplayPixelFormat::RGBA8, PLACEHOLDER_ICON_DATA,
                                                          sizeof(u32) * PLACEHOLDER_ICON_WIDTH, false);
  if (!texture)
    Panic("Failed to create placeholder texture");

  return texture;
#else
	return {};
#endif
}

//////////////////////////////////////////////////////////////////////////
// Utility
//////////////////////////////////////////////////////////////////////////

ImGuiFullscreen::FileSelectorFilters FullscreenUI::GetDiscImageFilters()
{
	return {"*.bin", "*.iso", "*.cue", "*.chd", "*.cso", "*.gz", "*.elf", "*.irx", "*.m3u", "*.gs", "*.gs.xz"};
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

		VMManager::SetLimiterMode((EmuConfig.LimiterMode != LimiterModeType::Unlimited) ?
									  LimiterModeType::Unlimited :
                                      LimiterModeType::Nominal);
	});
}

void FullscreenUI::DoShutdown()
{
	Host::RunOnCPUThread([]() {
		if (!VMManager::HasValidVM())
			return;

		VMManager::SetState(VMState::Stopping);
	});
}

void FullscreenUI::DoReset()
{
	Host::RunOnCPUThread([]() {
		if (!VMManager::HasValidVM())
			return;

		VMManager::Reset();
	});
}

#if 0

void FullscreenUI::DoChangeDiscFromFile()
{
  auto callback = [](const std::string& path) {
    if (!path.empty())
      System::InsertMedia(path.c_str());

    ClearImGuiFocus();
    CloseFileSelector();
    ReturnToMainWindow();
  };

  OpenFileSelector(ICON_FA_COMPACT_DISC "  Select Disc Image", false, std::move(callback), GetDiscImageFilters(),
                   std::string(FileSystem::GetPathDirectory(System::GetMediaFileName())));
}

void FullscreenUI::DoChangeDisc()
{
  if (!System::HasMediaSubImages())
  {
    DoChangeDiscFromFile();
    return;
  }

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
}
#endif

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
		ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() * 0.5f) - (image_size * 0.5f),
			(ImGui::GetWindowHeight() * 0.5f) - (image_size * 0.5f)));
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

		if (MenuButton(" " ICON_FA_LIST "  Open Game List",
				"Launch a game from images scanned from your game directories."))
		{
			// SwitchToGameList();
		}

		if (MenuButton(" " ICON_FA_SLIDERS_H "  Settings", "Change settings for the emulator."))
			SwitchToSettings();

		if (MenuButton(" " ICON_FA_SIGN_OUT_ALT "  Exit", "Exits the program."))
		{
			//s_host_interface->RequestExit();
		}

		{
			ImVec2 fullscreen_pos;
			if (FloatingButton(ICON_FA_WINDOW_CLOSE, 0.0f, 0.0f, -1.0f, -1.0f, 1.0f, 0.0f, true, g_large_font,
					&fullscreen_pos))
			{
				//s_host_interface->RequestExit();
			}

			if (FloatingButton(ICON_FA_EXPAND, fullscreen_pos.x, 0.0f, -1.0f, -1.0f, -1.0f, 0.0f, true, g_large_font,
					&fullscreen_pos))
			{
				//s_host_interface->RunLater([]() { s_host_interface->SetFullscreen(!s_host_interface->IsFullscreen()); });
			}

			if (FloatingButton(ICON_FA_QUESTION_CIRCLE, fullscreen_pos.x, 0.0f, -1.0f, -1.0f, -1.0f, 0.0f))
				OpenAboutWindow();
		}

		EndMenuButtons();
	}

	EndFullscreenColumnWindow();

	EndFullscreenColumns();
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

bool FullscreenUI::ToggleButtonForNonSetting(const char* title, const char* summary, const char* section, const char* key,
	bool default_value, bool enabled, float height, ImFont* font, ImFont* summary_font)
{
	auto lock = Host::GetSettingsLock();
	SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();

	bool value = bsi->GetBoolValue(section, key, default_value);
	if (!ToggleButton(title, summary, &value, enabled, height, font, summary_font))
		return false;

	bsi->SetBoolValue(section, key, value);
	Host::RunOnCPUThread([]() { VMManager::ApplySettings(); });
	return true;
}

void FullscreenUI::SettingsToggleButton(const char* title, const char* summary, bool* v, bool enabled,
	float height, ImFont* font, ImFont* summary_font)
{
	s_settings_changed |= ToggleButton(title, summary, v, enabled, height, font, summary_font);
}

bool FullscreenUI::BitfieldToggleButton(const char* title, const char* summary, bool v, bool enabled,
	float height, ImFont* font, ImFont* summary_font)
{
	SettingsToggleButton(title, summary, &v, enabled, height, font, summary_font);
	return v;
}

void FullscreenUI::SwitchToSettings()
{
	// populate the cache with all settings from ini
	auto lock = Host::GetSettingsLock();
	SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();

	s_settings_cache.LoadSave(SettingsLoadWrapper(*bsi));
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
	ImVec2 heading_size = ImVec2(
		io.DisplaySize.x, LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY + LAYOUT_MENU_BUTTON_Y_PADDING * 2.0f + 2.0f));

	if (BeginFullscreenWindow(ImVec2(0.0f, ImGuiFullscreen::g_menu_bar_size), heading_size, "settings_category",
			ImVec4(0.18f, 0.18f, 0.18f, 1.00f)))
	{
		static constexpr float ITEM_WIDTH = 22.0f;

		static constexpr std::array<const char*, static_cast<u32>(SettingsPage::Count)> icons = {
			{ICON_FA_WINDOW_MAXIMIZE, ICON_FA_LIST, ICON_FA_MICROCHIP, ICON_FA_SLIDERS_H, ICON_FA_HDD,
				ICON_FA_MAGIC, ICON_FA_HEADPHONES, ICON_FA_SD_CARD, ICON_FA_GAMEPAD, ICON_FA_KEYBOARD,
				ICON_FA_TROPHY}};

		static constexpr std::array<const char*, static_cast<u32>(SettingsPage::Count)> titles = {
			{"Interface Settings", "Game List Settings", "BIOS Settings", "Emulation Settings", "System Settings",
				"Graphics Settings", "Audio Settings", "Memory Card Settings", "Controller Settings", "Hotkey Settings",
				"Achievements Settings"}};

		BeginNavBar();

		if (ImGui::IsNavInputTest(ImGuiNavInput_FocusPrev, ImGuiInputReadMode_Pressed))
		{
			s_settings_page = static_cast<SettingsPage>((s_settings_page == static_cast<SettingsPage>(0)) ?
															(static_cast<u32>(SettingsPage::Count) - 1) :
                                                            (static_cast<u32>(s_settings_page) - 1));
		}
		else if (ImGui::IsNavInputTest(ImGuiNavInput_FocusNext, ImGuiInputReadMode_Pressed))
		{
			s_settings_page =
				static_cast<SettingsPage>((static_cast<u32>(s_settings_page) + 1) % static_cast<u32>(SettingsPage::Count));
		}

		if (NavButton(ICON_FA_BACKWARD, false, true))
			ReturnToMainWindow();

		NavTitle(titles[static_cast<u32>(s_settings_page)]);

		RightAlignNavButtons(static_cast<u32>(titles.size()), ITEM_WIDTH, LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);

		for (u32 i = 0; i < static_cast<u32>(titles.size()); i++)
		{
			if (NavButton(icons[i], s_settings_page == static_cast<SettingsPage>(i), true, ITEM_WIDTH,
					LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY))
			{
				s_settings_page = static_cast<SettingsPage>(i);
			}
		}

		EndNavBar();
	}

	EndFullscreenWindow();

	if (BeginFullscreenWindow(
			ImVec2(0.0f, ImGuiFullscreen::g_menu_bar_size + heading_size.y),
			ImVec2(io.DisplaySize.x, io.DisplaySize.y - heading_size.y - ImGuiFullscreen::g_menu_bar_size),
			"settings_parent"))
	{
		if (ImGui::IsNavInputTest(ImGuiNavInput_Cancel, ImGuiInputReadMode_Pressed))
		{
			if (ImGui::IsWindowFocused())
				ReturnToMainWindow();
		}

		switch (s_settings_page)
		{
			case SettingsPage::InterfaceSettings:
				DrawInterfaceSettingsPage();
				break;

			case SettingsPage::GameListSettings:
				DrawGameListSettingsPage();
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

	MenuHeading("Behavior");

	ToggleButtonForNonSetting("Pause On Start", "Pauses the emulator when a game is started.", "UI", "StartPaused", false);
	ToggleButtonForNonSetting("Pause On Focus Loss",
		"Pauses the emulator when you minimize the window or switch to another "
		"application, and unpauses when you switch back.",
		"UI", "PauseOnFocusLoss", false);
	ToggleButtonForNonSetting(
		"Pause On Menu", "Pauses the emulator when you open the quick menu, and unpauses when you close it.",
		"UI", "PauseOnFocusLoss", true);
	ToggleButtonForNonSetting("Confirm Shutdown",
		"Determines whether a prompt will be displayed to confirm shutting down the emulator/game "
		"when the hotkey is pressed.",
		"UI", "ConfirmShutdown", true);
	s_settings_cache.SaveStateOnShutdown = BitfieldToggleButton("Save State On Shutdown",
		"Automatically saves the emulator state when powering down or exiting. You can then "
		"resume directly from where you left off next time.",
		s_settings_cache.SaveStateOnShutdown);
	ToggleButtonForNonSetting("Start Fullscreen", "Automatically switches to fullscreen mode when the program is started.",
		"UI", "StartFullscreen", false);
	ToggleButtonForNonSetting(
		"Hide Cursor In Fullscreen", "Hides the mouse pointer/cursor when the emulator is in fullscreen mode.",
		"UI", "HideMouseCursor", false);
	ToggleButtonForNonSetting(
		"Inhibit Screensaver",
		"Prevents the screen saver from activating and the host from sleeping while emulation is running.",
		"UI", "InhibitScreensaver", true);

#ifdef WITH_DISCORD_PRESENCE
	MenuHeading("Integration");
	ToggleButtonForNonSetting(
		"Enable Discord Presence", "Shows the game you are currently playing as part of your profile on Discord.",
		"Main", "EnableDiscordPresence", false);
#endif

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

#if 0
        QueueGameListRefresh();
#endif
			}

			CloseFileSelector();
		});
	}

	if (MenuButton(ICON_FA_FOLDER_OPEN "  Change Recursive Directories",
			"Sets whether subdirectories are searched for each game directory"))
	{
		OpenChoiceDialog(
			ICON_FA_FOLDER_OPEN "  Change Recursive Directories", true, GetGameListDirectoryOptions(true),
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
#if 0
      QueueGameListRefresh();
#endif
			});
	}

	if (MenuButton(ICON_FA_FOLDER_MINUS "  Remove Search Directory",
			"Removes a directory from the game search list."))
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
#if 0
      QueueGameListRefresh();
#endif
				CloseChoiceDialog();
			});
	}

	MenuHeading("Search Directories");
	for (const auto& it : s_game_list_directories_cache)
		MenuButton(it.first.c_str(), it.second ? "Scanning Subdirectories" : "Not Scanning Subdirectories", false);

	EndMenuButtons();
}

#if 0
      case SettingsPage::ConsoleSettings:
        {
          static constexpr auto cdrom_read_speeds =
            make_array("None (Double Speed)", "2x (Quad Speed)", "3x (6x Speed)", "4x (8x Speed)", "5x (10x Speed)",
              "6x (12x Speed)", "7x (14x Speed)", "8x (16x Speed)", "9x (18x Speed)", "10x (20x Speed)");

          static constexpr auto cdrom_seek_speeds = make_array("Infinite/Instantaneous", "None (Normal Speed)", "2x",
            "3x", "4x", "5x", "6x", "7x", "8x", "9x", "10x");

          BeginMenuButtons();

          MenuHeading("Console Settings");

          settings_changed |=
            EnumChoiceButton("Region", "Determines the emulated hardware type.", &s_settings_copy.region,
              &Settings::GetConsoleRegionDisplayName, ConsoleRegion::Count);

          MenuHeading("CPU Emulation (MIPS R3000A Derivative)");

          settings_changed |= EnumChoiceButton(
            "Execution Mode", "Determines how the emulated CPU executes instructions. Recompiler is recommended.",
            &s_settings_copy.cpu_execution_mode, &Settings::GetCPUExecutionModeDisplayName, CPUExecutionMode::Count);

          settings_changed |=
            ToggleButton("Enable Overclocking", "When this option is chosen, the clock speed set below will be used.",
              &s_settings_copy.cpu_overclock_enable);

          s32 overclock_percent =
            s_settings_copy.cpu_overclock_enable ? static_cast<s32>(s_settings_copy.GetCPUOverclockPercent()) : 100;
          if (RangeButton("Overclocking Percentage",
            "Selects the percentage of the normal clock speed the emulated hardware will run at.",
            &overclock_percent, 10, 1000, 10, "%d%%", s_settings_copy.cpu_overclock_enable))
          {
            s_settings_copy.SetCPUOverclockPercent(static_cast<u32>(overclock_percent));
            settings_changed = true;
          }

          MenuHeading("CD-ROM Emulation");

          const u32 read_speed_index =
            std::clamp<u32>(s_settings_copy.cdrom_read_speedup, 1u, static_cast<u32>(cdrom_read_speeds.size())) - 1u;
          if (MenuButtonWithValue("Read Speedup",
            "Speeds up CD-ROM reads by the specified factor. May improve loading speeds in some "
            "games, and break others.",
            cdrom_read_speeds[read_speed_index]))
          {
            ImGuiFullscreen::ChoiceDialogOptions options;
            options.reserve(cdrom_read_speeds.size());
            for (u32 i = 0; i < static_cast<u32>(cdrom_read_speeds.size()); i++)
              options.emplace_back(cdrom_read_speeds[i], i == read_speed_index);
            OpenChoiceDialog("CD-ROM Read Speedup", false, std::move(options),
              [](s32 index, const std::string& title, bool checked) {
              if (index >= 0)
                s_settings_copy.cdrom_read_speedup = static_cast<u32>(index) + 1;
              CloseChoiceDialog();
            });
          }

          const u32 seek_speed_index =
            std::min(s_settings_copy.cdrom_seek_speedup, static_cast<u32>(cdrom_seek_speeds.size()));
          if (MenuButtonWithValue("Seek Speedup",
            "Speeds up CD-ROM seeks by the specified factor. May improve loading speeds in some "
            "games, and break others.",
            cdrom_seek_speeds[seek_speed_index]))
          {
            ImGuiFullscreen::ChoiceDialogOptions options;
            options.reserve(cdrom_seek_speeds.size());
            for (u32 i = 0; i < static_cast<u32>(cdrom_seek_speeds.size()); i++)
              options.emplace_back(cdrom_seek_speeds[i], i == seek_speed_index);
            OpenChoiceDialog("CD-ROM Seek Speedup", false, std::move(options),
              [](s32 index, const std::string& title, bool checked) {
              if (index >= 0)
                s_settings_copy.cdrom_seek_speedup = static_cast<u32>(index);
              CloseChoiceDialog();
            });
          }

          s32 readahead_sectors = s_settings_copy.cdrom_readahead_sectors;
          if (RangeButton(
            "Readahead Sectors",
            "Reduces hitches in emulation by reading/decompressing CD data asynchronously on a worker thread.",
            &readahead_sectors, 0, 32, 1))
          {
            s_settings_copy.cdrom_readahead_sectors = static_cast<u8>(readahead_sectors);
            settings_changed = true;
          }

          settings_changed |=
            ToggleButton("Enable Region Check", "Simulates the region check present in original, unmodified consoles.",
              &s_settings_copy.cdrom_region_check);
          settings_changed |= ToggleButton(
            "Preload Images to RAM",
            "Loads the game image into RAM. Useful for network paths that may become unreliable during gameplay.",
            &s_settings_copy.cdrom_load_image_to_ram);
          settings_changed |= ToggleButtonForNonSetting(
            "Apply Image Patches",
            "Automatically applies patches to disc images when they are present, currently only PPF is supported.",
            "CDROM", "LoadImagePatches", false);

          MenuHeading("Controller Ports");

          settings_changed |= EnumChoiceButton("Multitap", nullptr, &s_settings_copy.multitap_mode,
            &Settings::GetMultitapModeDisplayName, MultitapMode::Count);

          EndMenuButtons();
        }
        break;

      case SettingsPage::EmulationSettings:
        {
          static constexpr auto emulation_speeds =
            make_array(0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f,
              3.0f, 3.5f, 4.0f, 4.5f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f);
          static constexpr auto get_emulation_speed_options = [](float current_speed) {
            ImGuiFullscreen::ChoiceDialogOptions options;
            options.reserve(emulation_speeds.size());
            for (const float speed : emulation_speeds)
            {
              options.emplace_back(
                (speed != 0.0f) ?
                StringUtil::StdStringFromFormat("%d%% [%d FPS (NTSC) / %d FPS (PAL)]", static_cast<int>(speed * 100.0f),
                  static_cast<int>(60.0f * speed), static_cast<int>(50.0f * speed)) :
                "Unlimited",
                speed == current_speed);
            }
            return options;
          };

          BeginMenuButtons();

          MenuHeading("Speed Control");

#define MAKE_EMULATION_SPEED(setting_title, setting_var) \
	if (MenuButtonWithValue( \
			setting_title, \
			"Sets the target emulation speed. It is not guaranteed that this speed will be reached on all systems.", \
			(setting_var != 0.0f) ? TinyString::FromFormat("%.0f%%", setting_var * 100.0f) : TinyString("Unlimited"))) \
	{ \
		OpenChoiceDialog(setting_title, false, get_emulation_speed_options(setting_var), \
			[](s32 index, const std::string& title, bool checked) { \
				if (index >= 0) \
				{ \
					setting_var = emulation_speeds[index]; \
					s_host_interface->RunLater(SaveAndApplySettings); \
				} \
				CloseChoiceDialog(); \
			}); \
	}

          MAKE_EMULATION_SPEED("Emulation Speed", s_settings_copy.emulation_speed);
          MAKE_EMULATION_SPEED("Fast Forward Speed", s_settings_copy.fast_forward_speed);
          MAKE_EMULATION_SPEED("Turbo Speed", s_settings_copy.turbo_speed);

#undef MAKE_EMULATION_SPEED

          MenuHeading("Runahead/Rewind");

          settings_changed |=
            ToggleButton("Enable Rewinding", "Saves state periodically so you can rewind any mistakes while playing.",
              &s_settings_copy.rewind_enable);
          settings_changed |= RangeButton(
            "Rewind Save Frequency",
            "How often a rewind state will be created. Higher frequencies have greater system requirements.",
            &s_settings_copy.rewind_save_frequency, 0.0f, 3600.0f, 0.1f, "%.2f Seconds", s_settings_copy.rewind_enable);
          settings_changed |=
            RangeButton("Rewind Save Frequency",
              "How many saves will be kept for rewinding. Higher values have greater memory requirements.",
              reinterpret_cast<s32*>(&s_settings_copy.rewind_save_slots), 1, 10000, 1, "%d Frames",
              s_settings_copy.rewind_enable);

          TinyString summary;
          if (!s_settings_copy.IsRunaheadEnabled())
            summary = "Disabled";
          else
            summary.Format("%u Frames", s_settings_copy.runahead_frames);

          if (MenuButtonWithValue("Runahead",
            "Simulates the system ahead of time and rolls back/replays to reduce input lag. Very "
            "high system requirements.",
            summary))
          {
            ImGuiFullscreen::ChoiceDialogOptions options;
            for (u32 i = 0; i <= 10; i++)
            {
              if (i == 0)
                options.emplace_back("Disabled", s_settings_copy.runahead_frames == i);
              else
                options.emplace_back(StringUtil::StdStringFromFormat("%u Frames", i),
                  s_settings_copy.runahead_frames == i);
            }
            OpenChoiceDialog("Runahead", false, std::move(options),
              [](s32 index, const std::string& title, bool checked) {
              s_settings_copy.runahead_frames = index;
              s_host_interface->RunLater(SaveAndApplySettings);
              CloseChoiceDialog();
            });
            settings_changed = true;
          }

          TinyString rewind_summary;
          if (s_settings_copy.IsRunaheadEnabled())
          {
            rewind_summary = "Rewind is disabled because runahead is enabled. Runahead will significantly increase "
              "system requirements.";
          }
          else if (s_settings_copy.rewind_enable)
          {
            const float duration = ((s_settings_copy.rewind_save_frequency <= std::numeric_limits<float>::epsilon()) ?
              (1.0f / 60.0f) :
              s_settings_copy.rewind_save_frequency) *
              static_cast<float>(s_settings_copy.rewind_save_slots);

            u64 ram_usage, vram_usage;
            System::CalculateRewindMemoryUsage(s_settings_copy.rewind_save_slots, &ram_usage, &vram_usage);
            rewind_summary.Format("Rewind for %u frames, lasting %.2f seconds will require up to %" PRIu64
              "MB of RAM and %" PRIu64 "MB of VRAM.",
              s_settings_copy.rewind_save_slots, duration, ram_usage / 1048576, vram_usage / 1048576);
          }
          else
          {
            rewind_summary =
              "Rewind is not enabled. Please note that enabling rewind may significantly increase system requirements.";
          }

          ActiveButton(rewind_summary, false, false, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY,
            g_medium_font);

          EndMenuButtons();
        }
        break;

      case SettingsPage::BIOSSettings:
        {
          static constexpr auto config_keys = make_array("", "PathNTSCJ", "PathNTSCU", "PathPAL");
          static std::string bios_region_filenames[static_cast<u32>(ConsoleRegion::Count)];
          static std::string bios_directory;
          static bool bios_filenames_loaded = false;

          if (!bios_filenames_loaded)
          {
            for (u32 i = 0; i < static_cast<u32>(ConsoleRegion::Count); i++)
            {
              if (i == static_cast<u32>(ConsoleRegion::Auto))
                continue;
              bios_region_filenames[i] = s_host_interface->GetSettingsInterface()->GetStringValue("BIOS", config_keys[i]);
            }
            bios_directory = s_host_interface->GetBIOSDirectory();
            bios_filenames_loaded = true;
          }

          BeginMenuButtons();

          MenuHeading("BIOS Selection");

          for (u32 i = 0; i < static_cast<u32>(ConsoleRegion::Count); i++)
          {
            const ConsoleRegion region = static_cast<ConsoleRegion>(i);
            if (region == ConsoleRegion::Auto)
              continue;

            TinyString title;
            title.Format("BIOS for %s", Settings::GetConsoleRegionName(region));

            if (MenuButtonWithValue(title,
              SmallString::FromFormat("BIOS to use when emulating %s consoles.",
                Settings::GetConsoleRegionDisplayName(region)),
              bios_region_filenames[i].c_str()))
            {
              ImGuiFullscreen::ChoiceDialogOptions options;
              auto images = s_host_interface->FindBIOSImagesInDirectory(s_host_interface->GetBIOSDirectory().c_str());
              options.reserve(images.size() + 1);
              options.emplace_back("Auto-Detect", bios_region_filenames[i].empty());
              for (auto& [path, info] : images)
              {
                const bool selected = bios_region_filenames[i] == path;
                options.emplace_back(std::move(path), selected);
              }

              OpenChoiceDialog(title, false, std::move(options), [i](s32 index, const std::string& path, bool checked) {
                if (index >= 0)
                {
                  bios_region_filenames[i] = path;
                  s_host_interface->GetSettingsInterface()->SetStringValue("BIOS", config_keys[i], path.c_str());
                  s_host_interface->GetSettingsInterface()->Save();
                }
                CloseChoiceDialog();
              });
            }
          }

          if (MenuButton("BIOS Directory", bios_directory.c_str()))
          {
            OpenFileSelector("BIOS Directory", true, [](const std::string& path) {
              if (!path.empty())
              {
                bios_directory = path;
                s_host_interface->GetSettingsInterface()->SetStringValue("BIOS", "SearchDirectory", path.c_str());
                s_host_interface->GetSettingsInterface()->Save();
              }
              CloseFileSelector();
            });
          }

          MenuHeading("Patches");

          settings_changed |=
            ToggleButton("Enable Fast Boot", "Patches the BIOS to skip the boot animation. Safe to enable.",
              &s_settings_copy.bios_patch_fast_boot);
          settings_changed |= ToggleButton(
            "Enable TTY Output", "Patches the BIOS to log calls to printf(). Only use when debugging, can break games.",
            &s_settings_copy.bios_patch_tty_enable);

          EndMenuButtons();
        }
        break;

      case SettingsPage::ControllerSettings:
        {
          BeginMenuButtons();

          MenuHeading("Input Profiles");
          if (MenuButton(ICON_FA_FOLDER_OPEN "  Load Input Profile",
            "Applies a saved configuration of controller types and bindings."))
          {
            CommonHostInterface::InputProfileList profiles(s_host_interface->GetInputProfileList());
            ImGuiFullscreen::ChoiceDialogOptions options;
            options.reserve(profiles.size());
            for (CommonHostInterface::InputProfileEntry& entry : profiles)
              options.emplace_back(std::move(entry.name), false);

            auto callback = [profiles](s32 index, const std::string& title, bool checked) {
              if (index < 0)
                return;

              // needs a reload...
              s_host_interface->ApplyInputProfile(profiles[index].path.c_str());
              s_settings_copy.Load(*s_host_interface->GetSettingsInterface());
              s_host_interface->RunLater(SaveAndApplySettings);
              CloseChoiceDialog();
            };
            OpenChoiceDialog(ICON_FA_FOLDER_OPEN "  Load Input Profile", false, std::move(options), std::move(callback));
          }

          static std::array<ControllerType, NUM_CONTROLLER_AND_CARD_PORTS> type_cache = {};
          static std::array<Controller::ButtonList, NUM_CONTROLLER_AND_CARD_PORTS> button_cache;
          static std::array<Controller::AxisList, NUM_CONTROLLER_AND_CARD_PORTS> axis_cache;
          static std::array<Controller::SettingList, NUM_CONTROLLER_AND_CARD_PORTS> setting_cache;
          static std::array<std::string,
            NUM_CONTROLLER_AND_CARD_PORTS* CommonHostInterface::NUM_CONTROLLER_AUTOFIRE_BUTTONS>
            autofire_buttons_cache;
          TinyString section;
          TinyString key;

          std::array<TinyString, NUM_CONTROLLER_AND_CARD_PORTS> port_labels = s_settings_copy.GeneratePortLabels();

          for (u32 port = 0; port < NUM_CONTROLLER_AND_CARD_PORTS; port++)
          {
            if (port_labels[port].IsEmpty())
              continue;
            else
              MenuHeading(port_labels[port]);

            settings_changed |= EnumChoiceButton(
              TinyString::FromFormat(ICON_FA_GAMEPAD "  Controller Type##type%u", port),
              "Determines the simulated controller plugged into this port.", &s_settings_copy.controller_types[port],
              &Settings::GetControllerTypeDisplayName, ControllerType::Count);

            section.Format("Controller%u", port + 1);

            const ControllerType ctype = s_settings_copy.controller_types[port];
            if (ctype != type_cache[port])
            {
              type_cache[port] = ctype;
              button_cache[port] = Controller::GetButtonNames(ctype);
              axis_cache[port] = Controller::GetAxisNames(ctype);
              setting_cache[port] = Controller::GetSettings(ctype);

              for (u32 i = 0; i < CommonHostInterface::NUM_CONTROLLER_AUTOFIRE_BUTTONS; i++)
              {
                autofire_buttons_cache[port * CommonHostInterface::NUM_CONTROLLER_AUTOFIRE_BUTTONS + i] =
                  s_host_interface->GetStringSettingValue(section, TinyString::FromFormat("AutoFire%uButton", i + 1));
              }
            }

            for (const auto& it : button_cache[port])
            {
              key.Format("Button%s", it.first.c_str());
              DrawInputBindingButton(InputBindingType::Button, section, key, it.first.c_str());
            }

            for (const auto& it : axis_cache[port])
            {
              key.Format("Axis%s", std::get<0>(it).c_str());
              DrawInputBindingButton(std::get<2>(it) == Controller::AxisType::Half ? InputBindingType::HalfAxis :
                InputBindingType::Axis,
                section, key, std::get<0>(it).c_str());
            }

            if (Controller::GetVibrationMotorCount(ctype) > 0)
              DrawInputBindingButton(InputBindingType::Rumble, section, "Rumble", "Rumble/Vibration");

            for (const SettingInfo& it : setting_cache[port])
              settings_changed |= SettingInfoButton(it, section);

            for (u32 autofire_index = 0; autofire_index < CommonHostInterface::NUM_CONTROLLER_AUTOFIRE_BUTTONS;
              autofire_index++)
            {
              const u32 cache_index = port * CommonHostInterface::NUM_CONTROLLER_AUTOFIRE_BUTTONS + autofire_index;

              if (MenuButtonWithValue(
                TinyString::FromFormat("Auto Fire %u##autofire_%u_%u", autofire_index + 1, port, autofire_index),
                "Selects the button to toggle with this auto fire binding.",
                autofire_buttons_cache[cache_index].c_str()))

              {
                auto callback = [port, autofire_index, cache_index](s32 index, const std::string& title, bool checked) {
                  if (index < 0)
                    return;

                  auto lock = s_host_interface->GetSettingsLock();
                  if (index == 0)
                  {
                    s_host_interface->GetSettingsInterface()->DeleteValue(
                      TinyString::FromFormat("Controller%u", port + 1),
                      TinyString::FromFormat("AutoFire%uButton", autofire_index + 1));
                    std::string().swap(autofire_buttons_cache[cache_index]);
                  }
                  else
                  {
                    s_host_interface->GetSettingsInterface()->SetStringValue(
                      TinyString::FromFormat("Controller%u", port + 1),
                      TinyString::FromFormat("AutoFire%uButton", autofire_index + 1),
                      button_cache[port][index - 1].first.c_str());
                    autofire_buttons_cache[cache_index] = button_cache[port][index - 1].first;
                  }

                  // needs a reload...
                  s_host_interface->RunLater(SaveAndApplySettings);
                  CloseChoiceDialog();
                };

                ImGuiFullscreen::ChoiceDialogOptions options;
                options.reserve(button_cache[port].size() + 1);
                options.emplace_back("(None)", autofire_buttons_cache[cache_index].empty());
                for (const auto& it : button_cache[port])
                  options.emplace_back(it.first, autofire_buttons_cache[cache_index] == it.first);

                OpenChoiceDialog(ICON_FA_GAMEPAD "  Select Auto Fire Button", false, std::move(options),
                  std::move(callback));
              }

              if (autofire_buttons_cache[cache_index].empty())
                continue;

              key.Format("AutoFire%u", autofire_index + 1);
              DrawInputBindingButton(InputBindingType::Button, section, key,
                TinyString::FromFormat("Auto Fire %u Binding##autofire_binding_%u_%u",
                  autofire_index + 1, port, autofire_index),
                false);

              key.Format("AutoFire%uFrequency", autofire_index + 1);
              int frequency = s_host_interface->GetSettingsInterface()->GetIntValue(
                section, key, CommonHostInterface::DEFAULT_AUTOFIRE_FREQUENCY);
              settings_changed |= RangeButton(TinyString::FromFormat("Auto Fire %u Frequency##autofire_frequency_%u_%u",
                autofire_index + 1, port, autofire_index),
                "Sets the rate at which the auto fire will trigger on and off.", &frequency,
                1, 255, 1, "%d Frames");
            }
          }

          EndMenuButtons();
        }
        break;

      case SettingsPage::HotkeySettings:
        {
          BeginMenuButtons();

          TinyString last_category;
          for (const CommonHostInterface::HotkeyInfo& hotkey : s_host_interface->GetHotkeyInfoList())
          {
            if (hotkey.category != last_category)
            {
              MenuHeading(hotkey.category);
              last_category = hotkey.category;
            }

            DrawInputBindingButton(InputBindingType::Button, "Hotkeys", hotkey.name, hotkey.display_name);
          }

          EndMenuButtons();
        }
        break;

      case SettingsPage::MemoryCardSettings:
        {
          BeginMenuButtons();

          for (u32 i = 0; i < 2; i++)
          {
            MenuHeading(TinyString::FromFormat("Memory Card Port %u", i + 1));

            settings_changed |= EnumChoiceButton(
              TinyString::FromFormat("Memory Card %u Type", i + 1),
              SmallString::FromFormat("Sets which sort of memory card image will be used for slot %u.", i + 1),
              &s_settings_copy.memory_card_types[i], &Settings::GetMemoryCardTypeDisplayName, MemoryCardType::Count);

            settings_changed |= MenuButton(TinyString::FromFormat("Shared Memory Card %u Path", i + 1),
              s_settings_copy.memory_card_paths[i].c_str(),
              s_settings_copy.memory_card_types[i] == MemoryCardType::Shared);
          }

          MenuHeading("Shared Settings");

          settings_changed |= ToggleButton("Use Single Card For Sub-Images",
            "When using a multi-disc image (m3u/pbp) and per-game (title) memory cards, "
            "use a single memory card for all discs.",
            &s_settings_copy.memory_card_use_playlist_title);

          static std::string memory_card_directory;
          static bool memory_card_directory_set = false;
          if (!memory_card_directory_set)
          {
            memory_card_directory = s_host_interface->GetMemoryCardDirectory();
            memory_card_directory_set = true;
          }

          if (MenuButton("Memory Card Directory", memory_card_directory.c_str()))
          {
            OpenFileSelector("Memory Card Directory", true, [](const std::string& path) {
              if (!path.empty())
              {
                memory_card_directory = path;
                s_settings_copy.memory_card_directory = path;
                s_host_interface->RunLater(SaveAndApplySettings);
              }
              CloseFileSelector();
            });
          }

          if (MenuButton("Reset Memory Card Directory", "Resets memory card directory to default (user directory)."))
          {
            s_settings_copy.memory_card_directory.clear();
            s_host_interface->RunLater(SaveAndApplySettings);
            memory_card_directory_set = false;
          }

          EndMenuButtons();
        }
        break;

      case SettingsPage::DisplaySettings:
        {
          BeginMenuButtons();

          MenuHeading("Device Settings");

          settings_changed |=
            EnumChoiceButton("GPU Renderer", "Chooses the backend to use for rendering the console/game visuals.",
              &s_settings_copy.gpu_renderer, &Settings::GetRendererDisplayName, GPURenderer::Count);

          static std::string fullscreen_mode;
          static bool fullscreen_mode_set;
          if (!fullscreen_mode_set)
          {
            fullscreen_mode = s_host_interface->GetSettingsInterface()->GetStringValue("GPU", "FullscreenMode", "");
            fullscreen_mode_set = true;
          }

#ifndef _UWP
          if (MenuButtonWithValue("Fullscreen Resolution", "Selects the resolution to use in fullscreen modes.",
            fullscreen_mode.empty() ? "Borderless Fullscreen" : fullscreen_mode.c_str()))
          {
            HostDisplay::AdapterAndModeList aml(s_host_interface->GetDisplay()->GetAdapterAndModeList());

            ImGuiFullscreen::ChoiceDialogOptions options;
            options.reserve(aml.fullscreen_modes.size() + 1);
            options.emplace_back("Borderless Fullscreen", fullscreen_mode.empty());
            for (std::string& mode : aml.fullscreen_modes)
              options.emplace_back(std::move(mode), mode == fullscreen_mode);

            auto callback = [](s32 index, const std::string& title, bool checked) {
              if (index < 0)
                return;
              else if (index == 0)
                std::string().swap(fullscreen_mode);
              else
                fullscreen_mode = title;

              s_host_interface->GetSettingsInterface()->SetStringValue("GPU", "FullscreenMode", fullscreen_mode.c_str());
              s_host_interface->GetSettingsInterface()->Save();
              s_host_interface->AddOSDMessage("Resolution change will be applied after restarting.", 10.0f);
              CloseChoiceDialog();
            };
            OpenChoiceDialog(ICON_FA_TV "  Fullscreen Resolution", false, std::move(options), std::move(callback));
          }
#endif

          switch (s_settings_copy.gpu_renderer)
          {
#ifdef _WIN32
          case GPURenderer::HardwareD3D11:
            {
              settings_changed |= ToggleButtonForNonSetting(
                "Use Blit Swap Chain",
                "Uses a blit presentation model instead of flipping. This may be needed on some systems.", "Display",
                "UseBlitSwapChain", false);
            }
            break;
#endif

          case GPURenderer::HardwareVulkan:
            {
              settings_changed |=
                ToggleButton("Threaded Presentation",
                  "Presents frames on a background thread when fast forwarding or vsync is disabled.",
                  &s_settings_copy.gpu_threaded_presentation);
            }
            break;

          case GPURenderer::Software:
            {
              settings_changed |= ToggleButton("Threaded Rendering",
                "Uses a second thread for drawing graphics. Speed boost, and safe to use.",
                &s_settings_copy.gpu_use_thread);
            }
            break;

          default:
            break;
          }

          if (!s_settings_copy.IsUsingSoftwareRenderer())
          {
            settings_changed |=
              ToggleButton("Use Software Renderer For Readbacks",
                "Runs the software renderer in parallel for VRAM readbacks. On some systems, this may result "
                "in greater performance.",
                &s_settings_copy.gpu_use_software_renderer_for_readbacks);
          }

          settings_changed |=
            ToggleButton("Enable VSync",
              "Synchronizes presentation of the console's frames to the host. Enable for smoother animations.",
              &s_settings_copy.video_sync_enabled);

          settings_changed |= ToggleButton("Sync To Host Refresh Rate",
            "Adjusts the emulation speed so the console's refresh rate matches the host "
            "when VSync and Audio Resampling are enabled.",
            &s_settings_copy.sync_to_host_refresh_rate, s_settings_copy.audio_resampling);

          settings_changed |= ToggleButton("Optimal Frame Pacing",
            "Ensures every frame generated is displayed for optimal pacing. Disable if "
            "you are having speed or sound issues.",
            &s_settings_copy.display_all_frames);

          MenuHeading("Screen Display");

          settings_changed |= EnumChoiceButton(
            "Aspect Ratio", "Changes the aspect ratio used to display the console's output to the screen.",
            &s_settings_copy.display_aspect_ratio, &Settings::GetDisplayAspectRatioName, DisplayAspectRatio::Count);

          settings_changed |= EnumChoiceButton(
            "Crop Mode", "Determines how much of the area typically not visible on a consumer TV set to crop/hide.",
            &s_settings_copy.display_crop_mode, &Settings::GetDisplayCropModeDisplayName, DisplayCropMode::Count);

          settings_changed |=
            EnumChoiceButton("Downsampling",
              "Downsamples the rendered image prior to displaying it. Can improve "
              "overall image quality in mixed 2D/3D games.",
              &s_settings_copy.gpu_downsample_mode, &Settings::GetDownsampleModeDisplayName,
              GPUDownsampleMode::Count, !s_settings_copy.IsUsingSoftwareRenderer());

          settings_changed |=
            ToggleButton("Linear Upscaling", "Uses a bilinear filter when upscaling to display, smoothing out the image.",
              &s_settings_copy.display_linear_filtering, !s_settings_copy.display_integer_scaling);

          settings_changed |=
            ToggleButton("Integer Upscaling", "Adds padding to ensure pixels are a whole number in size.",
              &s_settings_copy.display_integer_scaling);

          settings_changed |= ToggleButton(
            "Stretch To Fit", "Fills the window with the active display area, regardless of the aspect ratio.",
            &s_settings_copy.display_stretch);

          settings_changed |=
            ToggleButtonForNonSetting("Internal Resolution Screenshots",
              "Saves screenshots at internal render resolution and without postprocessing.",
              "Display", "InternalResolutionScreenshots", false);

          MenuHeading("On-Screen Display");

          settings_changed |= ToggleButton("Show OSD Messages", "Shows on-screen-display messages when events occur.",
            &s_settings_copy.display_show_osd_messages);
          settings_changed |= ToggleButton(
            "Show Game Frame Rate", "Shows the internal frame rate of the game in the top-right corner of the display.",
            &s_settings_copy.display_show_fps);
          settings_changed |= ToggleButton("Show Display FPS",
            "Shows the number of frames (or v-syncs) displayed per second by the system "
            "in the top-right corner of the display.",
            &s_settings_copy.display_show_vps);
          settings_changed |= ToggleButton(
            "Show Speed",
            "Shows the current emulation speed of the system in the top-right corner of the display as a percentage.",
            &s_settings_copy.display_show_speed);
          settings_changed |=
            ToggleButton("Show Resolution",
              "Shows the current rendering resolution of the system in the top-right corner of the display.",
              &s_settings_copy.display_show_resolution);
          settings_changed |= ToggleButtonForNonSetting(
            "Show Controller Input",
            "Shows the current controller state of the system in the bottom-left corner of the display.", "Display",
            "ShowInputs", false);

          EndMenuButtons();
        }
        break;

      case SettingsPage::EnhancementSettings:
        {
          static const auto resolution_scale_text_callback = [](u32 value) -> const char* {
            static constexpr std::array<const char*, 17> texts = {
              {"Automatic based on window size", "1x", "2x", "3x (for 720p)", "4x", "5x (for 1080p)", "6x (for 1440p)",
               "7x", "8x", "9x (for 4K)", "10x", "11x", "12x", "13x", "14x", "15x", "16x"

              } };
            return (value >= texts.size()) ? "" : texts[value];
          };

          BeginMenuButtons();

          MenuHeading("Rendering Enhancements");

          settings_changed |= EnumChoiceButton<u32, u32>(
            "Internal Resolution Scale",
            "Scales internal VRAM resolution by the specified multiplier. Some games require 1x VRAM resolution.",
            &s_settings_copy.gpu_resolution_scale, resolution_scale_text_callback, 17);
          settings_changed |= EnumChoiceButton(
            "Texture Filtering",
            "Smooths out the blockyness of magnified textures on 3D objects. Will have a greater effect "
            "on higher resolution scales.",
            &s_settings_copy.gpu_texture_filter, &Settings::GetTextureFilterDisplayName, GPUTextureFilter::Count);
          settings_changed |=
            ToggleButton("True Color Rendering",
              "Disables dithering and uses the full 8 bits per channel of color information. May break "
              "rendering in some games.",
              &s_settings_copy.gpu_true_color);
          settings_changed |= ToggleButton(
            "Scaled Dithering",
            "Scales the dithering pattern with the internal rendering resolution, making it less noticeable. "
            "Usually safe to enable.",
            &s_settings_copy.gpu_scaled_dithering, s_settings_copy.gpu_resolution_scale > 1);
          settings_changed |= ToggleButton(
            "Widescreen Hack", "Increases the field of view from 4:3 to the chosen display aspect ratio in 3D games.",
            &s_settings_copy.gpu_widescreen_hack);

          MenuHeading("Display Enhancements");

          settings_changed |=
            ToggleButton("Disable Interlacing",
              "Disables interlaced rendering and display in the GPU. Some games can render in 480p this way, "
              "but others will break.",
              &s_settings_copy.gpu_disable_interlacing);
          settings_changed |= ToggleButton(
            "Force NTSC Timings",
            "Forces PAL games to run at NTSC timings, i.e. 60hz. Some PAL games will run at their \"normal\" "
            "speeds, while others will break.",
            &s_settings_copy.gpu_force_ntsc_timings);
          settings_changed |=
            ToggleButton("Force 4:3 For 24-Bit Display",
              "Switches back to 4:3 display aspect ratio when displaying 24-bit content, usually FMVs.",
              &s_settings_copy.display_force_4_3_for_24bit);
          settings_changed |= ToggleButton(
            "Chroma Smoothing For 24-Bit Display",
            "Smooths out blockyness between colour transitions in 24-bit content, usually FMVs. Only applies "
            "to the hardware renderers.",
            &s_settings_copy.gpu_24bit_chroma_smoothing);

          MenuHeading("PGXP (Precision Geometry Transform Pipeline");

          settings_changed |=
            ToggleButton("PGXP Geometry Correction",
              "Reduces \"wobbly\" polygons by attempting to preserve the fractional component through memory "
              "transfers.",
              &s_settings_copy.gpu_pgxp_enable);
          settings_changed |=
            ToggleButton("PGXP Texture Correction",
              "Uses perspective-correct interpolation for texture coordinates and colors, straightening out "
              "warped textures.",
              &s_settings_copy.gpu_pgxp_texture_correction, s_settings_copy.gpu_pgxp_enable);
          settings_changed |=
            ToggleButton("PGXP Culling Correction",
              "Increases the precision of polygon culling, reducing the number of holes in geometry.",
              &s_settings_copy.gpu_pgxp_culling, s_settings_copy.gpu_pgxp_enable);
          settings_changed |=
            ToggleButton("PGXP Preserve Projection Precision",
              "Adds additional precision to PGXP data post-projection. May improve visuals in some games.",
              &s_settings_copy.gpu_pgxp_preserve_proj_fp, s_settings_copy.gpu_pgxp_enable);
          settings_changed |= ToggleButton(
            "PGXP Depth Buffer", "Reduces polygon Z-fighting through depth testing. Low compatibility with games.",
            &s_settings_copy.gpu_pgxp_depth_buffer,
            s_settings_copy.gpu_pgxp_enable && s_settings_copy.gpu_pgxp_texture_correction);
          settings_changed |= ToggleButton("PGXP CPU Mode", "Uses PGXP for all instructions, not just memory operations.",
            &s_settings_copy.gpu_pgxp_cpu, s_settings_copy.gpu_pgxp_enable);

          EndMenuButtons();
        }
        break;

      case SettingsPage::AudioSettings:
        {
          BeginMenuButtons();

          MenuHeading("Audio Control");

          settings_changed |= RangeButton("Output Volume", "Controls the volume of the audio played on the host.",
            &s_settings_copy.audio_output_volume, 0, 100, 1, "%d%%");
          settings_changed |= RangeButton("Fast Forward Volume",
            "Controls the volume of the audio played on the host when fast forwarding.",
            &s_settings_copy.audio_fast_forward_volume, 0, 100, 1, "%d%%");
          settings_changed |= ToggleButton("Mute All Sound", "Prevents the emulator from producing any audible sound.",
            &s_settings_copy.audio_output_muted);
          settings_changed |= ToggleButton("Mute CD Audio",
            "Forcibly mutes both CD-DA and XA audio from the CD-ROM. Can be used to "
            "disable background music in some games.",
            &s_settings_copy.cdrom_mute_cd_audio);

          MenuHeading("Backend Settings");

          settings_changed |= EnumChoiceButton(
            "Audio Backend",
            "The audio backend determines how frames produced by the emulator are submitted to the host.",
            &s_settings_copy.audio_backend, &Settings::GetAudioBackendDisplayName, AudioBackend::Count);
          settings_changed |= RangeButton(
            "Buffer Size", "The buffer size determines the size of the chunks of audio which will be pulled by the host.",
            reinterpret_cast<s32*>(&s_settings_copy.audio_buffer_size), 1024, 8192, 128, "%d Frames");

          settings_changed |= ToggleButton("Sync To Output",
            "Throttles the emulation speed based on the audio backend pulling audio "
            "frames. Enable to reduce the chances of crackling.",
            &s_settings_copy.audio_sync_enabled);
          settings_changed |= ToggleButton(
            "Resampling",
            "When running outside of 100% speed, resamples audio from the target speed instead of dropping frames.",
            &s_settings_copy.audio_resampling);

          EndMenuButtons();
        }
        break;

      case SettingsPage::AchievementsSetings:
        {
#ifdef WITH_RAINTEGRATION
          if (Achievements::IsUsingRAIntegration())
          {
            BeginMenuButtons();
            ActiveButton(ICON_FA_BAN "  RAIntegration is being used instead of the built-in Achievements implementation.",
              false, false, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
            EndMenuButtons();
            break;
          }
#endif

#ifdef ENABLE_ACHIEVEMENTS
          BeginMenuButtons();

          MenuHeading("Settings");
          if (ToggleButtonForNonSetting(ICON_FA_TROPHY "  Enable RetroAchievements",
            "When enabled and logged in, DuckStation will scan for achievements on startup.",
            "Achievements", "Enabled", false))
          {
            settings_changed = true;
            s_host_interface->RunLater([]() {
              if (!ConfirmChallengeModeEnable())
                s_host_interface->GetSettingsInterface()->SetBoolValue("Achievements", "Enabled", false);
            });
          }

          settings_changed |= ToggleButtonForNonSetting(
            ICON_FA_USER_FRIENDS "  Rich Presence",
            "When enabled, rich presence information will be collected and sent to the server where supported.",
            "Achievements", "RichPresence", true);
          settings_changed |=
            ToggleButtonForNonSetting(ICON_FA_STETHOSCOPE "  Test Mode",
              "When enabled, DuckStation will assume all achievements are locked and not "
              "send any unlock notifications to the server.",
              "Achievements", "TestMode", false);
          settings_changed |=
            ToggleButtonForNonSetting(ICON_FA_MEDAL "  Test Unofficial Achievements",
              "When enabled, DuckStation will list achievements from unofficial sets. These "
              "achievements are not tracked by RetroAchievements.",
              "Achievements", "UnofficialTestMode", false);
          settings_changed |= ToggleButtonForNonSetting(ICON_FA_COMPACT_DISC "  Use First Disc From Playlist",
            "When enabled, the first disc in a playlist will be used for "
            "achievements, regardless of which disc is active.",
            "Achievements", "UseFirstDiscFromPlaylist", true);

          if (ToggleButtonForNonSetting(ICON_FA_HARD_HAT "  Hardcore Mode",
            "\"Challenge\" mode for achievements. Disables save state, cheats, and slowdown "
            "functions, but you receive double the achievement points.",
            "Achievements", "ChallengeMode", false))
          {
            s_host_interface->RunLater([]() {
              if (!ConfirmChallengeModeEnable())
                s_host_interface->GetSettingsInterface()->SetBoolValue("Achievements", "ChallengeMode", false);
            });
          }

          MenuHeading("Account");
          if (Achievements::IsLoggedIn())
          {
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyle().Colors[ImGuiCol_Text]);
            ActiveButton(SmallString::FromFormat(ICON_FA_USER "  Username: %s", Achievements::GetUsername().c_str()), false,
              false, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);

            Timestamp ts;
            TinyString ts_string;
            ts.SetUnixTimestamp(StringUtil::FromChars<u64>(s_host_interface->GetSettingsInterface()->GetStringValue(
              "Achievements", "LoginTimestamp", "0"))
              .value_or(0));
            ts.ToString(ts_string, "%Y-%m-%d %H:%M:%S");
            ActiveButton(SmallString::FromFormat(ICON_FA_CLOCK "  Login token generated on %s", ts_string.GetCharArray()),
              false, false, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
            ImGui::PopStyleColor();

            if (MenuButton(ICON_FA_KEY "  Logout", "Logs out of RetroAchievements."))
              Achievements::Logout();
          }
          else if (Achievements::IsActive())
          {
            ActiveButton(ICON_FA_USER "  Not Logged In", false, false,
              ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);

            if (MenuButton(ICON_FA_KEY "  Login", "Logs in to RetroAchievements."))
              ImGui::OpenPopup("Achievements Login");

            DrawAchievementsLoginWindow();
          }
          else
          {
            ActiveButton(ICON_FA_USER "  Achievements are disabled.", false, false,
              ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
          }

          MenuHeading("Current Game");
          if (Achievements::HasActiveGame())
          {
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyle().Colors[ImGuiCol_Text]);
            ActiveButton(TinyString::FromFormat(ICON_FA_BOOKMARK "  Game ID: %u", Achievements::GetGameID()), false, false,
              ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
            ActiveButton(TinyString::FromFormat(ICON_FA_BOOK "  Game Title: %s", Achievements::GetGameTitle().c_str()), false,
              false, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
            ActiveButton(
              TinyString::FromFormat(ICON_FA_DESKTOP "  Game Developer: %s", Achievements::GetGameDeveloper().c_str()), false,
              false, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
            ActiveButton(
              TinyString::FromFormat(ICON_FA_DESKTOP "  Game Publisher: %s", Achievements::GetGamePublisher().c_str()), false,
              false, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
            ActiveButton(TinyString::FromFormat(ICON_FA_TROPHY "  Achievements: %u (%u points)",
              Achievements::GetAchievementCount(), Achievements::GetMaximumPointsForGame()),
              false, false, ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);

            const std::string& rich_presence_string = Achievements::GetRichPresenceString();
            if (!rich_presence_string.empty())
            {
              ActiveButton(SmallString::FromFormat(ICON_FA_MAP "  %s", rich_presence_string.c_str()), false, false,
                ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
            }
            else
            {
              ActiveButton(ICON_FA_MAP "  Rich presence inactive or unsupported.", false, false,
                ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
            }

            ImGui::PopStyleColor();
          }
          else
          {
            ActiveButton(ICON_FA_BAN "  Game not loaded or no RetroAchievements available.", false, false,
              ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
          }

          EndMenuButtons();
#else
          BeginMenuButtons();
          ActiveButton(ICON_FA_BAN "  This build was not compiled with RetroAchivements support.", false, false,
            ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);
          EndMenuButtons();
#endif
          // ImGuiFullscreen::moda
          // if (ImGui::BeginPopup("))
        }
        break;

      case SettingsPage::AdvancedSettings:
        {
          BeginMenuButtons();

          MenuHeading("Logging Settings");
          settings_changed |=
            EnumChoiceButton("Log Level", "Sets the verbosity of messages logged. Higher levels will log more messages.",
              &s_settings_copy.log_level, &Settings::GetLogLevelDisplayName, LOGLEVEL_COUNT);
          settings_changed |= ToggleButton("Log To System Console", "Logs messages to the console window.",
            &s_settings_copy.log_to_console);
          settings_changed |= ToggleButton("Log To Debug Console", "Logs messages to the debug console where supported.",
            &s_settings_copy.log_to_debug);
          settings_changed |= ToggleButton("Log To File", "Logs messages to duckstation.log in the user directory.",
            &s_settings_copy.log_to_file);

          MenuHeading("Debugging Settings");

          bool debug_menu = s_debug_menu_enabled;
          if (ToggleButton("Enable Debug Menu", "Shows a debug menu bar with additional statistics and quick settings.",
            &debug_menu))
          {
            s_host_interface->RunLater([debug_menu]() { SetDebugMenuEnabled(debug_menu); });
          }

          settings_changed |=
            ToggleButton("Disable All Enhancements", "Temporarily disables all enhancements, useful when testing.",
              &s_settings_copy.disable_all_enhancements);

          settings_changed |= ToggleButton(
            "Use Debug GPU Device", "Enable debugging when supported by the host's renderer API. Only for developer use.",
            &s_settings_copy.gpu_use_debug_device);

#ifdef _WIN32
          settings_changed |=
            ToggleButton("Increase Timer Resolution", "Enables more precise frame pacing at the cost of battery life.",
              &s_settings_copy.increase_timer_resolution);
#endif

          settings_changed |= ToggleButtonForNonSetting("Allow Booting Without SBI File",
            "Allows loading protected games without subchannel information.",
            "CDROM", "AllowBootingWithoutSBIFile", false);

          settings_changed |= ToggleButtonForNonSetting("Create Save State Backups",
            "Renames existing save states when saving to a backup file.",
            "General", "CreateSaveStateBackups", false);

          MenuHeading("Display Settings");
          settings_changed |=
            ToggleButton("Show Status Indicators", "Shows persistent icons when turbo is active or when paused.",
              &g_settings.display_show_status_indicators);
          settings_changed |= ToggleButton("Show Enhancement Settings",
            "Shows enhancement settings in the bottom-right corner of the screen.",
            &g_settings.display_show_enhancements);
          settings_changed |= RangeButton(
            "Display FPS Limit", "Limits how many frames are displayed to the screen. These frames are still rendered.",
            &s_settings_copy.display_max_fps, 0.0f, 500.0f, 1.0f, "%.2f FPS");

          MenuHeading("PGXP Settings");

          settings_changed |= ToggleButton(
            "Enable PGXP Vertex Cache", "Uses screen positions to resolve PGXP data. May improve visuals in some games.",
            &s_settings_copy.gpu_pgxp_vertex_cache, s_settings_copy.gpu_pgxp_enable);
          settings_changed |= RangeButton(
            "PGXP Geometry Tolerance",
            "Sets a threshold for discarding precise values when exceeded. May help with glitches in some games.",
            &s_settings_copy.gpu_pgxp_tolerance, -1.0f, 10.0f, 0.1f, "%.1f Pixels", s_settings_copy.gpu_pgxp_enable);
          settings_changed |= RangeButton(
            "PGXP Depth Clear Threshold",
            "Sets a threshold for discarding the emulated depth buffer. May help in some games.",
            &s_settings_copy.gpu_pgxp_tolerance, 0.0f, 4096.0f, 1.0f, "%.1f", s_settings_copy.gpu_pgxp_enable);

          MenuHeading("Texture Dumping/Replacements");

          settings_changed |= ToggleButton("Enable VRAM Write Texture Replacement",
            "Enables the replacement of background textures in supported games.",
            &s_settings_copy.texture_replacements.enable_vram_write_replacements);
          settings_changed |= ToggleButton("Preload Replacement Textures",
            "Loads all replacement texture to RAM, reducing stuttering at runtime.",
            &s_settings_copy.texture_replacements.preload_textures,
            s_settings_copy.texture_replacements.AnyReplacementsEnabled());
          settings_changed |=
            ToggleButton("Dump Replacable VRAM Writes", "Writes textures which can be replaced to the dump directory.",
              &s_settings_copy.texture_replacements.dump_vram_writes);
          settings_changed |=
            ToggleButton("Set VRAM Write Dump Alpha Channel", "Clears the mask/transparency bit in VRAM write dumps.",
              &s_settings_copy.texture_replacements.dump_vram_write_force_alpha_channel,
              s_settings_copy.texture_replacements.dump_vram_writes);

          MenuHeading("CPU Emulation");

          settings_changed |=
            ToggleButton("Enable Recompiler ICache",
              "Simulates the CPU's instruction cache in the recompiler. Can help with games running too fast.",
              &s_settings_copy.cpu_recompiler_icache);
          settings_changed |= ToggleButton("Enable Recompiler Memory Exceptions",
            "Enables alignment and bus exceptions. Not needed for any known games.",
            &s_settings_copy.cpu_recompiler_memory_exceptions);
          settings_changed |= ToggleButton(
            "Enable Recompiler Block Linking",
            "Performance enhancement - jumps directly between blocks instead of returning to the dispatcher.",
            &s_settings_copy.cpu_recompiler_block_linking);
          settings_changed |= EnumChoiceButton("Recompiler Fast Memory Access",
            "Avoids calls to C++ code, significantly speeding up the recompiler.",
            &s_settings_copy.cpu_fastmem_mode, &Settings::GetCPUFastmemModeDisplayName,
            CPUFastmemMode::Count, !s_settings_copy.cpu_recompiler_memory_exceptions);

          EndMenuButtons();
        }
        break;
#endif

void FullscreenUI::DrawQuickMenu(MainWindowType type)
{
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImVec2 display_size(ImGui::GetIO().DisplaySize);
	dl->AddRectFilled(ImVec2(0.0f, 0.0f), display_size, IM_COL32(0x21, 0x21, 0x21, 200));

	// title info
	{
		const ImVec2 title_size(
			g_large_font->CalcTextSizeA(g_large_font->FontSize, std::numeric_limits<float>::max(), -1.0f, s_current_game_title.c_str()));
		const ImVec2 subtitle_size(
			g_medium_font->CalcTextSizeA(g_medium_font->FontSize, std::numeric_limits<float>::max(), -1.0f, s_current_game_subtitle.c_str()));

		ImVec2 title_pos(display_size.x - LayoutScale(20.0f + 50.0f + 20.0f) - title_size.x,
			display_size.y - LayoutScale(20.0f + 50.0f));
		ImVec2 subtitle_pos(display_size.x - LayoutScale(20.0f + 50.0f + 20.0f) - subtitle_size.x,
			title_pos.y + g_large_font->FontSize + LayoutScale(4.0f));
		float rp_height = 0.0f;

#ifdef ENABLE_ACHIEVEMENTS
		if (Achievements::IsActive())
		{
			const std::string& rp = Achievements::GetRichPresenceString();
			if (!rp.empty())
			{
				const float wrap_width = LayoutScale(350.0f);
				const ImVec2 rp_size = g_medium_font->CalcTextSizeA(g_medium_font->FontSize, std::numeric_limits<float>::max(),
					wrap_width, rp.data(), rp.data() + rp.size());
				rp_height = rp_size.y + LayoutScale(4.0f);

				const ImVec2 rp_pos(display_size.x - LayoutScale(20.0f + 50.0f + 20.0f) - rp_size.x - rp_height,
					subtitle_pos.y + LayoutScale(4.0f));

				title_pos.x -= rp_height;
				title_pos.y -= rp_height;
				subtitle_pos.x -= rp_height;
				subtitle_pos.y -= rp_height;

				dl->AddText(g_medium_font, g_medium_font->FontSize, rp_pos, IM_COL32(255, 255, 255, 255), rp.data(),
					rp.data() + rp.size(), wrap_width);
			}
		}
#endif

		dl->AddText(g_large_font, g_large_font->FontSize, title_pos, IM_COL32(255, 255, 255, 255), s_current_game_title.c_str());
		dl->AddText(g_medium_font, g_medium_font->FontSize, subtitle_pos, IM_COL32(255, 255, 255, 255), s_current_game_subtitle.c_str());

		const ImVec2 image_min(display_size.x - LayoutScale(20.0f + 50.0f) - rp_height,
			display_size.y - LayoutScale(20.0f + 50.0f) - rp_height);
		const ImVec2 image_max(image_min.x + LayoutScale(50.0f) + rp_height, image_min.y + LayoutScale(50.0f) + rp_height);
		dl->AddImage(GetCoverForCurrentGame()->GetHandle(), image_min, image_max);
	}

	const ImVec2 window_size(LayoutScale(500.0f, LAYOUT_SCREEN_HEIGHT));
	const ImVec2 window_pos(0.0f, display_size.y - window_size.y);
	if (BeginFullscreenWindow(window_pos, window_size, "pause_menu", ImVec4(0.0f, 0.0f, 0.0f, 0.0f), 0.0f, 10.0f,
			ImGuiWindowFlags_NoBackground))
	{
		BeginMenuButtons(11, 1.0f, ImGuiFullscreen::LAYOUT_MENU_BUTTON_X_PADDING,
			ImGuiFullscreen::LAYOUT_MENU_BUTTON_Y_PADDING,
			ImGuiFullscreen::LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY);

		// NOTE: Menu close must come first, because otherwise VM destruction options will race.

		if (ActiveButton(ICON_FA_PLAY "  Resume Game", false) || WantsToCloseMenu())
			CloseQuickMenu();

		if (ActiveButton(ICON_FA_FAST_FORWARD "  Toggle Frame Limit", false))
		{
			CloseQuickMenu();
			DoToggleFrameLimit();
		}

#ifdef ENABLE_ACHIEVEMENTS
		const bool achievements_enabled = Achievements::HasActiveGame() && (Achievements::GetAchievementCount() > 0);
		if (ActiveButton(ICON_FA_TROPHY "  Achievements", false, achievements_enabled))
			OpenAchievementsWindow();

		const bool leaderboards_enabled = Achievements::HasActiveGame() && (Achievements::GetLeaderboardCount() > 0);
		if (ActiveButton(ICON_FA_STOPWATCH "  Leaderboards", false, leaderboards_enabled))
			OpenLeaderboardsWindow();

#else
		ActiveButton(ICON_FA_TROPHY "  Achievements", false, false);
		ActiveButton(ICON_FA_STOPWATCH "  Leaderboards", false, false);
#endif

		if (ActiveButton(ICON_FA_CAMERA "  Save Screenshot", false))
		{
			CloseQuickMenu();
			//s_host_interface->RunLater([]() { s_host_interface->SaveScreenshot(); });
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
			//DoChangeDisc();
		}

		if (ActiveButton(ICON_FA_SLIDERS_H "  Settings", false))
			SwitchToSettings();

		if (ActiveButton(ICON_FA_SYNC "  Reset System", false))
		{
			CloseQuickMenu();
			DoReset();
		}

		if (ActiveButton(ICON_FA_POWER_OFF "  Exit Game", false))
		{
			CloseQuickMenu();
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
			screenshot_width, screenshot_height, screenshot_pixels.data(),
			sizeof(u32) * screenshot_width, false);
	}
	else
	{
#if 0
		li->preview_texture = s_host_interface->GetDisplay()->CreateTexture(
			PLACEHOLDER_ICON_WIDTH, PLACEHOLDER_ICON_HEIGHT, 1, 1, 1, HostDisplayPixelFormat::RGBA8, PLACEHOLDER_ICON_DATA,
			sizeof(u32) * PLACEHOLDER_ICON_WIDTH, false);
#endif
	}

	if (!li->preview_texture)
		Console.Error("Failed to upload save state image to GPU");

	return true;
}

void FullscreenUI::PopulateSaveStateListEntries()
{
	s_save_state_selector_slots.clear();

	if (s_save_state_selector_loading)
	{
#if 0
    std::optional<CommonHostInterface::ExtendedSaveStateInfo> ssi = s_host_interface->GetUndoSaveStateInfo();
    if (ssi)
    {
      SaveStateListEntry li;
      InitializeSaveStateListEntry(&li, &ssi.value());
      li.title = "Undo Load State";
      li.summary = "Restores the state of the system prior to the last state loaded.";
      s_save_state_selector_slots.push_back(std::move(li));
    }
#endif
	}

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
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, LayoutScale(ImGuiFullscreen::LAYOUT_MENU_BUTTON_X_PADDING,
															ImGuiFullscreen::LAYOUT_MENU_BUTTON_Y_PADDING));

		ImGui::SetNextWindowSize(LayoutScale(1000.0f, 680.0f));
		ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::OpenPopup(window_title);
		bool is_open = !WantsToCloseMenu();
		if (!ImGui::BeginPopupModal(window_title, &is_open,
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove) ||
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
		const float image_height =
			max_image_width / (static_cast<float>(image->GetWidth()) / static_cast<float>(image->GetHeight()));
		const float image_margin = (max_image_height - image_height) / 2.0f;
		const ImRect image_bb(ImVec2(pos.x, pos.y + LayoutScale(image_margin)),
			pos + LayoutScale(max_image_width, image_margin + image_height));
		pos.x += LayoutScale(max_image_width + padding);

		ImRect text_bb(pos, ImVec2(bb.Max.x, pos.y + g_large_font->FontSize));
		ImGui::PushFont(g_large_font);
		ImGui::RenderTextClipped(text_bb.Min, text_bb.Max, entry.title.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f),
			&text_bb);
		ImGui::PopFont();

		ImGui::PushFont(g_medium_font);

		if (!entry.summary.empty())
		{
			text_bb.Min.y = text_bb.Max.y + LayoutScale(4.0f);
			text_bb.Max.y = text_bb.Min.y + g_medium_font->FontSize;
			ImGui::RenderTextClipped(text_bb.Min, text_bb.Max, entry.summary.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f),
				&text_bb);
		}

		if (!entry.path.empty())
		{
			text_bb.Min.y = text_bb.Max.y + LayoutScale(4.0f);
			text_bb.Max.y = text_bb.Min.y + g_medium_font->FontSize;
			ImGui::RenderTextClipped(text_bb.Min, text_bb.Max, entry.path.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f),
				&text_bb);
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
				Host::RunOnCPUThread([path = entry.path]() {
					VMManager::LoadState(path.c_str());
				});
				CloseSaveStateSelector();
			}
			else
			{
				Host::RunOnCPUThread([slot = entry.slot]() {
					VMManager::SaveStateToSlot(slot);
				});
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

#if 0

void FullscreenUI::DrawGameListWindow()
{
  const GameList::Entry* selected_entry = nullptr;

  if (!BeginFullscreenColumns())
  {
    EndFullscreenColumns();
    return;
  }

  if (BeginFullscreenColumnWindow(450.0f, LAYOUT_SCREEN_WIDTH, "game_list_entries"))
  {
    const ImVec2 image_size(LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT, LAYOUT_MENU_BUTTON_HEIGHT));

    BeginMenuButtons();

    SmallString summary;

    for (const GameList::Entry* entry : s_game_list_sorted_entries)
    {
      ImRect bb;
      bool visible, hovered;
      bool pressed =
        MenuButtonFrame(entry->path.c_str(), true, LAYOUT_MENU_BUTTON_HEIGHT, &visible, &hovered, &bb.Min, &bb.Max);
      if (!visible)
        continue;

      HostDisplayTexture* cover_texture = GetGameListCover(entry);
      if (entry->code.empty())
        summary.Format("%s - ", Settings::GetDiscRegionName(entry->region));
      else
        summary.Format("%s - %s - ", entry->code.c_str(), Settings::GetDiscRegionName(entry->region));

      summary.AppendString(FileSystem::GetFileNameFromPath(entry->path));

      ImGui::GetWindowDrawList()->AddImage(cover_texture->GetHandle(), bb.Min, bb.Min + image_size, ImVec2(0.0f, 0.0f),
                                           ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 255));

      const float midpoint = bb.Min.y + g_large_font->FontSize + LayoutScale(4.0f);
      const float text_start_x = bb.Min.x + image_size.x + LayoutScale(15.0f);
      const ImRect title_bb(ImVec2(text_start_x, bb.Min.y), ImVec2(bb.Max.x, midpoint));
      const ImRect summary_bb(ImVec2(text_start_x, midpoint), bb.Max);

      ImGui::PushFont(g_large_font);
      ImGui::RenderTextClipped(title_bb.Min, title_bb.Max, entry->title.c_str(),
                               entry->title.c_str() + entry->title.size(), nullptr, ImVec2(0.0f, 0.0f), &title_bb);
      ImGui::PopFont();

      if (summary)
      {
        ImGui::PushFont(g_medium_font);
        ImGui::RenderTextClipped(summary_bb.Min, summary_bb.Max, summary, summary.GetCharArray() + summary.GetLength(),
                                 nullptr, ImVec2(0.0f, 0.0f), &summary_bb);
        ImGui::PopFont();
      }

      if (pressed)
      {
        // launch game
        const std::string& path_to_launch(entry->path);
        s_host_interface->RunLater([path_to_launch]() { DoStartPath(path_to_launch, true); });
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
                                  GetTextureForGameList::EntryType(GameList::EntryType::Count)->GetHandle(),
                 LayoutScale(ImVec2(350.0f, 350.0f)));

    const float work_width = ImGui::GetCurrentWindow()->WorkRect.GetWidth();
    constexpr float field_margin_y = 10.0f;
    constexpr float start_x = 50.0f;
    float text_y = 425.0f;
    float text_width;
    SmallString text;

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
      if (!selected_entry->developer.empty())
      {
        text_width = ImGui::CalcTextSize(selected_entry->developer.c_str(), nullptr, false, work_width).x;
        ImGui::SetCursorPosX((work_width - text_width) / 2.0f);
        ImGui::TextWrapped("%s", selected_entry->developer.c_str());
      }

      // code
      text_width = ImGui::CalcTextSize(selected_entry->code.c_str(), nullptr, false, work_width).x;
      ImGui::SetCursorPosX((work_width - text_width) / 2.0f);
      ImGui::TextWrapped("%s", selected_entry->code.c_str());
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15.0f);

      // region
      ImGui::TextUnformatted("Region: ");
      ImGui::SameLine();
      ImGui::Image(s_disc_region_textures[static_cast<u32>(selected_entry->region)]->GetHandle(),
                   LayoutScale(23.0f, 16.0f));
      ImGui::SameLine();
      ImGui::Text(" (%s)", Settings::GetDiscRegionDisplayName(selected_entry->region));

      // genre
      ImGui::Text("Genre: %s", selected_entry->genre.c_str());

      // release date
      char release_date_str[64];
      selected_entry->GetReleaseDateString(release_date_str, sizeof(release_date_str));
      ImGui::Text("Release Date: %s", release_date_str);

      // compatibility
      ImGui::TextUnformatted("Compatibility: ");
      ImGui::SameLine();
      ImGui::Image(s_game_compatibility_textures[static_cast<u32>(selected_entry->compatibility_rating)]->GetHandle(),
                   LayoutScale(64.0f, 16.0f));
      ImGui::SameLine();
      ImGui::Text(" (%s)", GameList::GetGameListCompatibilityRatingString(selected_entry->compatibility_rating));

      // size
      ImGui::Text("Size: %.2f MB", static_cast<float>(selected_entry->total_size) / 1048576.0f);

      // game settings
      const u32 user_setting_count = selected_entry->settings.GetUserSettingsCount();
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

void SwitchToGameList()
{
  s_current_main_window = MainWindowType::GameList;
}

#endif

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
	ImGui::OpenPopup("About DuckStation");

	ImGui::PushFont(g_large_font);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, LayoutScale(10.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, LayoutScale(10.0f, 10.0f));

	if (ImGui::BeginPopupModal("About DuckStation", &s_about_window_open,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
	{
		ImGui::TextWrapped("DuckStation is a free and open-source simulator/emulator of the Sony PlayStation(TM) console, "
						   "focusing on playability, speed, and long-term maintainability.");
		ImGui::NewLine();
		ImGui::TextWrapped("Contributor List: https://github.com/stenzek/duckstation/blob/master/CONTRIBUTORS.md");
		ImGui::NewLine();
		ImGui::TextWrapped("Duck icon by icons8 (https://icons8.com/icon/74847/platforms.undefined.short-title)");
		ImGui::NewLine();
		ImGui::TextWrapped("\"PlayStation\" and \"PSX\" are registered trademarks of Sony Interactive Entertainment Europe "
						   "Limited. This software is not affiliated in any way with Sony Interactive Entertainment.");

		ImGui::NewLine();

		BeginMenuButtons();
		if (ActiveButton(ICON_FA_GLOBE "  GitHub Repository", false))
		{
			//s_host_interface->RunLater([]() { s_host_interface->ReportError("Go to https://github.com/stenzek/duckstation/"); });
		}
		if (ActiveButton(ICON_FA_BUG "  Issue Tracker", false))
		{
			//s_host_interface->RunLater([]() { s_host_interface->ReportError("Go to https://github.com/stenzek/duckstation/issues"); });
		}
		if (ActiveButton(ICON_FA_COMMENT "  Discord Server", false))
		{
			//s_host_interface->RunLater([]() { s_host_interface->ReportError("Go to https://discord.gg/Buktv3t"); });
		}

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
