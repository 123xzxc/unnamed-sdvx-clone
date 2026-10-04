#pragma once
/*
	Minimal stand-in for discord-rpc on iPadOS.

	Discord IPC is a Windows/macOS desktop feature. The stub keeps the rich
	presence calls in Application.cpp compiling and turns them into no-ops so the
	rest of the engine stays untouched.
*/

#include <cstdint>

typedef struct DiscordRichPresence
{
	const char* state;
	const char* details;
	int64_t startTimestamp;
	int64_t endTimestamp;
	const char* largeImageKey;
	const char* largeImageText;
	const char* smallImageKey;
	const char* smallImageText;
	const char* partyId;
	int partySize;
	int partyMax;
	const char* matchSecret;
	const char* joinSecret;
	const char* spectateSecret;
	int8_t instance;
} DiscordRichPresence;

typedef struct DiscordUser
{
	const char* userId;
	const char* username;
	const char* discriminator;
	const char* avatar;
} DiscordUser;

typedef struct DiscordEventHandlers
{
	void (*ready)(const DiscordUser* request);
	void (*disconnected)(int errorCode, const char* message);
	void (*errored)(int errorCode, const char* message);
	void (*joinGame)(const char* joinSecret);
	void (*spectateGame)(const char* spectateSecret);
	void (*joinRequest)(const DiscordUser* request);
} DiscordEventHandlers;

void Discord_Initialize(const char* applicationId, DiscordEventHandlers* handlers, int autoRegister, const char* optionalSteamId);
void Discord_Shutdown(void);
void Discord_RunCallbacks(void);
void Discord_UpdatePresence(const DiscordRichPresence* presence);
void Discord_ClearPresence(void);
void Discord_UpdateHandlers(DiscordEventHandlers* handlers);
