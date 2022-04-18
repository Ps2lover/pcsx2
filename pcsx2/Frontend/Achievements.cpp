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

#include "Frontend/Achievements.h"

#include "common/Assertions.h"
#include "common/FileSystem.h"
#include "common/HTTPDownloader.h"
#include "common/Console.h"
#include "common/MD5Digest.h"
#include "common/Path.h"
#include "common/StringUtil.h"
#include "common/Timer.h"

#include "rc_url.h"
#include "rcheevos.h"
#include "rapidjson/document.h"
#include "fmt/core.h"

#include "CDVD/IsoFS/IsoFSCDVD.h"
#include "CDVD/IsoFS/IsoFS.h"
#include "Elfheader.h"
#include "Host.h"
#include "HostSettings.h"
#include "IopMem.h"
#include "Memory.h"
#include "VMManager.h"
#include "vtlb.h"
#include "svnrev.h"

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <limits>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#ifdef ENABLE_RAINTEGRATION
// RA_Interface.h indirectly includes windows.h, which defines a bunch of stuff and breaks rapidjson.
#include "RA_Interface.h"
#undef GetObject
#endif

namespace Achievements
{
	enum : s32
	{
		HTTP_OK = Common::HTTPDownloader::HTTP_OK,

		// Number of seconds between rich presence pings. RAIntegration uses 2 minutes.
		RICH_PRESENCE_PING_FREQUENCY = 2 * 60,
		NO_RICH_PRESENCE_PING_FREQUENCY = RICH_PRESENCE_PING_FREQUENCY * 2,

		// Size of the EE physical memory exposed to RetroAchievements.
		EXPOSED_EE_MEMORY_SIZE = Ps2MemSize::MainRam + Ps2MemSize::Scratch,
	};

	static void FormattedError(const char* format, ...);
	static void CheevosEventHandler(const rc_runtime_event_t* runtime_event);
	static unsigned PeekMemory(unsigned address, unsigned num_bytes, void* ud);
	static void PokeMemory(unsigned address, unsigned num_bytes, void* ud, unsigned value);
	static void ActivateLockedAchievements();
	static bool ActivateAchievement(Achievement* achievement);
	static void DeactivateAchievement(Achievement* achievement);
	static void SendPing();
	static void SendPlaying();
	static void UpdateRichPresence();
	static std::string GetErrorFromResponseJSON(const rapidjson::Document& doc);
	static void LogFailedResponseJSON(const Common::HTTPDownloader::Request::Data& data);
	static bool ParseResponseJSON(const char* request_type, s32 status_code,
		const Common::HTTPDownloader::Request::Data& data, rapidjson::Document& doc,
		const char* success_field = "Success");
	static Achievement* GetAchievementByID(u32 id);
	static void ClearGameInfo(bool clear_achievements = true, bool clear_leaderboards = true);
	static void ClearGameHash();
	static std::string GetUserAgent();
	static void LoginCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data);
	static void LoginASyncCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data);
	static void SendLogin(const char* username, const char* password, Common::HTTPDownloader* http_downloader,
		Common::HTTPDownloader::Request::Callback callback);
	static void UpdateImageDownloadProgress();
	static void DownloadImage(std::string url, std::string cache_filename);
	static std::string GetBadgeImageFilename(const char* badge_name, bool locked, bool cache_path);
	static std::string ResolveBadgePath(const char* badge_name, bool locked);
	static void DisplayAchievementSummary();
	static void GetUserUnlocksCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data);
	static void GetUserUnlocks();
	static void GetPatchesCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data);
	static void GetLbInfoCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data);
	static void GetPatches(u32 game_id);
	static std::string_view GetELFNameForHash(const std::string& elf_path);
	static std::optional<std::vector<u8>> ReadELFFromCurrentDisc(const std::string& elf_path);
	static std::string GetGameHash();
	static void GetGameIdCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data);
	static void SendPlayingCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data);
	static void UpdateRichPresence();
	static void SendPingCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data);
	static void UnlockAchievementCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data);
	static void SubmitLeaderboardCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data);

	bool g_active = false;
	bool g_challenge_mode = false;
	u32 g_game_id = 0;

	static bool g_logged_in = false;
	static bool s_test_mode = false;
	static bool s_unofficial_test_mode = false;
	static bool s_use_first_disc_from_playlist = true;
	static bool s_rich_presence_enabled = false;

#ifdef ENABLE_RAINTEGRATION
	bool g_using_raintegration = false;
#endif

	static rc_runtime_t s_rcheevos_runtime;
	static std::unique_ptr<Common::HTTPDownloader> s_http_downloader;

	static std::string s_username;
	static std::string s_login_token;

	static u32 s_last_game_crc;
	static std::string s_game_hash;
	static std::string s_game_title;
	static std::string s_game_developer;
	static std::string s_game_publisher;
	static std::string s_game_release_date;
	static std::string s_game_icon;
	static std::vector<Achievements::Achievement> s_achievements;
	static std::vector<Achievements::Leaderboard> s_leaderboards;

	static bool s_has_rich_presence = false;
	static std::string s_rich_presence_string;
	static Common::Timer s_last_ping_time;

	static u32 s_last_queried_lboard;
	static std::optional<std::vector<Achievements::LeaderboardEntry>> s_lboard_entries;

	static u32 s_total_image_downloads;
	static u32 s_completed_image_downloads;
	static bool s_image_download_progress_active;

} // namespace Achievements

template <typename T>
static std::string GetOptionalString(const T& value, const char* key)
{
	if (!value.HasMember(key) || !value[key].IsString())
		return std::string();

	return value[key].GetString();
}

template <typename T>
static u32 GetOptionalUInt(const T& value, const char* key)
{
	if (!value.HasMember(key) || !value[key].IsUint())
		return 0;

	return value[key].GetUint();
}

void Achievements::FormattedError(const char* format, ...)
{
	std::va_list ap;
	va_start(ap, format);
	std::string error(fmt::format("Achievements error: {}", StringUtil::StdStringFromFormatV(format, ap)));
	va_end(ap);

	Console.Error(error);
	Host::AddOSDMessage(std::move(error), 10.0f);
}

std::string Achievements::GetErrorFromResponseJSON(const rapidjson::Document& doc)
{
	if (doc.HasMember("Error") && doc["Error"].IsString())
		return doc["Error"].GetString();

	return "";
}

void Achievements::LogFailedResponseJSON(const Common::HTTPDownloader::Request::Data& data)
{
	const std::string str_data(reinterpret_cast<const char*>(data.data()), data.size());
	Console.Error("API call failed. Response JSON was:\n%s", str_data.c_str());
}

bool Achievements::ParseResponseJSON(const char* request_type, s32 status_code,
	const Common::HTTPDownloader::Request::Data& data, rapidjson::Document& doc,
	const char* success_field)
{
	if (status_code != HTTP_OK || data.empty())
	{
		FormattedError("%s failed: empty response", request_type);
		LogFailedResponseJSON(data);
		return false;
	}

	doc.Parse(reinterpret_cast<const char*>(data.data()), data.size());
	if (doc.HasParseError())
	{
		FormattedError("%s failed: parse error at offset %zu: %u", request_type, doc.GetErrorOffset(),
			static_cast<unsigned>(doc.GetParseError()));
		LogFailedResponseJSON(data);
		return false;
	}

	if (success_field && (!doc.HasMember(success_field) || !doc[success_field].GetBool()))
	{
		const std::string error(GetErrorFromResponseJSON(doc));
		FormattedError("%s failed: Server returned an error: %s", request_type, error.c_str());
		LogFailedResponseJSON(data);
		return false;
	}

	return true;
}

