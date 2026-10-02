/*
	iPadOS/iOS entry point.

	SDL2 does not export a main() from its static library: upstream builds it into
	libSDL2main.a from src/main/uikit/SDL_uikit_main.c, but the vcpkg package of
	SDL2 only ships libSDL2.a, which leaves '_main' undefined. This file contains
	the same two lines that SDL's stub does, compiled against the SDL2 headers.

	The application's own main() in Main/src/Main.cpp is renamed to SDL_main() by
	the macro in SDL_main.h (SDL_MAIN_AVAILABLE is defined for the iOS target), and
	SDL's UIKit delegate calls it through UIApplicationMain.

	SDL_Main.h must not be included through SDL.h here: SDL.h always includes
	SDL_main.h, and its "main is SDL_main" macro would rename the main() below as
	well, leaving the binary without a '_main' symbol again. Only the two platform
	headers that are actually needed (SDL_UIKitRunApp and the basic types) are
	included, and the declaration of SDL_main is spelled out instead.
*/

#include <SDL2/SDL_main.h>
#include <SDL2/SDL_stdinc.h>

#if defined(__IPHONEOS__)

extern int SDL_main(int argc, char* argv[]);

int main(int argc, char* argv[])
{
	return SDL_UIKitRunApp(argc, argv, SDL_main);
}

#endif
