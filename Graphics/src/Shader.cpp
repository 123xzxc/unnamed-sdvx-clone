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
	String WidenFloatConstants(const String& line);

	/*
		Desktop GLSL accepts a trailing "f" on floating constants (1.0f, 0.5f);
		GLSL ES 3.00 rejects it with an invalid-suffix error. The suffix carries no
		meaning, so it is dropped wherever it follows a numeric literal. Identifiers
		that merely end in f (a variable named "f") are not touched because a number
		must appear immediately before it.
	*/
	String StripFloatSuffix(const String& line)
	{
		String out;
		out.reserve(line.size());

		for(size_t i = 0; i < line.size(); )
		{
			const char c = line[i];
			if(c != 'f' && c != 'F')
			{
				out += c;
				i++;
				continue;
			}

			// A suffix only when the previous character belongs to a numeric literal:
			// a digit, or the dot/exponent of one such as 1. or 1e5.
			const char prev = (i > 0) ? line[i - 1] : '\0';
			const bool afterNumber = (prev >= '0' && prev <= '9') || prev == '.';
			// A hex digit or an identifier character means this is part of a name.
			const bool inWord = (i + 1 < line.size()) &&
				((line[i + 1] >= 'a' && line[i + 1] <= 'z') ||
				 (line[i + 1] >= 'A' && line[i + 1] <= 'Z') ||
				 (line[i + 1] >= '0' && line[i + 1] <= '9') || line[i + 1] == '_');

			if(afterNumber && !inWord)
				i++;   // drop the suffix
			else
			{
				out += c;
				i++;
			}
		}

		return out;
	}

	String DowngradeDesktopShader(const String& source, bool isVertexShader)
	{
		String out;
		out.reserve(source.size());

		/*
			Vertex inputs are numbered in declaration order, which is exactly the
			order Mesh::SetData() binds the streams in. Most of the shipped vertex
			shaders only declare locations in their desktop branch, so the ES 3.00
			build was relying on the compiler to hand out 0, 1, ... by itself. It
			does, but it is not required to, and a swap of the position and texcoord
			streams is what makes the playfield and the notes come out skewed.
			Every input is given an explicit location here instead.
		*/
		int vertexInputLocation = 0;

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
			else if(trimmed.substr(0, 8).compare("#version") == 0)
			{
				/*
					Some shaders carry their own "#version 330". The loader prepends
					"#version 300 es" for iOS, and a shader may only contain one version
					directive - and it has to be the first thing in the file - so the
					original line is dropped and the prepended one is the only one left.
				*/
			}
			else if(trimmed.substr(0, 7).compare("layout(") == 0 &&
				(trimmed.find(") in ") != String::npos || trimmed.find(") out ") != String::npos))
			{
				/*
					Desktop GLSL pins stage interface locations. GLSL ES 3.00 only allows
					a location on a vertex input (and a fragment output); on a vertex output
					or a fragment input it is rejected outright:

						background.vs:10: Invalid use of layout 'location'   (out vec2 texVp)
						bg.fs:7:         Invalid use of layout 'location'   (in vec2 texVp)

					Vertex inputs are the exception and keep theirs. Mesh::SetData binds the
					streams by index in declaration order, and an unqualified input may be
					given any location by the compiler, so dropping the qualifier here would
					leave the attribute binding to chance. The outputs and fragment inputs
					are matched by name at link time, so only their qualifier is removed and
					the declaration behind it is kept: "layout(location=1) out vec2 texVp;"
					becomes "out vec2 texVp;".

					Dropping the whole line (instead of just the qualifier) is what removed
					the interface declarations and garbled the playfield in the previous
					build.
				*/
				const bool isInput = trimmed.find(") in ") != String::npos;
				if(isInput && isVertexShader)
				{
					// Valid ES 3.00, and needed for the attribute binding. Keep the
					// numbering in step with the inputs that are added below.
					{
						size_t eq = trimmed.find("location=");
						int explicitLoc = 0;
						for(size_t p = (eq == String::npos) ? trimmed.size() : eq + 9;
							p < trimmed.size() && trimmed[p] >= '0' && trimmed[p] <= '9'; p++)
							explicitLoc = explicitLoc * 10 + (trimmed[p] - '0');
						if(eq != String::npos && explicitLoc >= vertexInputLocation)
							vertexInputLocation = explicitLoc + 1;
					}
					out += line;
					if(eol == String::npos)
						break;
					out += '\n';
				}
				else
				{
					size_t close = trimmed.find(") ");
					if(close != String::npos)
					{
						String rest = trimmed.substr(close + 2);
						out += rest;
						if(eol == String::npos)
							break;
						out += '\n';
					}
					else
					{
						out += line;
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
				/*
					A vertex input that reached this branch has no location yet (the
					layout() branch above handles the ones that do), so it gets the next
					one in declaration order. This is what keeps the attribute streams
					lined up with the vertex struct the mesh was uploaded with.
				*/
				String emitted = line;
				if(isVertexShader && trimmed.size() > 3 && trimmed.substr(0, 3).compare("in ") == 0)
				{
					emitted = line.substr(0, indent) +
						Utility::Sprintf("layout(location=%d) ", vertexInputLocation) + trimmed;
					vertexInputLocation++;
				}
				out += StripFloatSuffix(WidenFloatConstants(emitted));
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

	/*
		GLSL ES 3.00 dropped the implicit int-to-float conversion that desktop
		GLSL (and ES 1.00) still allow, so desktop shaders routinely mix the two:

			float speed = 1;        -> Incompatible types in initialization
			cos(rot * 10)           -> no operation '*' on float and int
			y -= 1;                 -> no operation '-' on float and int

		Every int literal that sits in a floating point operation is rewritten as
		a float literal. The scan is deliberately narrow - only literals adjacent
		to an arithmetic operator or used to initialise a float/vec - so that
		genuine integer arithmetic, loop bounds and array indices keep their type.
	*/
	String WidenFloatConstants(const String& line)
	{
		String out;
		out.reserve(line.size() + 8);

		for(size_t i = 0; i < line.size(); )
		{
			const char c = line[i];

			// Skip anything that is not the start of a number.
			const bool startsNumber = (c >= '0' && c <= '9');
			if(!startsNumber)
			{
				out += c;
				i++;
				continue;
			}

			// Do not touch a number that is part of an identifier or a directive.
			if(i > 0)
			{
				const char prev = line[i - 1];
				if((prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z') ||
				   prev == '_' || prev == '#' || (prev >= '0' && prev <= '9'))
				{
					out += c;
					i++;
					continue;
				}
			}

			// Consume the run of digits.
			size_t start = i;
			while(i < line.size() && line[i] >= '0' && line[i] <= '9')
				i++;

			// Already a float (has a dot or an exponent), or an index/number after a
			// dot such as vec2(1.0, 2.0): leave it alone.
			bool isFloat = false;
			if(i < line.size() && line[i] == '.')
				isFloat = true;
			if(i < line.size() && (line[i] == 'e' || line[i] == 'E'))
				isFloat = true;
			// A component swizzle or a cast like "float(N)" must stay an int.
			bool insideCast = false;
			{
				size_t open = line.find_last_of('(', start);
				if(open != String::npos)
				{
					size_t closeBefore = line.find_last_of(')', start);
					if(closeBefore == String::npos || closeBefore < open)
					{
						String callee = line.substr(0, open);
						size_t nameEnd = callee.find_last_of(" \t+-*/,(");
						String name = (nameEnd == String::npos) ? callee : callee.substr(nameEnd + 1);
						if(name.compare("float") == 0 || name.compare("int") == 0 ||
						   name.compare("uint") == 0 || name.compare("vec2") == 0 ||
						   name.compare("vec3") == 0 || name.compare("vec4") == 0 ||
						   name.compare("ivec2") == 0 || name.compare("ivec3") == 0 ||
						   name.compare("ivec4") == 0 || name.compare("mod") == 0 ||
						   name.compare("texture") == 0 || name.compare("texelFetch") == 0)
							insideCast = true;
					}
				}
			}

			const String literal = line.substr(start, i - start);

			if(isFloat || insideCast)
			{
				out += literal;
				continue;
			}

			/*
				An int literal needs a .0 when it shares an expression with a float.
				Rather than type check the expression, look at what surrounds it: an
				operand of an arithmetic operator, an assignment to something that is not
				an int, a comparison, or a function argument that is not one of the
				integer-taking functions above.
			*/
			bool widen = false;

			// Operator immediately before the literal.
			for(size_t j = start; j > 0; )
			{
				const char prev = line[j - 1];
				if(prev == ' ' || prev == '\t')
				{
					j--;
					continue;
				}
				if(prev == '-')
				{
					// Unary minus on a literal: check what is before it as well.
					size_t k = j - 1;
					while(k > 0 && (line[k - 1] == ' ' || line[k - 1] == '\t'))
						k--;
					const char before = (k > 0) ? line[k - 1] : '\0';
					if(before == '=' || before == '(' || before == ',' || before == '\0')
						widen = true;
				}
				else if(prev == '+' || prev == '*' || prev == '/')
				{
					widen = true;
				}
				/*
					Note the deliberate absence of a rule for '='. The type of the
					assignment target is not known here, and widening on '=' would turn
					"int N = 4;" into "int N = 4.0;" - a new error in place of the old one.
					Declarations seeded with a bare int literal are fixed in the shader
					sources instead; this pass only handles literals in arithmetic, which
					is where the float/int mixing actually shows up.
				*/
				break;
			}

			if(widen)
			{
				out += literal;
				out += ".0";
			}
			else
			{
				out += literal;
			}
		}

		return out;
	}

	/*
		Prints a shader exactly as it was handed to the driver, with the same line
		numbering the GLSL compiler uses in its error messages. Carriage returns
		are shown as <CR> because a stray one inside a token is otherwise invisible
		in the log and produces errors that look unrelated to the real mistake.

		Only called when compilation failed, so the log stays readable on a run
		where everything works.
	*/
	void DumpShaderSource(const String& source, const String& path, bool isVertex)
	{
		Logf("Full shader source for %s (vertex=%d), %d bytes:", Logger::Severity::Error,
			path, (int)isVertex, (int)source.size());

		int lineNumber = 1;
		String line;
		for(size_t i = 0; i <= source.size(); i++)
		{
			const bool atEnd = (i == source.size());
			const char c = atEnd ? '\n' : source[i];
			if(c == '\n' || atEnd)
			{
				Logf("  %3d| %s", Logger::Severity::Error, lineNumber, line);
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
			sourceStr = "#version 300 es\n#define EMBEDDED\n#define target target\n#define texture texture\nprecision highp float;\nprecision highp int;\n"
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
				// The compiler reports a line number; print the source with matching
				// numbering so that line can be read without guessing.
				DumpShaderSource(sourceStr, m_sourcePath, m_type == ShaderType::Vertex);
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
				DumpShaderSource(sourceStr, m_sourcePath, m_type == ShaderType::Vertex);
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
