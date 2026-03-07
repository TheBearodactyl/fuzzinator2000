#pragma once

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fuzzy {
	constexpr int SCORE_MATCH = 16;
	constexpr int BONUS_CONSECUTIVE = 32;
	constexpr int BONUS_WORD_START = 48;
	constexpr int BONUS_STR_START = 64;
	constexpr int BONUS_CAMEL_CASE = 24;
	constexpr int PENALTY_SKIP = -3;
	constexpr int PENALTY_INNER_SKIP = -1;
	constexpr int PENALTY_TYPO = -20;
	constexpr int SCORE_NO_MATCH = std::numeric_limits<int>::min();

	struct MatchResult {
		int score;
		std::vector<int> positions;
	};

	[[nodiscard]]
	std::optional<MatchResult> find_match(std::string_view needle, std::string_view haystack);

	[[nodiscard]]
	int score_of(std::string_view needle, std::string_view haystack, int max_typos = 0);

	[[nodiscard]]
	bool is_subsequence(std::string_view needle, std::string_view haystack);

	[[nodiscard]]
	std::string normalise(std::string_view raw);

	template<typename T, typename KeyFn>
	void sort_by_score(std::vector<T>& items, std::string_view query, KeyFn key_fn);
} // namespace fuzzy

namespace fuzzy {
	template<typename T, typename KeyFn>
	void sort_by_score(std::vector<T>& items, std::string_view query, KeyFn key_fn) {
		if (query.empty()) {
			return;
		}

		std::vector<std::pair<int, std::size_t>> scored;
		scored.reserve(items.size());
		for (std::size_t i = 0; i < items.size(); ++i) {
			scored.emplace_back(score_of(query, key_fn(items[i])), i);
		}

		std::stable_sort(scored.begin(), scored.end(), [](auto const& a, auto const& b) { return a.first > b.first; });

		std::vector<T> sorted;
		sorted.reserve(items.size());
		for (auto const& [sc, idx] : scored) {
			sorted.push_back(std::move(items[idx]));
		}
		items = std::move(sorted);
	}
} // namespace fuzzy
