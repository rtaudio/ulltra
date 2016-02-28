#include "HttpClient.h"

#include "mongoose/mongoose.h"

JsonHttpClient::JsonHttpClient()
{
}


JsonHttpClient::~JsonHttpClient()
{
}


const JsonHttpClient::Response &JsonHttpClient::requestAsync(const NodeAddr &host, std::string path, const std::string &body)
{
	SOCKET soc = socket(host.ss_family, SOCK_STREAM, IPPROTO_TCP);
	try {
		socketSetBlocking(soc, false);		
		connect(soc, (const sockaddr*)(&host), sizeof(host));
		int res = socketSelectTimeout(soc, UP::DiscoveryTcpTimeout);

		if (res != 0) {
			throw Exception("Connect failed!");
		}

		std::ostringstream buf;
		std::string str;

		buf << "POST " << path << " HTTP/1.1\n";
		buf << "Content-Type: application/json\n";
		buf << "Content-Length: " << std::to_string(body.length()) << "\n\n";
		buf << body;
		str = buf.str();
		send(soc, str.data(), str.size(), NULL);
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