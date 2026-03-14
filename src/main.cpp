#include <algorithm>
#include <list>
#include <memory>
#include <unordered_set>
#include <utility>

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/utils/web.hpp>

#include "dictionary.hpp"
#include "fuzzy.hpp"
#include "query.hpp"

using namespace geode::prelude;

template<typename T>
using Vec = std::vector<T>;

using String = std::string;

static const String GDHISTORY_SEARCH_URL = "https://history.geometrydash.eu/api/v1/search/level/advanced/";

namespace {
	CCArray* pending_levels = nullptr;
	bool INJECT_ON_LOAD = false;
} // namespace

$on_mod(Loaded) {
	auto path = Mod::get()->getResourcesDir() / "dictionary.txt";
	if (dictionary::load(path)) {
		log::info("[The Fuzz-inator 2000] Dictionary loaded ({} words)", dictionary::word_count());
	} else {
		log::warn("[The Fuzz-inator 2000] Could not load dictionary from {}", path.string());
	}
}

[[nodiscard]]
static auto json_int(matjson::Value const& obj, std::string_view key, int fallback = 0) -> int {
	auto const& val = obj[key];
	if (!val.isNumber()) {
		return fallback;
	}
	return static_cast<int>(val.asInt().unwrapOr(fallback));
}

[[nodiscard]]
static auto json_str(matjson::Value const& obj, std::string_view key) -> String {
	auto const& val = obj[key];
	if (!val.isString()) {
		return {};
	}
	return val.asString().unwrapOr("");
}

[[nodiscard]]
static auto json_bool(matjson::Value const& obj, std::string_view key) -> bool {
	auto const& val = obj[key];
	if (val.isBool()) {
		return val.asBool().unwrapOr(false);
	}
	if (val.isNumber()) {
		return val.asInt().unwrapOr(0) != 0;
	}
	return false;
}

[[nodiscard]]
static constexpr auto is_demon(int diff) -> bool {
	return diff >= 7 && diff <= 11;
}

[[nodiscard]]
static auto level_from_hit(matjson::Value const& hit) -> std::optional<GJGameLevel*> {
	const int online_id = json_int(hit, "online_id");
	if (online_id <= 0) {
		return std::nullopt;
	}

	auto* level = GJGameLevel::create();
	if (level == nullptr) {
		return std::nullopt;
	}

	level->setLevelID(online_id);
	level->m_levelName = json_str(hit, "cache_level_name");
	level->m_creatorName = json_str(hit, "cache_username");
	level->m_userID = json_int(hit, "cache_user_id");
	level->m_accountID = json_int(hit, "cache_account_id");
	level->m_downloads = json_int(hit, "cache_downloads");
	level->m_likes = json_int(hit, "cache_likes");
	level->m_levelLength = json_int(hit, "cache_length");
	level->m_featured = json_int(hit, "cache_featured");
	level->m_isEpic = json_int(hit, "cache_epic");
	level->m_twoPlayerMode = json_bool(hit, "cache_two_player");
	level->m_gameVersion = json_int(hit, "cache_game_version", 21);
	level->m_objectCount = json_int(hit, "cache_object_count");

	const int stars = json_int(hit, "cache_stars");
	level->setStars(stars);
	level->m_starsRequested = stars;

	const int filter_diff = json_int(hit, "cache_filter_difficulty");
	if (filter_diff == 1) {
		level->m_autoLevel = true;
	} else if (is_demon(filter_diff)) {
		level->setDemon(1);
		level->m_demonDifficulty = filter_diff - 6;
		level->m_ratings = 10;
		level->m_ratingsSum = 50;
	} else if (filter_diff >= 2 && filter_diff <= 6) {
		level->m_ratings = 10;
		level->m_ratingsSum = (filter_diff - 1) * 10;
	}

	const int song_id = json_int(hit, "cache_song_id");
	if (song_id > 0) {
		level->m_songID = song_id;
		level->m_audioTrack = 0;
	} else {
		level->m_audioTrack = json_int(hit, "cache_audiotrack");
	}

	level->m_levelType = GJLevelType::Saved;
	level->m_levelNotDownloaded = true;

	return level;
}

[[nodiscard]]
static auto url_encode(std::string_view raw) -> String {
	String out;
	out.reserve(raw.size() * 3);
	for (unsigned char chr : raw) {
		if ((std::isalnum(chr) != 0) || chr == '-' || chr == '_' || chr == '.' || chr == '~') {
			out += static_cast<char>(chr);
		} else if (chr == ' ') {
			out += '+';
		} else {
			constexpr char hex[] = "0123456789ABCDEF";
			out += '%';
			out += hex[(chr >> 4) & 0xF];
			out += hex[chr & 0xF];
		}
	}
	return out;
}

