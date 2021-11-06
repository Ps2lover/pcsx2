/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2020  PCSX2 Dev Team
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

#include "GameDatabase.h"
#include "Config.h"
#include "Host.h"

#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/StringUtil.h"
#include "common/Timer.h"

#include <sstream>
#include "ryml_std.hpp"
#include "ryml.hpp"
#include "fmt/core.h"
#include "fmt/ranges.h"
#include <fstream>
#include <mutex>

#include "svnrev.h"

static constexpr char GAMEDB_YAML_FILE_NAME[] = "GameIndex.yaml";

static constexpr char GAMEDB_CACHE_FILE_NAME[] = "gamedb.cache";
static constexpr u64 CACHE_FILE_MAGIC = UINT64_C(0x47414D4544423032); // GAMEDB03

static std::unordered_map<std::string, GameDatabaseSchema::GameEntry> s_game_db;
static std::once_flag s_load_once_flag;

std::string GameDatabaseSchema::GameEntry::memcardFiltersAsString() const
{
	return fmt::to_string(fmt::join(memcardFilters, "/"));
}

const GameDatabaseSchema::Patch* GameDatabaseSchema::GameEntry::findPatch(const std::string_view& crc) const
{
	std::string crcLower = StringUtil::toLower(crc);
	Console.WriteLn(fmt::format("[GameDB] Searching for patch with CRC '{}'", crc));

	auto it = patches.find(crcLower);
	if (it != patches.end())
	{
		Console.WriteLn(fmt::format("[GameDB] Found patch with CRC '{}'", crc));
		return &patches.at(crcLower);
	}

	it = patches.find("default");
	if (it != patches.end())
	{
		Console.WriteLn("[GameDB] Found and falling back to default patch");
		return &patches.at("default");
	}
	Console.WriteLn("[GameDB] No CRC-specific patch or default patch found");
	return nullptr;
}

const char* GameDatabaseSchema::GameEntry::compatAsString() const
{
	switch (compat)
	{
		case GameDatabaseSchema::Compatibility::Perfect:
			return "Perfect";
		case GameDatabaseSchema::Compatibility::Playable:
			return "Playable";
		case GameDatabaseSchema::Compatibility::InGame:
			return "In-Game";
		case GameDatabaseSchema::Compatibility::Menu:
			return "Menu";
		case GameDatabaseSchema::Compatibility::Intro:
			return "Intro";
		case GameDatabaseSchema::Compatibility::Nothing:
			return "Nothing";
		default:
			return "Unknown";
	}
}