static Achievements::Achievement* Achievements::GetAchievementByID(u32 id)
{
	for (Achievement& ach : s_achievements)
	{
		if (ach.id == id)
			return &ach;
	}

	return nullptr;
}

void Achievements::ClearGameInfo(bool clear_achievements, bool clear_leaderboards)
{
	const bool had_game = (g_game_id != 0);

	if (clear_achievements)
	{
		while (!s_achievements.empty())
		{
			Achievement& ach = s_achievements.back();
			DeactivateAchievement(&ach);
			s_achievements.pop_back();
		}
	}
	if (clear_leaderboards)
	{
		while (!s_leaderboards.empty())
		{
			Leaderboard& lb = s_leaderboards.back();
			rc_runtime_deactivate_lboard(&s_rcheevos_runtime, lb.id);
			s_leaderboards.pop_back();
		}

		s_last_queried_lboard = 0;
		s_lboard_entries.reset();
	}

	if (s_achievements.empty() && s_leaderboards.empty())
	{
		// Ready to tear down cheevos completely
		std::string().swap(s_game_title);
		std::string().swap(s_game_developer);
		std::string().swap(s_game_publisher);
		std::string().swap(s_game_release_date);
		std::string().swap(s_game_icon);
		std::string().swap(s_rich_presence_string);
		s_has_rich_presence = false;
		g_game_id = 0;
	}

	if (had_game)
		Host::OnRetroAchievementsRefreshed();
}

void Achievements::ClearGameHash()
{
	s_last_game_crc = 0;
	std::string().swap(s_game_hash);
}

std::string Achievements::GetUserAgent()
{
	// FIXME: Include platform and stuff.
	const char* platform = "Unknown";
	const char* arch = "Unknown";
	const char* ver = (GIT_TAGGED_COMMIT) ? (GIT_TAG) : (GIT_REV);
	return StringUtil::StdStringFromFormat("PCSX2 for %s (%s) %s", platform, arch, ver);
}

void Achievements::Initialize()
{
	if (IsUsingRAIntegration())
		return;

	pxAssertRel(EmuConfig.Achievements.Enabled, "Achievements are enabled");

	s_http_downloader = Common::HTTPDownloader::Create(GetUserAgent().c_str());
	if (!s_http_downloader)
	{
		Host::ReportErrorAsync("Achievements Error", "Failed to create HTTPDownloader, cannot use RetroAchievements");
		return;
	}

	g_active = true;
	rc_runtime_init(&s_rcheevos_runtime);

	s_last_ping_time.Reset();
	s_username = Host::GetBaseStringSettingValue("Achievements", "Username");
	s_login_token = Host::GetBaseStringSettingValue("Achievements", "Token");
	g_logged_in = (!s_username.empty() && !s_login_token.empty());

	if (IsLoggedIn() && VMManager::HasValidVM())
		GameChanged();
}

void Achievements::UpdateSettings(const Pcsx2Config::RetroAchievementsOptions& old_config)
{
	if (IsUsingRAIntegration())
		return;

	if (!EmuConfig.Achievements.Enabled)
	{
		// we're done here
		Shutdown();
		return;
	}

	if (!g_active)
	{
		// we just got enabled
		Initialize();
	}

	// FIXME: Handle changes to various settings individually
	if (EmuConfig.Achievements.TestMode != old_config.TestMode ||
		EmuConfig.Achievements.UnofficialTestMode != old_config.UnofficialTestMode ||
		EmuConfig.Achievements.UseFirstDiscFromPlaylist != old_config.UseFirstDiscFromPlaylist ||
		EmuConfig.Achievements.RichPresence != old_config.RichPresence ||
		EmuConfig.Achievements.ChallengeMode != old_config.ChallengeMode)
	{
		Shutdown();
		Initialize();
	}
}

bool Achievements::Shutdown()
{
#ifdef ENABLE_RAINTEGRATION
	if (IsUsingRAIntegration())
	{
		if (!RA_ConfirmLoadNewRom(true))
			return false;

		RA_SetPaused(false);
		RA_ActivateGame(0);
		return true;
	}
#endif

	if (!g_active)
		return true;

	pxAssertRel(!s_image_download_progress_active, "Image download still in progress on shutdown");

	s_http_downloader->WaitForAllRequests();

	ClearGameInfo();
	ClearGameHash();
	std::string().swap(s_username);
	std::string().swap(s_login_token);
	g_logged_in = false;
	Host::OnRetroAchievementsRefreshed();

	g_active = false;
	rc_runtime_destroy(&s_rcheevos_runtime);

	s_http_downloader.reset();
	return true;
}

bool Achievements::Reset()
{
#ifdef ENABLE_RAINTEGRATION
	if (IsUsingRAIntegration())
	{
		if (!RA_ConfirmLoadNewRom(false))
			return false;

		RA_OnReset();
		return true;
	}
#endif

	if (!g_active)
		return true;

	DevCon.WriteLn("Resetting rcheevos state...");
	rc_runtime_reset(&s_rcheevos_runtime);
	return true;
}

void Achievements::OnPaused(bool paused)
{
#ifdef ENABLE_RAINTEGRATION
	if (IsUsingRAIntegration())
		RA_SetPaused(paused);
#endif
}

void Achievements::VSyncUpdate()
{
#ifdef ENABLE_RAINTEGRATION
	if (IsUsingRAIntegration())
	{
		RA_DoAchievementsFrame();
		return;
	}
#endif

	s_http_downloader->PollRequests();

	if (HasActiveGame())
	{
		rc_runtime_do_frame(&s_rcheevos_runtime, &CheevosEventHandler, &PeekMemory, nullptr, nullptr);
		UpdateRichPresence();

		if (!s_test_mode)
		{
			const s32 ping_frequency =
				s_rich_presence_enabled ? RICH_PRESENCE_PING_FREQUENCY : NO_RICH_PRESENCE_PING_FREQUENCY;
			if (static_cast<s32>(s_last_ping_time.GetTimeSeconds()) >= ping_frequency)
				SendPing();
		}
	}
}

void Achievements::LoadState(const u8* state_data, u32 state_data_size)
{
	pxAssertRel(g_active, "Achievements are active");

#ifdef ENABLE_RAINTEGRATION
	if (IsUsingRAIntegration())
	{
		if (state_data_size == 0)
		{
			Console.Warning("State is missing cheevos data, resetting RAIntegration");
			RA_OnReset();
		}
		else
		{
			RA_RestoreState(reinterpret_cast<const char*>(state_data));
		}

		return;
	}
#endif

	if (state_data_size == 0)
	{
		// reset runtime, no data (state might've been created without cheevos)
		Console.Warning("State is missing cheevos data, resetting runtime");
		rc_runtime_reset(&s_rcheevos_runtime);
		return;
	}

	// These routines scare me a bit.. the data isn't bounds checked.
	// Really hope that nobody puts any thing malicious in a save state...
	const int result = rc_runtime_deserialize_progress(&s_rcheevos_runtime, state_data, nullptr);
	if (result != RC_OK)
	{
		Console.Warning("Failed to deserialize cheevos state (%d), resetting", result);
		rc_runtime_reset(&s_rcheevos_runtime);
	}
}

