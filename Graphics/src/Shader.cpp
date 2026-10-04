#include "stdafx.h"
#include "Shader.hpp"
#include <Graphics/ResourceManagers.hpp>
#include "OpenGL.hpp"

namespace Graphics
{
#ifdef USC_IOS
	/*
		The skin shaders shipped with the default skin (.vs / .fs files under
		bin/skins) are written for the desktop
		core-profile pipeline, and two constructs in them are simply not part of
		GLSL ES 3.00:

		  * "#extension GL_ARB_separate_shader_objects : enable" - the extension
		    that lets the desktop path compile stages individually with
		    glCreateShaderProgramv. ES rejects the name outright, and because
		    #version has to come first it ends up directly after it, which is the
		    "#extension must always be before any non-preprocessor tokens" error
		    seen in usc-ios.log. Every shader in the default skin began with it,
		    so every material failed to load and the game rendered white.

		  * "out gl_PerVertex { vec4 gl_Position; };" - a redeclaration of a
		    built-in interface block that only exists in desktop GLSL. ES provides
		    gl_Position directly, so the block is dropped.

		Rewriting here rather than editing the 42 shipped shader files keeps
		skins the user installs themselves working too, and leaves the desktop
		builds byte-for-byte untouched.
	*/
	String DowngradeDesktopShader(const String& source, bool isVertexShader)
	{
		String out;
		out.reserve(source.size());

		size_t pos = 0;
		while(pos <= source.size())
		{
			size_t eol = source.find('\n', pos);
			String line = source.substr(pos, eol == String::npos ? String::npos : eol - pos);
			// Trailing CR from a CRLF file would otherwise end up inside the shader.
			String trimmed = line;
			trimmed.Trim('\r');
			// Trim() only strips the given character, so leading spaces and tabs are
			// removed explicitly to make the comparisons below indentation-proof.
			// substr() is used instead of erase() because the String wrapper only
			// re-exports a subset of the std::basic_string members.
			size_t indent = 0;
			while(indent < trimmed.size() && (trimmed[indent] == ' ' || trimmed[indent] == '\t'))
				indent++;
			if(indent > 0)
				trimmed = trimmed.substr(indent);

			if(trimmed.substr(0, 10).compare("#extension") == 0)
			{
				// Desktop-only extension directive: drop the whole line.
			}
			else if(trimmed.substr(0, 7).compare("layout(") == 0 &&
				(trimmed.find(") in ") != String::npos || trimmed.find(") out ") != String::npos))
			{
				/*
					Layout qualifiers on the stage interface are the other desktop-only
					construct. On the real device GLSL ES 3.00 rejected them with
					"Invalid use of layout 'location'", which took down every material
					that used the background shaders:

					  * vertex outputs and fragment inputs are matched by name at link
					    time, so pinning their locations is both unnecessary and, here,
					    rejected. Dropping them lets the linker do its job.

					  * vertex inputs DO need explicit locations, because Mesh::SetData
					    feeds the streams in declaration order with index 0, 1, ... These
					    are the ones that must stay.

					The qualifier is only removed, never the rest of the declaration, so
					"layout(location=1) out vec2 texVp;" becomes "out vec2 texVp;".
				*/
				const bool isInput = trimmed.find(") in ") != String::npos;
				if(isInput && isVertexShader)
				{
					out += line;
					if(eol == String::npos)
						break;
					out += '\n';
				}
				else
				{
					size_t close = trimmed.find(') ');
					if(close != String::npos)
					{
						String rest = trimmed.substr(close + 2);
						out += rest;
						if(eol == String::npos)
							break;
						out += '\n';
					}
				}
			}
			else if(trimmed.compare("out gl_PerVertex") == 0)
			{
				// Skip the block body and its closing brace as well.
				size_t blockEnd = source.find("};", pos);
				if(blockEnd == String::npos)
					blockEnd = pos;
				pos = blockEnd + 2;
				// Consume the rest of that line.
				size_t after = source.find('\n', pos);
				pos = (after == String::npos) ? source.size() + 1 : after + 1;
				continue;
			}
			else
			{
				out += line;
				if(eol == String::npos)
					break;
				out += '\n';
			}

			if(eol == String::npos)
				break;
			pos = eol + 1;
		}

		return out;
	}
#endif

#ifdef EMBEDDED
	const uint32 typeMap[] =
	{
		GL_VERTEX_SHADER,
		GL_FRAGMENT_SHADER,
	};
#else
	const uint32 typeMap[] =
	{
		GL_VERTEX_SHADER,
		GL_FRAGMENT_SHADER,
		GL_GEOMETRY_SHADER,
	};
	const uint32 shaderStageMap[] =
	{
		GL_VERTEX_SHADER_BIT,
		GL_FRAGMENT_SHADER_BIT,
		GL_GEOMETRY_SHADER_BIT,
	};
#endif
	class Shader_Impl : public ShaderRes
	{
		ShaderType m_type;
		uint32 m_prog;
		OpenGL* m_gl;

