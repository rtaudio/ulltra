#include <string>
#include "networking.h"


#include "JsonHttpServer.h"


#define MG_ENABLE_IPV6
#include "mongoose/mongoose.h"

// need to override LOG() macro:
#undef LOG
#define LOG(level) if (level > Log::ReportingLevel()) ; else Log().get(level)

JsonHttpServer::JsonHttpServer()
{
	m_mgr = new mg_mgr();
	mg_mgr_init(m_mgr, this);
}


JsonHttpServer::~JsonHttpServer()
{
	mg_mgr_free(m_mgr);
	delete m_mgr;
	m_handlerPaths = 0;
	m_handlerMethods = 0;

	delete m_handlerPaths;
	delete m_handlerMethods;
}


void JsonHttpServer::update()
{
	mg_mgr_poll(m_mgr, 1);
}

void JsonHttpServer::on(std::string requestPath, const RequestHandler &handler)
{
	m_handlers.insert(std::pair<std::string, const RequestHandler&>(requestPath, handler));
}


int JsonHttpServer::mgHandlerWrapper(std::string path, char *buf, int len, struct mg_rpc_request *req)
{
	auto &h = m_handlers.at(path);
	auto s = h(*m_currentNodeAddr, *req->params);
	return mg_rpc_create_reply(buf, len, req, "S", s.c_str());
}

bool JsonHttpServer::start(int port)
{
	m_handlerPaths = new const char*[m_handlers.size()+1];
	m_handlerMethods = new mg_rpc_handler_t[m_handlers.size()+1];

	typedef std::function<int(char *buf, int len, struct mg_rpc_request *req)> mgFunc;

	int i = 0;
	// auto-purge dead nodes
	for (auto it = m_handlers.begin(); it != m_handlers.end(); it++) {
		m_handlerPaths[i] = it->first.c_str();
		mgFunc h(std::bind(&JsonHttpServer::mgHandlerWrapper, this, it->first, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		m_handlerMethods[i] = h.target<mg_rpc_handler_t_np>();
		i++;
	}
	m_handlerPaths[i] = 0;
	m_handlerMethods[i] = 0;

	nc = mg_bind(m_mgr, std::to_string(port).c_str(), JsonHttpServer::ev_handler);
	if (!nc) {
		LOG(logERROR) << "Failed to bind mongoose web server to port " << port;
		return false;
	}
	mg_set_protocol_http_websocket(nc);
	//mg_enable_multithreading(nc);

	LOG(logDEBUG) << "started mongoose on port " << port;

	return true;
}


static int is_equal(const struct mg_str *s1, const struct mg_str *s2) {
	return s1->len == s2->len && memcmp(s1->p, s2->p, s2->len) == 0;
}

void JsonHttpServer::handleHttpRequest(struct mg_connection *nc, struct http_message *hm)
{
	static const struct mg_str s_get_method = MG_STR("GET");
	static struct mg_serve_http_opts s_http_server_opts;

	// for any GET request server static content
	if (is_equal(&hm->method, &s_get_method)) {
		s_http_server_opts.document_root = "webroot";
		mg_serve_http(nc, hm, s_http_server_opts);
		return;
	}

	NodeAddr addr;
	
	
	m_currentNodeAddr = (NodeAddr*)&nc->sa.sin;	
	char buf[1024*8];
	mg_rpc_dispatch(hm->body.p, hm->body.len, buf, sizeof(buf),
		m_handlerPaths, (mg_rpc_handler_t *)m_handlerMethods);

	mg_printf(nc, "HTTP/1.0 200 OK\r\nContent-Length: %d\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Type: application/json\r\n\r\n%s", (int)strlen(buf), buf);

	nc->flags |= MG_F_SEND_AND_CLOSE;
}


void JsonHttpServer::ev_handler(struct mg_connection *nc, int ev, void *ev_data) {

	JsonHttpServer *jhs = (JsonHttpServer*)nc->mgr->user_data;
	struct http_message *hm = (struct http_message *) ev_data;

	switch (ev) {
	case MG_EV_HTTP_REQUEST:
		jhs->handleHttpRequest(nc, hm);
		break;
	default:
		break;
	}
}