#if defined(USE_OPENGLES_20) || defined (USE_OPENGL_21)

#include "renderers/Renderer.h"
#include "math/Transform4x4f.h"
#include "Log.h"
#include "Settings.h"

#include <vector>

#include "GlExtensions.h"
#include "Shader.h"

namespace Renderer
{
//////////////////////////////////////////////////////////////////////////

	static SDL_GLContext sdlContext       = nullptr;
	
	static Transform4x4f projectionMatrix = Transform4x4f::Identity();
	static Transform4x4f worldViewMatrix  = Transform4x4f::Identity();
	static Transform4x4f mvpMatrix		  = Transform4x4f::Identity();

	static Shader  	vertexShaderTexture;
	static Shader  	fragmentShaderColorTexture;
	static ShaderProgram    shaderProgramColorTexture;

	static Shader  	vertexShaderNoTexture;
	static Shader  	fragmentShaderColorNoTexture;
	static ShaderProgram    shaderProgramColorNoTexture;

	static GLuint        vertexBuffer     = 0;

//////////////////////////////////////////////////////////////////////////

	static GLenum convertBlendFactor(const Blend::Factor _blendFactor)
	{
		switch(_blendFactor)
		{
			case Blend::ZERO:                { return GL_ZERO;                } break;
			case Blend::ONE:                 { return GL_ONE;                 } break;
			case Blend::SRC_COLOR:           { return GL_SRC_COLOR;           } break;
			case Blend::ONE_MINUS_SRC_COLOR: { return GL_ONE_MINUS_SRC_COLOR; } break;
			case Blend::SRC_ALPHA:           { return GL_SRC_ALPHA;           } break;
			case Blend::ONE_MINUS_SRC_ALPHA: { return GL_ONE_MINUS_SRC_ALPHA; } break;
			case Blend::DST_COLOR:           { return GL_DST_COLOR;           } break;
			case Blend::ONE_MINUS_DST_COLOR: { return GL_ONE_MINUS_DST_COLOR; } break;
			case Blend::DST_ALPHA:           { return GL_DST_ALPHA;           } break;
			case Blend::ONE_MINUS_DST_ALPHA: { return GL_ONE_MINUS_DST_ALPHA; } break;
			default:                         { return GL_ZERO;                }
		}

	} // convertBlendFactor

//////////////////////////////////////////////////////////////////////////

	static GLenum convertTextureType(const Texture::Type _type)
	{
		switch(_type)
		{
			case Texture::RGBA:  { return GL_RGBA;            } break;
			case Texture::ALPHA: { return GL_LUMINANCE_ALPHA; } break;
			default:             { return GL_ZERO;            }
		}

	} // convertTextureType

//////////////////////////////////////////////////////////////////////////

	unsigned int convertColor(const unsigned int _color)
	{
		// convert from rgba to abgr
		const unsigned char r = ((_color & 0xff000000) >> 24) & 255;
		const unsigned char g = ((_color & 0x00ff0000) >> 16) & 255;
		const unsigned char b = ((_color & 0x0000ff00) >>  8) & 255;
		const unsigned char a = ((_color & 0x000000ff)      ) & 255;

		return ((a << 24) | (b << 16) | (g << 8) | (r));

	} // convertColor

//////////////////////////////////////////////////////////////////////////

	unsigned int getWindowFlags()
	{
		return SDL_WINDOW_OPENGL;

	} // getWindowFlags

//////////////////////////////////////////////////////////////////////////

	void setupWindow()
	{
#if OPENGL_EXTENSIONS
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,       1);
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE,           8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,         8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,          8);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,        24);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,       1);
		SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

	} // setupWindow

