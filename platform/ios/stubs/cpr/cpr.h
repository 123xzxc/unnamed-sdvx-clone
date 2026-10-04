#pragma once
/*
	Stand-in for the cpr (libcurl) API used by USC, for iPadOS builds that are
	compiled without HTTP support (USC_IOS_HTTP=OFF).

	Internet Ranking, skin downloads and in-app downloads all need TLS and a
	network stack; when this shim is used they simply fail with a readable error
	instead of pulling libcurl into the iOS build.

	Build with -DUSC_IOS_HTTP=ON (the default) to use the real cpr package from
	vcpkg and get those features.
*/

#include <chrono>
#include <future>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cpr
{
	enum class ErrorCode
	{
		OK = 0,
		INTERNAL_ERROR,
		FAILED_TO_CONNECT,
	};

	struct Error
	{
		ErrorCode code = ErrorCode::INTERNAL_ERROR;
		std::string message = "This build was compiled without HTTP support";
	};

	// Header behaves like a std::map<std::string, std::string>.
	class Header : public std::map<std::string, std::string>
	{
	public:
		Header() = default;
		Header(std::initializer_list<value_type> init) : std::map<std::string, std::string>(init) {}
	};

	struct Cookies
	{
		std::string GetEncoded(const struct CurlHolder& holder) const { return std::string(); }
	};

	// Only ever used as a temporary to call Cookies::GetEncoded().
	struct CurlHolder
	{
	};

	struct Url
	{
		Url() = default;
		Url(const std::string& value) : value(value) {}
		std::string value;
	};

	struct Body
	{
		Body() = default;
		Body(const std::string& value) : value(value) {}
		std::string value;
	};

	struct Parameters : public std::map<std::string, std::string>
	{
		Parameters() = default;
		Parameters(std::initializer_list<value_type> init) : std::map<std::string, std::string>(init) {}
	};

	struct File
	{
		File() = default;
		File(const std::string& path) : path(path) {}
		std::string path;
	};

	struct Multipart
	{
		// Same constructor set as the real cpr::Multipart::Part, so that
		//   cpr::Multipart{ {"identifier", value}, {"replay", cpr::File{path}} }
		// keeps compiling (Main/src/IR.cpp uploads replays this way).
		struct Part
		{
			Part() = default;
			Part(const std::string& key, const std::string& value) : key(key), value(value) {}
			Part(const std::string& key, const File& file)
				: key(key), value(file.path), filename(file.path), filePath(file.path), isFile(true) {}
			Part(const std::string& key, const std::string& value, const std::string& contentType)
				: key(key), value(value), contentType(contentType) {}
			Part(const std::string& key, const std::string& value, const std::string& contentType, const std::string& filename)
				: key(key), value(value), contentType(contentType), filename(filename) {}

			std::string key;
			std::string value;
			std::string contentType;
			std::string filename;
			std::string filePath;
			bool isFile = false;
		};
		std::vector<Part> parts;

		Multipart() = default;
		Multipart(std::initializer_list<Part> init) : parts(init) {}
	};

	struct Response
	{
		std::string url;
		std::string text;
		int32_t status_code = 0;
		double elapsed = 0.0;
		Header header;
		Cookies cookies;
		Error error;
	};

	class AsyncResponse
	{
	public:
		AsyncResponse() = default;
		AsyncResponse(Response response)
		{
			std::promise<Response> promise;
			promise.set_value(std::move(response));
			m_future = promise.get_future();
		}

		AsyncResponse(AsyncResponse&&) = default;
		AsyncResponse& operator=(AsyncResponse&&) = default;
		AsyncResponse(const AsyncResponse&) = delete;
		AsyncResponse& operator=(const AsyncResponse&) = delete;

		Response get() { return m_future.get(); }
		// Mirror of cpr::AsyncResponse::wait_for: the engine polls the request before
		// calling get() so it does not block the render thread.
		std::future_status wait_for(const std::chrono::milliseconds& timeout) { return m_future.wait_for(timeout); }
		template<typename Rep, typename Period>
		std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout) { return m_future.wait_for(timeout); }

	private:
		std::future<Response> m_future;
	};

	inline Response MakeUnavailableResponse()
	{
		Response response;
		response.error.code = ErrorCode::INTERNAL_ERROR;
		response.error.message = "This build was compiled without HTTP support (USC_IOS_HTTP=OFF). Copy song folders into the app Files folder to play them.";
		return response;
	}

	inline Response Get(const Url&) { return MakeUnavailableResponse(); }
	inline Response Get(const Url&, const Header&) { return MakeUnavailableResponse(); }
	inline Response Post(const Url&, const Body&) { return MakeUnavailableResponse(); }
	inline Response Post(const Url&, const Body&, const Header&) { return MakeUnavailableResponse(); }
	inline Response Post(const Url&, const Header&, const Body&) { return MakeUnavailableResponse(); }
	inline Response Post(const Url&, const Header&, const Multipart&) { return MakeUnavailableResponse(); }
	inline Response Post(const Url&, const Multipart&) { return MakeUnavailableResponse(); }

	inline AsyncResponse GetAsync(const Url&) { return AsyncResponse(MakeUnavailableResponse()); }
	inline AsyncResponse GetAsync(const Url&, const Header&) { return AsyncResponse(MakeUnavailableResponse()); }
	inline AsyncResponse GetAsync(const Url&, const Header&, const Parameters&) { return AsyncResponse(MakeUnavailableResponse()); }
	inline AsyncResponse PostAsync(const Url&, const Body&) { return AsyncResponse(MakeUnavailableResponse()); }
	inline AsyncResponse PostAsync(const Url&, const Body&, const Header&) { return AsyncResponse(MakeUnavailableResponse()); }
	inline AsyncResponse PostAsync(const Url&, const Header&, const Body&) { return AsyncResponse(MakeUnavailableResponse()); }
	inline AsyncResponse PostAsync(const Url&, const Header&, const Multipart&) { return AsyncResponse(MakeUnavailableResponse()); }
}