std::vector<u8> Achievements::SaveState()
{
	std::vector<u8> ret;

#ifdef ENABLE_RAINTEGRATION
	if (IsUsingRAIntegration())
	{
		const int size = RA_CaptureState(nullptr, 0);

		const u32 data_size = (size >= 0) ? static_cast<u32>(size) : 0;
		ret.resize(data_size);

		const int result = RA_CaptureState(reinterpret_cast<char*>(ret.data()), static_cast<int>(data_size));
		if (result != static_cast<int>(data_size))
		{
			Console.Warning("Failed to serialize cheevos state from RAIntegration.");
			ret.clear();
		}

		return ret;
	}
#endif

	// internally this happens twice.. not great.
	const int size = rc_runtime_progress_size(&s_rcheevos_runtime, nullptr);

	const u32 data_size = (size >= 0) ? static_cast<u32>(size) : 0;
	ret.resize(data_size);

	const int result = rc_runtime_serialize_progress(ret.data(), &s_rcheevos_runtime, nullptr);
	if (result != RC_OK)
	{
		// set data to zero, effectively serializing nothing
		Console.Warning("Failed to serialize cheevos state (%d)", result);
		ret.clear();
	}

	return ret;
}

std::string Achievements::GetUsername()
{
	return s_username;
}

std::string Achievements::GetRichPresenceString()
{
	return s_rich_presence_string;
}

void Achievements::LoginCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data)
{
	rapidjson::Document doc;
	if (!ParseResponseJSON("Login", status_code, data, doc))
		return;

	if (!doc["User"].IsString() || !doc["Token"].IsString())
	{
		FormattedError("Login failed. Please check your user name and password, and try again.");
		return;
	}

	std::string username(doc["User"].GetString());
	std::string login_token(doc["Token"].GetString());

	// save to config
	Host::SetBaseStringSettingValue("Achievements", "Username", username.c_str());
	Host::SetBaseStringSettingValue("Achievements", "Token", login_token.c_str());
	Host::SetBaseStringSettingValue("Achievements", "LoginTimestamp", fmt::format("{}", std::time(nullptr)).c_str());
	Host::CommitBaseSettingChanges();

	if (g_active)
	{
		s_username = std::move(username);
		s_login_token = std::move(login_token);
		g_logged_in = true;

		// If we have a game running, set it up.
		if (VMManager::HasValidVM())
			GameChanged();
	}
}

void Achievements::LoginASyncCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data)
{
#if 0
	// FIXME
	if (ImGuiFullscreen::IsInitialized())
		ImGuiFullscreen::CloseBackgroundProgressDialog("cheevos_async_login");
#endif

	LoginCallback(status_code, data);
}

void Achievements::SendLogin(const char* username, const char* password, Common::HTTPDownloader* http_downloader,
	Common::HTTPDownloader::Request::Callback callback)
{
	char url[768] = {};
	int res = rc_url_login_with_password(url, sizeof(url), username, password);
	pxAssertRel(res == 0, "URL generation failed");

	http_downloader->CreateRequest(url, std::move(callback));
}

bool Achievements::LoginAsync(const char* username, const char* password)
{
	s_http_downloader->WaitForAllRequests();

	if (g_logged_in || std::strlen(username) == 0 || std::strlen(password) == 0 || IsUsingRAIntegration())
		return false;

#if 0
	// FIXME
	if (ImGuiFullscreen::IsInitialized())
	{
		ImGuiFullscreen::OpenBackgroundProgressDialog(
			"cheevos_async_login", "Logging in to RetroAchivements...", 0, 1,
			0);
	}
#endif

	SendLogin(username, password, s_http_downloader.get(), LoginASyncCallback);
	return true;
}

bool Achievements::Login(const char* username, const char* password)
{
	if (g_active)
		s_http_downloader->WaitForAllRequests();

	if (g_logged_in || std::strlen(username) == 0 || std::strlen(password) == 0 || IsUsingRAIntegration())
		return false;

	if (g_active)
	{
		SendLogin(username, password, s_http_downloader.get(), LoginCallback);
		s_http_downloader->WaitForAllRequests();
		return IsLoggedIn();
	}

	// create a temporary downloader if we're not initialized
	pxAssertRel(!g_active, "RetroAchievements is not active on login");
	std::unique_ptr<Common::HTTPDownloader> http_downloader =
		Common::HTTPDownloader::Create(GetUserAgent().c_str());
	if (!http_downloader)
		return false;

	SendLogin(username, password, http_downloader.get(), LoginCallback);
	http_downloader->WaitForAllRequests();

	return !Host::GetBaseStringSettingValue("Achievements", "Token").empty();
}

void Achievements::Logout()
{
	if (g_active)
	{
		s_http_downloader->WaitForAllRequests();
		if (g_logged_in)
		{
			ClearGameInfo();
			std::string().swap(s_username);
			std::string().swap(s_login_token);
			g_logged_in = false;
			Host::OnRetroAchievementsRefreshed();
		}
	}

	// remove from config
	Host::DeleteBaseSettingValue("Achievements", "Username");
	Host::DeleteBaseSettingValue("Achievements", "Token");
	Host::DeleteBaseSettingValue("Achievements", "LoginTimestamp");
	Host::CommitBaseSettingChanges();
}

void Achievements::UpdateImageDownloadProgress()
{
	// FIXME
#if 0
	static const char* str_id = "cheevo_image_download";

	if (s_completed_image_downloads >= s_total_image_downloads)
	{
		s_completed_image_downloads = 0;
		s_total_image_downloads = 0;

		if (s_image_download_progress_active)
		{
			ImGuiFullscreen::CloseBackgroundProgressDialog(str_id);
			s_image_download_progress_active = false;
		}

		return;
	}

	if (!ImGuiFullscreen::IsInitialized())
		return;

	std::string message = "Downloading achievement resources...");
	if (!s_image_download_progress_active)
	{
		ImGuiFullscreen::OpenBackgroundProgressDialog(str_id, std::move(message), 0,
			static_cast<s32>(s_total_image_downloads),
			static_cast<s32>(s_completed_image_downloads));
		s_image_download_progress_active = true;
	}
	else
	{
		ImGuiFullscreen::UpdateBackgroundProgressDialog(str_id, std::move(message), 0,
			static_cast<s32>(s_total_image_downloads),
			static_cast<s32>(s_completed_image_downloads));
	}
#endif
}

void Achievements::DownloadImage(std::string url, std::string cache_filename)
{
	auto callback = [cache_filename](s32 status_code, const Common::HTTPDownloader::Request::Data& data) {
		s_completed_image_downloads++;
		UpdateImageDownloadProgress();

		if (status_code != HTTP_OK)
			return;

		if (!FileSystem::WriteBinaryFile(cache_filename.c_str(), data.data(), data.size()))
		{
			Console.Error("Failed to write badge image to '%s'", cache_filename.c_str());
			return;
		}

#if 0
		// FIXME
		ImGuiFullscreen::InvalidateCachedTexture(cache_filename);
#endif
		UpdateImageDownloadProgress();
	};

	s_total_image_downloads++;
	UpdateImageDownloadProgress();

	s_http_downloader->CreateRequest(std::move(url), std::move(callback));
}

std::string Achievements::GetBadgeImageFilename(const char* badge_name, bool locked, bool cache_path)
{
	if (!cache_path)
	{
		return StringUtil::StdStringFromFormat("%s%s.png", badge_name, locked ? "_lock" : "");
	}
	else
	{
		// well, this comes from the internet.... :)
		std::string clean_name(badge_name);
		Path::SanitizeFileName(clean_name);

		std::string filename(
			StringUtil::StdStringFromFormat("achievement_badge" FS_OSPATH_SEPARATOR_STR "%s%s.png",
				clean_name.c_str(), locked ? "_lock" : ""));

		return Path::Combine(EmuFolders::Cache, filename);
	}
}

