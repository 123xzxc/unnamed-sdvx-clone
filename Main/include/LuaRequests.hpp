#pragma once

struct AsyncRequest
{
	struct lua_State* L;
	cpr::AsyncResponse r;
	// The requested URL. cpr only fills Response::url on some builds (the iOS
	// stub leaves it empty), so the scripts are handed the URL they asked for
	// instead of relying on the response.
	String url;
	int callback;

	// url is optional: requests that are not tied to a user visible message
	// (IR heartbeat/score uploads) leave it empty.
	AsyncRequest(struct lua_State* luaState, cpr::AsyncResponse asyncResponse, int callback,
		String url = "")
		: L(luaState), r(std::move(asyncResponse)), url(url), callback(callback)
	{
	}
};

struct CompleteRequest
{
	struct lua_State* L;
	cpr::Response r;
	String url;
	int callback;
};
