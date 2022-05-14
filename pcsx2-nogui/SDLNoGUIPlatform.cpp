#include "PrecompiledHeader.h"
#include "NoGUIPlatform.h"

#include "common/Threading.h"

#include "pcsx2/HostSettings.h"

#include "SDL.h"
#include "SDL_syswm.h"

static constexpr u32 DEFAULT_WINDOW_WIDTH = 1280;
static constexpr u32 DEFAULT_WINDOW_HEIGHT = 720;

class SDLNoGUIPlatform : public NoGUIPlatform
{
public:
	SDLNoGUIPlatform();
	~SDLNoGUIPlatform();

	void ReportError(const std::string_view& title, const std::string_view& message) override;

	bool CreatePlatformWindow() override;
	void DestroyPlatformWindow() override;
	std::optional<WindowInfo> GetPlatformWindowInfo() override;

	void RunMessageLoop() override;
	void ExecuteInMessageLoop(std::function<void()> func) override;
	void QuitMessageLoop() override;

	bool IsFullscreen() override;
	void SetFullscreen(bool enabled) override;

	bool RequestRenderWindowSize(s32 new_window_width, s32 new_window_height) override;

private:
	void HandleSDLEvent(const SDL_Event* event);

	void GetSavedWindowGeometry(int* x, int* y, int* width, int* height);
	void SaveWindowGeometry();

	SDL_Window* m_window = nullptr;

	int m_func_event_id = 0;
	int m_quit_event_id = 0;
	
	bool m_fullscreen = false;
	bool m_was_paused_by_focus_loss = false;
};

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
}

SDLNoGUIPlatform::~SDLNoGUIPlatform()
{
}

void SDLNoGUIPlatform::ReportError(const std::string_view& title, const std::string_view& message)
{
	const std::string title_copy(title);
	const std::string message_copy(message);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title_copy.c_str(), message_copy.c_str(), m_window);
}

bool SDLNoGUIPlatform::CreatePlatformWindow()
{
	// Create window.
	const u32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

	int window_x, window_y, window_width, window_height;
	GetSavedWindowGeometry(&window_x, &window_y, &window_width, &window_height);
	m_window = SDL_CreateWindow("kek", window_x, window_y, window_width, window_height, window_flags);
	if (!m_window)
		return false;

#if 0
	// Set window icon.
	SDL_Surface* icon_surface =
		SDL_CreateRGBSurfaceFrom(const_cast<unsigned int*>(WINDOW_ICON_DATA), WINDOW_ICON_WIDTH, WINDOW_ICON_HEIGHT, 32,
			WINDOW_ICON_WIDTH * sizeof(u32), UINT32_C(0x000000FF), UINT32_C(0x0000FF00),
			UINT32_C(0x00FF0000), UINT32_C(0xFF000000));
	if (icon_surface)
	{
		SDL_SetWindowIcon(m_window, icon_surface);
		SDL_FreeSurface(icon_surface);
	}
#endif

	// Process events so that we have everything sorted out before creating a child window for the GL context (X11).
	SDL_PumpEvents();
	return true;
}

void SDLNoGUIPlatform::DestroyPlatformWindow()
{
	SaveWindowGeometry();
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

	WindowInfo wi;
	wi.surface_width = static_cast<u32>(window_width);
	wi.surface_height = static_cast<u32>(window_height);
	wi.surface_scale = GetDPIScaleFactor(m_window);

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

void SDLNoGUIPlatform::RunMessageLoop()
{
#if 0
	// Process SDL events before the controller interface can steal them.
	const bool is_sdl_controller_interface =
		(m_controller_interface && m_controller_interface->GetBackend() == ControllerInterface::Backend::SDL);
#endif

	for (;;)
	{
		SDL_Event ev;
		if (!SDL_WaitEvent(&ev))
			continue;

#if 0
		if (is_sdl_controller_interface &&
			static_cast<SDLControllerInterface*>(m_controller_interface.get())->ProcessSDLEvent(&ev))
		{
			continue;
		}
#endif

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
	SDL_Event my_ev = {};
	my_ev.type = m_quit_event_id;
	SDL_PushEvent(&my_ev);
}

bool SDLNoGUIPlatform::IsFullscreen()
{
	throw std::logic_error("The method or operation is not implemented.");
}

void SDLNoGUIPlatform::SetFullscreen(bool enabled)
{
	throw std::logic_error("The method or operation is not implemented.");
}

bool SDLNoGUIPlatform::RequestRenderWindowSize(s32 new_window_width, s32 new_window_height)
{
	throw std::logic_error("The method or operation is not implemented.");
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
#if 0
					m_display->ResizeRenderWindow(window_width, window_height);
					OnHostDisplayResized();
#endif
				}
				break;

				case SDL_WINDOWEVENT_FOCUS_LOST:
				{
#if 0
					if (g_settings.pause_on_focus_loss && System::IsRunning() && !m_was_paused_by_focus_loss)
					{
						PauseSystem(true);
						m_was_paused_by_focus_loss = true;
					}
#endif
				}
				break;

				case SDL_WINDOWEVENT_FOCUS_GAINED:
				{
#if 0
					if (m_was_paused_by_focus_loss)
					{
						if (System::IsPaused())
							PauseSystem(false);
						m_was_paused_by_focus_loss = false;
					}
#endif
				}
				break;

				default:
					break;
			}
		}
		break;

		case SDL_QUIT:
