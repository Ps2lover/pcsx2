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

#pragma once
#include "common/Pcsx2Defs.h"
#include "common/ProgressCallback.h"
#include <string>
#include <memory>

class HostDisplayTexture;
class SettingsInterface;

namespace FullscreenUI
{
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
	void OnVMStarted();
	void OnVMPaused();
	void OnVMResumed();
	void OnVMDestroyed();
	void OnRunningGameChanged(std::string path, std::string serial, std::string title, u32 crc);
	void OpenPauseMenu();

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

	class ProgressCallback final : public BaseProgressCallback
	{
	public:
		ProgressCallback(std::string name);
		~ProgressCallback() override;

		void PushState() override;
		void PopState() override;

		void SetCancellable(bool cancellable) override;
		void SetTitle(const char* title) override;
		void SetStatusText(const char* text) override;
		void SetProgressRange(u32 range) override;
		void SetProgressValue(u32 value) override;

		void DisplayError(const char* message) override;
		void DisplayWarning(const char* message) override;
		void DisplayInformation(const char* message) override;
		void DisplayDebugMessage(const char* message) override;

		void ModalError(const char* message) override;
		bool ModalConfirmation(const char* message) override;
		void ModalInformation(const char* message) override;

		void SetCancelled();

	private:
		void Redraw(bool force);

		std::string m_name;
		int m_last_progress_percent = -1;
	};
} // namespace FullscreenUI
