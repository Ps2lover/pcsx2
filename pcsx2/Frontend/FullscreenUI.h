#pragma once
#include "common/Pcsx2Defs.h"
#include <string>
#include <memory>

class HostDisplayTexture;
class SettingsInterface;

namespace FullscreenUI {
enum class MainWindowType
{
  None,
  Landing,
  GameList,
  Settings,
  QuickMenu
};

enum class SettingsPage
{
  InterfaceSettings,
  GameListSettings,
	BIOSSettings,
	EmulationSettings,
  SystemSettings,
  GraphicsSettings,
  AudioSettings,
  MemoryCardSettings,
  ControllerSettings,
  HotkeySettings,
  AchievementsSettings,
  Count
};

bool Initialize();
bool IsInitialized();
bool HasActiveWindow();
void UpdateSettings();
void OnVMStarted();
void OnVMDestroyed();
void OnRunningGameChanged(std::string path, std::string serial, std::string title, u32 crc);
void OpenPauseMenu();
void CloseQuickMenu();

#ifdef WITH_ACHIEVEMENTS
bool OpenAchievementsWindow();
bool OpenLeaderboardsWindow();
#endif

void Shutdown();
void Render();

bool IsBindingInput();
bool HandleKeyboardBinding(const char* keyName, bool pressed);

std::unique_ptr<HostDisplayTexture> LoadTextureResource(const char* name, bool allow_fallback = true);

// Returns true if the message has been dismissed.
bool DrawErrorWindow(const char* message);
bool DrawConfirmWindow(const char* message, bool* result);

//Settings& GetSettingsCopy();
void SaveAndApplySettings();
void SetDebugMenuAllowed(bool allowed);

/// Only ImGuiNavInput_Activate, ImGuiNavInput_Cancel, and DPad should be forwarded.
/// Returns true if the UI consumed the event, and it should not execute the normal handler.
//bool SetControllerNavInput(FrontendCommon::ControllerNavigationButton button, bool value);

/// Forwards the controller navigation to ImGui for fullscreen navigation. Call before NewFrame().
void SetImGuiNavInputs();

} // namespace FullscreenUI
