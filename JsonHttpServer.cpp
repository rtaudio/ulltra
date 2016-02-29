#include <string>
#include "networking.h"

#include <sstream>
#include "JsonHttpServer.h"


#define MG_ENABLE_IPV6
#undef LOG
#include "mongoose/mongoose.h"
#undef LOG // need to override LOG() macro:
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
}

void JsonHttpServer::update()
{
	mg_mgr_poll(m_mgr, 1);
}


void JsonHttpServer::on(std::string requestPath, const RequestHandler &handler)
{
	m_handlers.insert(std::pair<std::string, const RequestHandler&>(requestPath, handler));
}



bool JsonHttpServer::start(const std::string &bindAddr)
{
    nc = mg_bind(m_mgr, bindAddr.c_str(), JsonHttpServer::ev_handler);
	if (!nc) {
        LOG(logERROR) << "Failed to bind mongoose web server to " << bindAddr.c_str();
		return false;
	}
	mg_set_protocol_http_websocket(nc);
	//mg_enable_multithreading(nc);

    LOG(logDEBUG) << "started mongoose on " << bindAddr.c_str();

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
	
	// method & addr
	std::string path(hm->uri.p, hm->uri.len);
	auto nodeAddr = (NodeAddr*)&nc->sa.sin;	
	LOG(logDEBUG) << "incoming http " << std::string(hm->method.p, hm->method.len) << " request from " << *nodeAddr << "; /" << path;

	JsonNode jreq, jres;

	// parse request
	if (!jreq.parse(std::string(hm->body.p, hm->body.len))) {
		LOG(logERROR) << "JSON parse error!";
		return;
	}

	LOG(logDEBUG1) << "body: " << jreq;

	// find & execute handlers
	auto &hit = m_handlers.find(path);
	if (hit == m_handlers.end() || !hit->second) {
		LOG(logERROR) << "No handler for /" << path;
		return;
	}

	hit->second(*nodeAddr, jreq, jres);

	// generate response
	std::stringstream ss;
	ss << jres;
	std::string &respBody(ss.str());
	mg_printf(nc, "HTTP/1.0 200 OK\r\nContent-Length: %d\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Type: application/json\r\n\r\n%s", (int)respBody.length(), respBody);
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
