#include "stdafx.h"
#include "Application.hpp"

#ifdef USC_IOS
#include "iOSPlatform.h"
#endif

#ifdef _WIN32
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

#ifdef USC_IOS
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
	but only for translation units that include SDL.h. This file does not - it
	uses the SDL platform headers directly - so both functions are defined with
	their real names here and the renaming macro is never involved.

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

	new Application();
	g_application->SetCommandLine(argc, argv);
	int32 ret = g_application->Run();
	delete g_application;
	return ret;
}

int main(int argc, char* argv[])
{
	return SDL_UIKitRunApp(argc, argv, SDL_main);
}
#endif
