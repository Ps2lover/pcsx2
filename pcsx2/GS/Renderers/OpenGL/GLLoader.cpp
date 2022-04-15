/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021 PCSX2 Dev Team
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
#include "GLLoader.h"
#include "GS/GS.h"
#include <unordered_set>
#include "Host.h"

namespace ReplaceGL
{
	void APIENTRY ScissorIndexed(GLuint index, GLint left, GLint bottom, GLsizei width, GLsizei height)
	{
		glScissor(left, bottom, width, height);
	}

	void APIENTRY ViewportIndexedf(GLuint index, GLfloat x, GLfloat y, GLfloat w, GLfloat h)
	{
		glViewport(GLint(x), GLint(y), GLsizei(w), GLsizei(h));
	}
} // namespace ReplaceGL

namespace Emulate_DSA
{
	// Texture entry point
	void APIENTRY BindTextureUnit(GLuint unit, GLuint texture)
	{
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, texture);
	}

	void APIENTRY CreateTexture(GLenum target, GLsizei n, GLuint* textures)
	{
		glGenTextures(1, textures);
	}

	void APIENTRY TextureStorage(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
	{
		BindTextureUnit(7, texture);
		glTexStorage2D(GL_TEXTURE_2D, levels, internalformat, width, height);
	}

	void APIENTRY TextureSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels)
	{
		BindTextureUnit(7, texture);
		glTexSubImage2D(GL_TEXTURE_2D, level, xoffset, yoffset, width, height, format, type, pixels);
	}

	void APIENTRY CompressedTextureSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data)
	{
		BindTextureUnit(7, texture);
		glCompressedTexSubImage2D(GL_TEXTURE_2D, level, xoffset, yoffset, width, height, format, imageSize, data);
	}

	void APIENTRY GetTexureImage(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, void* pixels)
	{
		BindTextureUnit(7, texture);
		glGetTexImage(GL_TEXTURE_2D, level, format, type, pixels);
	}

	void APIENTRY TextureParameteri(GLuint texture, GLenum pname, GLint param)
	{
		BindTextureUnit(7, texture);
		glTexParameteri(GL_TEXTURE_2D, pname, param);
	}

	void APIENTRY GenerateTextureMipmap(GLuint texture)
	{
		BindTextureUnit(7, texture);
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	// Misc entry point
	void APIENTRY CreateSamplers(GLsizei n, GLuint* samplers)
	{
		glGenSamplers(n, samplers);
	}

	// Replace function pointer to emulate DSA behavior
	void Init()
	{
		DevCon.Warning("DSA is not supported. Expect slower performance");
		glBindTextureUnit = BindTextureUnit;
		glCreateTextures = CreateTexture;
		glTextureStorage2D = TextureStorage;
		glTextureSubImage2D = TextureSubImage;
		glCompressedTextureSubImage2D = CompressedTextureSubImage;
		glGetTextureImage = GetTexureImage;
		glTextureParameteri = TextureParameteri;
		glGenerateTextureMipmap = GenerateTextureMipmap;
		glCreateSamplers = CreateSamplers;
	}
} // namespace Emulate_DSA

namespace GLLoader
{
	bool vendor_id_amd = false;
	bool vendor_id_nvidia = false;
	bool vendor_id_intel = false;
	bool mesa_driver = false;
	bool in_replayer = false;
	bool buggy_pbo = false;

	bool is_gles = false;
	bool has_dual_source_blend = false;
	bool has_clip_control = true;
	bool has_binding_layout = false;
	bool has_enhanced_layouts = false;
	bool found_framebuffer_fetch = false;
	bool found_geometry_shader = true; // we require GL3.3 so geometry must be supported by default
	bool found_texture_barrier = false;
	bool found_texture_storage = false;
	bool found_GL_ARB_clear_texture = false;
	// DX11 GPU
	bool found_GL_ARB_gpu_shader5 = false;             // Require IvyBridge
	bool found_GL_ARB_shader_image_load_store = false; // Intel IB. Nvidia/AMD miss Mesa implementation.

	// In case sparse2 isn't supported
	bool found_compatible_GL_ARB_sparse_texture2 = false;
	bool found_compatible_sparse_depth = false;

