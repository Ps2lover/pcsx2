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

#include "NoGUIPlatform.h"

#include "SDL.h"

#include <atomic>

class SDLNoGUIPlatform : public NoGUIPlatform
{
public:
	SDLNoGUIPlatform();
	~SDLNoGUIPlatform();

	void ReportError(const std::string_view& title, const std::string_view& message) override;

	bool CreatePlatformWindow(std::string title) override;
	void DestroyPlatformWindow() override;
	std::optional<WindowInfo> GetPlatformWindowInfo() override;
	void SetPlatformWindowTitle(std::string title) override;

	std::optional<u32> ConvertHostKeyboardStringToCode(const std::string_view& str) override;
	std::optional<std::string> ConvertHostKeyboardCodeToString(u32 code) override;

	void RunMessageLoop() override;
	void ExecuteInMessageLoop(std::function<void()> func) override;
	void QuitMessageLoop() override;

	void SetFullscreen(bool enabled) override;

	bool RequestRenderWindowSize(s32 new_window_width, s32 new_window_height) override;

private:
	void HandleSDLEvent(const SDL_Event* event);

	static constexpr s32 DEFAULT_WINDOW_WIDTH = 1280;
	static constexpr s32 DEFAULT_WINDOW_HEIGHT = 720;

	SDL_Window* m_window = nullptr;
	float m_window_scale = 1.0f;

	std::atomic_bool m_message_loop_running{false};
	std::atomic_bool m_fullscreen{false};

	int m_func_event_id = 0;
	int m_quit_event_id = 0;
};
