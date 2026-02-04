#include <iostream>
#include <string_view>
#include <charconv>
#include <vector>

enum ParserOutcomes { good, bad, unknown };

struct output {
	int tag;
	std::string_view val;
};

#define assert_test(cond){if(!(cond)) return false;};

void run_test(bool (*fun)(), int& total, int& success) {
	if (fun()) success++;
	total++;
}

bool parseField(const char*& start, const char* end, std::string_view& field) {
	if (start >= end) return false;
	const char soh = '|';
	const char* pos = start;
	while (pos < end and *pos != soh) pos++;
	if (pos >= end) return false;
	field = std::string_view(start, pos - start);
	start = pos + 1;
	return true;
}

bool parseTag(std::string_view field, int& tag, std::string_view& val) {
	const char* start = field.data();
	const char* end = start + field.size();
	const char* pos = start;
	while (pos < end and *pos != '=') pos++;
	if (pos >= end) return false;
	std::from_chars_result result = std::from_chars(start, pos, tag);
	if (result.ec != std::errc()) return false;
	val = std::string_view(pos + 1, end - (pos + 1));
	return true;
}

ParserOutcomes parseMsg(std::string_view msg, std::vector<output>& get_out) {
	// parse each field
	const char* start = msg.data();
	const char* end = start + msg.size();
	std::string_view field;

	while (parseField(start, end, field)) {
		if (field.empty()) continue;
		// per field parse tag/val
		output cur;
		if (!parseTag(field, cur.tag, cur.val) or cur.val.empty()) return ParserOutcomes::unknown;
		std::cout << cur.tag << "=" << cur.val << std::endl;
	}
	if (end-start > 1) {
		std::cout << end-start << std::endl;
		return ParserOutcomes::bad;
	}
	return ParserOutcomes::good;
}