	static bool mandatory(const char* ext_name, int ext_var, int version_var)
	{
		if (!ext_var && !version_var)
		{
			Host::ReportFormattedErrorAsync("GS", "ERROR: %s is NOT SUPPORTED\n", ext_name);
			return false;
		}

		return true;
	}

	static bool optional(const char* ext_name, int ext_var, int version_var)
	{
		if (!ext_var && !version_var)
		{
			DevCon.Warning("INFO: %s is NOT SUPPORTED", ext_name);
			return false;
		}
		else
		{
			DevCon.WriteLn("INFO: %s is available", ext_name);
			return true;
		}
	}

	bool check_gl_version()
	{
		const char* vendor = (const char*)glGetString(GL_VENDOR);
		if (strstr(vendor, "Advanced Micro Devices") || strstr(vendor, "ATI Technologies Inc.") || strstr(vendor, "ATI"))
			vendor_id_amd = true;
		else if (strstr(vendor, "NVIDIA Corporation"))
			vendor_id_nvidia = true;

#ifdef _WIN32
		else if (strstr(vendor, "Intel"))
			vendor_id_intel = true;
#else
		// On linux assumes the free driver if it isn't nvidia or amd pro driver
		mesa_driver = !vendor_id_nvidia && !vendor_id_amd;
#endif

		if (!GLAD_GL_VERSION_3_3 && !GLAD_GL_ES_VERSION_3_1)
		{
			GLint major_gl = 0;
			GLint minor_gl = 0;
			glGetIntegerv(GL_MAJOR_VERSION, &major_gl);
			glGetIntegerv(GL_MINOR_VERSION, &minor_gl);

			Host::ReportFormattedErrorAsync("GS", "OpenGL is not supported. Only OpenGL %d.%d\n was found", major_gl, minor_gl);
			return false;
		}

		return true;
	}