		String m_sourcePath;

		// Hot Reload detection on windows
#ifdef _WIN32
		HANDLE m_changeNotification = INVALID_HANDLE_VALUE;
		uint64 m_lwt = -1;
#endif
	public:
		Shader_Impl(OpenGL* gl) : m_gl(gl)
		{
		}
		~Shader_Impl()
		{
			// Cleanup OpenGL resource
			if(glIsProgram(m_prog))
			{
				glDeleteProgram(m_prog);
			}

#ifdef _WIN32
			// Close change notification handle
			if(m_changeNotification != INVALID_HANDLE_VALUE)
			{
				CloseHandle(m_changeNotification);
			}
#endif
		}
		void SetupChangeHandler()
		{
#ifdef _WIN32
			if(m_changeNotification != INVALID_HANDLE_VALUE)
			{
				CloseHandle(m_changeNotification);
				m_changeNotification = INVALID_HANDLE_VALUE;
			}

			WString rootFolder = Utility::ConvertToWString(Path::RemoveLast(m_sourcePath));
			m_changeNotification = FindFirstChangeNotificationW(*rootFolder, false, FILE_NOTIFY_CHANGE_LAST_WRITE);
#endif
		}

#ifdef EMBEDDED
		bool LoadProgram(uint32& programOut)
		{
			File in;
			if(!in.OpenRead(m_sourcePath))
				return false;

			String sourceStr;
			sourceStr.resize(in.GetSize());
			if(sourceStr.size() == 0)
				return false;

			in.Read(&sourceStr.front(), sourceStr.size());
#ifdef USC_IOS
			/*
				GLSL ES 1.00 leaves attribute locations to the driver, but
				Mesh::SetData binds the vertex attributes by index (0, 1, ...)
				in the order the vertex struct declares them. On the desktop
				path that works because the shaders pin the locations with
				layout(location=...); the ES 1.00 shaders used for embedded
				targets did not, so iOS assigned the attributes freely and the
				position/texcoord streams were swapped - the track, the lasers
				and the notes all rendered with the wrong geometry. The iOS
				context is ES 3.0, so the shaders are compiled as GLSL ES 3.00
				with the same location convention as the desktop path.
			*/
			// The shipped skin shaders are desktop GLSL; strip the parts GLSL ES
			// 3.00 has no equivalent for before prepending the version directive.
			sourceStr = DowngradeDesktopShader(sourceStr, m_type == ShaderType::Vertex);
			sourceStr = "#version 300 es\n#define EMBEDDED\n#define target target\n#define texture texture\nprecision mediump float;\n"
				+ sourceStr;
#else
			sourceStr = "#version 100\n#define EMBEDDED\n#define target gl_FragColor\n#define texture texture2D\nprecision mediump float;\n" + sourceStr;
#endif
			const GLint programsize = sourceStr.size();

			/*
				The first line decides which GLSL version the compiler parses the shader
				as, and a stray byte in front of "#version" silently downgrades the whole
				file to ES 1.00 - which shows up as unrelated errors like "does not
				operate on float and int". Printing the head of the source and the decision
				makes that visible without a debugger.
			*/
			/*
				Dump the rewritten source line by line. An earlier revision logged only
				the first bytes, which showed #version in the right place while the real
				error was a mangled line further down ("ut : syntax error" from an "out"
				that had lost its first character). Carriage returns are spelled out so
				CRLF artefacts are visible, and the numbering matches what the GLSL
				compiler reports.
			*/
			{
				Logf("Shader source for %s (vertex=%d), %d bytes:", Logger::Severity::Info,
					m_sourcePath, (int)(m_type == ShaderType::Vertex), (int)sourceStr.size());

				int lineNumber = 1;
				String line;
				for(size_t i = 0; i <= sourceStr.size(); i++)
				{
					const bool atEnd = (i == sourceStr.size());
					const char c = atEnd ? '\n' : sourceStr[i];
					if(c == '\n' || atEnd)
					{
						Logf("  %3d| %s", Logger::Severity::Info, lineNumber, line);
						lineNumber++;
						line.clear();
						if(atEnd)
							break;
					}
					else if(c == '\r')
					{
						line += "<CR>";
					}
					else
					{
						line += c;
					}
				}
			}

			const char* pChars = *sourceStr;
			glShaderSource(programOut, 1, &pChars, &programsize);
			glCompileShader(programOut);

			int nStatus = 0;
			glGetShaderiv(programOut, GL_COMPILE_STATUS, &nStatus);
			if(nStatus == GL_FALSE)
			{
				static char infoLogBuffer[2048];
				int s = 0;
				glGetShaderInfoLog(programOut, sizeof(infoLogBuffer), &s, infoLogBuffer);

				Logf("Shader program compile log for %s: %s", Logger::Severity::Error, m_sourcePath, infoLogBuffer);
				return false;
			}

			// Shader hot-reload in debug mode
#if defined(_DEBUG) && defined(_WIN32)
			// Store last write time
			m_lwt = in.GetLastWriteTime();
			SetupChangeHandler();
#endif
			return true;
		}
#else
		
