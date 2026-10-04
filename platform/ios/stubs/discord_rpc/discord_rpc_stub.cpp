#include "discord_rpc.h"

#include <cstdio>

/*
	No-op discord-rpc implementation for iPadOS. Every entry point logs once at
	startup so it is obvious in the log that rich presence is unavailable, but the
	game keeps running normally.
*/

namespace
{
	void LogOnce(const char* message)
	{
		static bool logged = false;
		if(logged)
			return;
		logged = true;
		std::fputs(message, stderr);
		std::fputc('\n', stderr);
	}
}

void Discord_Initialize(const char* applicationId, DiscordEventHandlers* handlers, int autoRegister, const char* optionalSteamId)
{
	LogOnce("Discord RPC is not available on this platform, rich presence is disabled.");
}

void Discord_Shutdown(void)
{
}

void Discord_RunCallbacks(void)
{
}

void Discord_UpdatePresence(const DiscordRichPresence* presence)
{
}

void Discord_ClearPresence(void)
{
}

void Discord_UpdateHandlers(DiscordEventHandlers* handlers)
{
}