std::string Achievements::ResolveBadgePath(const char* badge_name, bool locked)
{
	char url[256];

	// unlocked image
	std::string cache_path(GetBadgeImageFilename(badge_name, locked, true));
	if (FileSystem::FileExists(cache_path.c_str()))
		return cache_path;

	std::string badge_name_with_extension(GetBadgeImageFilename(badge_name, locked, false));
	int res = rc_url_get_badge_image(url, sizeof(url), badge_name_with_extension.c_str());
	pxAssertRel(res == 0, "URL generation failed");
	DownloadImage(url, cache_path);
	return cache_path;
}

void Achievements::DisplayAchievementSummary()
{
	std::string title = s_game_title;
	if (g_challenge_mode)
		title += " (Hardcore Mode)";

	std::string summary;
	if (GetAchievementCount() > 0)
	{
		summary = StringUtil::StdStringFromFormat("You have earned %u of %u achievements, and %u of %u points.",
			GetUnlockedAchiementCount(), GetAchievementCount(), GetCurrentPointsForGame(), GetMaximumPointsForGame());
	}
	else
	{
		summary = "This game has no achievements.";
	}
	if (GetLeaderboardCount() > 0)
	{
		summary.push_back('\n');
		if (g_challenge_mode)
		{
			summary.append("Leaderboards are enabled.");
		}
		else
		{
			summary.append("Leaderboards are DISABLED because Hardcore Mode is off.");
		}
	}

#if 0
	// FIXME
	ImGuiFullscreen::AddNotification(10.0f, std::move(title), std::move(summary), s_game_icon);
#endif
}

void Achievements::GetUserUnlocksCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data)
{
	rapidjson::Document doc;
	if (!ParseResponseJSON("Get User Unlocks", status_code, data, doc))
	{
		ClearGameInfo(true, false);
		return;
	}

	// verify game id for sanity
	const u32 game_id = GetOptionalUInt(doc, "GameID");
	if (game_id != g_game_id)
	{
		FormattedError("GameID from user unlocks doesn't match (got %u expected %u)", game_id, g_game_id);
		ClearGameInfo(true, false);
		return;
	}

	// flag achievements as unlocked
	if (doc.HasMember("UserUnlocks") && doc["UserUnlocks"].IsArray())
	{
		for (const auto& value : doc["UserUnlocks"].GetArray())
		{
			if (!value.IsUint())
				continue;

			const u32 achievement_id = value.GetUint();
			Achievement* cheevo = GetAchievementByID(achievement_id);
			if (!cheevo)
			{
				Console.Error("Server returned unknown achievement %u", achievement_id);
				continue;
			}

			cheevo->locked = false;
		}
	}

	// start scanning for locked achievements
	ActivateLockedAchievements();
	DisplayAchievementSummary();
	SendPlaying();
	UpdateRichPresence();
	SendPing();
	Host::OnRetroAchievementsRefreshed();
}

void Achievements::GetUserUnlocks()
{
	char url[512];
	int res = rc_url_get_unlock_list(url, sizeof(url), s_username.c_str(), s_login_token.c_str(), g_game_id,
		static_cast<int>(g_challenge_mode));
	pxAssertRel(res == 0, "URL generation failed");

	s_http_downloader->CreateRequest(url, GetUserUnlocksCallback);
}

void Achievements::GetPatchesCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data)
{
	ClearGameInfo();

	rapidjson::Document doc;
	if (!ParseResponseJSON("Get Patches", status_code, data, doc))
		return;

	if (!doc.HasMember("PatchData") || !doc["PatchData"].IsObject())
	{
		FormattedError("No patch data returned from server.");
		return;
	}

	// parse info
	const auto patch_data(doc["PatchData"].GetObject());
	if (!patch_data["ID"].IsUint())
	{
		FormattedError("Patch data is missing game ID");
		return;
	}

	g_game_id = GetOptionalUInt(patch_data, "ID");
	s_game_title = GetOptionalString(patch_data, "Title");
	s_game_developer = GetOptionalString(patch_data, "Developer");
	s_game_publisher = GetOptionalString(patch_data, "Publisher");
	s_game_release_date = GetOptionalString(patch_data, "Released");

	// try for a icon
	std::string icon_name(GetOptionalString(patch_data, "ImageIcon"));
	if (!icon_name.empty())
	{
		s_game_icon = Path::Combine(EmuFolders::Cache, StringUtil::StdStringFromFormat(
																	"achievement_gameicon" FS_OSPATH_SEPARATOR_STR "%u.png", g_game_id));
		if (!FileSystem::FileExists(s_game_icon.c_str()))
		{
			// for some reason rurl doesn't have this :(
			std::string icon_url(StringUtil::StdStringFromFormat("http://i.retroachievements.org%s", icon_name.c_str()));
			DownloadImage(std::move(icon_url), s_game_icon);
		}
	}

	// parse achievements
	if (patch_data.HasMember("Achievements") && patch_data["Achievements"].IsArray())
	{
		const auto achievements(patch_data["Achievements"].GetArray());
		for (const auto& achievement : achievements)
		{
			if (!achievement.HasMember("ID") || !achievement["ID"].IsNumber() || !achievement.HasMember("Flags") ||
				!achievement["Flags"].IsNumber() || !achievement.HasMember("MemAddr") || !achievement["MemAddr"].IsString() ||
				!achievement.HasMember("Title") || !achievement["Title"].IsString())
			{
				continue;
			}

			const u32 id = achievement["ID"].GetUint();
			const AchievementCategory category = static_cast<AchievementCategory>(achievement["Flags"].GetUint());
			const char* memaddr = achievement["MemAddr"].GetString();
			std::string title = achievement["Title"].GetString();
			std::string description = GetOptionalString(achievement, "Description");
			std::string badge_name = GetOptionalString(achievement, "BadgeName");
			const u32 points = GetOptionalUInt(achievement, "Points");

			// Skip local and unofficial achievements for now, unless "Test Unofficial Achievements" is enabled
			if (!s_unofficial_test_mode &&
				(category == AchievementCategory::Local || category == AchievementCategory::Unofficial))
			{
				Console.Warning("Skipping unofficial achievement %u (%s)", id, title.c_str());
				continue;
			}

			if (GetAchievementByID(id))
			{
				Console.Error("Achievement %u already exists", id);
				continue;
			}

			Achievement cheevo;
			cheevo.id = id;
			cheevo.memaddr = memaddr;
			cheevo.title = std::move(title);
			cheevo.description = std::move(description);
			cheevo.locked = true;
			cheevo.active = false;
			cheevo.points = points;
			cheevo.category = category;

			if (!badge_name.empty())
			{
				cheevo.locked_badge_path = ResolveBadgePath(badge_name.c_str(), true);
				cheevo.unlocked_badge_path = ResolveBadgePath(badge_name.c_str(), false);
			}

			s_achievements.push_back(std::move(cheevo));
		}
	}

	// parse leaderboards
	if (patch_data.HasMember("Leaderboards") && patch_data["Leaderboards"].IsArray())
	{
		const auto leaderboards(patch_data["Leaderboards"].GetArray());
		for (const auto& leaderboard : leaderboards)
		{
			if (!leaderboard.HasMember("ID") || !leaderboard["ID"].IsNumber() || !leaderboard.HasMember("Mem") ||
				!leaderboard["Mem"].IsString() || !leaderboard.HasMember("Title") || !leaderboard["Title"].IsString() ||
				!leaderboard.HasMember("Format") || !leaderboard["Format"].IsString())
			{
				continue;
			}

			const unsigned int id = leaderboard["ID"].GetUint();
			const char* title = leaderboard["Title"].GetString();
			const char* memaddr = leaderboard["Mem"].GetString();
			const char* format = leaderboard["Format"].GetString();
			std::string description = GetOptionalString(leaderboard, "Description");

			Leaderboard lboard;
			lboard.id = id;
			lboard.title = title;
			lboard.description = std::move(description);
			lboard.format = rc_parse_format(format);
			s_leaderboards.push_back(std::move(lboard));

			const int err = rc_runtime_activate_lboard(&s_rcheevos_runtime, id, memaddr, nullptr, 0);
			if (err != RC_OK)
			{
				Console.Error("Leaderboard %u memaddr parse error: %s", id, rc_error_str(err));
			}
			else
			{
				DevCon.WriteLn("Activated leaderboard %s (%u)", title, id);
			}
		}
	}

	// parse rich presence
	if (s_rich_presence_enabled && patch_data.HasMember("RichPresencePatch") &&
		patch_data["RichPresencePatch"].IsString())
	{
		const char* patch = patch_data["RichPresencePatch"].GetString();
		int res = rc_runtime_activate_richpresence(&s_rcheevos_runtime, patch, nullptr, 0);
		if (res == RC_OK)
			s_has_rich_presence = true;
		else
			Console.Warning("Failed to activate rich presence: %s", rc_error_str(res));
	}

	Console.WriteLn("Game Title: %s", s_game_title.c_str());
	Console.WriteLn("Game Developer: %s", s_game_developer.c_str());
	Console.WriteLn("Game Publisher: %s", s_game_publisher.c_str());
	Console.WriteLn("Achievements: %zu", s_achievements.size());
	Console.WriteLn("Leaderboards: %zu", s_leaderboards.size());

	if (!s_achievements.empty() || s_has_rich_presence)
	{
		if (!s_test_mode)
		{
			GetUserUnlocks();
		}
		else
		{
			ActivateLockedAchievements();
			DisplayAchievementSummary();
			Host::OnRetroAchievementsRefreshed();
		}
	}
	else
	{
		DisplayAchievementSummary();
	}

	if (s_achievements.empty() && s_leaderboards.empty() && !s_has_rich_presence)
	{
		ClearGameInfo();
	}
}

