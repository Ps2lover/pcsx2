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
#include "Image.h"
#include "FileSystem.h"
#include "Console.h"
#include "Path.h"
#include "ScopedGuard.h"
#include "StringUtil.h"

#include <png.h>

using namespace Common;

static bool PNGBufferLoader(RGBA8Image* image, const void* buffer, size_t buffer_size);
static bool PNGFileLoader(RGBA8Image* image, const char* filename, std::FILE* fp);
static bool PNGFileSaver(const RGBA8Image& image, const char* filename, std::FILE* fp);

struct FormatHandler
{
	const char* extension;
	bool (*buffer_loader)(RGBA8Image*, const void*, size_t);
	bool (*file_loader)(RGBA8Image*, const char*, std::FILE*);
	bool (*file_saver)(const RGBA8Image&, const char*, std::FILE*);
};

static constexpr FormatHandler s_format_handlers[] = {
	{"png", PNGBufferLoader, PNGFileLoader, PNGFileSaver},
};

static const FormatHandler* GetFormatHandler(const std::string_view& extension)
{
	for (const FormatHandler& handler : s_format_handlers)
	{
		if (StringUtil::compareNoCase(extension, handler.extension))
			return &handler;
	}

	return nullptr;
}

RGBA8Image::RGBA8Image() = default;

RGBA8Image::RGBA8Image(const RGBA8Image& copy)
	: Image(copy)
{
}

RGBA8Image::RGBA8Image(u32 width, u32 height, const u32* pixels)
	: Image(width, height, pixels)
{
}

RGBA8Image::RGBA8Image(RGBA8Image&& move)
	: Image(move)
{
}

bool RGBA8Image::LoadFromFile(const char* filename)
{
	auto fp = FileSystem::OpenManagedCFile(filename, "rb");
	if (!fp)
		return false;

	return LoadFromFile(filename, fp.get());
}

bool RGBA8Image::SaveToFile(const char* filename) const
{
	auto fp = FileSystem::OpenManagedCFile(filename, "rb");
	if (!fp)
		return false;

	if (SaveToFile(filename, fp.get()))
		return true;

	// save failed
	fp.reset();
	FileSystem::DeleteFilePath(filename);
	return false;
}

bool RGBA8Image::LoadFromFile(const char* filename, std::FILE* fp)
{
	const std::string_view extension(Path::GetExtension(filename));
	const FormatHandler* handler = GetFormatHandler(extension);
	if (!handler || !handler->file_loader)
	{
		Console.Error("(RGBA8Image::LoadFromFile) Unknown extension '%.*s'",
			static_cast<int>(extension.size()), extension.data());
		return false;
	}

	return handler->file_loader(this, filename, fp);
}

bool RGBA8Image::LoadFromBuffer(const char* filename, const void* buffer, size_t buffer_size)
{
	const std::string_view extension(Path::GetExtension(filename));
	const FormatHandler* handler = GetFormatHandler(extension);
	if (!handler || !handler->buffer_loader)
	{
		Console.Error("(RGBA8Image::LoadFromBuffer) Unknown extension '%.*s'",
			static_cast<int>(extension.size()), extension.data());
		return false;
	}

	return handler->buffer_loader(this, buffer, buffer_size);
}

bool RGBA8Image::SaveToFile(const char* filename, std::FILE* fp) const
{
	const std::string_view extension(Path::GetExtension(filename));
	const FormatHandler* handler = GetFormatHandler(extension);
	if (!handler || !handler->file_saver)
	{
		Console.Error("(RGBA8Image::SaveToFile) Unknown extension '%.*s'",
			static_cast<int>(extension.size()), extension.data());
		return false;
	}

	return handler->file_saver(*this, filename, fp);
}

static bool PNGCommonLoader(RGBA8Image* image, png_structp png_ptr, png_infop info_ptr)
{
	png_read_info(png_ptr, info_ptr);

	png_uint_32 width = 0;
	png_uint_32 height = 0;
	int bitDepth = 0;
	int colorType = -1;
	if (png_get_IHDR(png_ptr, info_ptr, &width, &height, &bitDepth, &colorType, nullptr, nullptr, nullptr) != 1 ||
		width == 0 || height == 0)
	{
		return false;
	}

	std::vector<u32> new_data(width * height);

	const png_uint_32 row_bytes = png_get_rowbytes(png_ptr, info_ptr);
	std::vector<u8> row_data(row_bytes);

	for (u32 y = 0; y < height; y++)
	{
		png_read_row(png_ptr, static_cast<png_bytep>(row_data.data()), nullptr);

		const u8* row_ptr = row_data.data();
		u32* out_ptr = new_data.data() + y * width;
		if (colorType == PNG_COLOR_TYPE_RGB)
		{
			for (u32 x = 0; x < width; x++)
			{
				u32 pixel = static_cast<u32>(*(row_ptr)++);
				pixel |= static_cast<u32>(*(row_ptr)++) << 8;
				pixel |= static_cast<u32>(*(row_ptr)++) << 16;
				pixel |= 0x80000000u; // make opaque
				*(out_ptr++) = pixel;
			}
		}
		else if (colorType == PNG_COLOR_TYPE_RGBA)
		{
			std::memcpy(out_ptr, row_ptr, width * sizeof(u32));
		}
	}

	image->SetPixels(width, height, std::move(new_data));
	return true;
}

bool PNGFileLoader(RGBA8Image* image, const char* filename, std::FILE* fp)
{
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png_ptr)
		return false;

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		png_destroy_read_struct(&png_ptr, nullptr, nullptr);
		return false;
	}

	ScopedGuard cleanup([&png_ptr, &info_ptr]() {
		png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
	});

	if (setjmp(png_jmpbuf(png_ptr)))
		return false;

	png_init_io(png_ptr, fp);
	return PNGCommonLoader(image, png_ptr, info_ptr);
}

bool PNGBufferLoader(RGBA8Image* image, const void* buffer, size_t buffer_size)
{
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png_ptr)
		return false;

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		png_destroy_read_struct(&png_ptr, nullptr, nullptr);
		return false;
	}

	ScopedGuard cleanup([&png_ptr, &info_ptr]() {
		png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
	});

	if (setjmp(png_jmpbuf(png_ptr)))
		return false;

	struct IOData
	{
		const u8* buffer;
		size_t buffer_size;
		size_t buffer_pos;
	};
	IOData data = {static_cast<const u8*>(buffer), buffer_size, 0};

	png_set_read_fn(png_ptr, &data, [](png_structp png_ptr, png_bytep data_ptr, png_size_t size) {
		IOData* data = static_cast<IOData*>(png_get_io_ptr(png_ptr));
		const size_t read_size = std::min<size_t>(data->buffer_size - data->buffer_pos, size);
		if (read_size > 0)
		{
			std::memcpy(data_ptr, data->buffer + data->buffer_pos, read_size);
			data->buffer_pos += read_size;
		}
	});

	return PNGCommonLoader(image, png_ptr, info_ptr);
}

bool PNGFileSaver(const RGBA8Image& image, const char* filename, std::FILE* fp)
{
	// TODO
	return false;
}
