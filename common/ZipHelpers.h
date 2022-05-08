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
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "folly/memory/UninitializedMemoryHacks.h"
#include "zip.h"

#include "Assertions.h"
#include "Console.h"

static inline std::unique_ptr<zip_t, void (*)(zip_t*)> zip_open_managed(const char* filename, int flags, zip_error_t* ze)
{
	zip_source_t* zs = zip_source_file_create(filename, 0, 0, ze);
	zip_t* zip = nullptr;
	if (zs && !(zip = zip_open_from_source(zs, flags, ze)))
	{
		// have to clean up source
		zip_source_free(zs);
	}

	return std::unique_ptr<zip_t, void (*)(zip_t*)>(zip, [](zip_t* zf) {
		if (!zf)
			return;

		int err = zip_close(zf);
		if (err != 0)
		{
			Console.Error("Failed to close zip file: %d", err);
			zip_discard(zf);
		}
	});
}

static inline std::unique_ptr<zip_t, void (*)(zip_t*)> zip_open_buffer_managed(const void* buffer, size_t size, int flags, int freep, zip_error_t* ze)
{
	zip_source_t* zs = zip_source_buffer_create(buffer, size, freep, ze);
	zip_t* zip = nullptr;
	if (zs && !(zip = zip_open_from_source(zs, flags, ze)))
	{
		// have to clean up source
		zip_source_free(zs);
	}

	return std::unique_ptr<zip_t, void (*)(zip_t*)>(zip, [](zip_t* zf) {
		if (!zf)
			return;

		int err = zip_close(zf);
		if (err != 0)
		{
			Console.Error("Failed to close zip file: %d", err);
			zip_discard(zf);
		}
	});
}

static inline std::unique_ptr<zip_file_t, int (*)(zip_file_t*)> zip_fopen_managed(zip_t* zip, const char* filename, zip_flags_t flags)
{
	return std::unique_ptr<zip_file_t, int (*)(zip_file_t*)>(zip_fopen(zip, filename, flags), zip_fclose);
}

static inline std::unique_ptr<zip_file_t, int (*)(zip_file_t*)> zip_fopen_index_managed(zip_t* zip, zip_uint64_t index, zip_flags_t flags)
{
	return std::unique_ptr<zip_file_t, int (*)(zip_file_t*)>(zip_fopen_index(zip, index, flags), zip_fclose);
}

template<typename T>
static inline std::optional<T> ReadFileInZipToContainer(zip_t* zip, const char* name)
{
	std::optional<T> ret;
	const zip_int64_t file_index = zip_name_locate(zip, name, 0);
	if (file_index >= 0)
	{
		zip_stat_t zst;
		if (zip_stat_index(zip, file_index, 0, &zst) == 0)
		{
			zip_file_t* zf = zip_fopen_index(zip, file_index, 0);
			if (zf)
			{
				ret = T();
				ret->resize(static_cast<size_t>(zst.size));
				if (zip_fread(zf, ret->data(), ret->size()) != static_cast<zip_int64_t>(ret->size()))
				{
					ret.reset();
				}
			}
		}
	}

	return ret;
}

static inline std::optional<std::string> ReadFileInZipToString(zip_t* zip, const char* name)
{
	return ReadFileInZipToContainer<std::string>(zip, name);
}

static inline std::optional<std::vector<u8>> ReadBinaryFileInZip(zip_t* zip, const char* name)
{
	return ReadFileInZipToContainer<std::vector<u8>>(zip, name);
}

class ZipSourceVector
{
public:
	ZipSourceVector()
	{
		Init();
	}

	ZipSourceVector(std::vector<u8> data)
		: m_data(std::move(data))
	{
		Init();
	}
	~ZipSourceVector()
	{
		if (m_source)
			zip_source_free(m_source);
	}

	const std::vector<u8>& GetBuffer() const { return m_data; }

	std::vector<u8> TakeBuffer()
	{
		std::vector<u8> ret(std::move(m_data));
		m_data = std::vector<u8>();
		m_data_pos = 0;
		m_data_write_pos = 0;
		return ret;
	}

	zip_t* Open(u32 flags)
	{
		zip_t* ret = zip_open_from_source(m_source, flags, &m_error);
		if (!ret)
			return nullptr;

		// add an extra ref, because it's transferred to zip_t
		zip_source_keep(m_source);
		return ret;
	}

private:
	void Init()
	{
		m_source = zip_source_function_create(SourceCallback, this, &m_error);
	}