void Achievements::GetLbInfoCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data)
{
	rapidjson::Document doc;
	if (!ParseResponseJSON("Get Leaderboard Info", status_code, data, doc))
		return;

	if (!doc.HasMember("LeaderboardData") || !doc["LeaderboardData"].IsObject())
	{
		FormattedError("No leaderboard returned from server.");
		return;
	}

	// parse info
	const auto lb_data(doc["LeaderboardData"].GetObject());
	if (!lb_data["LBID"].IsUint())
	{
		FormattedError("Leaderboard data is missing leaderboard ID");
		return;
	}

	const u32 lbid = lb_data["LBID"].GetUint();
	if (lbid != s_last_queried_lboard)
	{
		// User has already requested another leaderboard, drop this data
		return;
	}

	if (lb_data.HasMember("Entries") && lb_data["Entries"].IsArray())
	{
		const Leaderboard* leaderboard = GetLeaderboardByID(lbid);
		if (leaderboard == nullptr)
		{
			Console.Error("Attempting to list unknown leaderboard %u", lbid);
			return;
		}

		std::vector<LeaderboardEntry> entries;

		const auto lb_entries(lb_data["Entries"].GetArray());
		for (const auto& entry : lb_entries)
		{
			if (!entry.HasMember("User") || !entry["User"].IsString() || !entry.HasMember("Score") ||
				!entry["Score"].IsNumber() || !entry.HasMember("Rank") || !entry["Rank"].IsNumber())
			{
				continue;
			}

			char score[128];
			rc_runtime_format_lboard_value(score, sizeof(score), entry["Score"].GetInt(), leaderboard->format);

			LeaderboardEntry lbe;
			lbe.user = entry["User"].GetString();
			lbe.rank = entry["Rank"].GetUint();
			lbe.formatted_score = score;
			lbe.is_self = lbe.user == s_username;

			entries.push_back(std::move(lbe));
		}

		s_lboard_entries = std::move(entries);
	}
}

void Achievements::GetPatches(u32 game_id)
{
	char url[512];
	int res = rc_url_get_patch(url, sizeof(url), s_username.c_str(), s_login_token.c_str(), game_id);
	pxAssertRel(res == 0, "URL generation failed");

	s_http_downloader->CreateRequest(url, GetPatchesCallback);
}

std::string_view Achievements::GetELFNameForHash(const std::string& elf_path)
{
	std::string::size_type start = elf_path.rfind('\\');
	if (start == std::string::npos)
		start = 0;
	else
		start++; // skip \

	std::string::size_type end = elf_path.rfind(';');
	if (end == std::string::npos)
		end = elf_path.size();

	if (end < start)
		end = start;

	return std::string_view(elf_path).substr(start, end - start);
}

std::optional<std::vector<u8>> Achievements::ReadELFFromCurrentDisc(const std::string& elf_path)
{
	// This CDVD stuff is super nasty and full of exceptions..
	std::optional<std::vector<u8>> ret;
	try
	{
		IsoFSCDVD isofs;
		IsoFile file(isofs, elf_path);
		const u32 size = file.getLength();

		ret = std::vector<u8>();
		ret->resize(size);

		if (size > 0)
		{
			const s32 bytes_read = file.read(ret->data(), static_cast<s32>(size));
			if (bytes_read != static_cast<s32>(size))
			{
				Console.Error("(Achievements) Only read %d of %u bytes of ELF '%s'", bytes_read, size, elf_path.c_str());
				ret.reset();
			}
		}
	}
	catch (...)
	{
		Console.Error("(Achievements) Caught exception while trying to read ELF '%s'.", elf_path.c_str());
		ret.reset();
	}

	return ret;
}

std::string Achievements::GetGameHash()
{
	const std::string& elf_path = LastELF;
	if (elf_path.empty())
		return {};

	// this.. really shouldn't be invalid
	const std::string_view name_for_hash(GetELFNameForHash(elf_path));
	if (name_for_hash.empty())
		return {};

	std::optional<std::vector<u8>> elf_data(ReadELFFromCurrentDisc(elf_path));
	if (!elf_data.has_value())
		return {};

	// See rcheevos hash.c - rc_hash_ps2().
	const u32 MAX_HASH_SIZE = 64 * 1024 * 1024;
	const u32 hash_size = std::min<u32>(static_cast<u32>(elf_data->size()), MAX_HASH_SIZE);
	pxAssert(hash_size <= elf_data->size());

	MD5Digest digest;
	if (!name_for_hash.empty())
		digest.Update(name_for_hash.data(), static_cast<u32>(name_for_hash.size()));
	if (hash_size > 0)
		digest.Update(elf_data->data(), hash_size);

	u8 hash[16];
	digest.Final(hash);

	std::string hash_str(StringUtil::StdStringFromFormat(
		"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", hash[0], hash[1], hash[2], hash[3], hash[4],
		hash[5], hash[6], hash[7], hash[8], hash[9], hash[10], hash[11], hash[12], hash[13], hash[14], hash[15]));

	Console.WriteLn("Hash for '%.*s' (%zu bytes, %u bytes hashed): %s", static_cast<int>(name_for_hash.size()), name_for_hash.data(),
		elf_data->size(), hash_size, hash_str.c_str());
	return hash_str;
}

