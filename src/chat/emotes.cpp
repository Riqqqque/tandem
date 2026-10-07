// SPDX-License-Identifier: GPL-2.0-or-later
#include "emotes.h"

#include "net.h"

#include <json.hpp>

#include <algorithm>

namespace tandem::chat {

namespace {

using json = nlohmann::json;

// Public APIs of the emote services. They can change; keep them in one place.
constexpr const char *k7tvUserUrl = "https://7tv.io/v3/users/";        // + twitch|kick|youtube / <id>
constexpr const char *k7tvGlobalUrl = "https://7tv.io/v3/emote-sets/global";
constexpr const char *kBttvUserUrl = "https://api.betterttv.net/3/cached/users/twitch/";
constexpr const char *kBttvGlobalUrl = "https://api.betterttv.net/3/cached/emotes/global";
constexpr const char *kFfzRoomUrl = "https://api.frankerfacez.com/v1/room/id/";
constexpr const char *kFfzGlobalUrl = "https://api.frankerfacez.com/v1/set/global";

const json &Child(const json &j, const char *key)
{
	static const json null_json;
	if (!j.is_object())
		return null_json;
	auto it = j.find(key);
	return it == j.end() ? null_json : *it;
}

std::string Str(const json &j, const char *key)
{
	const json &v = Child(j, key);
	if (v.is_string())
		return v.get<std::string>();
	if (v.is_number_integer())
		return std::to_string(v.get<int64_t>());
	return {};
}

bool SafeName(const std::string &name)
{
	if (name.empty() || name.size() > 100)
		return false;
	for (unsigned char c : name) {
		if (c <= ' ')
			return false;
	}
	return true;
}

bool SafeId(const std::string &id)
{
	if (id.empty() || id.size() > 64)
		return false;
	for (char c : id) {
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			return false;
	}
	return true;
}

void Add7tvEmotes(const json &emotes, EmoteMap &out)
{
	if (!emotes.is_array())
		return;
	for (const auto &e : emotes) {
		std::string name = Str(e, "name");
		std::string id = Str(e, "id");
		if (!SafeName(name) || !SafeId(id))
			continue;
		out.emplace(name, EmoteRef{"https://cdn.7tv.app/emote/" + id + "/2x.webp", "7tv"});
	}
}

void AddFfzSet(const json &set, EmoteMap &out)
{
	const json &emoticons = Child(set, "emoticons");
	if (!emoticons.is_array())
		return;
	for (const auto &e : emoticons) {
		std::string name = Str(e, "name");
		std::string id = Str(e, "id");
		if (!SafeName(name) || !SafeId(id))
			continue;
		const json &urls = Child(e, "urls");
		std::string scale = Child(urls, "2").is_string() ? "2" : "1";
		out.emplace(name, EmoteRef{"https://cdn.frankerfacez.com/emote/" + id + "/" + scale, "ffz"});
	}
}

void AddBttvArray(const json &arr, EmoteMap &out)
{
	if (!arr.is_array())
		return;
	for (const auto &e : arr) {
		std::string name = Str(e, "code");
		std::string id = Str(e, "id");
		if (!SafeName(name) || !SafeId(id))
			continue;
		out.emplace(name, EmoteRef{"https://cdn.betterttv.net/emote/" + id + "/2x", "bttv"});
	}
}

json ParseOrNull(std::string_view body)
{
	return json::parse(body, nullptr, false);
}

} // namespace

EmoteMap Parse7tvEmoteSet(std::string_view body)
{
	EmoteMap out;
	json j = ParseOrNull(body);
	Add7tvEmotes(Child(j, "emotes"), out);
	return out;
}

EmoteMap Parse7tvUser(std::string_view body)
{
	EmoteMap out;
	json j = ParseOrNull(body);
	Add7tvEmotes(Child(Child(j, "emote_set"), "emotes"), out);
	return out;
}

EmoteMap ParseBttvEmotes(std::string_view body)
{
	EmoteMap out;
	AddBttvArray(ParseOrNull(body), out);
	return out;
}

EmoteMap ParseBttvUser(std::string_view body)
{
	EmoteMap out;
	json j = ParseOrNull(body);
	AddBttvArray(Child(j, "channelEmotes"), out);
	AddBttvArray(Child(j, "sharedEmotes"), out);
	return out;
}

EmoteMap ParseFfzRoom(std::string_view body)
{
	EmoteMap out;
	json j = ParseOrNull(body);
	const json &sets = Child(j, "sets");
	if (sets.is_object()) {
		for (auto it = sets.begin(); it != sets.end(); ++it)
			AddFfzSet(it.value(), out);
	}
	return out;
}

EmoteMap ParseFfzGlobal(std::string_view body)
{
	EmoteMap out;
	json j = ParseOrNull(body);
	const json &defaults = Child(j, "default_sets");
	const json &sets = Child(j, "sets");
	if (!defaults.is_array() || !sets.is_object())
		return out;
	for (const auto &d : defaults) {
		std::string key = d.is_number_integer() ? std::to_string(d.get<int64_t>()) : d.is_string() ? d.get<std::string>() : "";
		auto it = sets.find(key);
		if (it != sets.end())
			AddFfzSet(*it, out);
	}
	return out;
}

namespace {
const char *const kEmoteHosts[] = {
	"https://static-cdn.jtvnw.net/", "https://files.kick.com/",       "https://cdn.7tv.app/",
	"https://cdn.betterttv.net/",    "https://cdn.frankerfacez.com/",
};
} // namespace

bool IsTrustedEmoteUrl(std::string_view url)
{
	for (const char *host : kEmoteHosts) {
		std::string_view h(host);
		if (url.size() > h.size() && url.substr(0, h.size()) == h) {
			for (char c : url) {
				if (c == '"' || c == '\'' || c == '<' || c == '>' || c == ' ' || c == '\\')
					return false;
			}
			return true;
		}
	}
	return false;
}

const char *EmoteCspSources()
{
	return "https://static-cdn.jtvnw.net https://files.kick.com https://cdn.7tv.app https://cdn.betterttv.net "
	       "https://cdn.frankerfacez.com";
}

void AnnotateWords(ChatMessage &m, const EmoteMap &map)
{
	if (map.empty() || m.text.empty())
		return;
	const std::string &t = m.text;
	std::vector<EmoteSpan> added;
	size_t i = 0;
	while (i < t.size()) {
		while (i < t.size() && t[i] == ' ')
			++i;
		size_t start = i;
		while (i < t.size() && t[i] != ' ')
			++i;
		if (i == start)
			break;
		bool covered = false;
		for (const auto &s : m.emotes) {
			if (start < s.end && i > s.begin) {
				covered = true;
				break;
			}
		}
		if (covered)
			continue;
		auto it = map.find(t.substr(start, i - start));
		if (it == map.end())
			continue;
		EmoteSpan s;
		s.begin = start;
		s.end = i;
		s.name = it->first;
		s.url = it->second.url;
		s.source = it->second.source;
		added.push_back(std::move(s));
	}
	if (added.empty())
		return;
	m.emotes.insert(m.emotes.end(), std::make_move_iterator(added.begin()), std::make_move_iterator(added.end()));
	std::sort(m.emotes.begin(), m.emotes.end(), [](const EmoteSpan &a, const EmoteSpan &b) { return a.begin < b.begin; });
}

EmoteAnnotator::EmoteAnnotator(ChatSink *next) : next_(next)
{
	worker_ = std::thread([this] { WorkerMain(); });
}

EmoteAnnotator::~EmoteAnnotator()
{
	Shutdown();
}

void EmoteAnnotator::Shutdown()
{
	stop_ = true;
	cv_.notify_all();
	if (worker_.joinable())
		worker_.join();
}

void EmoteAnnotator::SetEnabled(bool enabled)
{
	enabled_ = enabled;
}

void EmoteAnnotator::Post(ChatEvent &&ev)
{
	const int p = static_cast<int>(ev.platform);
	if (ev.kind == ChatEvent::Kind::ChannelInfo || ev.kind == ChatEvent::Kind::Message) {
		std::lock_guard lock(mu_);
		if (!globals_requested_ && enabled_) {
			globals_requested_ = true;
			jobs_.push_back({Platform::Twitch, {}});
			cv_.notify_all();
		}
		if (ev.kind == ChatEvent::Kind::ChannelInfo && p >= 0 && p < kPlatformCount &&
		    channel_ids_[p] != ev.target_id) {
			channel_ids_[p] = ev.target_id;
			channel7tv_[p].clear();
			channelBttv_[p].clear();
			channelFfz_[p].clear();
			Rebuild(ev.platform);
			if (enabled_) {
				jobs_.push_back({ev.platform, ev.target_id});
				cv_.notify_all();
			}
		}
		if (ev.kind == ChatEvent::Kind::Message && enabled_ && p >= 0 && p < kPlatformCount)
			AnnotateWords(ev.message, effective_[p]);
	}
	next_->Post(std::move(ev));
}

void EmoteAnnotator::Rebuild(Platform platform)
{
	int p = static_cast<int>(platform);
	EmoteMap merged;
	// emplace keeps the first entry, so insert in priority order.
	auto add = [&](const EmoteMap &m) {
		for (const auto &kv : m)
			merged.emplace(kv.first, kv.second);
	};
	add(channel7tv_[p]);
	add(channelBttv_[p]);
	add(channelFfz_[p]);
	add(global7tv_);
	if (platform == Platform::Twitch) {
		// BetterTTV and FrankerFaceZ are Twitch extensions.
		add(globalBttv_);
		add(globalFfz_);
	}
	effective_[p] = std::move(merged);
}

void EmoteAnnotator::WorkerMain()
{
	for (;;) {
		Job job;
		{
			std::unique_lock lock(mu_);
			cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
			if (stop_)
				return;
			job = jobs_.front();
			jobs_.pop_front();
		}
		Load(job);
	}
}

void EmoteAnnotator::Load(const Job &job)
{
	auto get = [this](const std::string &url) -> std::string {
		HttpResponse r = HttpGet(url, {"Accept: application/json"}, 15000, &stop_);
		return r.error.empty() && r.status == 200 ? r.body : std::string();
	};

	if (job.channel_id.empty()) {
		EmoteMap g7 = Parse7tvEmoteSet(get(k7tvGlobalUrl));
		EmoteMap gb = ParseBttvEmotes(get(kBttvGlobalUrl));
		EmoteMap gf = ParseFfzGlobal(get(kFfzGlobalUrl));
		std::lock_guard lock(mu_);
		global7tv_ = std::move(g7);
		globalBttv_ = std::move(gb);
		globalFfz_ = std::move(gf);
		for (int p = 0; p < kPlatformCount; ++p)
			Rebuild(static_cast<Platform>(p));
		Log(LogLevel::Info, "[emotes] global sets: %zu 7TV, %zu BetterTTV, %zu FrankerFaceZ", global7tv_.size(),
		    globalBttv_.size(), globalFfz_.size());
		return;
	}

	if (!SafeId(job.channel_id))
		return;
	const int p = static_cast<int>(job.platform);
	const char *seventv_platform = job.platform == Platform::Twitch ? "twitch"
				       : job.platform == Platform::Kick ? "kick"
									 : "youtube";
	EmoteMap c7 = Parse7tvUser(get(std::string(k7tvUserUrl) + seventv_platform + "/" + job.channel_id));
	EmoteMap cb, cf;
	if (job.platform == Platform::Twitch) {
		cb = ParseBttvUser(get(kBttvUserUrl + job.channel_id));
		cf = ParseFfzRoom(get(kFfzRoomUrl + job.channel_id));
	}
	std::lock_guard lock(mu_);
	if (channel_ids_[p] != job.channel_id)
		return; // the channel changed while loading
	channel7tv_[p] = std::move(c7);
	channelBttv_[p] = std::move(cb);
	channelFfz_[p] = std::move(cf);
	Rebuild(job.platform);
	Log(LogLevel::Info, "[emotes] %s channel sets: %zu 7TV, %zu BetterTTV, %zu FrankerFaceZ", PlatformName(job.platform),
	    channel7tv_[p].size(), channelBttv_[p].size(), channelFfz_[p].size());
}

} // namespace tandem::chat
