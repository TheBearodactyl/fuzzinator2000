#include <list>
#include <memory>
#include <unordered_set>

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <Geode/utils/web.hpp>

#include "dictionary.hpp"
#include "fuzzy.hpp"

using namespace geode::prelude;

static const std::string GDHISTORY_SEARCH_URL = "https://history.geometrydash.eu/api/v1/search/level/advanced/";

namespace {
	CCArray* pending_levels = nullptr;
	bool inject_on_load = false;
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
static int json_int(matjson::Value const& obj, std::string_view key, int fallback = 0) {
	auto const& v = obj[key];
	if (!v.isNumber()) {
		return fallback;
	}
	return static_cast<int>(v.asInt().unwrapOr(fallback));
}

[[nodiscard]]
static std::string json_str(matjson::Value const& obj, std::string_view key) {
	auto const& v = obj[key];
	if (!v.isString()) {
		return {};
	}
	return v.asString().unwrapOr("");
}

[[nodiscard]]
static bool json_bool(matjson::Value const& obj, std::string_view key) {
	auto const& v = obj[key];
	if (v.isBool()) {
		return v.asBool().unwrapOr(false);
	}
	if (v.isNumber()) {
		return v.asInt().unwrapOr(0) != 0;
	}
	return false;
}

[[nodiscard]]
static GJDifficulty difficulty_from_filter(int f) {
	switch (f) {
		case 1:
			return GJDifficulty::Auto;
		case 2:
			return GJDifficulty::Easy;
		case 3:
			return GJDifficulty::Normal;
		case 4:
			return GJDifficulty::Hard;
		case 5:
			return GJDifficulty::Harder;
		case 6:
			return GJDifficulty::Insane;
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
			return GJDifficulty::Insane;
		default:
			return GJDifficulty::NA;
	}
}

[[nodiscard]]
static constexpr bool is_demon(int f) {
	return f >= 7 && f <= 11;
}

[[nodiscard]]
static std::optional<GJGameLevel*> level_from_hit(matjson::Value const& hit) {
	const int online_id = json_int(hit, "online_id");
	if (online_id <= 0) {
		return std::nullopt;
	}

	auto* level = GJGameLevel::create();
	if (!level) {
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
	level->m_difficulty = difficulty_from_filter(filter_diff);
	if (filter_diff == 1) {
		level->m_autoLevel = true;
	}
	if (is_demon(filter_diff)) {
		level->setDemon(1);
		level->m_demonDifficulty = filter_diff - 6;
	}

	level->m_ratings = 1;
	switch (filter_diff) {
		case 1:
			level->m_ratingsSum = 1;
			break;
		case 2:
			level->m_ratingsSum = 3;
			break;
		case 3:
			level->m_ratingsSum = 10;
			break;
		case 4:
			level->m_ratingsSum = 20;
			break;
		case 5:
			level->m_ratingsSum = 30;
			break;
		case 6:
			level->m_ratingsSum = 40;
			break;
		default:
			level->m_ratingsSum = 50;
			break;
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
static std::string url_encode(std::string_view raw) {
	std::string out;
	out.reserve(raw.size() * 3);
	for (unsigned char c : raw) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += static_cast<char>(c);
		} else if (c == ' ') {
			out += '+';
		} else {
			constexpr char HEX[] = "0123456789ABCDEF";
			out += '%';
			out += HEX[(c >> 4) & 0xF];
			out += HEX[c & 0xF];
		}
	}
	return out;
}

[[nodiscard]]
static std::vector<std::string> split_words(std::string const& s) {
	std::vector<std::string> words;
	std::string word;
	for (char c : s) {
		if (c == ' ') {
			if (!word.empty()) {
				words.push_back(word);
				word.clear();
			}
		} else {
			word += c;
		}
	}
	if (!word.empty()) {
		words.push_back(word);
	}
	return words;
}

[[nodiscard]]
static std::string
reconstruct_query(std::vector<std::string> const& words, std::size_t replace_idx, std::string const& replacement) {
	std::string full;
	for (std::size_t i = 0; i < words.size(); ++i) {
		if (i > 0) {
			full += ' ';
		}
		full += (i == replace_idx) ? replacement : words[i];
	}
	return full;
}

[[nodiscard]]
static std::string reconstruct_query_split(
	std::vector<std::string> const& words,
	std::size_t split_idx,
	std::string const& left,
	std::string const& right
) {
	std::string full;
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
static std::vector<std::string>
generate_query_variations(std::string const& query, int max_count, int max_edit_distance) {
	std::vector<std::string> result;
	std::unordered_set<std::string> seen;

	auto try_add = [&](std::string const& s) {
		if (s.empty() || static_cast<int>(result.size()) >= max_count) {
			return;
		}
		if (seen.count(s)) {
			return;
		}
		seen.insert(s);
		result.push_back(s);
	};

	try_add(query);

	auto words = split_words(query);
	if (words.empty()) {
		return result;
	}

	if (dictionary::loaded()) {
		for (std::size_t wi = 0; wi < words.size(); ++wi) {
			const std::string lower = fuzzy::normalise(words[wi]);

			if (dictionary::contains(lower)) {
				continue;
			}

			auto candidates = dictionary::find_corrections(lower, max_edit_distance, 5);
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
		for (auto const& w : words) {
			try_add(w);
		}
	}

	return result;
}

[[nodiscard]]
static int weighted_score(
	std::string_view query,
	std::string_view name,
	std::string_view creator,
	int name_weight,
	int creator_weight,
	int max_typos
) {
	int total = 0;
	if (name_weight > 0 && !name.empty()) {
		const int s = fuzzy::score_of(query, name, max_typos);
		if (s != fuzzy::SCORE_NO_MATCH) {
			total += (s * name_weight) / 100;
		}
	}
	if (creator_weight > 0 && !creator.empty()) {
		const int s = fuzzy::score_of(query, creator, max_typos);
		if (s != fuzzy::SCORE_NO_MATCH) {
			total += (s * creator_weight) / 100;
		}
	}
	return total;
}

struct SearchContext {
	std::string norm_query;
	int remaining;
	int failed_count;
	int total_queries;
	std::unordered_set<int> seen_ids;
	Ref<CCArray> collected;

	SearchContext(std::string nq, int count) :
		norm_query(std::move(nq)),
		remaining(count),
		failed_count(0),
		total_queries(count),
		collected(CCArray::create()) {}
};

class $modify(FuzzySearchLayer, LevelSearchLayer) {
	struct Fields {
		std::list<async::TaskHolder<web::WebResponse>> requests;
		std::shared_ptr<SearchContext> search_ctx;
		std::string pending_query;
		Ref<CCObject> pending_sender;
		bool is_searching = false;
	};

	void show_toast(std::string const& msg) {
		Notification::create(msg, NotificationIcon::None, 2.5f)->show();
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

	void collect_hits(matjson::Value const& json, std::shared_ptr<SearchContext> const& ctx) {
		if (!json.isObject()) {
			return;
		}
		auto hits_opt = json.get("hits");
		if (!hits_opt || !hits_opt.unwrap().isArray()) {
			return;
		}

		auto const& hits = hits_opt.unwrap().asArray().unwrap();
		for (auto const& hit : hits) {
			const int id = json_int(hit, "online_id");
			if (id <= 0 || ctx->seen_ids.count(id)) {
				continue;
			}
			auto maybe_level = level_from_hit(hit);
			if (!maybe_level) {
				continue;
			}
			ctx->seen_ids.insert(id);
			ctx->collected->addObject(*maybe_level);
		}
	}

	void finalize_search(std::shared_ptr<SearchContext> const& ctx) {
		if (ctx->collected->count() == 0) {
			log::info(
				"[The Fuzz-inator 2000] All queries returned 0 results (failed={}/{}), falling back",
				ctx->failed_count,
				ctx->total_queries
			);
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
			int sc = weighted_score(
				ctx->norm_query,
				fuzzy::normalise(std::string(lvl->m_levelName)),
				fuzzy::normalise(std::string(lvl->m_creatorName)),
				name_weight,
				creator_weight,
				max_typos
			);
			entries.push_back({lvl, sc});
		}

		std::stable_sort(entries.begin(), entries.end(), [](Entry const& a, Entry const& b) {
			return a.score > b.score;
		});

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
		for (auto& e : entries) {
			arr->addObject(e.level);
		}

		m_fields->pending_sender = nullptr;
		cancel_searches();

		if (pending_levels) {
			pending_levels->release();
			pending_levels = nullptr;
		}
		pending_levels = arr;
		inject_on_load = true;

		auto* search_obj = GJSearchObject::create(SearchType::Search);
		search_obj->m_searchQuery = m_fields->pending_query;
		auto* scene = LevelBrowserLayer::scene(search_obj);
		CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
	}

	void on_response(web::WebResponse const& res, std::shared_ptr<SearchContext> ctx) {
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

		if (!m_searchInput) {
			LevelSearchLayer::onSearch(sender);
			return;
		}

		const std::string raw_query = m_searchInput->getString();
		if (raw_query.empty()) {
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

		auto variations = generate_query_variations(raw_query, max_queries, max_typos);
		const auto norm_query = fuzzy::normalise(raw_query);

		log::info(
			"[The Fuzz-inator 2000] Searching with {} query variation(s) for \"{}\"",
			variations.size(),
			raw_query
		);
		for (auto const& v : variations) {
			log::debug("[The Fuzz-inator 2000]   variation: \"{}\"", v);
		}

		show_toast("Fuzzy searching via GDHistory…");

		auto ctx = std::make_shared<SearchContext>(norm_query, static_cast<int>(variations.size()));
		m_fields->search_ctx = ctx;

		const int per_query_limit = std::max(5, limit / static_cast<int>(variations.size()));

		Ref<FuzzySearchLayer> self = this;

		for (auto const& variation : variations) {
			const std::string url = GDHISTORY_SEARCH_URL + "?query=" + url_encode(variation) +
				"&limit=" + std::to_string(per_query_limit) + "&matching_strategy=last";

			log::info("[The Fuzz-inator 2000] Requesting: {}", url);

			auto req = web::WebRequest();
			req.timeout(std::chrono::seconds(timeout));
			req.userAgent("GeodeFuzzySearch/1.1.0");

			m_fields->requests.emplace_back();
			auto& holder = m_fields->requests.back();

			holder.spawn(req.get(url), [self, ctx](web::WebResponse value) {
				if (!self) {
					return;
				}
				self->on_response(value, ctx);
			});
		}
	}
};

class $modify(FuzzyBrowserLayer, LevelBrowserLayer) {
	void setupLevelBrowser(CCArray* items) {
		if (!inject_on_load || !pending_levels) {
			LevelBrowserLayer::setupLevelBrowser(items);
			return;
		}

		CCArray* arr = pending_levels;
		pending_levels = nullptr;
		inject_on_load = false;

		log::info("[The Fuzz-inator 2000] Injecting {} levels into LevelBrowserLayer", arr->count());

		LevelBrowserLayer::setupLevelBrowser(arr);
		this->loadLevelsFinished(arr, "", 0);
		arr->release();
	}
};