void parseAndInsert(const std::string_view& serial, const c4::yml::NodeRef& node)
{
	GameDatabaseSchema::GameEntry gameEntry;
	if (node.has_child("name"))
	{
		node["name"] >> gameEntry.name;
	}
	if (node.has_child("region"))
	{
		node["region"] >> gameEntry.region;
	}
	if (node.has_child("compat"))
	{
		int val = 0;
		node["compat"] >> val;
		gameEntry.compat = static_cast<GameDatabaseSchema::Compatibility>(val);
	}
	if (node.has_child("roundModes"))
	{
		if (node["roundModes"].has_child("eeRoundMode"))
		{
			int eeVal = -1;
			node["roundModes"]["eeRoundMode"] >> eeVal;
			gameEntry.eeRoundMode = static_cast<GameDatabaseSchema::RoundMode>(eeVal);
		}
		if (node["roundModes"].has_child("vuRoundMode"))
		{
			int vuVal = -1;
			node["roundModes"]["vuRoundMode"] >> vuVal;
			gameEntry.vuRoundMode = static_cast<GameDatabaseSchema::RoundMode>(vuVal);
		}
	}
	if (node.has_child("clampModes"))
	{
		if (node["clampModes"].has_child("eeClampMode"))
		{
			int eeVal = -1;
			node["clampModes"]["eeClampMode"] >> eeVal;
			gameEntry.eeClampMode = static_cast<GameDatabaseSchema::ClampMode>(eeVal);
		}
		if (node["clampModes"].has_child("vuClampMode"))
		{
			int vuVal = -1;
			node["clampModes"]["vuClampMode"] >> vuVal;
			gameEntry.vuClampMode = static_cast<GameDatabaseSchema::ClampMode>(vuVal);
		}
	}

	// Validate game fixes, invalid ones will be dropped!
	if (node.has_child("gameFixes") && node["gameFixes"].has_children())
	{
		for (const ryml::NodeRef& n : node["gameFixes"].children())
		{
			bool fixValidated = false;
			auto fix = std::string(n.val().str, n.val().len);

			// Enum values don't end with Hack, but gamedb does, so remove it before comparing.
			if (StringUtil::EndsWith(fix, "Hack"))
			{
				fix.erase(fix.size() - 4);
				for (GamefixId id = GamefixId_FIRST; id < pxEnumEnd; id++)
				{
					if (fix.compare(EnumToString(id)) == 0 &&
						std::find(gameEntry.gameFixes.begin(), gameEntry.gameFixes.end(), id) == gameEntry.gameFixes.end())
					{
						gameEntry.gameFixes.push_back(id);
						fixValidated = true;
						break;
					}
				}
			}

			if (!fixValidated)
			{
				Console.Error(fmt::format("[GameDB] Invalid gamefix: '{}', specified for serial: '{}'. Dropping!", fix, serial));
			}
		}
	}

	// Validate speed hacks, invalid ones will be dropped!
	if (node.has_child("speedHacks") && node["speedHacks"].has_children())
	{
		for (const ryml::NodeRef& n : node["speedHacks"].children())
		{
			bool speedHackValidated = false;
			auto speedHack = std::string(n.key().str, n.key().len);

			// Same deal with SpeedHacks
			if (StringUtil::EndsWith(speedHack, "SpeedHack"))
			{
				speedHack.erase(speedHack.size() - 9);
				for (SpeedhackId id = SpeedhackId_FIRST; id < pxEnumEnd; id++)
				{
					if (speedHack.compare(EnumToString(id)) == 0 &&
						std::none_of(gameEntry.speedHacks.begin(), gameEntry.speedHacks.end(), [id](const auto& it) { return it.first == id; }))
					{
						gameEntry.speedHacks.emplace_back(id, std::atoi(n.val().str));
						speedHackValidated = true;
						break;
					}
				}
			}

			if (!speedHackValidated)
			{
				Console.Error(fmt::format("[GameDB] Invalid speedhack: '{}', specified for serial: '{}'. Dropping!", speedHack.c_str(), serial));
			}
		}
	}

	// Memory Card Filters - Store as a vector to allow flexibility in the future
	// - currently they are used as a '\n' delimited string in the app
	if (node.has_child("memcardFilters") && node["memcardFilters"].has_children())
	{
		for (const ryml::NodeRef& n : node["memcardFilters"].children())
		{
			auto memcardFilter = std::string(n.val().str, n.val().len);
			gameEntry.memcardFilters.emplace_back(std::move(memcardFilter));
		}
	}

	// Game Patches
	if (node.has_child("patches") && node["patches"].has_children())
	{
		for (const ryml::NodeRef& n : node["patches"].children())
		{
			auto crc = StringUtil::toLower(std::string(n.key().str, n.key().len));
			if (gameEntry.patches.count(crc) == 1)
			{
				Console.Error(fmt::format("[GameDB] Duplicate CRC '{}' found for serial: '{}'. Skipping, CRCs are case-insensitive!", crc, serial));
				continue;
			}
			GameDatabaseSchema::Patch patch;
			if (n.has_child("content"))
			{
				std::string patchLines;
				n["content"] >> patchLines;
				patch = StringUtil::splitOnNewLine(patchLines);
			}
			gameEntry.patches[crc] = patch;
		}
	}

	s_game_db.emplace(std::move(serial), std::move(gameEntry));
}

static std::ifstream getFileStream(std::string path)
{
#ifdef _WIN32
	return std::ifstream(StringUtil::UTF8StringToWideString(path));
#else
	return std::ifstream(path.c_str());
#endif
}

