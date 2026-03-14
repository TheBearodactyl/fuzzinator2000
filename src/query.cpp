#include "query.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>

#include <Geode/Geode.hpp>

#include "utils.hpp"

using namespace geode::prelude;

namespace {
	auto to_lower(StringV str) -> String {
		String out(str);

		std::ranges::transform(out, out.begin(), [](unsigned char chr) -> char {
			return static_cast<char>(std::tolower(chr));
		});

		return out;
	}

	auto trim(StringV str) -> String {
		auto start = str.find_first_not_of(" \t\r\n");

		if (start == StringV ::npos) {
			return {};
		}

		auto end = str.find_last_not_of(" \t\r\n");

		return String(str.substr(start, end - start + 1));
	}

	auto parse_field(StringV token) -> Option<query::Field> {
		auto lower = to_lower(token);

		if (lower == "id") {
			return query::Field::Id;
		}

		if (lower == "name") {
			return query::Field::Name;
		}

		if (lower == "creator") {
			return query::Field::Creator;
		}

		if (lower == "downloads" || lower == "dl") {
			return query::Field::Downloads;
		}

		if (lower == "likes") {
			return query::Field::Likes;
		}

		if (lower == "stars") {
			return query::Field::Stars;
		}

		if (lower == "length") {
			return query::Field::Length;
		}

		if (lower == "objects") {
			return query::Field::Objects;
		}

		if (lower == "featured") {
			return query::Field::Featured;
		}

		if (lower == "epic") {
			return query::Field::Epic;
		}

		return std::nullopt;
	}

	struct OpMatch {
		query::CompOp op;
		std::size_t len;
	};

	auto try_match_op(StringV token, std::size_t pos) -> Option<OpMatch> {
		auto remaining = token.substr(pos);
		if (remaining.size() >= 2) {
			auto two = remaining.substr(0, 2);
			if (two == "!~") {
				return OpMatch {.op = query::CompOp::NotContains, .len = 2};
			}
			if (two == "=~") {
				return OpMatch {.op = query::CompOp::Contains, .len = 2};
			}
			if (two == "!=") {
				return OpMatch {.op = query::CompOp::Neq, .len = 2};
			}
			if (two == "<=") {
				return OpMatch {.op = query::CompOp::Lte, .len = 2};
			}
			if (two == ">=") {
				return OpMatch {.op = query::CompOp::Gte, .len = 2};
			}
		}

		if (!remaining.empty()) {
			if (remaining[0] == '=') {
				return OpMatch {.op = query::CompOp::Eq, .len = 1};
			}

			if (remaining[0] == '<') {
				return OpMatch {.op = query::CompOp::Lt, .len = 1};
			}

			if (remaining[0] == '>') {
				return OpMatch {.op = query::CompOp::Gt, .len = 1};
			}
		}

		return std::nullopt;
	}

	auto is_string_field(query::Field field) -> bool {
		return field == query::Field::Name || field == query::Field::Creator;
	}

	auto compare_int(int lhs, query::CompOp operation, int rhs) -> bool {
		switch (operation) {
			case query::CompOp::Eq:
				return lhs == rhs;
			case query::CompOp::Neq:
				return lhs != rhs;
			case query::CompOp::Lt:
				return lhs < rhs;
			case query::CompOp::Lte:
				return lhs <= rhs;
			case query::CompOp::Gt:
				return lhs > rhs;
			case query::CompOp::Gte:
				return lhs >= rhs;
			default:
				return true;
		}
	}

	auto compare_str_contains(StringV haystack, StringV needle, bool negate) -> bool {
		auto hay_lower = to_lower(haystack);
		auto needle_lower = to_lower(needle);
		bool found = hay_lower.find(needle_lower) != String ::npos;
		return negate ? !found : found;
	}
} // namespace

namespace query {
	auto parse(String const& raw) -> ParsedQuery {
		ParsedQuery result;
		auto trimmed = trim(raw);

		if (trimmed.empty()) {
			return result;
		}

		if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
			result.exact_match = true;
			auto inner = trimmed.substr(1, trimmed.size() - 2);
			result.search_text = inner;
			result.exact_needle = to_lower(inner);
			return result;
		}

		Vec<String> tokens;
		{
			std::istringstream iss(trimmed);
			String tok;
			while (iss >> tok) {
				tokens.push_back(tok);
			}
		}

		Vec<String> search_tokens;

