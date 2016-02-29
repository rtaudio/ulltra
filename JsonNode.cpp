#include "JsonNode.h"
#include <iostream>
#include <sstream>
#include "mongoose/mongoose.h"


const JsonNode JsonNode::undefined;

JsonNode& JsonNode::operator[](const std::string &key) {
	if (t == Type::Undefined) {
		t = Type::Object;
		obj[key] = JsonNode();
		return obj[key];
	}
	else if (t == Type::Object) {
		auto v = obj.find(key);
		if (v != obj.end()) {
			return v->second;
		}
		else {
			obj[key] = JsonNode();
			return obj[key];
		}
	}
}


JsonNode& JsonNode::operator[](const uint64_t &index) {
	if (t == Type::Undefined) {
		t = Type::Array;
		arr = std::vector<JsonNode>(index + 1, JsonNode());
		return arr[index];
	}
	else if (t == Type::Array) {
		if (index >= arr.size()) {
			arr.resize(index + 1, JsonNode());
		}
		return arr[index];
	}
}

JsonNode const& JsonNode::operator[](const std::string &key) const {
	if (t != Type::Object) {
		return undefined;
	}
	auto v = obj.find(key);
	if (v == obj.end()) {
		return undefined;
	}
	return v->second;
}


JsonNode const& JsonNode::operator[](const uint64_t &index) const {
	if (t != Type::Array || index >= arr.size()) {
		return undefined;
	}
	return arr[index];
}


int fillJson(JsonNode &jn, json_token *cur)
{
	switch (cur->type) {
	case JSON_TYPE_OBJECT:
		jn.t = JsonNode::Type::Object;
		for (int i = 1; i < cur->num_desc;) {
			if (cur[i].type == JSON_TYPE_STRING) {
				std::string key(cur[i].ptr, cur[i].len);
				i++;
				i += fillJson(jn[key], &cur[i]) + 1;
			}
		}
		return cur->num_desc;

	case JSON_TYPE_ARRAY:
		jn.t = JsonNode::Type::Array;
		jn.arr.reserve(cur->num_desc);
		for (int i = 1; i <= cur->num_desc;) {
			i += fillJson(jn[i - 1], &cur[i]) + 1;
		}
		return cur->num_desc;

	case JSON_TYPE_NUMBER:
		jn.t = JsonNode::Type::Number;
		jn.num = std::stod(std::string(cur->ptr, cur->len));
		return 0;

	case JSON_TYPE_STRING:
		jn.t = JsonNode::Type::String;
		jn.str = std::string(cur->ptr, cur->len);
		return 0;

	}

	return 0;
}

int JsonNode::fill(json_token *tokens)
{
	clear();
	return fillJson(*this, tokens);
}

bool JsonNode::parse(const std::string &str)
{
	json_token tokens[400];
	int n = parse_json(str.c_str(), str.length(), tokens, sizeof(tokens) / sizeof(tokens[0]));
	if (n <= 0) {
		return false;
	}
	fill(tokens);
	return true;
}

std::ostream& operator<< (std::ostream& stream, const JsonNode& jd) {
	static const std::string empty("");
	static const std::string sep(",");

	switch (jd.t) {
	case JsonNode::Type::Number: stream << jd.num; break;
	case JsonNode::Type::String: stream << "\"" << jd.str << "\""; break;
	case JsonNode::Type::Object:
		stream << "{";
		for (auto it = jd.obj.begin(); it != jd.obj.end(); it++) {
			stream << "\"" << it->first << "\":" << it->second << (it == --jd.obj.end() ? empty : sep);
		}
		stream << "}";
		break;


	case JsonNode::Type::Array:
		stream << "[";
		for (auto it = jd.arr.begin(); it != jd.arr.end(); it++) {
			stream << *it << (it == --jd.arr.end() ? empty : sep);
		}
		stream << "]";

	case JsonNode::Type::Undefined:
		stream << "null";
	}
	return stream;
}
