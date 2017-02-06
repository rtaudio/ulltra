#include "networking.h"

#include <string>
#include <regex>
#include <sstream>

#include "JsonHttpServer.h"
#include "pclog/pclog.h"



#define MG_ENABLE_IPV6 1
#undef LOG
#include "mongoose/mongoose.h"
#undef LOG // need to override LOG() macro:
#define LOG(level) if (level > pclog::Log::ReportingLevel()) ; else pclog::Log(level).get()
#define logDebugHttp logDEBUG

#include "frozen/frozen.h"

#include <rtt.h>


JsonHttpServer::JsonHttpServer()
	:threadPool(UP::HttpServerThreadPoolSize)
{
	m_mgEv = 0;
	m_mgr = new mg_mgr();
	mg_mgr_init(m_mgr, this);
	lastRequestTime = std::clock();
}


JsonHttpServer::~JsonHttpServer()
{
	mg_mgr_free(m_mgr);
	delete m_mgr;
}

void JsonHttpServer::update()
{
	clock_t end = clock();
	// poll untill all events processed
	do {
		m_mgEv = 0;
		mg_mgr_poll(m_mgr, 1);
	} while (m_mgEv != 0);
	
	// go into "sleep" mode after 2 seconds
	auto now = clock();
	double secondsSinceLastRequest = double(now - lastRequestTime) / CLOCKS_PER_SEC;
	if (secondsSinceLastRequest > 2) {
		mg_mgr_poll(m_mgr, 400);
	}
}

bool JsonHttpServer::hasActiveConnection() {
	return (m_mgr->active_connections && m_mgr->active_connections->next);
}

void JsonHttpServer::on(std::string rpcMethod, const RequestHandler &handler)
{
	m_handlers.insert(std::pair<std::string, RequestHandler>(rpcMethod, handler));
}

void JsonHttpServer::onStream(std::string streamPrefix, const StreamRequestHandler &handler)
{
	m_streamHandlers.insert(std::pair<std::string, StreamRequestHandler>(streamPrefix, handler));
}



bool JsonHttpServer::start(const std::string &bindAddr)
{
	mg_bind_opts opts;
	memset(&opts, 0, sizeof(opts));
	opts.user_data = this;
	nc = mg_bind_opt(m_mgr, bindAddr.c_str(), ev_handler, opts);

    //nc = mg_bind(m_mgr, bindAddr.c_str(), JsonHttpServer::ev_handler);
	if (!nc) {
        LOG(logERROR) << "Failed to bind mongoose web server to " << bindAddr.c_str();
		return false;
	}
	mg_set_protocol_http_websocket(nc);
	nc->user_data = (void*)this;

    LOG(logDebugHttp) << "started mongoose on " << bindAddr.c_str();

	return true;
}


static int is_equal(const struct mg_str *s1, const struct mg_str *s2) {
	return s1->len == s2->len && memcmp(s1->p, s2->p, s2->len) == 0;
}

static int starts_with(const struct mg_str *s1, const struct mg_str *s2) {

	return s1->len >= s2->len && memcmp(s1->p, s2->p, s2->len) == 0;
}


/*
int JsonHttpServer::rpc_dispatchInvoking(const rpc_dispatch_params &params){
	RttEvent done;
	HandlerParamsAndPromise hpp(params, done);
	dispatchQueue.enqueue(hpp);
	done.Wait();
	return hpp.result;
}
*/


JsonNode & rpcErrorReponse(JsonNode &response, const std::string &message, int code=1) {	
	response["error"]["code"] = code;
	response["error"]["message"] = message;
	response["id"] = message;
	return response;
}


