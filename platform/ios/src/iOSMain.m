/*
	iPadOS/iOS entry point.

	SDL2 does not export a main() from its static library: upstream builds it into
	libSDL2main.a from src/main/uikit/SDL_uikit_main.c, but the vcpkg package of
	SDL2 only ships libSDL2.a, which leaves '_main' undefined. This file contains
	the same two lines that SDL's stub does, compiled against the SDL2 headers that
	are available either way.

	SDL_main is the application's main() from Main/src/Main.cpp: SDL_main.h renames
	it through a macro (SDL_MAIN_AVAILABLE is defined for the iOS target), and
	Main.cpp includes SDL.h through its precompiled header.

	SDL.h (and therefore SDL_main.h) includes nothing but SDL_stdinc.h when
	SDL_MAIN_HANDLED is defined, no matter which platform headers are available.
	That is exactly what this file needs, because the macro would otherwise rename
	the main() defined below to SDL_main as well; SDL_MAIN_HANDLED is defined at
	the top of this file only, so the rest of the application keeps its SDL_main
	alias. SDL_UIKitRunApp comes from the same header and is unaffected.
*/

/* SDL_UIKitRunApp is the only SDL symbol that is needed here, so the platform
   headers are included on their own instead of through SDL.h (which also pulls
   in SDL_main.h and its "main is SDL_main" macro). */
#include <SDL2/SDL_main.h>
#include <SDL2/SDL_stdinc.h>

#if defined(__IPHONEOS__)

extern int SDL_main(int argc, char* argv[]);

int main(int argc, char* argv[])
{
	return SDL_UIKitRunApp(argc, argv, SDL_main);
}

#endif
