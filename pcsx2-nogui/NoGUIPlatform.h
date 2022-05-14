#pragma once

#include "common/Pcsx2Defs.h"

#include "pcsx2/HostDisplay.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

class NoGUIPlatform
{
public:
	virtual ~NoGUIPlatform() = default;

	virtual void ReportError(const std::string_view& title, const std::string_view& message) = 0;

	virtual bool CreatePlatformWindow(std::string title) = 0;
	virtual void DestroyPlatformWindow() = 0;

	virtual std::optional<WindowInfo> GetPlatformWindowInfo() = 0;
	virtual void SetPlatformWindowTitle(std::string title) = 0;

	virtual std::optional<u32> ConvertHostKeyboardStringToCode(const std::string_view& str) = 0;
	virtual std::optional<std::string> ConvertHostKeyboardCodeToString(u32 code) = 0;

	virtual void RunMessageLoop() = 0;
	virtual void ExecuteInMessageLoop(std::function<void()> func) = 0;
	virtual void QuitMessageLoop() = 0;

	virtual bool IsFullscreen() = 0;
	virtual void SetFullscreen(bool enabled) = 0;

	virtual bool RequestRenderWindowSize(s32 new_window_width, s32 new_window_height) = 0;

	static std::unique_ptr<NoGUIPlatform> CreateSDLPlatform();
};

extern std::unique_ptr<NoGUIPlatform> g_nogui_window;
