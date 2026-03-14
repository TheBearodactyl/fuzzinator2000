#include "fuzzy.hpp"

#include <cassert>
#include <cctype>

#include "utils.hpp"

namespace fuzzy {
	namespace detail {
		[[nodiscard]]
		static inline auto to_lower(char chr) -> char {
			return static_cast<char>(std::tolower(static_cast<unsigned char>(chr)));
		}

		[[nodiscard]]
		static inline auto is_word_sep(char chr) -> bool {
			return chr == ' ' || chr == '_' || chr == '-' || chr == '.' || chr == '/' || chr == '\\';
		}

		[[nodiscard]]
		static inline auto is_word_start(char prev, char cur) -> bool {
			if (is_word_sep(prev)) {
				return true;
			}

			auto uc_prev = static_cast<unsigned char>(prev);
			auto uc_cur = static_cast<unsigned char>(cur);

			if ((std::islower(uc_prev) != 0) && (std::isupper(uc_cur) != 0)) {
				return true;
			}

			if ((std::isdigit(uc_prev) != 0) && (std::isalpha(uc_cur) != 0)) {
				return true;
			}
			return false;
		}

		[[nodiscard]]
		static inline auto pos_bonus(StringV haystack, int hidx) -> int {
			if (hidx == 0) {
				return BONUS_STR_START;
			}

			if (is_word_start(haystack[static_cast<std::size_t>(hidx - 1)], haystack[static_cast<std::size_t>(hidx)])) {
				return BONUS_WORD_START;
			}

			auto uc_prev = static_cast<unsigned char>(haystack[static_cast<std::size_t>(hidx - 1)]);
			auto uc_cur = static_cast<unsigned char>(haystack[static_cast<std::size_t>(hidx)]);

			if ((std::islower(uc_prev) != 0) && (std::isupper(uc_cur) != 0)) {
				return BONUS_CAMEL_CASE;
			}
			return 0;
		}

		struct DpResult {
			int best_score;
			int best_hi;

			Vec<Vec<int>> dp_table;
			Vec<Vec<int>> run_table;
		};

		static auto run_dp(needle_haysack, bool need_positions, int max_typos) -> DpResult {
			const int needle_size = static_cast<int>(needle.size());
			const int haystack_size = static_cast<int>(haystack.size());

			if (needle_size == 0 || haystack_size == 0 || needle_size > haystack_size) {
				return {
					.best_score = SCORE_NO_MATCH,
					.best_hi = -1,
					.dp_table = {},
					.run_table = {},
				};
			}

			Vec<int> dp(static_cast<std::size_t>(needle_size * haystack_size), SCORE_NO_MATCH);
			Vec<int> run(static_cast<std::size_t>(needle_size * haystack_size), 0);
			Vec<int> typos(static_cast<std::size_t>(needle_size * haystack_size), 0);

			auto cell = [&](int needle_index, int haystack_index) -> int& {
				return dp[(needle_index * haystack_size) + haystack_index];
			};

			auto runc = [&](int needle_index, int haystack_index) -> int& {
				return run[(needle_index * haystack_size) + haystack_index];
			};

			auto typoc = [&](int needle_index, int haystack_index) -> int& {
				return typos[(needle_index * haystack_size) + haystack_index];
			};

			for (int hi = 0; hi < haystack_size; ++hi) {
				const bool exact = to_lower(needle[0]) == to_lower(haystack[static_cast<std::size_t>(hi)]);

				if (!exact && max_typos <= 0) {
					continue;
				}

				const int typo_pen = exact ? 0 : PENALTY_TYPO;

				cell(0, hi) = SCORE_MATCH + typo_pen + pos_bonus(haystack, hi) + (hi * PENALTY_SKIP);
				runc(0, hi) = 1;
				typoc(0, hi) = exact ? 0 : 1;
			}

			for (int ni = 1; ni < needle_size; ++ni) {
				const char needle_char = to_lower(needle[static_cast<std::size_t>(ni)]);
				for (int hi = ni; hi < haystack_size; ++hi) {
					const bool exact = to_lower(haystack[static_cast<std::size_t>(hi)]) == needle_char;
					if (!exact && max_typos <= 0) {
						continue;
					}

					int best_prev = SCORE_NO_MATCH;
					int best_prev_hi = -1;
					int best_prev_typos = 0;
					for (int phi = ni - 1; phi < hi; ++phi) {
						if (cell(ni - 1, phi) == SCORE_NO_MATCH) {
							continue;
						}
						const int prev_t = typoc(ni - 1, phi);
						const int new_t = prev_t + (exact ? 0 : 1);
						if (new_t > max_typos) {
							continue;
						}
						const int candidate = cell(ni - 1, phi) + ((hi - phi - 1) * PENALTY_SKIP);
						if (candidate > best_prev) {
							best_prev = candidate;
							best_prev_hi = phi;
							best_prev_typos = prev_t;
						}
					}

					if (best_prev == SCORE_NO_MATCH) {
						continue;
					}

					const int typo_pen = exact ? 0 : PENALTY_TYPO;

					int run_len = 1;
					int run_bonus = 0;
					if (best_prev_hi == hi - 1) {
						run_len = runc(ni - 1, best_prev_hi) + 1;
						run_bonus = BONUS_CONSECUTIVE * (run_len - 1);
					} else {
						run_bonus = (hi - best_prev_hi - 1) * PENALTY_INNER_SKIP;
					}

					cell(ni, hi) = best_prev + SCORE_MATCH + typo_pen + pos_bonus(haystack, hi) + run_bonus;
					runc(ni, hi) = run_len;
					typoc(ni, hi) = best_prev_typos + (exact ? 0 : 1);
				}
			}

			int best_score = SCORE_NO_MATCH;
			int best_hi = -1;
			for (int hi = needle_size - 1; hi < haystack_size; ++hi) {
				if (cell(needle_size - 1, hi) > best_score) {
					best_score = cell(needle_size - 1, hi);
					best_hi = hi;
				}
			}

			if (!need_positions) {
				return {
					.best_score = best_score,
					.best_hi = best_hi,
					.dp_table = {},
					.run_table = {},
				};
			}

			Vec<Vec<int>> dp_table(
				static_cast<std::size_t>(needle_size),
				std::vector<int>(static_cast<std::size_t>(haystack_size))
			);
			Vec<Vec<int>> run_table(
				static_cast<std::size_t>(needle_size),
				std::vector<int>(static_cast<std::size_t>(haystack_size))
			);
			for (int ni = 0; ni < needle_size; ++ni) {
				for (int hi = 0; hi < haystack_size; ++hi) {
					dp_table[static_cast<std::size_t>(ni)][static_cast<std::size_t>(hi)] = cell(ni, hi);
					run_table[static_cast<std::size_t>(ni)][static_cast<std::size_t>(hi)] = runc(ni, hi);
				}
			}

			return {
				.best_score = best_score,
				.best_hi = best_hi,
				.dp_table = std::move(dp_table),
				.run_table = std::move(run_table),
			};
		}

