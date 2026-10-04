#include "stdafx.h"
#include "Shared/LuaBindable.hpp"
#include "SkinHttp.hpp"

void SkinHttp::m_requestLoop()
{
	while (m_running)
	{
		m_mutex.lock(); ///TODO: use semaphore?
		if (m_requests.size() > 0)
		{
			//get request
			AsyncRequest* r = m_requests.front();
			m_mutex.unlock();

			//process request
			CompleteRequest cr;
			cr.L = r->L;
			cr.callback = r->callback;
			cr.r = r->r.get();
			cr.url = r->url;

			/*
				Failed requests are invisible on the device: the script only sees a
				non-200 status and the player has to report it by hand. Recording both
				the status and libcurl's error text here makes usc-ios.log the place
				to look when the in-game browser stays empty.
			*/
			if(cr.r.error.code == cpr::ErrorCode::OK && cr.r.status_code < 300)
			{
				Logf("Http: %s -> %d (%d bytes)", Logger::Severity::Info, cr.url,
					cr.r.status_code, (int)cr.r.text.size());
			}
			else
			{
				// cpr::Error::message is a std::string, not the engine's String, so it has to
				// be converted before it can be handed to Sprintf as a %s argument.
				const String errorText(cr.r.error.message.c_str());
				Logf("Http: %s FAILED status=%d error=%s", Logger::Severity::Warning, cr.url,
					cr.r.status_code, errorText);
			}

			//push result and pop request
			m_mutex.lock();
			delete r;
			m_callbackQueue.push(cr);
			m_requests.pop();
			m_mutex.unlock();
		}
		else
		{
			m_mutex.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}
}

//https://stackoverflow.com/a/6142700
cpr::Header SkinHttp::HeaderFromLuaTable(lua_State * L, int index)
{
	cpr::Header ret;
	if (!lua_istable(L, index))
	{
		return ret;
	}
	lua_pushvalue(L, index);
	lua_pushnil(L);
	while (lua_next(L, -2))
	{
		lua_pushvalue(L, -2);
		const char *key = lua_tostring(L, -1);
		const char *value = lua_tostring(L, -2);
		ret[key] = value;
		lua_pop(L, 2);
	}
	lua_pop(L, 1);
	return ret;
}

void SkinHttp::m_PushResponse(lua_State * L, const cpr::Response & r)
{
	auto pushString = [L](String key, String value)
	{
		lua_pushstring(L, *key);
		lua_pushstring(L, *value);
		lua_settable(L, -3);
	};
	auto pushNumber = [L](String key, double value)
	{
		lua_pushstring(L, *key);
		lua_pushnumber(L, value);
		lua_settable(L, -3);
	};


	lua_newtable(L);
	pushString("url", r.url.c_str());
	pushString("text", r.text);
	pushNumber("status", r.status_code);
	pushNumber("elapsed", r.elapsed);
	pushString("error", r.error.message.c_str());
	pushString("cookies", r.cookies.GetEncoded(cpr::CurlHolder()).c_str());
	lua_pushstring(L, "header");
	lua_newtable(L);
	for (auto& i : r.header)
	{
		pushString(i.first, i.second);
	}
	lua_settable(L, -3);

}

SkinHttp::SkinHttp()
{
	m_running = true;
	m_requestThread = Thread(&SkinHttp::m_requestLoop, this);
}

SkinHttp::~SkinHttp()
{
	m_running = false;
	if(m_requestThread.joinable())
		m_requestThread.join();

	while (!m_requests.empty())
	{
		delete m_requests.front();
		m_requests.pop();
	}

	for (auto& s : m_boundStates)
	{
		delete s.second;
	}
	m_boundStates.clear();
}

int SkinHttp::lGetAsync(lua_State * L)
{
	String url = luaL_checkstring(L, 2);
	cpr::Header header = HeaderFromLuaTable(L, 3);
	int callback = luaL_ref(L, LUA_REGISTRYINDEX);
	AsyncRequest* r = new AsyncRequest(L, cpr::GetAsync(cpr::Url{ url }, header), callback, url);
	Logf("Http: GET %s", Logger::Severity::Info, url);
	m_mutex.lock();
	m_requests.push(r);
	m_mutex.unlock();
	return 0;
}

int SkinHttp::lPostAsync(lua_State * L)
{
	String url = luaL_checkstring(L, 2);
	String payload = luaL_checkstring(L, 3);
	cpr::Header header = HeaderFromLuaTable(L, 4);
	int callback = luaL_ref(L, LUA_REGISTRYINDEX);
	AsyncRequest* r = new AsyncRequest(L, cpr::PostAsync(cpr::Url{ url }, cpr::Body{ *payload }, header), callback, url);
	Logf("Http: POST %s", Logger::Severity::Info, url);
	m_mutex.lock();
	m_requests.push(r);
	m_mutex.unlock();
	return 0;
}

int SkinHttp::lGet(lua_State * L)
{
	String url = luaL_checkstring(L, 2);
	cpr::Header header = HeaderFromLuaTable(L, 3);
	auto response = cpr::Get(cpr::Url{ url }, header);
	m_PushResponse(L, response);
	return 1;
}

int SkinHttp::lPost(lua_State * L)
{
	String url = luaL_checkstring(L, 2);
	String payload = luaL_checkstring(L, 3);
	cpr::Header header = HeaderFromLuaTable(L, 4);
	auto response = cpr::Post(cpr::Url{ url }, cpr::Body{ *payload }, header);
	m_PushResponse(L, response);
	return 1;
}

void SkinHttp::ProcessCallbacks()
{
	m_mutex.lock();
	if (m_callbackQueue.size() > 0)
	{
		//get response
		CompleteRequest& cr = m_callbackQueue.front();
		m_mutex.unlock();

		if (m_boundStates.Contains(cr.L))
		{
			//process response
			lua_rawgeti(cr.L, LUA_REGISTRYINDEX, cr.callback);
			m_PushResponse(cr.L, cr.r);
			lua_pushstring(cr.L, *cr.url);
			lua_setfield(cr.L, -2, "requestUrl");
			if (lua_pcall(cr.L, 1, 0, 0) != 0)
			{
				Logf("Lua error on calling http callback: %s", Logger::Severity::Error, lua_tostring(cr.L, -1));
			}
			lua_settop(cr.L, 0);
			luaL_unref(cr.L, LUA_REGISTRYINDEX, cr.callback);
		}
		//pop response
		m_mutex.lock();
		m_callbackQueue.pop();
	}
	m_mutex.unlock();
}

void SkinHttp::PushFunctions(lua_State * L)
{
	auto bindable = new LuaBindable(L, "Http");
	bindable->AddFunction("Get", this, &SkinHttp::lGet);
	bindable->AddFunction("Post", this, &SkinHttp::lPost);
	bindable->AddFunction("GetAsync", this, &SkinHttp::lGetAsync);
	bindable->AddFunction("PostAsync", this, &SkinHttp::lPostAsync);
	bindable->Push();
	lua_settop(L, 0);
	m_boundStates.Add(L, bindable);
}

void SkinHttp::ClearState(lua_State * L)
{
	if (!m_boundStates.Contains(L))
		return;
	delete m_boundStates.at(L);
	m_boundStates.erase(L);
}