void rpcReponse(struct mg_connection *nc, const JsonNode &response) {
	

	std::stringstream ss;
	ss << response;
	std::string respBody(ss.str());

	auto cs = respBody.c_str();

	// generate response	
	mg_printf(nc, "HTTP/1.0 200 OK\r\nContent-Length: %d\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"Content-Type: application/json\r\n\r\n%s", (int)strlen(cs), cs);
	nc->flags |= MG_F_SEND_AND_CLOSE;
}

void  JsonHttpServer::rpc_dispatch(struct mg_connection *nc, struct http_message *hm) {
	auto nodeAddr = (SocketAddress*)&nc->sa;
	

	JsonNode response;
	response["id"] = 0;
	response["jsonrpc"] = "2.0";

	if (nodeAddr->getFamily() == 0) {
		LOG(logERROR) << "Invalid/no client address!";
		rpcReponse(nc, rpcErrorReponse(response, "Server Error"));
		return;
	}
	
	JsonNode requestNode;
	if (!requestNode.tryParse(hm->body.p, hm->body.len)) {
		LOG(logERROR) << "Invalic JSON in RPC request :" << std::string(hm->body.p, hm->body.len);
		rpcReponse(nc, rpcErrorReponse(response, "Invalid JSON"));
		return;
	}

	auto id = requestNode["id"], params = requestNode["params"];
	auto method = requestNode["method"].str;

	response["id"] = id;

	if (id.isEmpty() || std::isnan(id.num) || id.num < 0 || method.empty()) {
		LOG(logERROR) << "Invalic RPC request!";
		rpcReponse(nc, rpcErrorReponse(response, "Invalid RPC request"));
		return;
	}


	// find & execute handlers
	auto const& hit(m_handlers.find(method));
	if (hit == m_handlers.end() || !hit->second) {
		LOG(logERROR) << "No handler for " << method;
		rpcReponse(nc, rpcErrorReponse(response, "No handler for " + method));
		return;
	}

	LOG(logDEBUG1) << "rpc_dispatch: " << method;
	auto f = hit->second;
	f(*nodeAddr, requestNode["params"], response["result"]);

	LOG(logDEBUG2) << "sending response: " << response;


	rpcReponse(nc, response);
	
}

std::string urlDecode(std::string &SRC) {
	std::string ret;
	char ch;
	int i, ii;
	for (i = 0; i<SRC.length(); i++) {
		if (int(SRC[i]) == 37) {
			sscanf(SRC.substr(i + 1, 2).c_str(), "%x", &ii);
			ch = static_cast<char>(ii);
			ret += ch;
			i = i + 2;
		}
		else {
			ret += SRC[i];
		}
	}
	return (ret);
}


std::map<std::string, std::string> parseQuery(const std::string& query)
{
	std::map<std::string, std::string> data;
	std::regex pattern("([\\w+%]+)=([^&]*)");
	auto words_begin = std::sregex_iterator(query.begin(), query.end(), pattern);
	auto words_end = std::sregex_iterator();

	for (std::sregex_iterator i = words_begin; i != words_end; i++)
	{
		std::string key = (*i)[1].str();
		std::string value = (*i)[2].str();
		data[urlDecode(key)] = urlDecode(value);
	}

	return data;
}


void JsonHttpServer::StreamResponse::addHeader(const std::string &name, const std::string &value) {
	if (headersSent)
		throw std::runtime_error("Cannot add header, headers already sent!");
	headers[name] = value;
}


bool JsonHttpServer::StreamResponse::write(const void *buf, int len) {
	ConnectionHandler *ch = (ConnectionHandler *)(nc->user_data);

	if (len < 0)
		return false;

	if (ch && ch->connectionClosed) {
		return false;
	}



	if (!headersSent) {
		sendHeaders();
	}

	// if len is too small always add to buffer
	if (len < minSendLen) {
		memcpy(&sendBuffer[curSendBufferBytes], buf, len);
		curSendBufferBytes += len;
		if (curSendBufferBytes >= minSendLen) {
			send(nc->sock, (const char *)sendBuffer.data(), curSendBufferBytes, 0);
			curSendBufferBytes = 0;
		}
	}
	else
	{
		// instant flush
		if (curSendBufferBytes > 0) {
			send(nc->sock, (const char *)sendBuffer.data(), curSendBufferBytes, 0);
			curSendBufferBytes = 0;
		}
		send(nc->sock, (const char *)buf, len, 0);
	}




	//send(nc->sock, (const char *)buf, len, 0);
	//mg_send(nc, buf, len);

	return true;
}

void  JsonHttpServer::StreamResponse::sendHeaders() {

#if _WIN32
	DWORD  flagYes = 1;
#else
	int flagYes = 1;
#endif

	int result = setsockopt(nc->sock, IPPROTO_TCP, TCP_NODELAY, (char *)&flagYes, sizeof(flagYes));
	if (result < 0) {
		LOG(logERROR) << "failed to enable TCP_NODELAY! " << lastError();
	}


	addHeader("Accept-Ranges", "none");
	addHeader("Connection", "close");
	addHeader("Access-Control-Allow-Origin", "*");
	addHeader("Cache-Control", "no-cache, no-store");
	addHeader("Pragma", "no-cache");

	std::string headerLines("HTTP/1.1 200 OK\r\n");

	for (auto h : headers) {
		headerLines += h.first + ": " + h.second + "\r\n";
	}

	headersSent = true;
	write(headerLines.c_str(), headerLines.size());	
}

/*
void JsonHttpServer::handleHttpRequest(struct mg_connection *nc, struct http_message *hm)
{
	static const struct mg_str s_get_method = MG_MK_STR("GET");
	static const struct mg_str s_stream_query_prefix = MG_MK_STR("/streamd/");

	static struct mg_serve_http_opts s_http_server_opts;

	auto nodeAddr = (SocketAddress*)&nc->sa;

	// for any GET request server static content
	if (is_equal(&hm->method, &s_get_method)) {

		if (starts_with(&hm->uri, &s_stream_query_prefix)) {
			auto streamName = std::string(&hm->uri.p[s_stream_query_prefix.len], &hm->uri.p[hm->uri.len]);
			LOG(logDebugHttp) << "incoming http stream request " << streamName;

			auto handlers = m_streamHandlers;
			auto streamHandler = handlers.find(streamName);
			if (streamHandler != handlers.end()) {
				auto args = JsonNode(); // ;
				
				StreamResponse response(this, nc);
				auto f = streamHandler->second;
				f(*nodeAddr, args, response);

				// if mg is running multithreaded, we are not in the polling thread
				// so the stream handler called before has blocked, and we can close the connection
//				if (multiThreadedHandling)
					//nc->flags |= MG_F_SEND_AND_CLOSE;

				return;
			}
		}

		s_http_server_opts.document_root = "webroot";
		mg_serve_http(nc, hm, s_http_server_opts);
		return;
	}
	

	std::string path(hm->uri.p, hm->uri.len);
	LOG(logDebugHttp) << "incoming http " << std::string(hm->method.p, hm->method.len) << " request from " << nodeAddr << "; /" << path;

	char buf[1024 * 16];

	//auto params = rpc_dispatch_params(hm->body.p, hm->body.len, buf, sizeof(buf), nc);
	//if(multiThreadedHandling)
//		rpc_dispatchInvoking(params);
//	else
		//rpc_dispatch(params);


	nc->flags |= MG_F_SEND_AND_CLOSE;
}
*/

void JsonHttpServer::ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
	JsonHttpServer *jhs = static_cast<JsonHttpServer*>(nc->mgr->user_data);
	struct http_message *hm = (struct http_message *) ev_data;
	ConnectionHandler *chPtr = (ConnectionHandler*)(nc->user_data);

	if (!jhs) {
		LOG(logERROR) << "Nullpointer to server object in ev_handler!";
		return;
	}

	if (ev != 0) {
		jhs->m_mgEv = ev;
		jhs->lastRequestTime = std::clock();
	}

	switch (ev) {
	case MG_EV_HTTP_REQUEST:
		LOG(logDEBUG) << "MG_EV_HTTP_REQUEST " << nc << " on socket " << nc->sock;
		
		
		if (chPtr != nullptr) {
			LOG(logWARNING) << "MG_EV_HTTP_REQUEST for connection that is already handled, waiting...";
			chPtr->done.Wait();
			//if(chPtr->future.valid())
			//	chPtr->future.wait(); // this should not take long
		}

		{
			static const struct mg_str s_get_method = MG_MK_STR("GET");
			static const struct mg_str s_stream_query_prefix = MG_MK_STR("/streamd/");

			static struct mg_serve_http_opts s_http_server_opts;

			auto nodeAddr = (SocketAddress*)&nc->sa;

			// for any GET request server static content
			if (is_equal(&hm->method, &s_get_method)) {
				if (chPtr == nullptr) {
					chPtr = new ConnectionHandler();
					jhs->activeConnectionHandlers.push_back(chPtr);
					nc->user_data = chPtr;
				}

				chPtr->connectionClosed = false;


				if (starts_with(&hm->uri, &s_stream_query_prefix)) {
					auto streamName = std::string(&hm->uri.p[s_stream_query_prefix.len], &hm->uri.p[hm->uri.len]);
					LOG(logDebugHttp) << "incoming http stream request " << streamName;

					auto streamHandler = jhs->m_streamHandlers.find(streamName);
					if (streamHandler != jhs->m_streamHandlers.end()) {		
						
						auto f = streamHandler->second;
						auto queryString = std::string(hm->query_string.p, hm->query_string.len);
						

						chPtr->future = jhs->threadPool.enqueue([queryString, jhs, nc, f, chPtr]() {
							try {
								auto args = JsonNode(parseQuery(queryString));
								StreamResponse response(jhs, nc);

								f(*(SocketAddress*)&nc->sa, args, response);
							}
							catch (std::exception &ex) {
								LOG(logERROR) << "Streaming error:" << ex.what();
							}
							nc->flags |= MG_F_SEND_AND_CLOSE;
							chPtr->done.Signal();
						});

						return;
					}
				}

				std::string httpMsgAndBody(hm->message.p, hm->message.len);

				chPtr->future = jhs->threadPool.enqueue([nc, httpMsgAndBody, chPtr]() {
					s_http_server_opts.document_root = "webroot";
					http_message hm;
					mg_parse_http(httpMsgAndBody.c_str(), httpMsgAndBody.length(), &hm, 1);
					mg_serve_http(nc, &hm, s_http_server_opts);
					chPtr->done.Signal();
				});				

				return;
			}


			std::string path(hm->uri.p, hm->uri.len);
			LOG(logDebugHttp) << "incoming http " << std::string(hm->method.p, hm->method.len) << " request from " << nodeAddr << "; /" << path;
			jhs->rpc_dispatch(nc, hm);
		}

		break;


	case MG_EV_CLOSE:
		LOG(logDEBUG) << "MG_EV_CLOSE " << nc;
		if (chPtr == nullptr) {
			//LOG(logERROR) << "MG_EV_CLOSE for connection without handler reference!";
			return;
		}

		// notify thread that we are closing down & join
		chPtr->connectionClosed = true;
		
		chPtr->done.Wait();
		

		// remove handler
		for (auto it = jhs->activeConnectionHandlers.begin(); it != jhs->activeConnectionHandlers.end(); it++) {
			if ((*it) == chPtr) {
				jhs->activeConnectionHandlers.erase(it);
				break;
			}
		}

		auto p = nc->user_data;
		nc->user_data = nullptr;
		delete p;

		break;
	}
}


bool JsonHttpServer::StreamResponse::isConnected() {
	ConnectionHandler *ch = (ConnectionHandler *)(nc->user_data);
	return  (ch && !ch->connectionClosed);
}