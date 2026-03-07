#include "fuzzy.hpp"

#include <cassert>
#include <cctype>

namespace fuzzy {
	namespace detail {
		[[nodiscard]]
		static inline char to_lower(char c) {
			return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

		[[nodiscard]]
		static inline bool is_word_sep(char c) {
			return c == ' ' || c == '_' || c == '-' || c == '.' || c == '/' || c == '\\';
		}

		[[nodiscard]]
		static inline bool is_word_start(char prev, char cur) {
			if (is_word_sep(prev)) {
				return true;
			}
			auto uc_prev = static_cast<unsigned char>(prev);
			auto uc_cur = static_cast<unsigned char>(cur);
			if (std::islower(uc_prev) && std::isupper(uc_cur)) {
				return true;
			}
			if (std::isdigit(uc_prev) && std::isalpha(uc_cur)) {
				return true;
			}
			return false;
		}

		[[nodiscard]]
		static inline int pos_bonus(std::string_view haystack, int hi) {
			if (hi == 0) {
				return BONUS_STR_START;
			}
			if (is_word_start(haystack[static_cast<std::size_t>(hi - 1)], haystack[static_cast<std::size_t>(hi)])) {
				return BONUS_WORD_START;
			}
			auto uc_prev = static_cast<unsigned char>(haystack[static_cast<std::size_t>(hi - 1)]);
			auto uc_cur = static_cast<unsigned char>(haystack[static_cast<std::size_t>(hi)]);
			if (std::islower(uc_prev) && std::isupper(uc_cur)) {
				return BONUS_CAMEL_CASE;
			}
			return 0;
		}

		struct DpResult {
			int best_score;
			int best_hi;

			std::vector<std::vector<int>> dp_table;
			std::vector<std::vector<int>> run_table;
		};

		static DpResult run_dp(std::string_view needle, std::string_view haystack, bool need_positions, int max_typos) {
			const int n = static_cast<int>(needle.size());
			const int h = static_cast<int>(haystack.size());

			if (n == 0 || h == 0 || n > h) {
				return {SCORE_NO_MATCH, -1, {}, {}};
			}

			std::vector<int> dp(static_cast<std::size_t>(n * h), SCORE_NO_MATCH);
			std::vector<int> run(static_cast<std::size_t>(n * h), 0);
			std::vector<int> typos(static_cast<std::size_t>(n * h), 0);

			auto cell = [&](int ni, int hi) -> int& { return dp[ni * h + hi]; };
			auto runc = [&](int ni, int hi) -> int& { return run[ni * h + hi]; };
			auto typoc = [&](int ni, int hi) -> int& { return typos[ni * h + hi]; };

			for (int hi = 0; hi < h; ++hi) {
				const bool exact = to_lower(needle[0]) == to_lower(haystack[static_cast<std::size_t>(hi)]);
				if (!exact && max_typos <= 0) {
					continue;
				}
				const int typo_pen = exact ? 0 : PENALTY_TYPO;
				cell(0, hi) = SCORE_MATCH + typo_pen + pos_bonus(haystack, hi) + hi * PENALTY_SKIP;
				runc(0, hi) = 1;
				typoc(0, hi) = exact ? 0 : 1;
			}

			for (int ni = 1; ni < n; ++ni) {
				const char nc = to_lower(needle[static_cast<std::size_t>(ni)]);
				for (int hi = ni; hi < h; ++hi) {
					const bool exact = to_lower(haystack[static_cast<std::size_t>(hi)]) == nc;
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
						const int candidate = cell(ni - 1, phi) + (hi - phi - 1) * PENALTY_SKIP;
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
			for (int hi = n - 1; hi < h; ++hi) {
				if (cell(n - 1, hi) > best_score) {
					best_score = cell(n - 1, hi);
					best_hi = hi;
				}
			}

			if (!need_positions) {
				return {best_score, best_hi, {}, {}};
			}

			std::vector<std::vector<int>> dp_table(
				static_cast<std::size_t>(n),
				std::vector<int>(static_cast<std::size_t>(h))
			);
			std::vector<std::vector<int>> run_table(
				static_cast<std::size_t>(n),
				std::vector<int>(static_cast<std::size_t>(h))
			);
			for (int ni = 0; ni < n; ++ni) {
				for (int hi = 0; hi < h; ++hi) {
					dp_table[static_cast<std::size_t>(ni)][static_cast<std::size_t>(hi)] = cell(ni, hi);
					run_table[static_cast<std::size_t>(ni)][static_cast<std::size_t>(hi)] = runc(ni, hi);
				}
			}

			return {best_score, best_hi, std::move(dp_table), std::move(run_table)};
		}

		static std::vector<int>
		backtrack(std::string_view needle, std::vector<std::vector<int>> const& dp_table, int start_hi) {
			const int n = static_cast<int>(needle.size());
			std::vector<int> positions(static_cast<std::size_t>(n), -1);

			int cur_hi = start_hi;
			for (int ni = n - 1; ni >= 0; --ni) {
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
					const int candidate = val + (cur_hi - phi - 1) * PENALTY_SKIP;
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

	bool is_subsequence(std::string_view needle, std::string_view haystack) {
		std::size_t ni = 0;
		for (std::size_t hi = 0; hi < haystack.size() && ni < needle.size(); ++hi) {
			if (detail::to_lower(needle[ni]) == detail::to_lower(haystack[hi])) {
				++ni;
			}
		}
		return ni == needle.size();
	}

	int score_of(std::string_view needle, std::string_view haystack, int max_typos) {
		if (needle.empty()) {
			return 0;
		}
		return detail::run_dp(needle, haystack, false, max_typos).best_score;
	}

	std::optional<MatchResult> find_match(std::string_view needle, std::string_view haystack) {
		if (needle.empty()) {
			return MatchResult {0, {}};
		}

		auto res = detail::run_dp(needle, haystack, true, 0);
		if (res.best_score == SCORE_NO_MATCH) {
			return std::nullopt;
		}

		auto positions = detail::backtrack(needle, res.dp_table, res.best_hi);
		return MatchResult {res.best_score, std::move(positions)};
	}

	std::string normalise(std::string_view raw) {
		std::string out;
		out.reserve(raw.size());
		bool prev_was_space = true;
		for (char c : raw) {
			const bool is_ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
			if (is_ws) {
				if (!prev_was_space) {
					out += ' ';
				}
				prev_was_space = true;
			} else {
				out += detail::to_lower(c);
				prev_was_space = false;
			}
		}

		if (!out.empty() && out.back() == ' ') {
			out.pop_back();
		}
		return out;
	}
} // namespace fuzzy
