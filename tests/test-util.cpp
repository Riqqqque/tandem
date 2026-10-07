// SPDX-License-Identifier: GPL-2.0-or-later
#include "test-framework.h"

#include "backoff.h"
#include "chat-util.h"

#include <random>

using namespace tandem::chat;
using std::chrono::milliseconds;
using std::chrono::seconds;

TEST_CASE(iso8601_valid)
{
	CHECK_EQ(ParseIso8601Ms("1970-01-01T00:00:00Z").value_or(-1), 0);
	CHECK_EQ(ParseIso8601Ms("2026-10-07T18:30:15+00:00").value_or(-1), 1791397815000LL);
	CHECK_EQ(ParseIso8601Ms("2026-10-07T18:30:15.123456+00:00").value_or(-1), 1791397815123LL);
	CHECK_EQ(ParseIso8601Ms("2026-10-07T18:30:16.250Z").value_or(-1), 1791397816250LL);
	CHECK_EQ(ParseIso8601Ms("2026-10-07T18:40:00.5Z").value_or(-1), 1791398400500LL);
	CHECK_EQ(ParseIso8601Ms("2026-10-07T20:30:15+02:00").value_or(-1), 1791397815000LL);
	CHECK_EQ(ParseIso8601Ms("2026-10-07T20:30:15+0200").value_or(-1), 1791397815000LL);
	CHECK_EQ(ParseIso8601Ms("2024-02-29T23:59:59.999-05:30").value_or(-1), 1709270999999LL);
	CHECK_EQ(ParseIso8601Ms("2000-03-01T00:00:00Z").value_or(-1), 951868800000LL);
	CHECK_EQ(ParseIso8601Ms("2026-10-07 18:30:15").value_or(-1), 1791397815000LL);
	CHECK_EQ(ParseIso8601Ms("  2026-10-07t18:30:15z ").value_or(-1), 1791397815000LL);
}

TEST_CASE(iso8601_invalid)
{
	CHECK(!ParseIso8601Ms(""));
	CHECK(!ParseIso8601Ms("yesterday"));
	CHECK(!ParseIso8601Ms("2026-10-07"));
	CHECK(!ParseIso8601Ms("2026-13-07T00:00:00Z"));
	CHECK(!ParseIso8601Ms("2025-02-29T00:00:00Z"));
	CHECK(!ParseIso8601Ms("2026-10-07T24:00:00Z"));
	CHECK(!ParseIso8601Ms("2026-10-07T18:30:15."));
	CHECK(!ParseIso8601Ms("2026-10-07T18:30:15+5"));
	CHECK(!ParseIso8601Ms("2026-10-07T18:30:15Zjunk"));
	CHECK(!ParseIso8601Ms("2026/10/07T18:30:15Z"));
}

TEST_CASE(recent_ids_lru)
{
	RecentIds ids(3);
	CHECK(ids.Insert("a"));
	CHECK(ids.Insert("b"));
	CHECK(!ids.Insert("a"));
	CHECK(ids.Insert("c"));
	CHECK(ids.Insert("d")); // evicts "a"
	CHECK_EQ(ids.size(), size_t(3));
	CHECK(ids.Insert("a"));
	CHECK(!ids.Insert("d"));
	CHECK(ids.Insert("")); // empty ids are never deduplicated
	CHECK(ids.Insert(""));
}

TEST_CASE(string_helpers)
{
	CHECK_EQ(ToLowerAscii("AbC_12-Z"), std::string("abc_12-z"));
	CHECK_EQ(std::string(TrimAscii(" \t x y \r\n")), std::string("x y"));
	CHECK_EQ(std::string(TrimAscii("   ")), std::string());
}

namespace {
Backoff::Clock::time_point T0()
{
	return Backoff::Clock::time_point(seconds(1000));
}
} // namespace

TEST_CASE(backoff_grows_and_caps)
{
	// rng = 1.0 exposes the ceiling: 1s, 2s, 4s ... capped at 60s.
	Backoff b([] { return 1.0; });
	auto t = T0();
	CHECK_EQ(b.Next(t).count(), 1000);
	CHECK_EQ(b.Next(t).count(), 2000);
	CHECK_EQ(b.Next(t).count(), 4000);
	CHECK_EQ(b.Next(t).count(), 8000);
	CHECK_EQ(b.Next(t).count(), 16000);
	CHECK_EQ(b.Next(t).count(), 32000);
	CHECK_EQ(b.Next(t).count(), 60000);
	for (int i = 0; i < 50; ++i)
		CHECK_EQ(b.Next(t).count(), 60000);
}

TEST_CASE(backoff_full_jitter_and_floor)
{
	Backoff zero([] { return 0.0; });
	CHECK_EQ(zero.Next(T0()).count(), 250); // floor, never a zero-delay loop

	std::mt19937 gen(42);
	std::uniform_real_distribution<double> dist(0.0, 1.0);
	Backoff b([&] { return dist(gen); });
	for (int attempt = 0; attempt < 12; ++attempt) {
		long long ceiling = std::min<long long>(60000, 1000LL << std::min(attempt, 16));
		long long d = b.Next(T0()).count();
		CHECK(d >= 250);
		CHECK(d <= ceiling);
	}
}

TEST_CASE(backoff_resets_only_after_healthy_period)
{
	Backoff b([] { return 1.0; });
	auto t = T0();
	b.Next(t);
	b.Next(t);
	b.Next(t); // next would be 8s
	// Connected, but dropped after 10s: not healthy, keep growing.
	b.MarkConnected(t);
	CHECK_EQ(b.Next(t + seconds(10)).count(), 8000);
	// Connected and stayed up for 30s: reset.
	b.MarkConnected(t);
	CHECK_EQ(b.Next(t + seconds(30)).count(), 1000);
	CHECK_EQ(b.Next(t + seconds(31)).count(), 2000);
	// A healthy period only counts when MarkConnected was called for this connection.
	CHECK_EQ(b.Next(t + seconds(500)).count(), 4000);
}

TEST_CASE(backoff_honors_retry_after)
{
	Backoff b([] { return 0.5; });
	CHECK_EQ(b.Next(T0(), seconds(90)).count(), 90000);
	// retry-after shorter than the computed delay does not shorten it
	Backoff c([] { return 1.0; });
	for (int i = 0; i < 6; ++i)
		c.Next(T0());
	CHECK_EQ(c.Next(T0(), seconds(1)).count(), 60000);
	// absurd values are clamped to an hour
	CHECK_EQ(b.Next(T0(), seconds(86400)).count(), 3600000);
}