[[nodiscard]]
static auto split_words(String const& str) -> Vec<std::string> {
	std::vector<std::string> words;
	String word;
	for (char chr : str) {
		if (chr == ' ') {
			if (!word.empty()) {
				words.push_back(word);
				word.clear();
			}
		} else {
			word += chr;
		}
	}
	if (!word.empty()) {
		words.push_back(word);
	}
	return words;
}

[[nodiscard]]
static auto reconstruct_query(Vec<std::string> const& words, std::size_t rep_idx, String const& rep) -> String {
	String full;
	for (std::size_t i = 0; i < words.size(); ++i) {
		if (i > 0) {
			full += ' ';
		}
		full += (i == rep_idx) ? rep : words[i];
	}
	return full;
}

[[nodiscard]]
static auto reconstruct_query_split(
	std::vector<std::string> const& words,
	std::size_t split_idx,
	String const& left,
	String const& right
) -> String {
	String full;
	for (std::size_t i = 0; i < words.size(); ++i) {
		if (i > 0) {
			full += ' ';
		}
		if (i == split_idx) {
			full += left + ' ' + right;
		} else {
			full += words[i];
		}
	}
	return full;
}

[[nodiscard]]
static auto generate_query_variations(String const& query, int max_count, int med) -> std::vector<std::string> {
	std::vector<std::string> result;
	std::unordered_set<std::string> seen;

	auto try_add = [&](String const& str) -> void {
		if (str.empty() || std::cmp_greater_equal(result.size(), max_count)) {
			return;
		}
		if (seen.contains(str)) {
			return;
		}
		seen.insert(str);
		result.push_back(str);
	};

	try_add(query);

	auto words = split_words(query);
	if (words.empty()) {
		return result;
	}

	if (dictionary::loaded()) {
		for (std::size_t wi = 0; wi < words.size(); ++wi) {
			const String lower = fuzzy::normalise(words[wi]);

			if (dictionary::contains(lower)) {
				continue;
			}

			auto candidates = dictionary::find_corrections(lower, med, 5);
			for (auto const& [candidate, _dist] : candidates) {
				try_add(reconstruct_query(words, wi, candidate));
			}

			auto splits = dictionary::try_split(lower);
			for (auto const& [left, right] : splits) {
				try_add(reconstruct_query_split(words, wi, left, right));
			}
		}
	}

	if (words.size() > 1) {
		for (auto const& word : words) {
			try_add(word);
		}
	}

	return result;
}

[[nodiscard]]
static auto weighted_score(
	std::string_view query,
	std::string_view name,
	std::string_view creator,
	int name_weight,
	int creator_weight,
	int max_typos
) -> int {
	int total = 0;
	if (name_weight > 0 && !name.empty()) {
		const int score = fuzzy::score_of(query, name, max_typos);
		if (score != fuzzy::SCORE_NO_MATCH) {
			total += (score * name_weight) / 100;
		}
	}

	if (creator_weight > 0 && !creator.empty()) {
		const int score = fuzzy::score_of(query, creator, max_typos);
		if (score != fuzzy::SCORE_NO_MATCH) {
			total += (score * creator_weight) / 100;
		}
	}

	return total;
}

struct SearchContext {
	String norm_query;
	query::ParsedQuery parsed;
	int remaining;
	int failed_count;
	int total_queries;
	std::unordered_set<int> seen_ids;
	Ref<CCArray> collected;

	SearchContext(String norm_query, query::ParsedQuery parsed_query, int count) :
		norm_query(std::move(norm_query)),
		parsed(std::move(parsed_query)),
		remaining(count),
		failed_count(0),
		total_queries(count),
		collected(CCArray::create()) {}
};

class $modify(FuzzySearchLayer, LevelSearchLayer) {
	struct Fields {
		std::list<async::TaskHolder<web::WebResponse>> requests;
		std::shared_ptr<SearchContext> search_ctx;
		String pending_query;
		Ref<CCObject> pending_sender;
		bool is_searching = false;
	};

	bool init(int type) {
		if (!LevelSearchLayer::init(type)) {
			return false;
		}

		if (m_searchInput != nullptr) {
			auto* input = m_searchInput;
			if (!input->m_allowedChars.empty()) {
				input->m_allowedChars = input->m_allowedChars + gd::string("\"<>=~!");
			}
			input->m_maxLabelLength = 256;
		}

		return true;
	}

	static void show_toast(String const& msg) {
		Notification::create(msg, NotificationIcon::None, 2.5F)->show();
	}

	void cancel_searches() {
		m_fields->requests.clear();
		m_fields->search_ctx.reset();
	}

