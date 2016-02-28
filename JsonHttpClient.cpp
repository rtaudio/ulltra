#include "JsonHttpClient.h"

#define MG_ENABLE_IPV6
#undef LOG
#include "mongoose/mongoose.h"

// need to override LOG() macro:
#undef LOG
#define LOG(level) if (level > Log::ReportingLevel()) ; else Log().get(level)


JsonHttpClient::JsonHttpClient()
{
}


JsonHttpClient::~JsonHttpClient()
{
}

const JsonHttpClient::Response &JsonHttpClient::request(const NodeAddr &host, std::string path, const StringMap &data)
{
    char res[1024];
    const int m = sizeof(res)-1;
    int n = 0;

    n += json_emit(&res[n], m, "{");
    for (auto it = data.begin(); it != data.end(); it++) {
        n += json_emit(&res[n], m, "s:s,", it->first.c_str(), it->second.c_str());
    }
    n += json_emit(&res[n-1], m, "}"); // n-1 to remove last ','

    return request(host, path, std::string(res));
}

const JsonHttpClient::Response &JsonHttpClient::request(const NodeAddr &host, std::string path, const std::string &body)
{
	SOCKET soc = socket(host.ss_family, SOCK_STREAM, IPPROTO_TCP);
	try {
        //socketSetBlocking(soc, false);
        int res = connect(soc, (const sockaddr*)(&host), sizeof(host));
        if (res != EINPROGRESS)
        {
            LOG(logDEBUG) << "connect() to " << host << " instantly failed! " << lastError();
            throw Exception("Async connect failed!");
        }

        res = socketSelectTimeout(soc, UP::DiscoveryTcpTimeout*1000);

        if (res == 0) {
             LOG(logDEBUG) << "connect() to " << host << " timed out";
            throw Exception("Connect timeout!");
        }


        if (res == -1) {
             LOG(logDEBUG) << "connect() to " << host << " failed!";
            throw Exception("Connect failed!");
        }

		std::ostringstream buf;
		std::string str;

		buf << "POST " << path << " HTTP/1.1\n";
		buf << "Content-Type: application/json\n";
		buf << "Content-Length: " << std::to_string(body.length()) << "\n\n";
		buf << body;
		str = buf.str();
        send(soc, str.data(), str.size(), 0);
		buf.clear();

		char chunk[1024*8+1];
		int rlen;
		while ((rlen = recv(soc, chunk, sizeof(chunk)-1, 0)) > 0) {
			chunk[rlen] = '\0';
			buf << chunk;
		}
		close(soc);

		str = buf.str();

		m_lastResponse = Response(str);
		return m_lastResponse;
	}

	catch (const Exception &ex) {
		close(soc);
		throw ex;
	}
}


JsonHttpClient::Response::Response(const std::string &str) {
	dataPtr = parse_json2(str.data(), str.length());
}
