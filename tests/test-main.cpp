// SPDX-License-Identifier: GPL-2.0-or-later
#include "test-framework.h"

#include "chat-types.h"

#include <cstdio>
#include <cstring>
#include <fstream>

#ifndef TANDEM_TEST_FIXTURES
#define TANDEM_TEST_FIXTURES "tests/fixtures"
#endif

namespace tt {

namespace {
int g_failures = 0;
const char *g_current = "";
} // namespace

std::vector<TestCase> &Registry()
{
	static std::vector<TestCase> r;
	return r;
}

void Fail(const char *file, int line, const std::string &what)
{
	++g_failures;
	const char *base = file;
	for (const char *p = file; *p; ++p) {
		if (*p == '/' || *p == '\\')
			base = p + 1;
	}
	std::fprintf(stderr, "  FAIL [%s] %s:%d: %s\n", g_current, base, line, what.c_str());
}

std::string ReadFixture(const char *name)
{
	std::string path = std::string(TANDEM_TEST_FIXTURES) + "/" + name;
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		Fail(__FILE__, __LINE__, "missing fixture " + path);
		return {};
	}
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

} // namespace tt

static void QuietLog(tandem::chat::LogLevel level, const char *msg)
{
	if (level >= tandem::chat::LogLevel::Warning)
		std::fprintf(stderr, "    log: %s\n", msg);
}

int main(int argc, char **argv)
{
	tandem::chat::SetLogFunction(QuietLog);
	const char *filter = argc > 1 ? argv[1] : nullptr;
	int run = 0;
	int failed_tests = 0;
	for (const auto &t : tt::Registry()) {
		if (filter && !std::strstr(t.name, filter))
			continue;
		tt::g_current = t.name;
		int before = tt::g_failures;
		t.fn();
		++run;
		bool ok = tt::g_failures == before;
		if (!ok)
			++failed_tests;
		std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", t.name);
	}
	std::printf("\n%d tests, %d failed, %d failed checks\n", run, failed_tests, tt::g_failures);
	return failed_tests == 0 && run > 0 ? 0 : 1;
}
