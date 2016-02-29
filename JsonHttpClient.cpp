#include <sstream>

#include "JsonHttpClient.h"

#define MG_ENABLE_IPV6
#undef LOG
#include "mongoose/mongoose.h"

// need to override LOG() macro:
#undef LOG
#define LOG(level) if (level > Log::ReportingLevel()) ; else Log().get(level)


JsonHttpClient::JsonHttpClient()
{
	m_connectTimeout = UP::HttpConnectTimeout;
}


JsonHttpClient::~JsonHttpClient()
{
}



const JsonNode &JsonHttpClient::rpc(const SocketAddress &host, const std::string &id, const std::string &method, const JsonNode &params)
{
	JsonNode rpc;
	rpc["id"] = id;
	rpc["method"] = method;
	rpc["params"] = params;
	std::stringstream ss;
	ss << rpc;
	const JsonNode &rpcResp(request(host, method, ss.str()));

	if (rpcResp["id"].str != id) {
		LOG(logDEBUG) << "response with invalid id: " << rpcResp;
		throw Exception("RPC response id does not match request!");
	}

	if (rpcResp["result"].isUndefined()) {
		throw Exception("RPC response without result!");
	}


	if (!rpcResp["result"]["error"].str.empty())
		throw JsonHttpClient::Exception(rpcResp["result"]["error"].str);

	return rpcResp["result"];
}

const JsonNode &JsonHttpClient::request(const SocketAddress &host, const std::string &method, const std::string &body)
{
	LOG(logDebugHttp) << "http request to " << host << " /" << method << " " << body;
	SOCKET soc = socket(host.getFamily(), SOCK_STREAM, IPPROTO_TCP);
	try {
		int res;
		if (m_connectTimeout > 0) {
			if (!socketSetBlocking(soc, false))
				throw Exception("Cannot set socket non-blocking!");

			res = connect(soc, (const sockaddr*)(&host), sizeof(host));
			if (res != 0) {
				res = socketConnectTimeout(soc, UP::HttpConnectTimeout * 1000);
				if (res != 1) {
					LOG(logDEBUG) << "connect() to " << host << " timed out";
					throw Exception("Connect timeout (" + std::to_string(UP::HttpConnectTimeout) + " ms)!");
				}
			}
		}
		else {
			res = connect(soc, (const sockaddr*)(&host), sizeof(host));
		}

        if (res == -1) {
             LOG(logDebugHttp) << "connect() to " << host << " failed!";
            throw Exception("Connect failed!");
        }

		std::string path("/x-" + method);

		std::ostringstream buf;
		buf << "POST " << path << " HTTP/1.1\n";
		buf << "Content-Type: application/json\n";
		buf << "Content-Length: " << std::to_string(body.length()) << "\n\n";
		buf << body;
		std::string str = buf.str();
        send(soc, str.data(), str.size(), 0);
		buf.str(""); buf.clear();

		char chunk[1024*16+1];
		int rlen;

		// set socket back to blocking if necessary
		if (m_connectTimeout > 0)
			socketSetBlocking(soc, true);

		while (true) {
			res = socketSelect(soc, UP::HttpResponseTimeout * 1000);
			if (res == 0) {
				throw Exception("Server " + host.toString() + " did not send a response within " + std::to_string(UP::HttpResponseTimeout) + " ms!");
			}

			if (res < 0) {
				throw Exception("socketSelectTimeout() failed!");
			}

			rlen = recv(soc, chunk, sizeof(chunk) - 1, 0);
			if (rlen <= 0) // eos
				break;
			chunk[rlen] = '\0';
			buf << chunk;
		}
		close(soc);

		str = buf.str();
		buf.str(""); buf.clear();

		auto skipHttpVer = str.find(' '), skipCode = str.find(' ', skipHttpVer + 1), skipStatus = str.find('\n', skipCode + 1);
		if (skipHttpVer == std::string::npos || skipCode == std::string::npos || skipStatus == std::string::npos) {
			throw Exception("Invalid HTTP response!");
		}
		auto code(std::stoi(str.substr(skipHttpVer + 1, skipCode - skipHttpVer)));
		
		auto status(str.substr(skipCode + 1, skipStatus - skipCode));
		trim(status);

		if (code != 200) {
			throw Exception("HTTP error: " + status);
		}

		bool winLb = str[skipStatus - 1] == '\r';
		auto skipHeader = str.find(winLb ? "\r\n\r\n" : "\n\n", skipStatus);
		auto body = str.substr(skipHeader + (winLb ? 4 : 2));

		if (!m_lastData.parse(body)) {
			LOG(logERROR) << "Failed to parse json " << body;
			throw Exception("JSON parse error!");
		}

		LOG(logDebugHttp) << "http response " << code << " (" << status << ") body: " << m_lastData;

		return m_lastData;
	}

	catch (const Exception &ex) {
		close(soc);
		throw ex;
	}
}


#if 0
const JsonHttpClient::Response &JsonHttpClient::request(const NodeAddr &host, std::string path, const StringMap &data)
{
	char res[1024];
	const int m = sizeof(res) - 1;
	int n = 0;

	n += json_emit(&res[n], m, "{");
	for (auto it = data.begin(); it != data.end(); it++) {
		n += json_emit(&res[n], m, "s:s,", it->first.c_str(), it->second.c_str());
	}
	n += json_emit(&res[n - 1], m, "}"); // n-1 to remove last ','

	return request(host, path, std::string(res));
}
#endif