		bool LoadProgram(uint32& programOut)
		{
			File in;
			if(!in.OpenRead(m_sourcePath))
				return false;

			String sourceStr;
			sourceStr.resize(in.GetSize());
			if(sourceStr.size() == 0)
				return false;

			in.Read(&sourceStr.front(), sourceStr.size());
			String firstLine;
			sourceStr.Split("\n", &firstLine, nullptr);
			firstLine.Trim('\r');
			firstLine.ToLower();
			if (firstLine.compare("#version 330") != 0)
			{
				sourceStr = "#version 330\n" + sourceStr;
			}
			const char* pChars = *sourceStr;
			programOut = glCreateShaderProgramv(typeMap[(size_t)m_type], 1, &pChars);
			if(programOut == 0)
				return false;

			int nStatus = 0;
			glGetProgramiv(programOut, GL_LINK_STATUS, &nStatus);
			if(nStatus == 0)
			{
				static char infoLogBuffer[2048];
				int s = 0;
				glGetProgramInfoLog(programOut, sizeof(infoLogBuffer), &s, infoLogBuffer);

				Logf("Shader program compile log for %s: %s", Logger::Severity::Error, m_sourcePath, infoLogBuffer);
				return false;
			}

			// Shader hot-reload in debug mode
#if defined(_DEBUG) && defined(_WIN32)
			// Store last write time
			m_lwt = in.GetLastWriteTime();
			SetupChangeHandler();
#endif
			return true;
		}
		
#endif

		bool UpdateHotReload() override
		{
#ifdef _WIN32
			if(m_changeNotification != INVALID_HANDLE_VALUE)
			{
				if(WaitForSingleObject(m_changeNotification, 0) == WAIT_OBJECT_0)
				{
					uint64 newLwt = File::GetLastWriteTime(m_sourcePath);
					if(newLwt != -1 && newLwt > m_lwt)
					{
						uint32 newProgram = 0;
						if(LoadProgram(newProgram))
						{
							// Successfully reloaded
							m_prog = newProgram;
							return true;
						}
					}

					// Watch for new change
					SetupChangeHandler();
				}
			}
#endif
			return false;
		}

