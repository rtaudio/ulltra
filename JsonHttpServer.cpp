#include <string>
#include "networking.h"

#include <sstream>
#include "JsonHttpServer.h"


#define MG_ENABLE_IPV6
#undef LOG
#include "mongoose/mongoose.h"
#undef LOG // need to override LOG() macro:
#define LOG(level) if (level > Log::ReportingLevel()) ; else Log(level).get()

JsonHttpServer::JsonHttpServer()
{
	m_mgEv = 0;
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
	// poll untill all events processed
	do {
		m_mgEv = 0;
		mg_mgr_poll(m_mgr, 1);
	} while (m_mgEv != 0);
//	std::cout << "update done" << std::endl;
}


void JsonHttpServer::on(std::string requestPath, const RequestHandler &handler)
{
	m_handlers.insert(std::pair<std::string, RequestHandler>(requestPath, handler));
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

    LOG(logDebugHttp) << "started mongoose on " << bindAddr.c_str();

	return true;
}


static int is_equal(const struct mg_str *s1, const struct mg_str *s2) {
	return s1->len == s2->len && memcmp(s1->p, s2->p, s2->len) == 0;
}

// this is copied from mongoose
int  JsonHttpServer::rpc_dispatch(const char *buf, int len, char *dst, int dst_len, struct mg_connection *nc) {
	struct json_token tokens[200];
	struct mg_rpc_request req;
	int i, n;

	memset(&req, 0, sizeof(req));
	n = parse_json(buf, len, tokens, sizeof(tokens) / sizeof(tokens[0]));
	if (n <= 0) {
		int err_code = (n == JSON_STRING_INVALID) ? JSON_RPC_PARSE_ERROR
			: JSON_RPC_SERVER_ERROR;
		LOG(logERROR) << "Invalic RPC JSON! " << buf;
		return mg_rpc_create_std_error(dst, dst_len, &req, err_code);
	}

	req.message = tokens;
	req.id = find_json_token(tokens, "id");
	req.method = find_json_token(tokens, "method");
	req.params = find_json_token(tokens, "params");

	if (req.id == NULL || req.method == NULL) {
		LOG(logERROR) << "Invalic RPC request!";
		return mg_rpc_create_std_error(dst, dst_len, &req,
			JSON_RPC_INVALID_REQUEST_ERROR);
	}
	
	auto nodeAddr = (SocketAddress*)&nc->sa;

	// find & execute handlers
	std::string method(req.method->ptr, req.method->len);
	auto const& hit(m_handlers.find(method));
	if (hit == m_handlers.end() || !hit->second) {
		LOG(logERROR) << "No handler for " << method;
		return mg_rpc_create_std_error(dst, dst_len, &req, JSON_RPC_METHOD_NOT_FOUND_ERROR);
	}

	JsonNode jreq, jres, id;
	jreq.fill(req.params);
	id.fill(req.id);

	if (id.str.empty()) {
		LOG(logERROR) << "Invalid RPC request ID!";
		return mg_rpc_create_std_error(dst, dst_len, &req, JSON_RPC_INVALID_REQUEST_ERROR);
	}

	LOG(logDEBUG1) << "rpc_dispatch: " << method;
	auto f = hit->second;
	f(*nodeAddr, id.str, jreq, jres);

	LOG(logDEBUG2) << "sending response: " << jres;


	std::stringstream ss;
	ss << jres;
	std::string respBody(ss.str());
	return mg_rpc_create_reply(dst, dst_len, &req, "S", respBody.c_str());
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
	

	std::string path(hm->uri.p, hm->uri.len);
	LOG(logDebugHttp) << "incoming http " << std::string(hm->method.p, hm->method.len) << " request from " << *(SocketAddress*)&nc->sa << "; /" << path;

	char buf[1024 * 16];

	rpc_dispatch(hm->body.p, hm->body.len, buf, sizeof(buf), nc);

	// generate response	
	mg_printf(nc, "HTTP/1.0 200 OK\r\nContent-Length: %d\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Type: application/json\r\n\r\n%s", (int)strlen(buf), buf);
	nc->flags |= MG_F_SEND_AND_CLOSE;
}


void JsonHttpServer::ev_handler(struct mg_connection *nc, int ev, void *ev_data) {

	JsonHttpServer *jhs = (JsonHttpServer*)nc->mgr->user_data;
	struct http_message *hm = (struct http_message *) ev_data;

	if (ev != 0) jhs->m_mgEv = ev;
	//std::cout << "ev" << ev << std::endl;

	switch (ev) {
	case MG_EV_HTTP_REQUEST:
		//std::this_thread::sleep_for(std::chrono::seconds(50));
		jhs->handleHttpRequest(nc, hm);
		break;
	default:
		break;
	}
}
