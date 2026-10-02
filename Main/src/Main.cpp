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
// Linux and iOS entry point.
//
// On iOS SDL2 redefines main() to SDL_main and bootstraps it from UIApplicationMain
// through its own UIKit app delegate, so this function must NOT be hidden behind
// SDL_MAIN_HANDLED (that define is only used by the console test targets).
//
// SDL_main.h declares SDL_main() as extern "C" and SDL's UIKit delegate calls it
// through a plain function pointer, so the definition has to be extern "C" as
// well: without it the compiler emits a C++ mangled symbol
// (__Z8SDL_main...) and the link fails with '_SDL_main not found'.
#ifdef USC_IOS
extern "C"
#endif
int main(int argc, char** argv)
{
#ifdef USC_IOS
	// Must happen before the SDL window is created.
	iOSPlatform::Init();
#endif
	new Application();
	g_application->SetCommandLine(argc, argv);
	int32 ret = g_application->Run();
	delete g_application;
	return ret;
}
#endif
