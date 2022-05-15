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

#include "SDLNoGUIPlatform.h"
#include "NoGUIHost.h"
#include "SDLKeyNames.h"

#include "common/Threading.h"

#include "pcsx2/HostSettings.h"

#include "SDL_syswm.h"

#ifdef __APPLE__
#include <objc/message.h>
struct NSView;

static NSView* GetContentViewFromWindow(NSWindow* window)
{
	// window.contentView
	return reinterpret_cast<NSView* (*)(id, SEL)>(objc_msgSend)(reinterpret_cast<id>(window), sel_getUid("contentView"));
}
#endif

static float GetDPIScaleFactor(SDL_Window* window)
{
#ifdef __APPLE__
	static constexpr float DEFAULT_DPI = 72.0f;
#else
	static constexpr float DEFAULT_DPI = 96.0f;
#endif

	if (!window)
	{
		SDL_Window* dummy_window = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1, 1,
			SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
		if (!dummy_window)
			return 1.0f;

		const float scale = GetDPIScaleFactor(dummy_window);

		SDL_DestroyWindow(dummy_window);

		return scale;
	}

	int display_index = SDL_GetWindowDisplayIndex(window);
	float display_dpi = DEFAULT_DPI;
	if (SDL_GetDisplayDPI(display_index, &display_dpi, nullptr, nullptr) != 0)
		return 1.0f;

	return display_dpi / DEFAULT_DPI;
}

SDLNoGUIPlatform::SDLNoGUIPlatform()
{
	m_func_event_id = SDL_RegisterEvents(2);
	m_quit_event_id = m_func_event_id + 1;
	m_message_loop_running.store(true, std::memory_order_release);
}

SDLNoGUIPlatform::~SDLNoGUIPlatform()
{
	SDL_Quit();
}

void SDLNoGUIPlatform::ReportError(const std::string_view& title, const std::string_view& message)
{
	const std::string title_copy(title);
	const std::string message_copy(message);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title_copy.c_str(), message_copy.c_str(), m_window);
}

bool SDLNoGUIPlatform::CreatePlatformWindow(std::string title)
{
	u32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	if (m_fullscreen.load(std::memory_order_acquire))
		window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

	s32 window_x, window_y, window_width, window_height;
	if (!NoGUIHost::GetSavedPlatformWindowGeometry(&window_x, &window_y, &window_width, &window_height))
	{
		window_x = SDL_WINDOWPOS_UNDEFINED;
		window_y = SDL_WINDOWPOS_UNDEFINED;
		window_width = DEFAULT_WINDOW_WIDTH;
		window_height = DEFAULT_WINDOW_HEIGHT;
	}

	m_window = SDL_CreateWindow(title.c_str(), window_x, window_y, window_width, window_height, window_flags);
	if (!m_window)
		return false;

	return true;
}

void SDLNoGUIPlatform::DestroyPlatformWindow()
{
	if (!(SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN_DESKTOP))
	{
		int x = 0;
		int y = 0;
		SDL_GetWindowPosition(m_window, &x, &y);

		int width = DEFAULT_WINDOW_WIDTH;
		int height = DEFAULT_WINDOW_HEIGHT;
		SDL_GetWindowSize(m_window, &width, &height);
		NoGUIHost::SavePlatformWindowGeometry(x, y, width, height);
	}

	SDL_DestroyWindow(m_window);
	m_window = nullptr;
	m_fullscreen = false;
}

std::optional<WindowInfo> SDLNoGUIPlatform::GetPlatformWindowInfo()
{
	SDL_SysWMinfo syswm = {};
	SDL_VERSION(&syswm.version);
	if (!SDL_GetWindowWMInfo(m_window, &syswm))
	{
		Console.Error("SDL_GetWindowWMInfo failed");
		return std::nullopt;
	}

	int window_width, window_height;
	SDL_GetWindowSize(m_window, &window_width, &window_height);
	m_window_scale = GetDPIScaleFactor(m_window);

	WindowInfo wi;
	wi.surface_width = static_cast<u32>(window_width);
	wi.surface_height = static_cast<u32>(window_height);
	wi.surface_scale = m_window_scale;

	switch (syswm.subsystem)
	{
#ifdef SDL_VIDEO_DRIVER_WINDOWS
		case SDL_SYSWM_WINDOWS:
			wi.type = WindowInfo::Type::Win32;
			wi.window_handle = syswm.info.win.window;
			break;
#endif

#ifdef SDL_VIDEO_DRIVER_COCOA
		case SDL_SYSWM_COCOA:
			wi.type = WindowInfo::Type::MacOS;
			wi.window_handle = GetContentViewFromWindow(syswm.info.cocoa.window);
			break;
#endif

#ifdef SDL_VIDEO_DRIVER_X11
		case SDL_SYSWM_X11:
			wi.type = WindowInfo::Type::X11;
			wi.window_handle = reinterpret_cast<void*>(static_cast<uintptr_t>(syswm.info.x11.window));
			wi.display_connection = syswm.info.x11.display;
			break;
#endif

#ifdef SDL_VIDEO_DRIVER_WAYLAND
		case SDL_SYSWM_WAYLAND:
			wi.type = WindowInfo::Type::Wayland;
			wi.window_handle = syswm.info.wl.surface;
			wi.display_connection = syswm.info.wl.display;
			break;
#endif

		default:
			Console.Error("Unhandled syswm subsystem %u", static_cast<u32>(syswm.subsystem));
			return std::nullopt;
	}

	return wi;
}

