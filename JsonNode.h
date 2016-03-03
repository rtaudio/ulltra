#pragma once
#include <string>
#include <vector>
#include <map>

#include <math.h>

#ifndef NAN
#define NAN NaN
#endif

struct json_token;

struct JsonNode {
	enum class Type { Undefined, Object, Array, String, Number };

	const static JsonNode undefined;

	inline JsonNode() { clear(); }

	/*
	template<typename ...Args>
	JsonNode(Args... args) {
		const int size = sizeof...(args);
		for (int i = 0; i < size; i += 2) {
			const std::string k(const_cast<const std::string>(args[i]));
			(*this)[k] = args[i+1];
		}
	}*/

	JsonNode(std::initializer_list<std::string> keyValues) {
		t = Type::Undefined;

		for (auto it = keyValues.begin(); it != keyValues.end(); it++){			
			std::string k(*it);
			(*this)[k] = *(++it);
		}
	}

	JsonNode& operator[](const std::string &key);
	JsonNode& operator[](const uint64_t &index);
	const JsonNode& operator[](const std::string &key) const;
	const JsonNode& operator[](const uint64_t &index) const;

	inline const std::string& operator=(const std::string &val) { t = Type::String; return str = val; }
	inline const double& operator=(double val) { t = Type::Number; 	return num = val; }

	Type t;
	std::map<std::string, JsonNode> obj;
	std::vector<JsonNode> arr;
	std::string str;
	double num;

	friend std::ostream& operator<< (std::ostream& stream, const JsonNode& jd);
	bool parse(const std::string &str);
	int fill(json_token *tokens);

	inline void clear() {
		t = Type::Undefined;
		obj.clear();
		arr.clear();
		str.erase();
		num = NAN;
	}

	inline bool isUndefined() const {
		return t == Type::Undefined;
	}
};
