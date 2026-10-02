#pragma once

#include <Shared/String.hpp>

/*
	iPadOS/iOS platform helpers.

	This is the only place in the port that talks to UIKit/Foundation directly;
	everything else keeps using the engine's portable Shared/ abstractions.
*/
namespace iOSPlatform
{
	/*
		Must be called once at the very start of main(), before SDL initializes its
		video subsystem. It installs the SDL hints that decide how the app handles
		orientation changes and whether SDL turns touches into mouse events
		(the game does that itself so that dragging the playfield cannot move the
		lasers by accident).
	*/
	void Init();

	// Absolute path of the app sandbox "Documents" folder (user visible through the Files app).
	// Returned without a trailing separator.
	String GetDocumentsPath();

	// Absolute path of the folder the game treats as its writable game directory.
	// This is Documents/<kGameDataFolderName>; created if it does not exist.
	String GetGameDataPath();

	// Absolute path of the read-only resources embedded in the .app bundle.
	String GetBundleResourcesPath();

	// Absolute path of the .app bundle itself.
	String GetBundlePath();

	// Copies the read-only game data shipped with the app (skins/, fonts/, audio/, ...)
	// into the writable game directory. Files that already exist are left untouched so
	// user modifications and downloaded skins survive app updates.
	// Returns false when a required source folder is missing from the bundle.
	bool InstallGameDataIfNeeded(const String& gameDataPath);

	// Stops the screen from dimming/locking while the player is inside a song.
	void SetIdleTimerDisabled(bool disabled);

	// The game is designed for landscape; portrait is only used by a few skins.
	void SetLandscapeOnly(bool landscapeOnly);

	// True when running on real hardware instead of the simulator.
	bool IsPhysicalDevice();

	// Native scale of the main screen (2.0 for most iPads, 3.0 for iPhone-class displays).
	float GetNativeScreenScale();

	// Name of the folder inside Documents that holds all game data.
	extern const char* const kGameDataFolderName;
}