//////////////////////////////////////////////////////////////////////////

	std::vector<std::pair<std::string, std::string>> getDriverInformation()
	{
		std::vector<std::pair<std::string, std::string>> info;

#if OPENGL_EXTENSIONS
		info.push_back(std::pair<std::string, std::string>("GRAPHICS API", "DESKTOP OPENGL 2.1"));
#else 
		info.push_back(std::pair<std::string, std::string>("GRAPHICS API", "OPENGL ES 2.0"));
#endif

		const std::string vendor = glGetString(GL_VENDOR) ? (const char*)glGetString(GL_VENDOR) : "";
		if (!vendor.empty())
			info.push_back(std::pair<std::string, std::string>("VENDOR", vendor));

		const std::string renderer = glGetString(GL_RENDERER) ? (const char*)glGetString(GL_RENDERER) : "";
		if (!renderer.empty())
			info.push_back(std::pair<std::string, std::string>("RENDERER", renderer));

		const std::string version = glGetString(GL_VERSION) ? (const char*)glGetString(GL_VERSION) : "";
		if (!version.empty())
			info.push_back(std::pair<std::string, std::string>("VERSION", version));

		const std::string shaders = glGetString(GL_SHADING_LANGUAGE_VERSION) ? (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION) : "";
		if (!shaders.empty())
			info.push_back(std::pair<std::string, std::string>("SHADERS", shaders));

		return info;
	}

	void createContext()
	{
		const std::string vendor     = glGetString(GL_VENDOR)     ? (const char*)glGetString(GL_VENDOR)     : "";
		const std::string renderer   = glGetString(GL_RENDERER)   ? (const char*)glGetString(GL_RENDERER)   : "";
		const std::string version    = glGetString(GL_VERSION)    ? (const char*)glGetString(GL_VERSION)    : "";
		const std::string extensions = glGetString(GL_EXTENSIONS) ? (const char*)glGetString(GL_EXTENSIONS) : "";
		const std::string shaders    = glGetString(GL_SHADING_LANGUAGE_VERSION) ? (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION) : "";
		
		LOG(LogInfo) << "GL vendor:   " << vendor;
		LOG(LogInfo) << "GL renderer: " << renderer;
		LOG(LogInfo) << "GL version:  " << version;
		LOG(LogInfo) << "GL shading:  " << shaders;
		LOG(LogInfo) << "GL exts:     " << extensions;

		LOG(LogInfo) << " ARB_texture_non_power_of_two: " << (extensions.find("ARB_texture_non_power_of_two") != std::string::npos ? "ok" : "MISSING");

#if OPENGL_EXTENSIONS
		initializeGlExtensions();
#endif

		SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
		SDL_RenderClear(sdlRenderer);
		SDL_RenderPresent(sdlRenderer);
		SDL_SetRenderDrawColor(sdlRenderer, 255, 255, 255, SDL_ALPHA_OPAQUE);

#if OPENGL_EXTENSIONS
		GL_CHECK_ERROR(glActiveTexture_(GL_TEXTURE0));
#else
		GL_CHECK_ERROR(glActiveTexture(GL_TEXTURE0));
#endif

		GL_CHECK_ERROR(glPixelStorei(GL_PACK_ALIGNMENT, 1));
		GL_CHECK_ERROR(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
		
	} // createContext

	int getAvailableVideoMemory()
	{
		float total = 0;

		float megabytes = 10.0;
		int sz = sqrtf(megabytes * 1024.0 * 1024.0 / 4.0f);

		std::vector<unsigned int> textures;
		textures.reserve(1000000);

		while (true)
		{
			unsigned int textureId;
			glGenTextures(1, &textureId);
			if (glGetError() != GL_NO_ERROR)
				break;

			textures.push_back(textureId);

			glBindTexture(GL_TEXTURE_2D, textureId);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz, sz, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			if (glGetError() != GL_NO_ERROR)
				break;

			textures.push_back(textureId);
			total += megabytes;
		}

		for (auto tx : textures)
            glDeleteTextures(1, &tx);

		return total;
	}

//////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////

	static SDL_Texture* boundTexture = 0;

	void bindTexture(SDL_Texture* _texture)
	{
		if (boundTexture == _texture)
			return;

		boundTexture = _texture;
	} // bindTexture

//////////////////////////////////////////////////////////////////////////

	void updateTexture(SDL_Texture* _texture, const Texture::Type _type, const unsigned int _x, const unsigned _y, const unsigned int _width, const unsigned int _height, void* _data)
	{
        Uint32* pixels = nullptr;
        Uint32* data32 = (Uint32*)_data;
        int pitch;
        if (_data)
        {
	        SDL_Rect rect = { _x, _y, _width, _height };
        	if (SDL_LockTexture(_texture, nullptr/*&rect*/, (void**)&pixels, &pitch) == 0)
        	{
        		for (int y = 0 ; y < _height ; y++)
        		    memcpy(&pixels[(_y+y)*(pitch/sizeof(Uint32))+_x], &data32[(y*_width)], _width*sizeof(Uint32));

        		SDL_UnlockTexture(_texture);
        	}
        }
	} // updateTexture


} // Renderer::

#endif // USE_OPENGLES_20
