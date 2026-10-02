#include "iOSPlatform.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <dispatch/dispatch.h>

#include <SDL2/SDL.h>

#include <Shared/Path.hpp>
#include <Shared/Files.hpp>
#include <Shared/Log.hpp>

const char* const iOSPlatform::kGameDataFolderName = "unnamed-sdvx-clone";

namespace
{
	// Folders that are shipped read-only inside the .app and are copied into the
	// writable game directory the first time the game starts. "LightPlugins" is
	// deliberately absent: it only contains a Windows DLL and dlopen() is not a
	// supported way to load code on iOS.
	const char* const kDataFoldersToInstall[] =
	{
		"skins",
		"fonts",
		"audio",
	};

	NSString* ToNSString(const String& str)
	{
		return [NSString stringWithUTF8String:str.c_str()];
	}

	String ToString(NSString* str)
	{
		if(!str)
			return String();
		return String([str UTF8String]);
	}

	// Recursively copies src into dst. Existing files are never overwritten.
	// Returns the number of files that were copied.
	uint32 CopyTreeIfMissing(NSString* src, NSString* dst)
	{
		NSFileManager* fm = [NSFileManager defaultManager];
		BOOL isDir = NO;
		if(![fm fileExistsAtPath:src isDirectory:&isDir] || !isDir)
			return 0;

		NSError* error = nil;
		if(![fm fileExistsAtPath:dst])
		{
			if(![fm createDirectoryAtPath:dst withIntermediateDirectories:YES attributes:nil error:&error])
			{
				Logf("iOS: failed to create \"%s\": %s", Logger::Severity::Error,
					 ToString(dst), ToString([error localizedDescription]));
				return 0;
			}
		}

		uint32 copied = 0;
		NSArray<NSString*>* entries = [fm contentsOfDirectoryAtPath:src error:&error];
		for(NSString* entry in entries)
		{
			NSString* srcPath = [src stringByAppendingPathComponent:entry];
			NSString* dstPath = [dst stringByAppendingPathComponent:entry];

			BOOL entryIsDir = NO;
			if(![fm fileExistsAtPath:srcPath isDirectory:&entryIsDir])
				continue;

			if(entryIsDir)
			{
				copied += CopyTreeIfMissing(srcPath, dstPath);
			}
			else if(![fm fileExistsAtPath:dstPath])
			{
				if([fm copyItemAtPath:srcPath toPath:dstPath error:&error])
					copied++;
				else
					Logf("iOS: failed to copy \"%s\": %s", Logger::Severity::Warning,
						 ToString(srcPath), ToString([error localizedDescription]));
			}
		}
		return copied;
	}
}

String iOSPlatform::GetDocumentsPath()
{
	NSArray<NSString*>* paths =
		NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
	if([paths count] == 0)
		return String();
	return Path::Normalize(ToString([paths objectAtIndex:0]));
}

String iOSPlatform::GetBundleResourcesPath()
{
	return Path::Normalize(ToString([[NSBundle mainBundle] resourcePath]));
}

String iOSPlatform::GetBundlePath()
{
	return Path::Normalize(ToString([[NSBundle mainBundle] bundlePath]));
}

void iOSPlatform::Init()
{
	// SDL would otherwise translate every touch into a mouse event. During play the
	// player drags the playfield, which would then be fed into the mouse laser
	// device and move the lasers. The on-screen controls inject the mouse events the
	// menus need explicitly instead.
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

	// The game is a landscape experience; portrait skins are handled by calling
	// SetLandscapeOnly(false) at runtime.
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
	SDL_SetHint(SDL_HINT_APP_NAME, "USC-Game");

	// Fullscreen game: get the home indicator out of the way.
	SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");

	SetIdleTimerDisabled(true);
}

String iOSPlatform::GetGameDataPath()
{
	String docs = GetDocumentsPath();
	if(docs.empty())
		return String();

	String gameDataPath = docs + Path::sep + kGameDataFolderName;
	if(!Path::IsDirectory(gameDataPath))
	{
		Logf("iOS: creating game data directory \"%s\"", Logger::Severity::Info, gameDataPath);
		Path::CreateDirRecursive(gameDataPath);
	}
	return gameDataPath;
}

bool iOSPlatform::InstallGameDataIfNeeded(const String& gameDataPath)
{
	if(gameDataPath.empty() || !Path::IsDirectory(gameDataPath))
	{
		Log("iOS: game data directory is not available", Logger::Severity::Error);
		return false;
	}

	NSString* resources = ToNSString(GetBundleResourcesPath());
	NSString* target = ToNSString(gameDataPath);

	bool allFoldersPresent = true;
	uint32 copied = 0;
	for(const char* folder : kDataFoldersToInstall)
	{
		NSString* src = [resources stringByAppendingPathComponent:ToNSString(String(folder))];
		BOOL isDir = NO;
		if(![[NSFileManager defaultManager] fileExistsAtPath:src isDirectory:&isDir] || !isDir)
		{
			Logf("iOS: bundled data folder \"%s\" is missing", Logger::Severity::Error,
				 String(folder));
			allFoldersPresent = false;
			continue;
		}
		copied += CopyTreeIfMissing(src, [target stringByAppendingPathComponent:ToNSString(String(folder))]);
	}

	if(copied > 0)
		Logf("iOS: installed %u bundled data files into the game directory", Logger::Severity::Info, copied);
	return allFoldersPresent;
}

void iOSPlatform::SetIdleTimerDisabled(bool disabled)
{
	dispatch_async(dispatch_get_main_queue(), ^{
		[UIApplication sharedApplication].idleTimerDisabled = disabled ? YES : NO;
	});
}

void iOSPlatform::SetLandscapeOnly(bool landscapeOnly)
{
	// SDL2 reads this hint from its UIKit application delegate, so changing it is
	// enough to make new geometry requests use the requested orientation set.
	SDL_SetHint(SDL_HINT_ORIENTATIONS, landscapeOnly ? "LandscapeLeft LandscapeRight" : "Portrait");

	if(@available(iOS 16.0, *))
	{
		UIWindowScene* scene = nil;
		for(UIScene* candidate in [UIApplication sharedApplication].connectedScenes)
		{
			if([candidate isKindOfClass:[UIWindowScene class]])
			{
				scene = (UIWindowScene*)candidate;
				break;
			}
		}
		if(!scene)
			return;

		UIInterfaceOrientationMask mask = landscapeOnly
			? UIInterfaceOrientationMaskLandscape
			: UIInterfaceOrientationMaskPortrait;

		UIWindowSceneGeometryPreferencesIOS* prefs =
			[[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:mask];

		dispatch_async(dispatch_get_main_queue(), ^{
			[scene requestGeometryUpdateWithPreferences:prefs errorHandler:^(NSError* error) {
				Logf("iOS: orientation update failed: %s", Logger::Severity::Warning,
					 ToString([error localizedDescription]));
			}];
		});
	}
}

bool iOSPlatform::IsPhysicalDevice()
{
#if TARGET_OS_SIMULATOR
	return false;
#else
	return true;
#endif
}

float iOSPlatform::GetNativeScreenScale()
{
	return (float)[UIScreen mainScreen].nativeScale;
}