	void do_fallback() {
		if (!m_fields->is_searching) {
			return;
		}
		m_fields->is_searching = false;
		cancel_searches();
		show_toast("GDHistory unreachable — using normal search");
		log::info("[The Fuzz-inator 2000] Falling back to original GD search");
		LevelSearchLayer::onSearch(m_fields->pending_sender.data());
		m_fields->pending_sender = nullptr;
	}

	static void collect_hits(matjson::Value const& json, std::shared_ptr<SearchContext> const& ctx) {
		if (!json.isObject()) {
			return;
		}
		auto hits_opt = json.get("hits");
		if (!hits_opt || !hits_opt.unwrap().isArray()) {
			return;
		}

		auto const& hits = hits_opt.unwrap().asArray().unwrap();
		for (auto const& hit : hits) {
			const int online_id = json_int(hit, "online_id");
			if (online_id <= 0 || ctx->seen_ids.contains(online_id)) {
				continue;
			}
			auto maybe_level = level_from_hit(hit);
			if (!maybe_level) {
				continue;
			}
			if (!query::level_passes(*maybe_level, ctx->parsed.filters)) {
				continue;
			}
			ctx->seen_ids.insert(online_id);
			ctx->collected->addObject(*maybe_level);
		}
	}

	void finalize_search(std::shared_ptr<SearchContext> const& ctx) {
		if (ctx->collected->count() == 0) {
			log::info(
				"[The Fuzz-inator 2000] All queries returned 0 results (failed={}/{})",
				ctx->failed_count,
				ctx->total_queries
			);

			if (!ctx->parsed.filters.empty() && ctx->failed_count < ctx->total_queries) {
				m_fields->is_searching = false;
				cancel_searches();
				m_fields->pending_sender = nullptr;
				show_toast("No levels matched your filters");
				return;
			}

			do_fallback();
			return;
		}

		m_fields->is_searching = false;

		const int name_weight = Mod::get()->getSettingValue<int64_t>("name-weight");
		const int creator_weight = Mod::get()->getSettingValue<int64_t>("creator-weight");
		const int max_typos = Mod::get()->getSettingValue<int64_t>("max-typos");

		struct Entry {
			GJGameLevel* level;
			int score;
		};

		std::vector<Entry> entries;
		entries.reserve(ctx->collected->count());

		for (unsigned int i = 0; i < ctx->collected->count(); ++i) {
			auto* lvl = static_cast<GJGameLevel*>(ctx->collected->objectAtIndex(i));
			int score = weighted_score(
				ctx->norm_query,
				fuzzy::normalise(std::string(lvl->m_levelName)),
				fuzzy::normalise(std::string(lvl->m_creatorName)),
				name_weight,
				creator_weight,
				max_typos
			);

			entries.push_back(
				{
					.level = lvl,
					.score = score,
				}
			);
		}

		if (ctx->parsed.exact_match) {
			std::ranges::stable_sort(entries, [](Entry const& lhs, Entry const& rhs) -> bool {
				return lhs.level->m_downloads > rhs.level->m_downloads;
			});
		} else {
			std::ranges::stable_sort(entries, [](Entry const& lhs, Entry const& rhs) -> bool {
				return lhs.score > rhs.score;
			});
		}

		log::info(
			"[The Fuzz-inator 2000] {} unique levels collected for \"{}\"",
			entries.size(),
			m_fields->pending_query
		);

		for (std::size_t i = 0; i < std::min(entries.size(), std::size_t(5)); ++i) {
			auto* lvl = entries[i].level;
			log::debug(
				"[The Fuzz-inator 2000]   #{}: \"{}\" by {} id={} score={}",
				i + 1,
				std::string(lvl->m_levelName),
				std::string(lvl->m_creatorName),
				static_cast<int>(lvl->m_levelID),
				entries[i].score
			);
		}

		auto* arr = CCArray::create();
		arr->retain();

		for (auto& entry : entries) {
			arr->addObject(entry.level);
		}

		m_fields->pending_sender = nullptr;
		cancel_searches();

		if (pending_levels != nullptr) {
			pending_levels->release();
			pending_levels = nullptr;
		}

		pending_levels = arr;
		INJECT_ON_LOAD = true;

		auto* search_obj = GJSearchObject::create(SearchType::Search);
		search_obj->m_searchQuery = m_fields->pending_query;
		auto* scene = LevelBrowserLayer::scene(search_obj);
		CCDirector::get()->replaceScene(CCTransitionFade::create(0.5F, scene));
	}

