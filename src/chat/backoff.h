// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - reconnect backoff: exponential growth with full jitter.
#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>
#include <random>

namespace tandem::chat {

// delay = uniform(0, min(cap, base * 2^attempt)), floored at a small minimum so a jitter
// draw near zero never produces a tight loop. The attempt counter only resets after the
// connection stayed healthy for `healthy` time; a connection that flaps right after it
// comes up keeps growing the delay, which is what prevents reconnect storms.
class Backoff {
public:
	using Clock = std::chrono::steady_clock;
	using Ms = std::chrono::milliseconds;
	using Rng = std::function<double()>; // returns a value in [0, 1)

	explicit Backoff(Rng rng = {}, Ms base = Ms(1000), Ms cap = Ms(60000), Ms healthy = Ms(30000),
			 Ms floor = Ms(250))
		: rng_(rng ? std::move(rng) : DefaultRng()),
		  base_(base),
		  cap_(cap),
		  healthy_(healthy),
		  floor_(floor)
	{
	}

	// Call when a connection is fully established (joined/subscribed).
	void MarkConnected(Clock::time_point now) { connected_since_ = now; }

	// Delay before the next attempt. A server-provided retry-after wins when it is longer.
	Ms Next(Clock::time_point now, Ms retry_after = Ms(0))
	{
		if (connected_since_ && now - *connected_since_ >= healthy_)
			attempt_ = 0;
		connected_since_.reset();

		const int shift = std::min(attempt_, 16);
		const double ceiling =
			std::min(static_cast<double>(cap_.count()), static_cast<double>(base_.count()) * double(1 << shift));
		double r = rng_();
		if (r < 0.0)
			r = 0.0;
		if (r > 1.0)
			r = 1.0;
		Ms delay(static_cast<long long>(r * ceiling));
		delay = std::max(delay, std::min(floor_, cap_));
		if (attempt_ < 1000)
			++attempt_;

		const Ms max_retry_after = Ms(60LL * 60 * 1000);
		if (retry_after > Ms(0))
			delay = std::max(delay, std::min(retry_after, max_retry_after));
		return delay;
	}

	void Reset()
	{
		attempt_ = 0;
		connected_since_.reset();
	}

	int attempt() const { return attempt_; }

private:
	static Rng DefaultRng()
	{
		return [] {
			thread_local std::mt19937 gen{std::random_device{}()};
			std::uniform_real_distribution<double> dist(0.0, 1.0);
			return dist(gen);
		};
	}

	Rng rng_;
	Ms base_, cap_, healthy_, floor_;
	int attempt_ = 0;
	std::optional<Clock::time_point> connected_since_;
};

} // namespace tandem::chat
