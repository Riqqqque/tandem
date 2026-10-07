// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - small helpers shared by the chat parsers and providers.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace tandem::chat {

// Wall clock, unix epoch milliseconds.
int64_t NowMs();

// Parses RFC 3339 / ISO 8601 date-times such as "2026-10-07T12:34:56Z",
// "2026-10-07T12:34:56.789012+02:00" or "2026-10-07 12:34:56" (no zone = UTC).
// Returns unix epoch milliseconds, or nullopt when the text is not a valid date-time.
std::optional<int64_t> ParseIso8601Ms(std::string_view s);

std::string ToLowerAscii(std::string_view s);
std::string_view TrimAscii(std::string_view s);

// Remembers the most recent N ids. Insert() returns false when the id was seen already.
class RecentIds {
public:
	explicit RecentIds(size_t capacity = 500) : capacity_(capacity) {}

	bool Insert(const std::string &id)
	{
		if (id.empty())
			return true;
		if (set_.count(id))
			return false;
		order_.push_back(id);
		set_.insert(id);
		while (order_.size() > capacity_) {
			set_.erase(order_.front());
			order_.pop_front();
		}
		return true;
	}

	bool Contains(const std::string &id) const { return set_.count(id) != 0; }
	size_t size() const { return order_.size(); }
	void Clear()
	{
		order_.clear();
		set_.clear();
	}

private:
	size_t capacity_;
	std::deque<std::string> order_;
	std::unordered_set<std::string> set_;
};

} // namespace tandem::chat