void SDLNoGUIPlatform::SetPlatformWindowTitle(std::string title)
{
	if (!m_window)
		return;

	SDL_SetWindowTitle(m_window, title.c_str());
}

std::optional<u32> SDLNoGUIPlatform::ConvertHostKeyboardStringToCode(const std::string_view& str)
{
	std::optional<SDL_Keycode> converted(SDLKeyNames::GetKeyCodeForName(str));
	return converted.has_value() ? std::optional<u32>(static_cast<u32>(converted.value())) : std::nullopt;
}

std::optional<std::string> SDLNoGUIPlatform::ConvertHostKeyboardCodeToString(u32 code)
{
	const char* converted = SDLKeyNames::GetKeyName(code);
	return converted ? std::optional<std::string>(converted) : std::nullopt;
}

void SDLNoGUIPlatform::RunMessageLoop()
{
	while (m_message_loop_running.load(std::memory_order_acquire))
	{
		SDL_Event ev;
		if (!SDL_WaitEvent(&ev))
			continue;

		HandleSDLEvent(&ev);
	}
}

void SDLNoGUIPlatform::ExecuteInMessageLoop(std::function<void()> func)
{
	SDL_Event my_ev = {};
	my_ev.type = m_func_event_id;
	my_ev.user.data1 = new std::function<void()>(std::move(func));

	// TODO: Probably should check errors here...
	SDL_PushEvent(&my_ev);
}

void SDLNoGUIPlatform::QuitMessageLoop()
{
	m_message_loop_running.store(false, std::memory_order_release);

	SDL_Event my_ev = {};
	my_ev.type = m_quit_event_id;
	SDL_PushEvent(&my_ev);
}

void SDLNoGUIPlatform::SetFullscreen(bool enabled)
{
	if (!m_window)
		return;

	m_fullscreen.store(enabled, std::memory_order_release);
	SDL_SetWindowFullscreen(m_window, enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

bool SDLNoGUIPlatform::RequestRenderWindowSize(s32 new_window_width, s32 new_window_height)
{
	if (!m_window)
		return false;

	SDL_SetWindowSize(m_window, new_window_width, new_window_height);
	return true;
}

void SDLNoGUIPlatform::HandleSDLEvent(const SDL_Event* event)
{
	if (event->type == m_func_event_id)
	{
		std::function<void()>* func = static_cast<std::function<void()>*>(event->user.data1);
		(*func)();
		delete func;
		return;
	}
	else if (event->type == m_quit_event_id)
	{
		return;
	}

	switch (event->type)
	{
		case SDL_WINDOWEVENT:
		{
			switch (event->window.event)
			{
				case SDL_WINDOWEVENT_SIZE_CHANGED:
				{
					s32 window_width, window_height;
					SDL_GetWindowSize(m_window, &window_width, &window_height);
					NoGUIHost::ProcessPlatformWindowResize(window_width, window_height, m_window_scale);
				}
				break;

				case SDL_WINDOWEVENT_FOCUS_LOST:
				{
					NoGUIHost::PlatformWindowFocusLost();
				}
				break;

				case SDL_WINDOWEVENT_FOCUS_GAINED:
				{
					NoGUIHost::PlatformWindowFocusGained();
				}
				break;

				default:
					break;
			}
		}
		break;

		case SDL_QUIT:
		{
			Host::RunOnCPUThread([]() { Host::RequestExit(EmuConfig.SaveStateOnShutdown); });
		}
		break;

		case SDL_KEYDOWN:
		case SDL_KEYUP:
		{
			const bool pressed = (event->type == SDL_KEYDOWN);
			NoGUIHost::ProcessPlatformKeyEvent(static_cast<s32>(event->key.keysym.sym), pressed);
		}
		break;

		case SDL_MOUSEMOTION:
		{
			NoGUIHost::ProcessPlatformMouseMoveEvent(event->motion.x, event->motion.y);
		}
		break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		{
			// map left -> 0, right -> 1, middle -> 2 to match with qt
			static constexpr std::array<s32, 5> mouse_mapping = {{1, 3, 2, 4, 5}};
			if (event->button.button <= mouse_mapping.size())
			{
				const s32 button = mouse_mapping[event->button.button - 1];
				const bool pressed = (event->type == SDL_MOUSEBUTTONDOWN);
				NoGUIHost::ProcessPlatformMouseButtonEvent(button, pressed);
			}
		}
		break;

		case SDL_MOUSEWHEEL:
		{
			NoGUIHost::ProcessPlatformMouseWheelEvent(event->wheel.preciseX, event->wheel.preciseY);
		}
		break;
	}
}

std::unique_ptr<NoGUIPlatform> NoGUIPlatform::CreateSDLPlatform()
{
	if (SDL_Init(0) < 0)
	{
		pxFailRel("SDL_Init(0) failed");
		return nullptr;
	}

	return std::unique_ptr<NoGUIPlatform>(new SDLNoGUIPlatform());
}
