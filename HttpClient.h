#pragma once

#include "UlltraProto.h"
#include "networking.h"


#include <string>

#include "JsonHttpServer.h"

/* mongoose.h */

class JsonHttpClient
{
public:
	struct Exception {
		int statusCode;
		std::string statusString;
		Exception(const std::string &msg, int code=-1) {
			statusString = msg;
		}
	};

	struct Response {
		int statusCode;
		std::string statusString;
		JsonObject *dataPtr;

		Response() : dataPtr(0) {}
		
		Response(const std::string &str);

		~Response() {
			if(dataPtr)
				free(dataPtr);
		}
	};

	JsonHttpClient();
	virtual ~JsonHttpClient();
	const Response &requestAsync(const NodeAddr &host, std::string path, const std::string &body);

	JsonHttpClient::Response m_lastResponse;
};

