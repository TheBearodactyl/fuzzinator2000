#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dictionary {
	struct Candidate {
		std::string word;
		int distance;
	};

	bool load(std::filesystem::path const& path);

	[[nodiscard]]
	bool loaded();

	[[nodiscard]]
	std::size_t word_count();

	[[nodiscard]]
	bool contains(std::string_view word);

	[[nodiscard]]
	std::vector<Candidate> find_corrections(std::string_view word, int max_distance, int max_results = 10);

	[[nodiscard]]
	std::vector<std::pair<std::string, std::string>> try_split(std::string_view word);
} // namespace dictionary