		for (std::size_t ti = 0; ti < tokens.size(); ++ti) {
			auto const& token = tokens[ti];
			bool parsed_as_filter = false;

			{
				auto lower_tok = to_lower(token);
				if (lower_tok.starts_with("is_deleted=")) {
					auto val = lower_tok.substr(11);
					result.include_deleted = (val == "true" || val == "1" || val == "yes");
					parsed_as_filter = true;
				}
			}

			if (parsed_as_filter) {
				continue;
			}

			for (std::size_t pos = 1; pos < token.size(); ++pos) {
				char chr = token[pos];
				if (chr != '=' && chr != '!' && chr != '<' && chr != '>' && chr != '~') {
					continue;
				}

				auto field_str = token.substr(0, pos);
				auto maybe_field = parse_field(field_str);
				if (!maybe_field) {
					continue;
				}

				auto maybe_op = try_match_op(token, pos);
				if (!maybe_op) {
					continue;
				}

				auto field = *maybe_field;
				auto operation = maybe_op->op;
				auto value_str = String(token.substr(pos + maybe_op->len));

				if ((operation == CompOp::Contains || operation == CompOp::NotContains) && is_string_field(field)) {
					if (!value_str.empty() && value_str.front() == '"') {
						value_str = value_str.substr(1);
						if (!value_str.empty() && value_str.back() == '"') {
							value_str.pop_back();
						} else {
							while (ti + 1 < tokens.size()) {
								ti++;
								auto const& next = tokens[ti];
								value_str += ' ';

								if (!next.empty() && next.back() == '"') {
									value_str += next.substr(0, next.size() - 1);
									break;
								}

								value_str += next;
							}
						}
					}

					Filter filter;
					filter.field = field;
					filter.op = operation;
					filter.str_value = value_str;
					result.filters.push_back(filter);
					parsed_as_filter = true;
					break;
				}

				if (is_string_field(field) && (operation == CompOp::Eq || operation == CompOp::Neq)) {
					if (!value_str.empty() && value_str.front() == '"') {
						value_str = value_str.substr(1);
						if (!value_str.empty() && value_str.back() == '"') {
							value_str.pop_back();
						} else {
							while (ti + 1 < tokens.size()) {
								ti++;
								auto const& next = tokens[ti];
								value_str += ' ';
								if (!next.empty() && next.back() == '"') {
									value_str += next.substr(0, next.size() - 1);
									break;
								}

								value_str += next;
							}
						}
					}

					Filter filter;
					filter.field = field;
					filter.op = operation;
					filter.str_value = value_str;
					result.filters.push_back(filter);
					parsed_as_filter = true;
					break;
				}

				if (!is_string_field(field)) {
					bool valid_number = !value_str.empty();
					for (std::size_t ci = 0; ci < value_str.size(); ++ci) {
						char chr = value_str[ci];
						if (ci == 0 && chr == '-') {
							if (value_str.size() == 1) {
								valid_number = false;
							}
							continue;
						}
						if (std::isdigit(static_cast<unsigned char>(chr)) == 0) {
							valid_number = false;
							break;
						}
					}
					if (valid_number) {
						int val = std::stoi(value_str);
						Filter filter;
						filter.field = field;
						filter.op = operation;
						filter.int_value = val;
						result.filters.push_back(filter);
						parsed_as_filter = true;
					}
				}
				break;
			}

			if (!parsed_as_filter) {
				search_tokens.push_back(token);
			}
		}

		for (std::size_t i = 0; i < search_tokens.size(); ++i) {
			if (i > 0) {
				result.search_text += ' ';
			}
			result.search_text += search_tokens[i];
		}

		return result;
	}

	bool level_passes(GJGameLevel* level, Vec<Filter> const& filters) {
		if ((level == nullptr) || filters.empty()) {
			return true;
		}

		for (auto const& filter : filters) {
			switch (filter.field) {
				case Field::Id: {
					if (!compare_int(static_cast<int>(level->m_levelID), filter.op, filter.int_value)) {
						return false;
					}
					break;
				}
				case Field::Downloads: {
					if (!compare_int(level->m_downloads, filter.op, filter.int_value)) {
						return false;
					}
					break;
				}
				case Field::Likes: {
					if (!compare_int(level->m_likes, filter.op, filter.int_value)) {
						return false;
					}
					break;
				}
				case Field::Stars: {
					if (!compare_int(level->m_starsRequested, filter.op, filter.int_value)) {
						return false;
					}
					break;
				}
				case Field::Length: {
					if (!compare_int(level->m_levelLength, filter.op, filter.int_value)) {
						return false;
					}
					break;
				}
				case Field::Objects: {
					if (!compare_int(level->m_objectCount, filter.op, filter.int_value)) {
						return false;
					}
					break;
				}
				case Field::Featured: {
					if (!compare_int(level->m_featured, filter.op, filter.int_value)) {
						return false;
					}
					break;
				}
				case Field::Epic: {
					if (!compare_int(level->m_isEpic, filter.op, filter.int_value)) {
						return false;
					}
					break;
				}
				case Field::Name: {
					auto name = String(level->m_levelName);
					if (filter.op == CompOp::Contains || filter.op == CompOp::NotContains) {
						if (!compare_str_contains(name, filter.str_value, filter.op == CompOp::NotContains)) {
							return false;
						}
					} else if (filter.op == CompOp::Eq) {
						if (to_lower(name) != to_lower(filter.str_value)) {
							return false;
						}
					} else if (filter.op == CompOp::Neq) {
						if (to_lower(name) == to_lower(filter.str_value)) {
							return false;
						}
					}
					break;
				}
				case Field::Creator: {
					auto creator = String(level->m_creatorName);
					if (filter.op == CompOp::Contains || filter.op == CompOp::NotContains) {
						if (!compare_str_contains(creator, filter.str_value, filter.op == CompOp::NotContains)) {
							return false;
						}
					} else if (filter.op == CompOp::Eq) {
						if (to_lower(creator) != to_lower(filter.str_value)) {
							return false;
						}
					} else if (filter.op == CompOp::Neq) {
						if (to_lower(creator) == to_lower(filter.str_value)) {
							return false;
						}
					}
					break;
				}
			}
		}

		return true;
	}
} // namespace query