static bool initDatabase()
{
	ryml::Callbacks rymlCallbacks = ryml::get_callbacks();
	rymlCallbacks.m_error = [](const char* msg, size_t msg_len, ryml::Location loc, void*) {
		throw std::runtime_error(fmt::format("[YAML] Parsing error at {}:{} (bufpos={}): {}",
			loc.line, loc.col, loc.offset, msg));
	};
	ryml::set_callbacks(rymlCallbacks);
	c4::set_error_callback([](const char* msg, size_t msg_size) {
		throw std::runtime_error(fmt::format("[YAML] Internal Parsing error: {}",
			msg));
	});
	try
	{
		std::optional<std::vector<u8>> buf(Host::ReadResourceFile(GAMEDB_YAML_FILE_NAME));
		if (!buf.has_value())
		{
			Console.Error("[GameDB] Unable to open GameDB file, file does not exist.");
			return false;
		}

		const ryml::substr view = c4::basic_substring<char>(reinterpret_cast<char*>(buf->data()), buf->size());
		ryml::Tree tree = ryml::parse(view);
		ryml::NodeRef root = tree.rootref();

		for (const ryml::NodeRef& n : root.children())
		{
			auto serial = StringUtil::toLower(std::string(n.key().str, n.key().len));

			// Serials and CRCs must be inserted as lower-case, as that is how they are retrieved
			// this is because the application may pass a lowercase CRC or serial along
			//
			// However, YAML's keys are as expected case-sensitive, so we have to explicitly do our own duplicate checking
			if (s_game_db.count(serial) == 1)
			{
				Console.Error(fmt::format("[GameDB] Duplicate serial '{}' found in GameDB. Skipping, Serials are case-insensitive!", serial));
				continue;
			}
			if (n.is_map())
			{
				parseAndInsert(serial, n);
			}
		}

		ryml::reset_callbacks();
		return true;
	}
	catch (const std::exception& e)
	{
		Console.Error(fmt::format("[GameDB] Error occured when initializing GameDB: {}", e.what()));
		ryml::reset_callbacks();
		return false;
	}
}

static bool ReadString(std::FILE* stream, std::string* dest)
{
	u32 size;
	if (std::fread(&size, sizeof(size), 1, stream) != 1)
		return false;

	dest->resize(size);
	if (size > 0 && std::fread(dest->data(), size, 1, stream) != 1)
		return false;

	return true;
}

static bool ReadS8(std::FILE* stream, s8* dest)
{
	return std::fread(dest, sizeof(s8), 1, stream) > 0;
}

static bool ReadU8(std::FILE* stream, u8* dest)
{
	return std::fread(dest, sizeof(u8), 1, stream) > 0;
}

static bool ReadS32(std::FILE* stream, s32* dest)
{
	return std::fread(dest, sizeof(s32), 1, stream) > 0;
}

static bool ReadU32(std::FILE* stream, u32* dest)
{
	return std::fread(dest, sizeof(u32), 1, stream) > 0;
}

static bool ReadS64(std::FILE* stream, s64* dest)
{
	return std::fread(dest, sizeof(s64), 1, stream) > 0;
}

static bool ReadU64(std::FILE* stream, u64* dest)
{
	return std::fread(dest, sizeof(u64), 1, stream) > 0;
}

static bool WriteString(std::FILE* stream, const std::string& str)
{
	const u32 size = static_cast<u32>(str.size());
	return (std::fwrite(&size, sizeof(size), 1, stream) > 0 &&
			(size == 0 || std::fwrite(str.data(), size, 1, stream) > 0));
}

static bool WriteS8(std::FILE* stream, s8 dest)
{
	return std::fwrite(&dest, sizeof(s8), 1, stream) > 0;
}

static bool WriteU8(std::FILE* stream, u8 dest)
{
	return std::fwrite(&dest, sizeof(u8), 1, stream) > 0;
}

static bool WriteS32(std::FILE* stream, s32 dest)
{
	return std::fwrite(&dest, sizeof(s32), 1, stream) > 0;
}

static bool WriteU32(std::FILE* stream, u32 dest)
{
	return std::fwrite(&dest, sizeof(u32), 1, stream) > 0;
}

static bool WriteS64(std::FILE* stream, s64 dest)
{
	return std::fwrite(&dest, sizeof(s64), 1, stream) > 0;
}

static bool WriteU64(std::FILE* stream, u64 dest)
{
	return std::fwrite(&dest, sizeof(u64), 1, stream) > 0;
}

static s64 GetExpectedMTime()
{
	const std::string yaml_filename(Path::CombineStdString(EmuFolders::Resources, GAMEDB_YAML_FILE_NAME));

	FILESYSTEM_STAT_DATA yaml_sd;
	if (!FileSystem::StatFile(yaml_filename.c_str(), &yaml_sd))
		return -1;

	return yaml_sd.ModificationTime;
}