		static auto backtrack(StringV needle, Vec<Vec<int>> const& dp_table, int start_hi) -> Vec<int> {
			const int needle_size = static_cast<int>(needle.size());
			Vec<int> positions(static_cast<std::size_t>(needle_size), -1);

			int cur_hi = start_hi;
			for (int ni = needle_size - 1; ni >= 0; --ni) {
				positions[static_cast<std::size_t>(ni)] = cur_hi;
				if (ni == 0) {
					break;
				}

				int best = SCORE_NO_MATCH;
				int prev_hi = -1;
				for (int phi = ni - 1; phi < cur_hi; ++phi) {
					const int val = dp_table[static_cast<std::size_t>(ni - 1)][static_cast<std::size_t>(phi)];
					if (val == SCORE_NO_MATCH) {
						continue;
					}
					const int candidate = val + ((cur_hi - phi - 1) * PENALTY_SKIP);
					if (candidate > best) {
						best = candidate;
						prev_hi = phi;
					}
				}
				if (prev_hi < 0) {
					break;
				}
				cur_hi = prev_hi;
			}
			return positions;
		}

	} // namespace detail

	auto is_subsequence(needle_haysack) -> bool {
		std::size_t needle_index = 0;
		for (std::size_t hi = 0; hi < haystack.size() && needle_index < needle.size(); ++hi) {
			if (detail::to_lower(needle[needle_index]) == detail::to_lower(haystack[hi])) {
				++needle_index;
			}
		}
		return needle_index == needle.size();
	}

	auto score_of(needle_haysack, int max_typos) -> int {
		if (needle.empty()) {
			return 0;
		}

		return detail::run_dp(needle, haystack, false, max_typos).best_score;
	}

	auto find_match(needle_haysack) -> Option<MatchResult> {
		if (needle.empty()) {
			return MatchResult {
				.score = 0,
				.positions = {},
			};
		}

		auto res = detail::run_dp(needle, haystack, true, 0);
		if (res.best_score == SCORE_NO_MATCH) {
			return std::nullopt;
		}

		auto positions = detail::backtrack(needle, res.dp_table, res.best_hi);
		return MatchResult {
			.score = res.best_score,
			.positions = std::move(positions),
		};
	}

	auto normalise(StringV raw) -> String {
		String out;
		out.reserve(raw.size());
		bool prev_was_space = true;
		for (char chr : raw) {
			const bool is_ws = (chr == ' ' || chr == '\t' || chr == '\n' || chr == '\r');
			if (is_ws) {
				if (!prev_was_space) {
					out += ' ';
				}
				prev_was_space = true;
			} else {
				out += detail::to_lower(chr);
				prev_was_space = false;
			}
		}

		if (!out.empty() && out.back() == ' ') {
			out.pop_back();
		}
		return out;
	}
} // namespace fuzzy
