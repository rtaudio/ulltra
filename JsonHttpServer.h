#pragma once

#include "networking.h"
#include<map>

typedef struct json_token JsonObject;
typedef std::map<std::string, std::string> StringMap;

struct mg_mgr;
struct mg_connection;
struct mg_rpc_request;
typedef int(*mg_rpc_handler_t)(char *buf, int len, struct mg_rpc_request *req);
typedef int(mg_rpc_handler_t_np)(char *buf, int len, struct mg_rpc_request *req);


#include <functional>
#include <map>

#define REGISTER_FUNCTION(n) static int n(char *buf, int len, struct mg_rpc_request *req)

class JsonHttpServer
{
public:
	typedef std::function<std::string(const NodeAddr &addr, const JsonObject &request)> RequestHandler;


	JsonHttpServer();
	~JsonHttpServer();
	void on(std::string requestPath, const RequestHandler &handler);
    bool start(const std::string &bindAddr);
	void update();
private:
	static void ev_handler(struct mg_connection *nc, int ev, void *ev_data);
	void handleHttpRequest(struct mg_connection *nc, struct http_message *hm);

	struct mg_mgr *m_mgr;
	struct mg_connection *nc;

	std::map<std::string, const RequestHandler&> m_handlers;
	const char **m_handlerPaths;

	int mgHandlerWrapper(std::string path, char *buf, int len, struct mg_rpc_request *req);

	mg_rpc_handler_t *m_handlerMethods;

	NodeAddr *m_currentNodeAddr;
};