	static zip_int64_t SourceCallback(void* state, void* data, zip_uint64_t len, zip_source_cmd_t cmd)
	{
		ZipSourceVector* zs = static_cast<ZipSourceVector*>(state);

		switch (cmd)
		{
			case ZIP_SOURCE_BEGIN_WRITE:
			{
				zs->m_data_write_start_pos = zs->m_data_write_pos;
				zs->m_data_write_start_size = zs->m_data.size();
				return 0;
			}

			case ZIP_SOURCE_CLOSE:
			{
				zs->m_data_pos = 0;
				zs->m_data_write_pos = 0;
				zs->m_data_write_start_pos = 0;
				zs->m_data_write_start_size = 0;
				return 0;
			}

			case ZIP_SOURCE_COMMIT_WRITE:
			{
				return 0;
			}

			case ZIP_SOURCE_ROLLBACK_WRITE:
			{
				zs->m_data_write_start_pos = zs->m_data_write_pos;
				if (zs->m_data_write_start_size != zs->m_data.size())
					folly::resizeWithoutInitialization(zs->m_data, zs->m_data_write_start_size);

				return 0;
			}

			case ZIP_SOURCE_ERROR:
			{
				return zip_error_to_data(&zs->m_error, data, len);
			}

			case ZIP_SOURCE_FREE:
			{
				return 0;
			}

			case ZIP_SOURCE_OPEN:
			{
				return 0;
			}

			case ZIP_SOURCE_READ:
			{
				if (len < 0 || len > std::numeric_limits<u32>::max())
				{
					zip_error_set(&zs->m_error, ZIP_ER_INVAL, 0);
					return -1;
				}

				const size_t copy_len = std::min<size_t>(zs->m_data.size() - zs->m_data_pos, static_cast<size_t>(len));
				if (copy_len > 0)
				{
					std::memcpy(data, zs->m_data.data() + zs->m_data_pos, copy_len);
					zs->m_data_pos += copy_len;
				}

				return copy_len;
			}

			case ZIP_SOURCE_REMOVE:
			{
				zs->m_data.clear();
				zs->m_data_pos = 0;
				zs->m_data_write_pos = 0;
				return 0;
			}

			case ZIP_SOURCE_SEEK:
			{
				if (len < 0)
				{
					zip_error_set(&zs->m_error, ZIP_ER_INVAL, 0);
					return -1;
				}

				if (static_cast<size_t>(len) > zs->m_data.size())
					return -1;

				zs->m_data_pos = static_cast<size_t>(len);
				return 0;
			}

			case ZIP_SOURCE_SEEK_WRITE:
			{
				if (len < 0)
				{
					zip_error_set(&zs->m_error, ZIP_ER_INVAL, 0);
					return -1;
				}

				if (static_cast<size_t>(len) > zs->m_data.size())
					return -1;

				zs->m_data_write_pos = static_cast<size_t>(len);
				return 0;
			}


			case ZIP_SOURCE_SUPPORTS:
			{
				return zip_source_make_command_bitmap(ZIP_SOURCE_OPEN, ZIP_SOURCE_READ, ZIP_SOURCE_CLOSE, ZIP_SOURCE_ERROR,
					ZIP_SOURCE_FREE, ZIP_SOURCE_STAT, ZIP_SOURCE_SEEK, ZIP_SOURCE_TELL, ZIP_SOURCE_BEGIN_WRITE,
					ZIP_SOURCE_COMMIT_WRITE, ZIP_SOURCE_ROLLBACK_WRITE, ZIP_SOURCE_REMOVE, ZIP_SOURCE_SEEK_WRITE,
					ZIP_SOURCE_TELL_WRITE, ZIP_SOURCE_WRITE, -1);
			}

			case ZIP_SOURCE_TELL:
			{
				return zs->m_data_pos;
			}

			case ZIP_SOURCE_TELL_WRITE:
			{
				return zs->m_data_write_pos;
			}

			case ZIP_SOURCE_WRITE:
			{
				if (len < 0 || (zs->m_data_write_pos + len) >= std::numeric_limits<u32>::max())
				{
					zip_error_set(&zs->m_error, ZIP_ER_INVAL, 0);
					return -1;
				}

				const u32 new_size = zs->m_data_write_pos + static_cast<u32>(len);
				if (new_size > zs->m_data.size())
					folly::resizeWithoutInitialization(zs->m_data, new_size);

				if (len > 0)
				{
					std::memcpy(zs->m_data.data() + zs->m_data_write_pos, data, len);
					zs->m_data_write_pos += len;
				}

				return len;
			}

			case ZIP_SOURCE_STAT:
			{
				zip_stat_t* st;

				if (len < sizeof(*st))
				{
					zip_error_set(&zs->m_error, ZIP_ER_INVAL, 0);
					return -1;
				}

				st = (zip_stat_t*)data;

				zip_stat_init(st);
				st->mtime = 0;
				st->size = zs->m_data.size();
				st->comp_size = st->size;
				st->comp_method = ZIP_CM_STORE;
				st->encryption_method = ZIP_EM_NONE;
				st->valid = ZIP_STAT_MTIME | ZIP_STAT_SIZE | ZIP_STAT_COMP_SIZE | ZIP_STAT_COMP_METHOD | ZIP_STAT_ENCRYPTION_METHOD;

				return sizeof(*st);
			}

			default:
			{
				zip_error_set(&zs->m_error, ZIP_ER_OPNOTSUPP, 0);
				return -1;
			}
		}
	}

	std::vector<u8> m_data;
	zip_source_t* m_source = nullptr;
	zip_error_t m_error = {};
	size_t m_data_pos = 0;
	size_t m_data_write_start_pos = 0;
	size_t m_data_write_start_size = 0;
	size_t m_data_write_pos = 0;
};
