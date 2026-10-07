// SPDX-License-Identifier: GPL-2.0-or-later
#include "chat-util.h"

#include <chrono>

namespace tandem::chat {

int64_t NowMs()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string ToLowerAscii(std::string_view s)
{
	std::string out(s);
	for (auto &c : out) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return out;
}

std::string_view TrimAscii(std::string_view s)
{
	auto ws = [](char c) {
		return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
	};
	while (!s.empty() && ws(s.front()))
		s.remove_prefix(1);
	while (!s.empty() && ws(s.back()))
		s.remove_suffix(1);
	return s;
}

namespace {

// Howard Hinnant's days_from_civil: days since 1970-01-01 for a proleptic Gregorian date.
int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d)
{
	y -= m <= 2 ? 1 : 0;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);
	const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool ReadDigits(std::string_view s, size_t &pos, size_t count, int &out)
{
	if (pos + count > s.size())
		return false;
	int v = 0;
	for (size_t i = 0; i < count; ++i) {
		char c = s[pos + i];
		if (c < '0' || c > '9')
			return false;
		v = v * 10 + (c - '0');
	}
	pos += count;
	out = v;
	return true;
}

bool IsLeap(int y)
{
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

} // namespace

std::optional<int64_t> ParseIso8601Ms(std::string_view s)
{
	s = TrimAscii(s);
	size_t p = 0;
	int year, mon, day, hour, min, sec;
	if (!ReadDigits(s, p, 4, year) || p >= s.size() || s[p++] != '-')
		return std::nullopt;
	if (!ReadDigits(s, p, 2, mon) || p >= s.size() || s[p++] != '-')
		return std::nullopt;
	if (!ReadDigits(s, p, 2, day) || p >= s.size())
		return std::nullopt;
	char sep = s[p++];
	if (sep != 'T' && sep != 't' && sep != ' ')
		return std::nullopt;
	if (!ReadDigits(s, p, 2, hour) || p >= s.size() || s[p++] != ':')
		return std::nullopt;
	if (!ReadDigits(s, p, 2, min) || p >= s.size() || s[p++] != ':')
		return std::nullopt;
	if (!ReadDigits(s, p, 2, sec))
		return std::nullopt;

	static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (mon < 1 || mon > 12 || day < 1)
		return std::nullopt;
	int mdays = kDays[mon - 1] + (mon == 2 && IsLeap(year) ? 1 : 0);
	if (day > mdays || hour > 23 || min > 59 || sec > 60)
		return std::nullopt;

	int ms = 0;
	if (p < s.size() && (s[p] == '.' || s[p] == ',')) {
		++p;
		size_t digits = 0;
		int scale = 100;
		while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
			if (digits < 3) {
				ms += (s[p] - '0') * scale;
				scale /= 10;
			}
			++digits;
			++p;
		}
		if (digits == 0)
			return std::nullopt;
	}

	int offset_min = 0;
	if (p < s.size()) {
		char z = s[p];
		if (z == 'Z' || z == 'z') {
			++p;
		} else if (z == '+' || z == '-') {
			++p;
			int oh, om = 0;
			if (!ReadDigits(s, p, 2, oh))
				return std::nullopt;
			if (p < s.size() && s[p] == ':')
				++p;
			if (p < s.size() && !ReadDigits(s, p, 2, om))
				return std::nullopt;
			if (oh > 23 || om > 59)
				return std::nullopt;
			offset_min = (oh * 60 + om) * (z == '-' ? -1 : 1);
		} else {
			return std::nullopt;
		}
	}
	if (p != s.size())
		return std::nullopt;

	int64_t days = DaysFromCivil(year, static_cast<unsigned>(mon), static_cast<unsigned>(day));
	int64_t secs = days * 86400 + hour * 3600 + min * 60 + sec - static_cast<int64_t>(offset_min) * 60;
	return secs * 1000 + ms;
}

} // namespace tandem::chat