#if 0
			m_quit_request = true;
#endif
			break;

		case SDL_KEYDOWN:
		case SDL_KEYUP:
		{
#if 0
			const bool pressed = (event->type == SDL_KEYDOWN);

			// Binding mode
			if (m_fullscreen_ui_enabled && FullscreenUI::IsBindingInput())
			{
				if (event->key.repeat > 0)
					return;

				TinyString key_string;
				if (SDLKeyNames::KeyEventToString(event, key_string))
				{
					if (FullscreenUI::HandleKeyboardBinding(key_string, pressed))
						return;
				}
			}

			if (!ImGui::GetIO().WantCaptureKeyboard && event->key.repeat == 0)
			{
				const u32 code = SDLKeyNames::KeyEventToInt(event);
				HandleHostKeyEvent(code & SDLKeyNames::KEY_MASK, code & SDLKeyNames::MODIFIER_MASK, pressed);
			}
#endif
		}
		break;

		case SDL_MOUSEMOTION:
		{
#if 0
			m_display->SetMousePosition(event->motion.x, event->motion.y);
#endif
		}
		break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		{
#if 0
			// map left -> 0, right -> 1, middle -> 2 to match with qt
			static constexpr std::array<s32, 5> mouse_mapping = {{1, 3, 2, 4, 5}};
			if (!ImGui::GetIO().WantCaptureMouse && event->button.button > 0 && event->button.button <= mouse_mapping.size())
			{
				const s32 button = mouse_mapping[event->button.button - 1];
				const bool pressed = (event->type == SDL_MOUSEBUTTONDOWN);
				HandleHostMouseEvent(button, pressed);
			}
#endif
		}
		break;
	}
}

void SDLNoGUIPlatform::GetSavedWindowGeometry(int* x, int* y, int* width, int* height)
{
	*x = Host::GetBaseIntSettingValue("SDLNoGUIWindow", "WindowX", SDL_WINDOWPOS_UNDEFINED);
	*y = Host::GetBaseIntSettingValue("SDLNoGUIWindow", "WindowY", SDL_WINDOWPOS_UNDEFINED);

	*width = Host::GetBaseIntSettingValue("SDLNoGUIWindow", "WindowWidth", -1);
	*height = Host::GetBaseIntSettingValue("SDLNoGUIWindow", "WindowHeight", -1);

	if (*width < 0 || *height < 0)
	{
		*width = DEFAULT_WINDOW_WIDTH;
		*height = DEFAULT_WINDOW_HEIGHT;

		// macOS does DPI scaling differently..
#ifndef __APPLE__
		{
			// scale by default monitor's DPI
			float scale = GetDPIScaleFactor(nullptr);
			*width = static_cast<int>(std::round(static_cast<float>(*width) * scale));
			*height = static_cast<int>(std::round(static_cast<float>(*height) * scale));
		}
#endif
	}
}

void SDLNoGUIPlatform::SaveWindowGeometry()
{
	if (m_fullscreen)
		return;

	int x = 0;
	int y = 0;
	SDL_GetWindowPosition(m_window, &x, &y);

	int width = DEFAULT_WINDOW_WIDTH;
	int height = DEFAULT_WINDOW_HEIGHT;
	SDL_GetWindowSize(m_window, &width, &height);

	int old_x, old_y, old_width, old_height;
	GetSavedWindowGeometry(&old_x, &old_y, &old_width, &old_height);
	if (x == old_x && y == old_y && width == old_width && height == old_height)
		return;

	//m_settings_interface->SetIntValue("SDLNoGUIWindow", "WindowX", x);
	//m_settings_interface->SetIntValue("SDLNoGUIWindow", "WindowY", y);
	//m_settings_interface->SetIntValue("SDLNoGUIWindow", "WindowWidth", width);
	//m_settings_interface->SetIntValue("SDLNoGUIWindow", "WindowHeight", height);
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
