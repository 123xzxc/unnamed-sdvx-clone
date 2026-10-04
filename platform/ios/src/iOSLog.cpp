#include "iOSPlatform.h"

#include <Shared/Log.hpp>
#include <Shared/Path.hpp>
#include <Shared/String.hpp>

#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <mutex>

/*
	Persistent log sink for the iOS build.

	The engine already has a Logger that writes log_<module>.txt next to the
	executable, but that path is decided in the Logger constructor, which runs
	during static initialization - before Application has worked out
	Path::gameDir. On iOS it therefore resolves to a folder inside the read-only
	.app bundle, every write fails and nothing is ever recorded.

	This file is the iOS specific fix: it opens a log in the writable game
	directory (the same place songs and skins live, visible in the Files app and
	reachable over the Finder / iTunes file sharing), and every Log() / Logf()
	call in the engine is mirrored into it.

	Design notes:
	- Appending, never truncating, so the log survives a crash mid-session and
	  three re-launches can be compared against each other.
	- Flushed (fsync) after every line. A bug that kills the process before the
	  next frame would otherwise lose exactly the lines that matter.
	- A file that grows past kMaxLogBytes is rotated to <name>.1 first.
	- The path is fixed at first use: Application sets Path::gameDir very early,
	  and until then lines are buffered in memory and flushed as soon as the
	  destination is known, so nothing that happens during start-up is lost.
*/

namespace
{
	const char* const kLogFileName = "usc-ios.log";
	const size_t kMaxLogBytes = 4u * 1024u * 1024u;

	std::mutex g_logLock;
	FILE* g_logFile = nullptr;
	bool g_openAttempted = false;
	String g_logPath;
	// Lines produced before the file is open. Bounded so a broken start-up cannot
	// eat memory forever.
	String g_pending;
	const size_t kMaxPendingBytes = 64u * 1024u;

	const char* SeverityName(Logger::Severity severity)
	{
		switch(severity)
		{
		case Logger::Severity::Debug:   return "DEBUG";
		case Logger::Severity::Info:    return "INFO";
		case Logger::Severity::Normal:  return "NORMAL";
		case Logger::Severity::Warning: return "WARN";
		case Logger::Severity::Error:   return "ERROR";
		}
		return "?";
	}

	// Writes what is left over from start-up once the file is available.
	void FlushPending()
	{
		if(!g_logFile || g_pending.empty())
			return;
		fwrite(g_pending.c_str(), 1, g_pending.size(), g_logFile);
		g_pending.clear();
	}

	bool OpenLocked()
	{
		if(g_openAttempted)
			return g_logFile != nullptr;
		g_openAttempted = true;

		// Path::gameDir is set by Application as soon as it can; fall back to the
		// raw Documents folder so even a failure before that still logs somewhere
		// the user can reach.
		String dir = Path::gameDir;
		if(dir.empty())
		{
			dir = iOSPlatform::GetGameDataPath();
			if(dir.empty())
				dir = iOSPlatform::GetDocumentsPath();
		}
		if(dir.empty())
			return false;

		if(!Path::IsDirectory(dir))
			Path::CreateDirRecursive(dir);

		g_logPath = dir + Path::sep + kLogFileName;

		// Keep the previous session instead of overwriting it: the interesting
		// crash is usually in the run before the one being investigated.
		struct stat st;
		if(::stat(g_logPath.c_str(), &st) == 0 && (size_t)st.st_size > kMaxLogBytes)
		{
			String old = g_logPath + ".1";
			Path::Delete(old);
			Path::Rename(g_logPath, old, true);
		}

		g_logFile = fopen(g_logPath.c_str(), "ab");
		if(!g_logFile)
			return false;

		FlushPending();
		return true;
	}
}

void iOSLog::WriteLine(Logger::Severity severity, const char* message)
{
	if(!message)
		message = "";

	char timeStr[32];
	time_t now = time(0);
	tm local;
	localtime_r(&now, &local);
	strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &local);

	/*
		Built by hand instead of with Utility::Sprintf: that helper formats through a
		function-local static buffer, and this sink is called from the audio thread,
		the HTTP worker and the main thread, which would race on it.
	*/
	char line[4096];
	snprintf(line, sizeof(line), "[%s][%s] %s\n", timeStr, SeverityName(severity), message);
	const String utf8(line);

	std::lock_guard<std::mutex> guard(g_logLock);

	if(!g_openAttempted)
		OpenLocked();

	if(!g_logFile)
	{
		// Not open yet (or the sandbox refused): hold on to the text so it still
		// reaches the file once the directory is known.
		if(g_pending.size() < kMaxPendingBytes)
			g_pending += utf8;
		return;
	}

	fwrite(utf8.c_str(), 1, utf8.size(), g_logFile);
	fflush(g_logFile);
	// fsync keeps the lines on disk even when the process is killed by a crash or
	// by the watchdog instead of exiting normally. It costs a little, but a log
	// that loses the crash is worthless.
	::fsync(fileno(g_logFile));
}

String iOSLog::GetPath()
{
	std::lock_guard<std::mutex> guard(g_logLock);
	return g_logPath;
}

int iOSLog::GetFileDescriptor()
{
	// Deliberately lock-free: this is called from the crash handler, where taking a
	// mutex could deadlock against a thread that was killed while holding it.
	if(!g_logFile)
		return -1;
	return fileno(g_logFile);
}