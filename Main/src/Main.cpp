#include "stdafx.h"
#include "Application.hpp"

#ifdef USC_IOS
#include "iOSPlatform.h"
#include "iOSLog.h"

#include <cstdlib>
#include <csignal>
#include <exception>
#include <unistd.h>

namespace
{
	/*
		Hard crashes (null dereference, bad memory access) are the ones that used to
		leave nothing behind at all. The log is written with fsync after every line,
		so by the time a signal arrives the lines leading up to it are already on
		disk; printing the signal name adds the reason.

		Only async-signal-safe calls are made here: write() straight to the log file
		descriptor, with no allocation or locking.
	*/
	void CrashHandler(int sig)
	{
		int fd = iOSLog::GetFileDescriptor();
		if(fd >= 0)
		{
			const char* name = "unknown";
			switch(sig)
			{
			case SIGSEGV: name = "SIGSEGV (invalid memory access)"; break;
			case SIGBUS:  name = "SIGBUS (bad memory alignment)"; break;
			case SIGABRT: name = "SIGABRT (abort)"; break;
			case SIGFPE:  name = "SIGFPE (arithmetic error)"; break;
			case SIGILL:  name = "SIGILL (illegal instruction)"; break;
			}
			const char* prefix = "\n[FATAL] crashed with ";
			ssize_t ignored = ::write(fd, prefix, __builtin_strlen(prefix));
			ignored = ::write(fd, name, __builtin_strlen(name));
			ignored = ::write(fd, "\n", 1);
			(void)ignored;
		}
		// Restore the default handler and re-raise so the system still produces its
		// own crash report and the process terminates with the correct status.
		signal(sig, SIG_DFL);
		raise(sig);
	}

	void InstallCrashHandlers()
	{
		signal(SIGSEGV, CrashHandler);
		signal(SIGBUS, CrashHandler);
		signal(SIGFPE, CrashHandler);
		signal(SIGILL, CrashHandler);
	}
}
#endif

#if defined(_WIN32)
// Windows entry point
int32 __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	new Application();

	String commandLine = Utility::ConvertToUTF8(GetCommandLineW());
	g_application->SetCommandLine(*commandLine);

	int32 ret = g_application->Run();
	delete g_application;
	return ret;
}
#elif defined(USC_IOS)
/*
	iPadOS/iOS entry point.

	SDL2 does not provide a main() for this build: upstream compiles one into
	libSDL2main.a (src/main/uikit/SDL_uikit_main.c), but the vcpkg package of SDL2
	only ships libSDL2.a. SDL's UIKit application delegate still calls a function
	named SDL_main through UIApplicationMain, so the application has to provide
	both halves itself:

	  * SDL_main() with C linkage, because that is how SDL_main.h declares it and
	    SDL_UIKitRunApp takes a plain function pointer, and
	  * main(), which is what the linker and UIApplicationMain look for.

	SDL.h (through SDL_main.h) normally renames main() to SDL_main with a macro,
	so the macro is dropped right below and both functions keep their real names.

	iOSPlatform::Init() has to run before SDL creates its window.
*/
#include <SDL2/SDL_main.h>

// stdafx.h pulls in SDL.h, whose SDL_main.h defines "main" as "SDL_main" for
// every translation unit. The two functions below need their real names, so the
// macro is dropped for the rest of this file (the guarded #ifndef also covers a
// future SDL that stops defining it).
#ifdef main
#undef main
#endif

extern "C" int SDL_main(int argc, char* argv[])
{
	iOSPlatform::Init();
	InstallCrashHandlers();

	/*
		A throwing tickable would otherwise unwind into SDL's UIKit delegate and
		terminate the process with nothing on record. Catching here puts the reason
		in usc-ios.log, which is the only diagnostic channel the device build has.
	*/
	try
	{
		new Application();
		g_application->SetCommandLine(argc, argv);
		int32 ret = g_application->Run();
		delete g_application;
		return ret;
	}
	catch(const std::exception& e)
	{
		Logf("FATAL: unhandled exception: %s", Logger::Severity::Error, e.what());
	}
	catch(...)
	{
		Log("FATAL: unhandled non-standard exception", Logger::Severity::Error);
	}

	return 1;
}

int main(int argc, char* argv[])
{
	return SDL_UIKitRunApp(argc, argv, SDL_main);
}

#else
// Linux entry point.
int main(int argc, char** argv)
{
	new Application();
	g_application->SetCommandLine(argc, argv);
	int32 ret = g_application->Run();
	delete g_application;
	return ret;
}
#endif
