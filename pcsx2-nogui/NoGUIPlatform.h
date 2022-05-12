#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "pcsx2/HostDisplay.h"

class NoGUIPlatform
{
public:
	virtual ~NoGUIPlatform() = default;

	virtual void ReportError(const std::string_view& title, const std::string_view& message) = 0;

	virtual bool CreatePlatformWindow() = 0;
	virtual void DestroyPlatformWindow() = 0;

	virtual std::optional<WindowInfo> GetPlatformWindowInfo() = 0;

	virtual void RunMessageLoop() = 0;
	virtual void ExecuteInMessageLoop(std::function<void()> func) = 0;
	virtual void QuitMessageLoop() = 0;

	virtual bool IsFullscreen() = 0;
	virtual void SetFullscreen(bool enabled) = 0;

	virtual bool RequestRenderWindowSize(s32 new_window_width, s32 new_window_height) = 0;

	static std::unique_ptr<NoGUIPlatform> CreateSDLPlatform();
};

extern std::unique_ptr<NoGUIPlatform> g_nogui_window;