		bool Init(ShaderType type, const String& name)
		{
			m_sourcePath = Path::Normalize(name);
			m_type = type;
			
			#ifdef EMBEDDED
			m_prog = glCreateShader(typeMap[(size_t)type]);
			#endif
			
			return LoadProgram(m_prog);
		}
#ifndef EMBEDDED
		void Bind() override
		{
			if(m_gl->m_activeShaders[(size_t)m_type] != this)
			{
				glUseProgramStages(m_gl->m_mainProgramPipeline, shaderStageMap[(size_t)m_type], m_prog);
				m_gl->m_activeShaders[(size_t)m_type] = this;
			}
		}
		bool IsBound() const override
		{
			return m_gl->m_activeShaders[(size_t)m_type] == this;
		}
		uint32 GetLocation(const String& name) const override
		{
			return glGetUniformLocation(m_prog, name.c_str());
		}
		virtual void BindUniform(uint32 loc, const Transform& mat)
		{
			glProgramUniformMatrix4fv(m_prog, loc, 1, false, mat.mat);
		}
		virtual void BindUniformVec2(uint32 loc, const Vector2& v)
		{
			glProgramUniform2fv(m_prog, loc, 1, &v.x);
		}
		virtual void BindUniformVec3(uint32 loc, const Vector3& v)
		{
			glProgramUniform3fv(m_prog, loc, 1, &v.x);
		}
		virtual void BindUniformVec4(uint32 loc, const Vector4& v)
		{
			glProgramUniform4fv(m_prog, loc, 1, &v.x);
		}
		virtual void BindUniform(uint32 loc, int i)
		{
			glProgramUniform1i(m_prog, loc, i);
		}
		virtual void BindUniform(uint32 loc, float i)
		{
			glProgramUniform1f(m_prog, loc, i);
		}
		virtual void BindUniformArray(uint32 loc, const Transform* mat, size_t count)
		{
			glProgramUniformMatrix4fv(m_prog, loc, (int)count, false, (float*)mat);
		}
		virtual void BindUniformArray(uint32 loc, const Vector2* v2, size_t count)
		{
			glProgramUniform2fv(m_prog, loc, (int)count, (float*)v2);
		}
		virtual void BindUniformArray(uint32 loc, const Vector3* v3, size_t count)
		{
			glProgramUniform3fv(m_prog, loc, (int)count, (float*)v3);
		}
		virtual void BindUniformArray(uint32 loc, const Vector4* v4, size_t count)
		{
			glProgramUniform4fv(m_prog, loc, (int)count, (float*)v4);
		}
		virtual void BindUniformArray(uint32 loc, const float* i, size_t count)
		{
			glProgramUniform1fv(m_prog, loc, (int)count, i);
		}
		virtual void BindUniformArray(uint32 loc, const int* i, size_t count)
		{
			glProgramUniform1iv(m_prog, loc, (int)count, i);
		}
#endif
		virtual uint32 Handle() override
		{
			return m_prog;
		}

		String GetOriginalName() const override
		{
			return m_sourcePath;
		}
	};

	Shader ShaderRes::Create(class OpenGL* gl, ShaderType type, const String& assetPath)
	{
		Shader_Impl* pImpl = new Shader_Impl(gl);
		if(!pImpl->Init(type, assetPath))
		{
			delete pImpl;
			return Shader();
		}
		else
		{
			return GetResourceManager<ResourceType::Shader>().Register(pImpl);
		}
	}
	void ShaderRes::Unbind(class OpenGL* gl, ShaderType type)
	{
		#ifndef EMBEDDED
		if(gl->m_activeShaders[(size_t)type] != 0)
		{
			glUseProgramStages(gl->m_mainProgramPipeline, shaderStageMap[(size_t)type], 0);
			gl->m_activeShaders[(size_t)type] = 0;
		}
		#endif
	}
}
