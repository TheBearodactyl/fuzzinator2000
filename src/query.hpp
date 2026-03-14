#pragma once

#include <string>
#include <vector>

class GJGameLevel;

namespace query {
	enum class CompOp {
		Eq,
		Neq,
		Lt,
		Lte,
		Gt,
		Gte,
		Contains,
		NotContains,
	};

	enum class Field {
		Id,
		Name,
		Creator,
		Downloads,
		Likes,
		Stars,
		Length,
		Objects,
		Featured,
		Epic,
	};

	struct Filter {
		Field field;
		CompOp op;
		int int_value = 0;
		std::string str_value;
	};

	struct ParsedQuery {
		std::string search_text;
		bool exact_match = false;
		std::string exact_needle;
		std::vector<Filter> filters;
		bool include_deleted = false;
	};

	[[nodiscard]]
	ParsedQuery parse(std::string const& raw);

	[[nodiscard]]
	bool level_passes(GJGameLevel* level, std::vector<Filter> const& filters);
} // namespace query