void Achievements::GetGameIdCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data)
{
	rapidjson::Document doc;
	if (!ParseResponseJSON("Get Game ID", status_code, data, doc))
		return;

	const u32 game_id = (doc.HasMember("GameID") && doc["GameID"].IsUint()) ? doc["GameID"].GetUint() : 0;
	Console.WriteLn("Server returned GameID %u", game_id);
	if (game_id == 0)
		return;

	GetPatches(game_id);
}

void Achievements::GameChanged()
{
	pxAssert(VMManager::HasValidVM());

	// avoid reading+hashing the executable if the crc hasn't changed
	const u32 crc = VMManager::GetGameCRC();
	if (s_last_game_crc == crc)
		return;

	std::string game_hash(GetGameHash());
	if (s_game_hash == game_hash)
		return;

	if (!IsUsingRAIntegration())
		s_http_downloader->WaitForAllRequests();

	if (IsUsingFirstDiscFromPlaylist())
	{
		// TODO: This is where we would handle first-disc-from-playlist.
		// ...
	}

	ClearGameInfo();
	ClearGameHash();
	s_last_game_crc = crc;
	s_game_hash = std::move(game_hash);

#ifdef ENABLE_RAINTEGRATION
	if (IsUsingRAIntegration())
	{
		RAIntegration::GameChanged();
		return;
	}
#endif

	if (s_game_hash.empty())
	{
		// when we're booting the bios, this will fail
		if (VMManager::GetGameCRC() != 0)
		{
			Host::AddKeyedOSDMessage("retroachievements_disc_read_failed",
				"Failed to read executable from disc. Achievements disabled.", 10.0f);
		}

		return;
	}

	char url[256];
	int res = rc_url_get_gameid(url, sizeof(url), s_game_hash.c_str());
	pxAssertRel(res == 0, "URL generation failed");

	s_http_downloader->CreateRequest(url, GetGameIdCallback);
}

void Achievements::SendPlayingCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data)
{
	rapidjson::Document doc;
	if (!ParseResponseJSON("Post Activity", status_code, data, doc))
		return;

	Console.WriteLn("Playing game updated to %u (%s)", g_game_id, s_game_title.c_str());
}

void Achievements::SendPlaying()
{
	if (!HasActiveGame())
		return;

	char url[512];
	int res = rc_url_post_playing(url, sizeof(url), s_username.c_str(), s_login_token.c_str(), g_game_id);
	pxAssertRel(res == 0, "URL generation failed");

	s_http_downloader->CreateRequest(url, SendPlayingCallback);
}

void Achievements::UpdateRichPresence()
{
	if (!s_has_rich_presence)
		return;

	char buffer[512];
	int res = rc_runtime_get_richpresence(&s_rcheevos_runtime, buffer, sizeof(buffer), PeekMemory, nullptr, nullptr);
	if (res <= 0)
	{
		const bool had_rich_presence = !s_rich_presence_string.empty();
		s_rich_presence_string.clear();
		if (had_rich_presence)
			Host::OnRetroAchievementsRefreshed();

		return;
	}

	if (s_rich_presence_string == buffer)
		return;

	s_rich_presence_string.assign(buffer);
	Host::OnRetroAchievementsRefreshed();
}

void Achievements::SendPingCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data)
{
	rapidjson::Document doc;
	if (!ParseResponseJSON("Ping", status_code, data, doc))
		return;
}

void Achievements::SendPing()
{
	if (!HasActiveGame())
		return;

	char url[512];
	char post_data[512];
	int res = rc_url_ping(url, sizeof(url), post_data, sizeof(post_data), s_username.c_str(), s_login_token.c_str(),
		g_game_id, s_rich_presence_string.c_str());
	pxAssertRel(res == 0, "URL generation failed");

	s_http_downloader->CreatePostRequest(url, post_data, SendPingCallback);
	s_last_ping_time.Reset();
}

std::string Achievements::GetGameTitle()
{
	return s_game_title;
}

std::string Achievements::GetGameDeveloper()
{
	return s_game_developer;
}

std::string Achievements::GetGamePublisher()
{
	return s_game_publisher;
}

std::string Achievements::GetGameReleaseDate()
{
	return s_game_release_date;
}

std::string Achievements::GetGameIcon()
{
	return s_game_icon;
}

bool Achievements::EnumerateAchievements(std::function<bool(const Achievement&)> callback)
{
	for (const Achievement& cheevo : s_achievements)
	{
		if (!callback(cheevo))
			return false;
	}

	return true;
}

u32 Achievements::GetUnlockedAchiementCount()
{
	u32 count = 0;
	for (const Achievement& cheevo : s_achievements)
	{
		if (!cheevo.locked)
			count++;
	}

	return count;
}

u32 Achievements::GetAchievementCount()
{
	return static_cast<u32>(s_achievements.size());
}

u32 Achievements::GetMaximumPointsForGame()
{
	u32 points = 0;
	for (const Achievement& cheevo : s_achievements)
		points += cheevo.points;

	return points;
}

u32 Achievements::GetCurrentPointsForGame()
{
	u32 points = 0;
	for (const Achievement& cheevo : s_achievements)
	{
		if (!cheevo.locked)
			points += cheevo.points;
	}

	return points;
}

bool Achievements::EnumerateLeaderboards(std::function<bool(const Leaderboard&)> callback)
{
	for (const Leaderboard& lboard : s_leaderboards)
	{
		if (!callback(lboard))
			return false;
	}

	return true;
}

std::optional<bool> Achievements::TryEnumerateLeaderboardEntries(u32 id,
	std::function<bool(const LeaderboardEntry&)> callback)
{
	if (id == s_last_queried_lboard)
	{
		if (s_lboard_entries)
		{
			for (const LeaderboardEntry& entry : *s_lboard_entries)
			{
				if (!callback(entry))
					return false;
			}
			return true;
		}
	}
	else
	{
		s_last_queried_lboard = id;
		s_lboard_entries.reset();

		// TODO: Add paging? For now, stick to defaults
		char url[512];

		// Just over what a single page can store, should be a reasonable amount for now
		rc_url_get_lboard_entries_near_user(url, sizeof(url), id, s_username.c_str(), 15);
		s_http_downloader->CreateRequest(url, GetLbInfoCallback);
	}

	return std::nullopt;
}

const Achievements::Leaderboard* Achievements::GetLeaderboardByID(u32 id)
{
	for (const Leaderboard& lb : s_leaderboards)
	{
		if (lb.id == id)
			return &lb;
	}

	return nullptr;
}

u32 Achievements::GetLeaderboardCount()
{
	return static_cast<u32>(s_leaderboards.size());
}

bool Achievements::IsLeaderboardTimeType(const Leaderboard& leaderboard)
{
	return leaderboard.format != RC_FORMAT_SCORE && leaderboard.format != RC_FORMAT_VALUE;
}

void Achievements::ActivateLockedAchievements()
{
	for (Achievement& cheevo : s_achievements)
	{
		if (cheevo.locked)
			ActivateAchievement(&cheevo);
	}
}

