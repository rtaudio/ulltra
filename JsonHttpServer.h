#pragma once

#include "networking.h"
#include "JsonNode.h"

#include<map>

typedef std::map<std::string, std::string> StringMap;

struct mg_mgr;
struct mg_connection;
struct mg_rpc_request;

#include <functional>

class JsonHttpServer
{
public:
	//template <typename ReturnType>
	//using RequestHandler = std::function<ReturnType(const NodeAddr &addr, const JsonObject &request)>;

	typedef std::function<void(const SocketAddress &addr, const std::string &id, JsonNode &request,  JsonNode &response)> RequestHandler;


	JsonHttpServer();
	~JsonHttpServer();
	void on(std::string requestPath, const RequestHandler &handler);
    bool start(const std::string &bindAddr);
	void update();

private:
	static void ev_handler(struct mg_connection *nc, int ev, void *ev_data);
	void handleHttpRequest(struct mg_connection *nc, struct http_message *hm);
	int  rpc_dispatch(const char *buf, int len, char *dst, int dst_len, struct mg_connection *hm);

	struct mg_mgr *m_mgr;
	struct mg_connection *nc;

	std::map<std::string, RequestHandler> m_handlers;
};

