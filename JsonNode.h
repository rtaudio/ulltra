#pragma once
#include <string>
#include <vector>
#include <map>

struct JsonNode {
	enum class Type { Undefined, Object, Array, String, Number };

	inline JsonNode() { clear(); }

	JsonNode& operator[](const std::string &key);
	JsonNode& operator[](const uint64_t &index);

	inline const std::string& operator=(const std::string &val) { t = Type::String; return str = val; }
	inline const double& operator=(double val) { t = Type::Number; 	return num = val; }

	Type t;
	std::map<std::string, JsonNode> obj;
	std::vector<JsonNode> arr;
	std::string str;
	double num;

	friend std::ostream& operator<< (std::ostream& stream, const JsonNode& jd);
	bool parse(const std::string &str);

	inline void clear() {
		t = Type::Undefined;
		obj.clear();
		arr.clear();
		str.erase();
		num = NAN;
	}
};