	bool check_gl_supported_extension()
	{
		// Extra
		{
			has_binding_layout = optional("GL_ARB_shading_language_420pack", GLAD_GL_ARB_shading_language_420pack, GLAD_GL_VERSION_4_2 | GLAD_GL_ES_VERSION_3_1) &&
				optional("GL_ARB_explicit_attrib_location", GLAD_GL_ARB_explicit_attrib_location, GLAD_GL_VERSION_4_3 | GLAD_GL_ES_VERSION_3_1);
			has_enhanced_layouts = optional("GL_ARB_enhanced_layouts", GLAD_GL_ARB_enhanced_layouts, GLAD_GL_VERSION_4_2 | GLAD_GL_ES_VERSION_3_2);
			found_texture_storage = optional("GL_ARB_texture_storage", GLAD_GL_ARB_texture_storage, GLAD_GL_VERSION_4_2 | GLAD_GL_ES_VERSION_3_0);

			// Bonus
			optional("GL_ARB_sparse_texture", GLAD_GL_ARB_sparse_texture, 0);
			optional("GL_ARB_sparse_texture2", GLAD_GL_ARB_sparse_texture2, 0);
			has_clip_control = optional("GL_ARB_clip_control", GLAD_GL_ARB_clip_control, GLAD_GL_VERSION_4_5);
			// GL4.0
			found_GL_ARB_gpu_shader5 = optional("GL_ARB_gpu_shader5", GLAD_GL_ARB_gpu_shader5, GLAD_GL_VERSION_4_0);
			// GL4.2
			found_GL_ARB_shader_image_load_store = optional("GL_ARB_shader_image_load_store", GLAD_GL_ARB_shader_image_load_store, GLAD_GL_VERSION_4_2 | GLAD_GL_ES_VERSION_3_1);
			// GL4.4
			found_GL_ARB_clear_texture = optional("GL_ARB_clear_texture", GLAD_GL_ARB_clear_texture, GLAD_GL_VERSION_4_4);
			// GL4.5
			optional("GL_ARB_direct_state_access", GLAD_GL_ARB_direct_state_access, GLAD_GL_VERSION_4_5);
			// Mandatory for the advance HW renderer effect. Unfortunately Mesa LLVMPIPE/SWR renderers doesn't support this extension.
			// Rendering might be corrupted but it could be good enough for test/virtual machine.
			found_texture_barrier = optional("GL_ARB_texture_barrier", GLAD_GL_ARB_texture_barrier, GLAD_GL_VERSION_4_5);

			found_geometry_shader = optional("GL_ARB_geometry_shader4", GLAD_GL_ARB_geometry_shader4 || GLAD_GL_OES_geometry_shader, GLAD_GL_VERSION_3_2 || GLAD_GL_ES_VERSION_3_2);
			if (GSConfig.OverrideGeometryShaders >= 0)
			{
				if (GSConfig.OverrideGeometryShaders != 1)
				{
					Console.Warning("Geometry shaders were found but disabled. This will reduce performance.");
					found_geometry_shader = false;
				}
			}

			has_dual_source_blend = GLAD_GL_VERSION_3_2 || GLAD_GL_ARB_blend_func_extended;
			found_framebuffer_fetch = GLAD_GL_EXT_shader_framebuffer_fetch || GLAD_GL_ARM_shader_framebuffer_fetch;
			if (found_framebuffer_fetch && GSConfig.DisableFramebufferFetch)
			{
				Console.Warning("Framebuffer fetch was found but is disabled. This will reduce performance.");
				found_framebuffer_fetch = false;
			}
		}

		if (vendor_id_amd)
		{
			Console.Warning("The OpenGL hardware renderer is slow on AMD GPUs due to an inefficient driver.\n"
							"Check out the link below for further information.\n"
							"https://github.com/PCSX2/pcsx2/wiki/OpenGL-and-AMD-GPUs---All-you-need-to-know");
		}

		if (vendor_id_intel && !found_texture_barrier && !found_framebuffer_fetch)
		{
			// Assume that driver support is good when texture barrier and DSA is supported, disable the log then.
			Console.Warning("The OpenGL renderer is inefficient on Intel GPUs due to an inefficient driver.\n"
							"Check out the link below for further information.\n"
							"https://github.com/PCSX2/pcsx2/wiki/OpenGL-and-Intel-GPUs-All-you-need-to-know");
		}

		if (!GLAD_GL_ARB_viewport_array)
		{
			glScissorIndexed = ReplaceGL::ScissorIndexed;
			glViewportIndexedf = ReplaceGL::ViewportIndexedf;
			DevCon.Warning("GL_ARB_viewport_array is not supported! Function pointer will be replaced");
		}

		if (is_gles)
		{
			has_dual_source_blend = GLAD_GL_EXT_blend_func_extended || GLAD_GL_ARB_blend_func_extended;
			if (!has_dual_source_blend && !found_framebuffer_fetch)
			{
				Host::AddOSDMessage("Both dual source blending and framebuffer fetch are missing, things will be broken.", 10.0f);
				Console.Error("Missing both dual-source blending and framebuffer fetch");
			}

			// For GLES3.1, we **need** GL_OES_draw_elements_base_vertex.
			if (!GLAD_GL_ES_VERSION_3_2)
			{
				if (!GLAD_GL_OES_draw_elements_base_vertex)
				{
					Host::ReportErrorAsync("GS", "OpenGL ES version 3.2 or GL_OES_draw_elements_base_vertex is required.");
					return false;
				}
				if (!GLAD_GL_OES_shader_io_blocks)
				{
					Host::ReportErrorAsync("GS", "OpenGL ES version 3.2 or GLAD_GL_OES_shader_io_blocks is required.");
					return false;
				}

				// Just cheat and replace the function pointer, the signature is the same.
				glDrawElementsBaseVertex = glDrawElementsBaseVertexOES;
				glDrawRangeElementsBaseVertex = glDrawRangeElementsBaseVertexOES;
				glDrawElementsInstancedBaseVertex = glDrawElementsInstancedBaseVertexOES;
			}
		}
		else
		{
			has_dual_source_blend = true;
#ifdef __APPLE__
			// No buffer storage on Macs, this won't go down well.
			buggy_pbo = true;
#else
			buggy_pbo = false;
#endif
		}

		// Thank you Intel for not providing support of basic features on your IGPUs.
		if (!GLAD_GL_ARB_direct_state_access)
		{
			Emulate_DSA::Init();
		}

		return true;
	}

