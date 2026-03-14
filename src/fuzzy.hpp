#pragma once

#include <algorithm>
#include <limits>
#include <vector>

#include "utils.hpp"

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
		Vec<int> positions;
	};

	[[nodiscard]]
	auto find_match(needle_haysack) -> Option<MatchResult>;

	[[nodiscard]]
	auto score_of(needle_haysack, int max_typos = 0) -> int;

	[[nodiscard]]
	auto is_subsequence(needle_haysack) -> bool;

	[[nodiscard]]
	auto normalise(StringV raw) -> String;

	template<typename T, typename KeyFn>
	void sort_by_score(Vec<T>& items, StringV query, KeyFn key_fn);
} // namespace fuzzy

namespace fuzzy {
	template<typename T, typename KeyFn>
	void sort_by_score(Vec<T>& items, StringV query, KeyFn key_fn) {
		if (query.empty()) {
			return;
		}

		Vec<std::pair<int, std::size_t>> scored;

		scored.reserve(items.size());

		for (std::size_t i = 0; i < items.size(); ++i) {
			scored.emplace_back(score_of(query, key_fn(items[i])), i);
		}

		std::stable_sort(scored.begin(), scored.end(), [](auto const& lhs, auto const& rhs) -> auto {
			return lhs.first > rhs.first;
		});

		Vec<T> sorted;

		sorted.reserve(items.size());

		for (auto const& [unused, idx] : scored) {
			sorted.push_back(std::move(items[idx]));
		}

		items = std::move(sorted);
	}
} // namespace fuzzy
