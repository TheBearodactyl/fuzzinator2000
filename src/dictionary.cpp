#include "dictionary.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <unordered_set>

#include "utils.hpp"

namespace dictionary {
	static std::unordered_set<std::string> S_SET;
	static Vec<std::string> S_LIST;
	static bool S_LOADED = false;

	[[nodiscard]]
	static std::string to_lower(std::string_view s) {
		std::string out;
		out.reserve(s.size());
		for (char chr : s) {
			out += static_cast<char>(std::tolower(static_cast<unsigned char>(chr)));
		}
		return out;
	}

	[[nodiscard]]
	static int osa_distance(std::string_view lhs, std::string_view rhs, int max_dist) {
		const int m = static_cast<int>(lhs.size());
		const int n = static_cast<int>(rhs.size());

		if (std::abs(m - n) > max_dist) {
			return max_dist + 1;
		}
		if (m == 0) {
			return n;
		}
		if (n == 0) {
			return m;
		}

		Vec<int> pp(static_cast<std::size_t>(n + 1));
		Vec<int> p(static_cast<std::size_t>(n + 1));
		Vec<int> c(static_cast<std::size_t>(n + 1));

		for (int j = 0; j <= n; ++j) {
			p[static_cast<std::size_t>(j)] = j;
		}

		for (int i = 1; i <= m; ++i) {
			c[0] = i;
			int row_min = i;

			const auto ai = static_cast<unsigned char>(lhs[static_cast<std::size_t>(i - 1)]);

			for (int j = 1; j <= n; ++j) {
				const auto sj = static_cast<std::size_t>(j);
				const auto bj = static_cast<unsigned char>(rhs[static_cast<std::size_t>(j - 1)]);

				const int cost = (std::tolower(ai) == std::tolower(bj)) ? 0 : 1;

				int val = std::min({p[sj] + 1, c[sj - 1] + 1, p[sj - 1] + cost});

				if (i > 1 && j > 1) {
					const auto ai_prev = static_cast<unsigned char>(lhs[static_cast<std::size_t>(i - 2)]);
					const auto bj_prev = static_cast<unsigned char>(rhs[static_cast<std::size_t>(j - 2)]);
					if (std::tolower(ai) == std::tolower(bj_prev) && std::tolower(ai_prev) == std::tolower(bj)) {
						val = std::min(val, pp[sj - 2] + 1);
					}
				}

				c[sj] = val;
				row_min = std::min(row_min, val);
			}

			if (row_min > max_dist) {
				return max_dist + 1;
			}

			pp = p;
			p = c;
		}

		return p[static_cast<std::size_t>(n)];
	}

	auto load(std::filesystem::path const& path) -> bool {
		std::ifstream file(path);
		if (!file.is_open()) {
			return false;
		}

		S_SET.clear();
		S_LIST.clear();

		std::string line;
		while (std::getline(file, line)) {
			while (!line.empty() && (std::isspace(static_cast<unsigned char>(line.back())) != 0)) {
				line.pop_back();
			}

			std::size_t start = 0;
			while (start < line.size() && (std::isspace(static_cast<unsigned char>(line[start])) != 0)) {
				++start;
			}
			if (start > 0) {
				line.erase(0, start);
			}

			if (line.empty() || line[0] == '#') {
				continue;
			}

			std::string lower = to_lower(line);
			if (S_SET.insert(lower).second) {
				S_LIST.push_back(lower);
			}
		}

		S_LOADED = true;
		return true;
	}

	bool loaded() {
		return S_LOADED;
	}

	std::size_t word_count() {
		return S_LIST.size();
	}

	bool contains(std::string_view word) {
		return S_SET.count(to_lower(word)) > 0;
	}

	Vec<Candidate> find_corrections(std::string_view word, int max_distance, int max_results) {
		Vec<Candidate> results;
		const std::string lower = to_lower(word);
		const int wlen = static_cast<int>(lower.size());

		for (auto const& dict_word : S_LIST) {
			const int dlen = static_cast<int>(dict_word.size());
			if (std::abs(dlen - wlen) > max_distance) {
				continue;
			}

			const int dist = osa_distance(lower, dict_word, max_distance);
			if (dist > 0 && dist <= max_distance) {
				results.push_back({dict_word, dist});
			}
		}

		std::sort(results.begin(), results.end(), [](Candidate const& a, Candidate const& b) {
			if (a.distance != b.distance) {
				return a.distance < b.distance;
			}
			return a.word < b.word;
		});

		if (static_cast<int>(results.size()) > max_results) {
			results.resize(static_cast<std::size_t>(max_results));
		}

		return results;
	}

	Vec<std::pair<std::string, std::string>> try_split(std::string_view word) {
		Vec<std::pair<std::string, std::string>> results;
		const std::string lower = to_lower(word);

		for (std::size_t i = 2; i + 2 <= lower.size(); ++i) {
			std::string left = lower.substr(0, i);
			std::string right = lower.substr(i);

			if (S_SET.count(left) && S_SET.count(right)) {
				results.emplace_back(std::move(left), std::move(right));
			}
		}

		return results;
	}

} // namespace dictionary