bool Achievements::ActivateAchievement(Achievement* achievement)
{
	if (achievement->active)
		return true;

	const int err =
		rc_runtime_activate_achievement(&s_rcheevos_runtime, achievement->id, achievement->memaddr.c_str(), nullptr, 0);
	if (err != RC_OK)
	{
		Console.Error("Achievement %u memaddr parse error: %s", achievement->id, rc_error_str(err));
		return false;
	}

	achievement->active = true;

	DevCon.WriteLn("Activated achievement %s (%u)", achievement->title.c_str(), achievement->id);
	return true;
}

void Achievements::DeactivateAchievement(Achievement* achievement)
{
	if (!achievement->active)
		return;

	rc_runtime_deactivate_achievement(&s_rcheevos_runtime, achievement->id);
	achievement->active = false;

	DevCon.WriteLn("Deactivated achievement %s (%u)", achievement->title.c_str(), achievement->id);
}

void Achievements::UnlockAchievementCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data)
{
	rapidjson::Document doc;
	if (!ParseResponseJSON("Award Cheevo", status_code, data, doc))
		return;

	// we don't really need to do anything here
}

void Achievements::SubmitLeaderboardCallback(s32 status_code, const Common::HTTPDownloader::Request::Data& data)
{
	// Force the next leaderboard query to repopulate everything, just in case the user wants to see their new score
	s_last_queried_lboard = 0;
}

void Achievements::UnlockAchievement(u32 achievement_id, bool add_notification /* = true*/)
{
	Achievement* achievement = GetAchievementByID(achievement_id);
	if (!achievement)
	{
		Console.Error("Attempting to unlock unknown achievement %u", achievement_id);
		return;
	}
	else if (!achievement->locked)
	{
		Console.Warning("Achievement %u for game %u is already unlocked", achievement_id, g_game_id);
		return;
	}

	achievement->locked = false;
	DeactivateAchievement(achievement);

	Console.WriteLn("Achievement %s (%u) for game %u unlocked", achievement->title.c_str(), achievement_id, g_game_id);

	std::string title;
	switch (achievement->category)
	{
		case AchievementCategory::Local:
			title = fmt::format("{} (Local)", achievement->title);
			break;
		case AchievementCategory::Unofficial:
			title = fmt::format("{} (Unofficial)", achievement->title);
			break;
		case AchievementCategory::Core:
		default:
			title = achievement->title;
			break;
	}

#if 0
	// FIXME
	ImGuiFullscreen::AddNotification(15.0f, std::move(title), achievement->description, achievement->unlocked_badge_path);
#endif

	if (s_test_mode)
	{
		Console.Warning("Skipping sending achievement %u unlock to server because of test mode.", achievement_id);
		return;
	}

	if (achievement->category != AchievementCategory::Core)
	{
		Console.Warning("Skipping sending achievement %u unlock to server because it's not from the core set.",
			achievement_id);
		return;
	}

	char url[512];
	rc_url_award_cheevo(url, sizeof(url), s_username.c_str(), s_login_token.c_str(), achievement_id,
		static_cast<int>(g_challenge_mode), s_game_hash.c_str());
	s_http_downloader->CreateRequest(url, UnlockAchievementCallback);
}

void Achievements::SubmitLeaderboard(u32 leaderboard_id, int value)
{
	if (s_test_mode)
	{
		Console.Warning("Skipping sending leaderboard %u result to server because of test mode.", leaderboard_id);
		return;
	}

	if (!g_challenge_mode)
	{
		Console.Warning("Skipping sending leaderboard %u result to server because Challenge mode is off.",
			leaderboard_id);
		return;
	}

	char url[512];
	rc_url_submit_lboard(url, sizeof(url), s_username.c_str(), s_login_token.c_str(), leaderboard_id, value);
	s_http_downloader->CreateRequest(url, SubmitLeaderboardCallback);
}

std::pair<u32, u32> Achievements::GetAchievementProgress(const Achievement& achievement)
{
	std::pair<u32, u32> result;
	rc_runtime_get_achievement_measured(&s_rcheevos_runtime, achievement.id, &result.first, &result.second);
	return result;
}

std::string Achievements::GetAchievementProgressText(const Achievement& achievement)
{
	char buf[256];
	rc_runtime_format_achievement_measured(&s_rcheevos_runtime, achievement.id, buf, std::size(buf));
	return buf;
}

void Achievements::CheevosEventHandler(const rc_runtime_event_t* runtime_event)
{
	static const char* events[] = {"RC_RUNTIME_EVENT_ACHIEVEMENT_ACTIVATED", "RC_RUNTIME_EVENT_ACHIEVEMENT_PAUSED",
		"RC_RUNTIME_EVENT_ACHIEVEMENT_RESET", "RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED",
		"RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED", "RC_RUNTIME_EVENT_LBOARD_STARTED",
		"RC_RUNTIME_EVENT_LBOARD_CANCELED", "RC_RUNTIME_EVENT_LBOARD_UPDATED",
		"RC_RUNTIME_EVENT_LBOARD_TRIGGERED", "RC_RUNTIME_EVENT_ACHIEVEMENT_DISABLED",
		"RC_RUNTIME_EVENT_LBOARD_DISABLED"};
	const char* event_text =
		((unsigned)runtime_event->type >= std::size(events)) ? "unknown" : events[(unsigned)runtime_event->type];
	DevCon.WriteLn("Cheevos Event %s for %u", event_text, runtime_event->id);

	if (runtime_event->type == RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED)
		UnlockAchievement(runtime_event->id);
	else if (runtime_event->type == RC_RUNTIME_EVENT_LBOARD_TRIGGERED)
		SubmitLeaderboard(runtime_event->id, runtime_event->value);
}

unsigned Achievements::PeekMemory(unsigned address, unsigned num_bytes, void* ud)
{
	if (!VMManager::HasValidVM() || (address + num_bytes) >= EXPOSED_EE_MEMORY_SIZE)
		return 0u;

	switch (num_bytes)
	{
		case 1:
		{
			u8 value;
			std::memcpy(&value, reinterpret_cast<u8*>(eeMem) + address, sizeof(value));
			return static_cast<unsigned>(value);
		}

		case 2:
		{
			u16 value;
			std::memcpy(&value, reinterpret_cast<u8*>(eeMem) + address, sizeof(value));
			return static_cast<unsigned>(value);
		}

		case 4:
		{
			u32 value;
			std::memcpy(&value, reinterpret_cast<u8*>(eeMem) + address, sizeof(value));
			return static_cast<unsigned>(value);
		}

		default:
			return 0u;
	}
}

void Achievements::PokeMemory(unsigned address, unsigned num_bytes, void* ud, unsigned value)
{
	if (!VMManager::HasValidVM() || (address + num_bytes) >= EXPOSED_EE_MEMORY_SIZE)
		return;

	switch (num_bytes)
	{
		case 1:
		{
			const u8 value8 = static_cast<u8>(value);
			std::memcpy(reinterpret_cast<u8*>(eeMem) + address, &value8, sizeof(value8));
		}
		break;

		case 2:
		{
			const u16 value16 = static_cast<u16>(value);
			std::memcpy(reinterpret_cast<u8*>(eeMem) + address, &value16, sizeof(value16));
		}
		break;

		case 4:
		{
			const u32 value32 = static_cast<u32>(value);
			std::memcpy(reinterpret_cast<u8*>(eeMem) + address, &value32, sizeof(value32));
		}
		break;

		default:
			break;
	}
}

#ifdef ENABLE_RAINTEGRATION

#include "RA_Consoles.h"

namespace Achievements::RAIntegration
{
	static void InitializeRAIntegration(void* main_window_handle);

