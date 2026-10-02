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
*/

#include <SDL2/SDL.h>

#if defined(__IPHONEOS__)

extern int SDL_main(int argc, char* argv[]);

int main(int argc, char* argv[])
{
	return SDL_UIKitRunApp(argc, argv, SDL_main);
}

#endif