	bool is_sparse2_compatible(const char* name, GLenum internal_fmt, int x_max, int y_max)
	{
		GLint index_count = 0;
		glGetInternalformativ(GL_TEXTURE_2D, internal_fmt, GL_NUM_VIRTUAL_PAGE_SIZES_ARB, 1, &index_count);
		if (!index_count)
		{
			DevCon.Warning("%s isn't sparse compatible. No index found", name);
			return false;
		}

		GLint x, y;
		glGetInternalformativ(GL_TEXTURE_2D, internal_fmt, GL_VIRTUAL_PAGE_SIZE_X_ARB, 1, &x);
		glGetInternalformativ(GL_TEXTURE_2D, internal_fmt, GL_VIRTUAL_PAGE_SIZE_Y_ARB, 1, &y);
		if (x > x_max && y > y_max)
		{
			DevCon.Warning("%s isn't sparse compatible. Page size (%d,%d) is too big (%d, %d)",
						 name, x, y, x_max, y_max);
			return false;
		}

		return true;
	}

	static void check_sparse_compatibility()
	{
		if (!GLAD_GL_ARB_sparse_texture || !GLAD_GL_EXT_direct_state_access)
		{
			found_compatible_GL_ARB_sparse_texture2 = false;
			found_compatible_sparse_depth = false;

			return;
		}

		found_compatible_GL_ARB_sparse_texture2 = true;
		if (!GLAD_GL_ARB_sparse_texture2)
		{
			// Only check format from GSTextureOGL
			found_compatible_GL_ARB_sparse_texture2 &= is_sparse2_compatible("GL_R8", GL_R8, 256, 256);

			found_compatible_GL_ARB_sparse_texture2 &= is_sparse2_compatible("GL_R16UI", GL_R16UI, 256, 128);

			found_compatible_GL_ARB_sparse_texture2 &= is_sparse2_compatible("GL_R32UI", GL_R32UI, 128, 128);
			found_compatible_GL_ARB_sparse_texture2 &= is_sparse2_compatible("GL_R32I", GL_R32I, 128, 128);
			found_compatible_GL_ARB_sparse_texture2 &= is_sparse2_compatible("GL_RGBA8", GL_RGBA8, 128, 128);

			found_compatible_GL_ARB_sparse_texture2 &= is_sparse2_compatible("GL_RGBA16", GL_RGBA16, 128, 64);
			found_compatible_GL_ARB_sparse_texture2 &= is_sparse2_compatible("GL_RGBA16I", GL_RGBA16I, 128, 64);
			found_compatible_GL_ARB_sparse_texture2 &= is_sparse2_compatible("GL_RGBA16UI", GL_RGBA16UI, 128, 64);
			found_compatible_GL_ARB_sparse_texture2 &= is_sparse2_compatible("GL_RGBA16F", GL_RGBA16F, 128, 64);

			found_compatible_GL_ARB_sparse_texture2 &= is_sparse2_compatible("GL_RGBA32F", GL_RGBA32F, 64, 64);
		}

		// Can fit in 128x64 but 128x128 is enough
		// Disable sparse depth for AMD. Bad driver strikes again.
		// driver reports a compatible sparse format for depth texture but it isn't attachable to a frame buffer.
		found_compatible_sparse_depth = !vendor_id_amd && is_sparse2_compatible("GL_DEPTH32F_STENCIL8", GL_DEPTH32F_STENCIL8, 128, 128);

		DevCon.WriteLn("INFO: sparse color texture is %s", found_compatible_GL_ARB_sparse_texture2 ? "available" : "NOT SUPPORTED");
		DevCon.WriteLn("INFO: sparse depth texture is %s", found_compatible_sparse_depth ? "available" : "NOT SUPPORTED");
	}

	bool check_gl_requirements()
	{
		if (!check_gl_version())
			return false;

		if (!check_gl_supported_extension())
			return false;

		// Bonus for sparse texture
		check_sparse_compatibility();

		return true;
	}
} // namespace GLLoader