static bool CheckAndLoad(const char* cached_filename, s64 expected_mtime)
{
	auto fp = FileSystem::OpenManagedCFile(cached_filename, "rb");
	if (!fp)
		return false;

	u64 file_signature;
	s64 file_mtime, start_pos, file_size;
	std::string file_version;
	if (!ReadU64(fp.get(), &file_signature) || file_signature != CACHE_FILE_MAGIC ||
		!ReadS64(fp.get(), &file_mtime) || file_mtime != expected_mtime ||
		!ReadString(fp.get(), &file_version) || file_version != GIT_REV ||
		(start_pos = FileSystem::FTell64(fp.get())) < 0 || FileSystem::FSeek64(fp.get(), 0, SEEK_END) != 0 ||
		(file_size = FileSystem::FTell64(fp.get())) < 0 || FileSystem::FSeek64(fp.get(), start_pos, SEEK_SET) != 0)
	{
		return false;
	}

	while (FileSystem::FTell64(fp.get()) != file_size)
	{
		std::string serial;
		GameDatabaseSchema::GameEntry entry;
		u8 compat;
		s8 ee_round, ee_clamp, vu_round, vu_clamp;
		u32 game_fix_count, speed_hack_count, memcard_filter_count, patch_count;

		if (!ReadString(fp.get(), &serial) ||
			!ReadString(fp.get(), &entry.name) ||
			!ReadString(fp.get(), &entry.region) ||
			!ReadU8(fp.get(), &compat) || compat > static_cast<u8>(GameDatabaseSchema::Compatibility::Perfect) ||
			!ReadS8(fp.get(), &ee_round) || ee_round < static_cast<s8>(GameDatabaseSchema::RoundMode::Undefined) || ee_round > static_cast<s8>(GameDatabaseSchema::RoundMode::ChopZero) ||
			!ReadS8(fp.get(), &ee_clamp) || ee_clamp < static_cast<s8>(GameDatabaseSchema::ClampMode::Undefined) || ee_clamp > static_cast<s8>(GameDatabaseSchema::ClampMode::Full) ||
			!ReadS8(fp.get(), &vu_round) || vu_round < static_cast<s8>(GameDatabaseSchema::RoundMode::Undefined) || vu_round > static_cast<s8>(GameDatabaseSchema::RoundMode::ChopZero) ||
			!ReadS8(fp.get(), &vu_clamp) || vu_clamp < static_cast<s8>(GameDatabaseSchema::ClampMode::Undefined) || vu_clamp > static_cast<s8>(GameDatabaseSchema::ClampMode::Full) ||
			!ReadU32(fp.get(), &game_fix_count) ||
			!ReadU32(fp.get(), &speed_hack_count) ||
			!ReadU32(fp.get(), &memcard_filter_count) ||
			!ReadU32(fp.get(), &patch_count))
		{
			Console.Error("GameDB: Read error while loading entry");
			return false;
		}

		entry.compat = static_cast<GameDatabaseSchema::Compatibility>(compat);
		entry.eeRoundMode = static_cast<GameDatabaseSchema::RoundMode>(ee_round);
		entry.eeClampMode = static_cast<GameDatabaseSchema::ClampMode>(ee_clamp);
		entry.vuRoundMode = static_cast<GameDatabaseSchema::RoundMode>(vu_round);
		entry.vuClampMode = static_cast<GameDatabaseSchema::ClampMode>(vu_clamp);

		entry.gameFixes.reserve(game_fix_count);
		for (u32 i = 0; i < game_fix_count; i++)
		{
			u32 game_fix_id;
			if (!ReadU32(fp.get(), &game_fix_id) || game_fix_id >= static_cast<u32>(GamefixId_COUNT))
				return false;

			entry.gameFixes.push_back(static_cast<GamefixId>(game_fix_id));
		}

		for (u32 i = 0; i < speed_hack_count; i++)
		{
			u32 speed_hack_id;
			s32 speed_hack_value;
			if (!ReadU32(fp.get(), &speed_hack_id) || !ReadS32(fp.get(), &speed_hack_value) || speed_hack_id >= static_cast<u32>(SpeedhackId_COUNT))
				return false;
			entry.speedHacks.emplace_back(static_cast<SpeedhackId>(speed_hack_id), speed_hack_value);
		}

		entry.memcardFilters.resize(memcard_filter_count);
		for (u32 i = 0; i < memcard_filter_count; i++)
		{
			if (!ReadString(fp.get(), &entry.memcardFilters[i]))
				return false;
		}

		for (u32 i = 0; i < patch_count; i++)
		{
			std::string patch_crc;
			u32 patch_line_count;
			if (!ReadString(fp.get(), &patch_crc) || !ReadU32(fp.get(), &patch_line_count))
				return false;

			GameDatabaseSchema::Patch patch_lines;
			patch_lines.resize(patch_line_count);
			for (u32 j = 0; j < patch_line_count; j++)
			{
				if (!ReadString(fp.get(), &patch_lines[j]))
					return false;
			}

			entry.patches.emplace(std::move(patch_crc), std::move(patch_lines));
		}

		s_game_db.emplace(std::move(serial), std::move(entry));
	}

	return true;
}