	void on_response(web::WebResponse const& res, const std::shared_ptr<SearchContext>& ctx) {
		if (ctx != m_fields->search_ctx) {
			return;
		}
		if (!m_fields->is_searching) {
			return;
		}

		if (res.ok()) {
			auto json_result = res.json();
			if (json_result) {
				collect_hits(json_result.unwrap(), ctx);
			} else {
				log::warn("[The Fuzz-inator 2000] JSON parse error: {}", json_result.unwrapErr());
			}
		} else {
			log::warn("[The Fuzz-inator 2000] GDHistory HTTP {}", res.code());
			ctx->failed_count++;
		}

		ctx->remaining--;
		if (ctx->remaining <= 0) {
			finalize_search(ctx);
		}
	}

	void onSearch(CCObject* sender) {
		if (m_fields->is_searching) {
			return;
		}

		if (!Mod::get()->getSettingValue<bool>("enabled")) {
			LevelSearchLayer::onSearch(sender);
			return;
		}

		if (m_searchInput == nullptr) {
			LevelSearchLayer::onSearch(sender);
			return;
		}

		const String raw_query = m_searchInput->getString();
		if (raw_query.empty()) {
			LevelSearchLayer::onSearch(sender);
			return;
		}

		auto parsed = query::parse(raw_query);

		if (parsed.search_text.empty() && parsed.filters.empty()) {
			LevelSearchLayer::onSearch(sender);
			return;
		}

		m_fields->pending_query = raw_query;
		m_fields->pending_sender = sender;
		m_fields->is_searching = true;

		const int limit = Mod::get()->getSettingValue<int64_t>("result-limit");
		const int timeout = Mod::get()->getSettingValue<int64_t>("timeout-secs");
		const int max_queries = Mod::get()->getSettingValue<int64_t>("query-variations");
		const int max_typos = Mod::get()->getSettingValue<int64_t>("max-typos");

		std::vector<std::string> variations;
		if (parsed.exact_match) {
			variations.push_back(parsed.search_text);
		} else if (!parsed.search_text.empty()) {
			variations = generate_query_variations(parsed.search_text, max_queries, max_typos);
		} else {
			variations.push_back("");
		}

		const auto norm_query = fuzzy::normalise(parsed.search_text);

		log::info(
			"[The Fuzz-inator 2000] Searching with {} query variation(s) for \"{}\"{}",
			variations.size(),
			raw_query,
			parsed.exact_match ? " (exact match)" : ""
		);
		for (auto const& var : variations) {
			log::debug("[The Fuzz-inator 2000]   variation: \"{}\"", var);
		}
		if (!parsed.filters.empty()) {
			log::info("[The Fuzz-inator 2000] {} filter(s) active", parsed.filters.size());
		}

		show_toast(parsed.exact_match ? "Exact searching via GDHistory…" : "Fuzzy searching via GDHistory…");

		auto ctx = std::make_shared<SearchContext>(norm_query, std::move(parsed), static_cast<int>(variations.size()));
		m_fields->search_ctx = ctx;

		const int per_query_limit = std::max(5, limit / static_cast<int>(variations.size()));

		Ref<FuzzySearchLayer> self = this;

		for (auto const& variation : variations) {
			String url = GDHISTORY_SEARCH_URL + "?limit=" + std::to_string(per_query_limit);
			if (!variation.empty()) {
				url += "&query=" + url_encode(variation) + "&matching_strategy=last";
			}
			if (ctx->parsed.include_deleted) {
				url += "&is_deleted=true";
			}

			log::info("[The Fuzz-inator 2000] Requesting: {}", url);

			auto req = web::WebRequest();
			req.timeout(std::chrono::seconds(timeout));
			req.userAgent("GeodeFuzzySearch/1.1.0");

			m_fields->requests.emplace_back();
			auto& holder = m_fields->requests.back();

			holder.spawn(req.get(url), [self, ctx](const web::WebResponse& value) -> void {
				if (!self) {
					return;
				}
				self->on_response(value, ctx);
			});
		}
	}
};

class $modify(FuzzyBrowserLayer, LevelBrowserLayer) {
	struct Fields {
		Ref<CCArray> fuzzy_levels;
	};

	void setupLevelBrowser(CCArray* items) {
		if (!INJECT_ON_LOAD || (pending_levels == nullptr)) {
			LevelBrowserLayer::setupLevelBrowser(items);
			return;
		}

		CCArray* arr = pending_levels;
		pending_levels = nullptr;
		INJECT_ON_LOAD = false;

		m_fields->fuzzy_levels = arr;

		log::info("[The Fuzz-inator 2000] Injecting {} levels into LevelBrowserLayer", arr->count());

		LevelBrowserLayer::setupLevelBrowser(arr);
		this->loadLevelsFinished(arr, "", 0);
		arr->release();
	}

	void loadPage(GJSearchObject* obj) {
		if (m_fields->fuzzy_levels != nullptr) {
			return;
		}
		LevelBrowserLayer::loadPage(obj);
	}
};
