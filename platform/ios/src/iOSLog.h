#pragma once

#include <Shared/Log.hpp>

/*
	Persistent log sink for the iOS build.

	The engine's own Logger decides its file name during static initialization,
	before the game directory is known, so on iOS it ends up pointing inside the
	read-only .app bundle and never records anything. This header exposes the
	replacement sink that writes into the writable game directory instead.

	iOSPlatform installs it as a Logger observer from Init(), so every Log() and
	Logf() call in the engine is mirrored to that file with no call site
	changes.
*/
namespace iOSLog
{
	// Appends one already formatted line to usc-ios.log in the game directory,
	// creating the directory and rotating the file as needed.
	void WriteLine(Logger::Severity severity, const char* message);

	// Absolute path of the log file, empty until it has been opened.
	String GetPath();

	/*
		Raw file descriptor of the open log, or -1 when it is not open yet. The
		crash handler writes to it directly because only async-signal-safe calls may
		run inside a signal handler.
	*/
	int GetFileDescriptor();
}