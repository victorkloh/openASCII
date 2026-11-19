#include <doctest/doctest.h>

#include "core/CommandLine.h"

#include <vector>

using namespace ap;

namespace {
ParseResult parse(std::vector<std::wstring_view> args) {
    return parseCommandLine(args);
}
} // namespace

TEST_CASE("sem argumentos = modo wallpaper por defeito") {
    const auto r = parse({});
    CHECK(r.error.empty());
    CHECK_FALSE(r.options.windowed);
    CHECK_FALSE(r.options.verbose);
    CHECK(r.options.benchSeconds == 0);
}

TEST_CASE("flags simples") {
    const auto r = parse({L"--windowed", L"--verbose"});
    CHECK(r.error.empty());
    CHECK(r.options.windowed);
    CHECK(r.options.verbose);
}

TEST_CASE("opções com valor") {
    const auto r = parse({L"--wallpaper", L"C:\\wp\\fluid", L"--bench", L"10"});
    CHECK(r.error.empty());
    CHECK(r.options.wallpaper == L"C:\\wp\\fluid");
    CHECK(r.options.benchSeconds == 10);
}

TEST_CASE("erros") {
    CHECK_FALSE(parse({L"--bench"}).error.empty());
    CHECK_FALSE(parse({L"--bench", L"abc"}).error.empty());
    CHECK_FALSE(parse({L"--bench", L"0"}).error.empty());
    CHECK_FALSE(parse({L"--bench", L"-5"}).error.empty());
    CHECK_FALSE(parse({L"--nope"}).error.empty());
    CHECK_FALSE(parse({L"--wallpaper"}).error.empty());
}