static bool SaveCache(const char* cached_filename, s64 mtime)
{
	auto fp = FileSystem::OpenManagedCFile(cached_filename, "wb");
	if (!fp)
		return false;

	if (!WriteU64(fp.get(), CACHE_FILE_MAGIC) || !WriteS64(fp.get(), mtime) || !WriteString(fp.get(), GIT_REV))
		return false;

	for (const auto& it : s_game_db)
	{
		const GameDatabaseSchema::GameEntry& entry = it.second;
		const u8 compat = static_cast<u8>(entry.compat);
		const s8 ee_round = static_cast<s8>(entry.eeRoundMode);
		const s8 ee_clamp = static_cast<s8>(entry.eeClampMode);
		const s8 vu_round = static_cast<s8>(entry.vuRoundMode);
		const s8 vu_clamp = static_cast<s8>(entry.vuClampMode);

		if (!WriteString(fp.get(), it.first) ||
			!WriteString(fp.get(), entry.name) ||
			!WriteString(fp.get(), entry.region) ||
			!WriteU8(fp.get(), compat) ||
			!WriteS8(fp.get(), ee_round) ||
			!WriteS8(fp.get(), ee_clamp) ||
			!WriteS8(fp.get(), vu_round) ||
			!WriteS8(fp.get(), vu_clamp) ||
			!WriteU32(fp.get(), static_cast<u32>(entry.gameFixes.size())) ||
			!WriteU32(fp.get(), static_cast<u32>(entry.speedHacks.size())) ||
			!WriteU32(fp.get(), static_cast<u32>(entry.memcardFilters.size())) ||
			!WriteU32(fp.get(), static_cast<u32>(entry.patches.size())))
		{
			return false;
		}

		for (const GamefixId it : entry.gameFixes)
		{
			if (!WriteU32(fp.get(), static_cast<u32>(it)))
				return false;
		}

		for (const auto& it : entry.speedHacks)
		{
			if (!WriteU32(fp.get(), static_cast<u32>(it.first)) || !WriteS32(fp.get(), it.second))
				return false;
		}

		for (const std::string& it : entry.memcardFilters)
		{
			if (!WriteString(fp.get(), it))
				return false;
		}

		for (const auto& it : entry.patches)
		{
			if (!WriteString(fp.get(), it.first) || !WriteU32(fp.get(), static_cast<u32>(it.second.size())))
				return false;

			for (const std::string& jt : it.second)
			{
				if (!WriteString(fp.get(), jt))
					return false;
			}
		}
	}

	return std::fflush(fp.get()) == 0;
}

static void Load()
{
	const std::string cache_filename(Path::CombineStdString(EmuFolders::Cache, GAMEDB_CACHE_FILE_NAME));
	const s64 expected_mtime = GetExpectedMTime();

	Common::Timer timer;

	if (!FileSystem::FileExists(cache_filename.c_str()) || !CheckAndLoad(cache_filename.c_str(), expected_mtime))
	{
		Console.Warning("GameDB cache file does not exist or failed validation, recreating");
		s_game_db.clear();

		if (!initDatabase())
		{
			Console.Error("GameDB: Failed to load YAML file");
			return;
		}

		if (!SaveCache(cache_filename.c_str(), expected_mtime))
			Console.Error("GameDB: Failed to save new cache");
	}

	Console.WriteLn("[GameDB] %zu games on record (loaded in %.2fms)", s_game_db.size(), timer.GetTimeMilliseconds());
}

void GameDatabase::ensureLoaded()
{
	std::call_once(s_load_once_flag, []() {
		Load();
	});
}

const GameDatabaseSchema::GameEntry* GameDatabase::findGame(const std::string_view& serial)
{
	GameDatabase::ensureLoaded();

	std::string serialLower = StringUtil::toLower(serial);
	Console.WriteLn(fmt::format("[GameDB] Searching for '{}' in GameDB", serialLower));
	const auto gameEntry = s_game_db.find(serialLower);
	if (gameEntry != s_game_db.end())
	{
		Console.WriteLn(fmt::format("[GameDB] Found '{}' in GameDB", serialLower));
		return &gameEntry->second;
	}

	Console.Error(fmt::format("[GameDB] Could not find '{}' in GameDB", serialLower));
	return nullptr;
}
