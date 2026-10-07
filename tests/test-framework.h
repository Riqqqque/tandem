// SPDX-License-Identifier: GPL-2.0-or-later
// Tandem - a tiny dependency-free test framework for the chat core.
#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace tt {

struct TestCase {
	const char *name;
	void (*fn)();
};

std::vector<TestCase> &Registry();

struct Registrar {
	Registrar(const char *name, void (*fn)()) { Registry().push_back({name, fn}); }
};

void Fail(const char *file, int line, const std::string &what);

// Reads tests/fixtures/<name>; fails the current test when missing.
std::string ReadFixture(const char *name);

template<typename T> std::string Show(const T &v)
{
	if constexpr (std::is_convertible_v<const T &, std::string_view>) {
		return "\"" + std::string(std::string_view(v)) + "\"";
	} else if constexpr (std::is_enum_v<T>) {
		return std::to_string(static_cast<long long>(v));
	} else if constexpr (requires(std::ostream &os) { os << v; }) {
		std::ostringstream os;
		os << v;
		return os.str();
	} else {
		return "<value>";
	}
}

} // namespace tt

#define TEST_CASE(name)                                       \
	static void name();                                   \
	static const tt::Registrar name##_registrar(#name, name); \
	static void name()

#define CHECK(expr)                                                  \
	do {                                                         \
		if (!(expr))                                         \
			tt::Fail(__FILE__, __LINE__, "CHECK(" #expr ")"); \
	} while (0)

#define CHECK_EQ(a, b)                                                                                     \
	do {                                                                                               \
		const auto &tt_a_ = (a);                                                                   \
		const auto &tt_b_ = (b);                                                                   \
		if (!(tt_a_ == tt_b_))                                                                     \
			tt::Fail(__FILE__, __LINE__,                                                       \
				 "CHECK_EQ(" #a ", " #b "): " + tt::Show(tt_a_) + " != " + tt::Show(tt_b_)); \
	} while (0)