	static int RACallbackIsActive();
	static void RACallbackCauseUnpause();
	static void RACallbackCausePause();
	static void RACallbackRebuildMenu();
	static void RACallbackEstimateTitle(char* buf);
	static void RACallbackResetEmulator();
	static void RACallbackLoadROM(const char* unused);
	static unsigned char RACallbackReadMemory(unsigned int address);
	static void RACallbackWriteMemory(unsigned int address, unsigned char value);

	static bool s_raintegration_initialized = false;
} // namespace Achievements::RAIntegration

void Achievements::SwitchToRAIntegration()
{
	g_using_raintegration = true;
	g_active = true;

	// Not strictly the case, but just in case we gate anything by IsLoggedIn().
	g_logged_in = true;
}

void Achievements::RAIntegration::InitializeRAIntegration(void* main_window_handle)
{
	RA_InitClient((HWND)main_window_handle, "PCSX2", GIT_TAG);
	RA_SetUserAgentDetail(Achievements::GetUserAgent().c_str());

	RA_InstallSharedFunctions(RACallbackIsActive, RACallbackCauseUnpause, RACallbackCausePause, RACallbackRebuildMenu,
		RACallbackEstimateTitle, RACallbackResetEmulator, RACallbackLoadROM);
	RA_SetConsoleID(PlayStation2);

	// EE physical memory and scratchpad are currently exposed (matching direct rcheevos implementation).
	RA_InstallMemoryBank(0, RACallbackReadMemory, RACallbackWriteMemory, EXPOSED_EE_MEMORY_SIZE);

	// Fire off a login anyway. Saves going into the menu and doing it.
	RA_AttemptLogin(0);

	g_challenge_mode = RA_HardcoreModeIsActive() != 0;
	s_raintegration_initialized = true;

	// this is pretty lame, but we may as well persist until we exit anyway
	std::atexit(RA_Shutdown);
}

void Achievements::RAIntegration::MainWindowChanged(void* new_handle)
{
	if (s_raintegration_initialized)
	{
		RA_UpdateHWnd((HWND)new_handle);
		return;
	}

	InitializeRAIntegration(new_handle);
}

void Achievements::RAIntegration::GameChanged()
{
	g_game_id = s_game_hash.empty() ? 0 : RA_IdentifyHash(s_game_hash.c_str());
	RA_ActivateGame(g_game_id);
}

std::vector<std::pair<int, const char*>> Achievements::RAIntegration::GetMenuItems()
{
	// NOTE: I *really* don't like doing this. But sadly it's the only way we can integrate with Qt.
	static constexpr int IDM_RA_RETROACHIEVEMENTS = 1700;
	static constexpr int IDM_RA_OVERLAYSETTINGS = 1701;
	static constexpr int IDM_RA_FILES_MEMORYBOOKMARKS = 1703;
	static constexpr int IDM_RA_FILES_ACHIEVEMENTS = 1704;
	static constexpr int IDM_RA_FILES_MEMORYFINDER = 1705;
	static constexpr int IDM_RA_FILES_LOGIN = 1706;
	static constexpr int IDM_RA_FILES_LOGOUT = 1707;
	static constexpr int IDM_RA_FILES_ACHIEVEMENTEDITOR = 1708;
	static constexpr int IDM_RA_HARDCORE_MODE = 1710;
	static constexpr int IDM_RA_REPORTBROKENACHIEVEMENTS = 1711;
	static constexpr int IDM_RA_GETROMCHECKSUM = 1712;
	static constexpr int IDM_RA_OPENUSERPAGE = 1713;
	static constexpr int IDM_RA_OPENGAMEPAGE = 1714;
	static constexpr int IDM_RA_PARSERICHPRESENCE = 1716;
	static constexpr int IDM_RA_TOGGLELEADERBOARDS = 1717;
	static constexpr int IDM_RA_NON_HARDCORE_WARNING = 1718;

	std::vector<std::pair<int, const char*>> ret;

	const char* username = RA_UserName();
	if (!username || std::strlen(username) == 0)
	{
		ret.emplace_back(IDM_RA_FILES_LOGIN, "&Login");
	}
	else
	{
		ret.emplace_back(IDM_RA_FILES_LOGOUT, "Log&out");
		ret.emplace_back(0, nullptr);
		ret.emplace_back(IDM_RA_OPENUSERPAGE, "Open my &User Page");
		ret.emplace_back(IDM_RA_OPENGAMEPAGE, "Open this &Game's Page");
		ret.emplace_back(0, nullptr);
		ret.emplace_back(IDM_RA_HARDCORE_MODE, "&Hardcore Mode");
		ret.emplace_back(IDM_RA_NON_HARDCORE_WARNING, "Non-Hardcore &Warning");
		ret.emplace_back(0, nullptr);
		ret.emplace_back(IDM_RA_TOGGLELEADERBOARDS, "Enable &Leaderboards");
		ret.emplace_back(IDM_RA_OVERLAYSETTINGS, "O&verlay Settings");
		ret.emplace_back(0, nullptr);
		ret.emplace_back(IDM_RA_FILES_ACHIEVEMENTS, "Assets Li&st");
		ret.emplace_back(IDM_RA_FILES_ACHIEVEMENTEDITOR, "Assets &Editor");
		ret.emplace_back(IDM_RA_FILES_MEMORYFINDER, "&Memory Inspector");
		ret.emplace_back(IDM_RA_FILES_MEMORYBOOKMARKS, "Memory &Bookmarks");
		ret.emplace_back(IDM_RA_PARSERICHPRESENCE, "Rich &Presence Monitor");
		ret.emplace_back(0, nullptr);
		ret.emplace_back(IDM_RA_REPORTBROKENACHIEVEMENTS, "&Report Achievement Problem");
		ret.emplace_back(IDM_RA_GETROMCHECKSUM, "View Game H&ash");
	}

	return ret;
}

void Achievements::RAIntegration::ActivateMenuItem(int item)
{
	RA_InvokeDialog(item);
}

int Achievements::RAIntegration::RACallbackIsActive()
{
	return static_cast<int>(HasActiveGame());
}

void Achievements::RAIntegration::RACallbackCauseUnpause()
{
	if (VMManager::HasValidVM())
		VMManager::SetState(VMState::Running);
}

void Achievements::RAIntegration::RACallbackCausePause()
{
	if (VMManager::HasValidVM())
		VMManager::SetState(VMState::Paused);
}

void Achievements::RAIntegration::RACallbackRebuildMenu()
{
	// unused, we build the menu on demand
}

void Achievements::RAIntegration::RACallbackEstimateTitle(char* buf)
{
	std::string title(fmt::format("{0} ({1}) [{2:08X}]", VMManager::GetGameName(), VMManager::GetGameSerial(), VMManager::GetGameCRC()));
	StringUtil::Strlcpy(buf, title, 256);
}

void Achievements::RAIntegration::RACallbackResetEmulator()
{
	g_challenge_mode = RA_HardcoreModeIsActive() != 0;
	if (VMManager::HasValidVM())
		VMManager::Reset();
}

void Achievements::RAIntegration::RACallbackLoadROM(const char* unused)
{
	// unused
	UNREFERENCED_PARAMETER(unused);
}

unsigned char Achievements::RAIntegration::RACallbackReadMemory(unsigned int address)
{
	return static_cast<unsigned char>(PeekMemory(address, sizeof(unsigned char), nullptr));
}

void Achievements::RAIntegration::RACallbackWriteMemory(unsigned int address, unsigned char value)
{
	PokeMemory(address, sizeof(value), nullptr, static_cast<unsigned>(value));
}

#endif
