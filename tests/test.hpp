#pragma once
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " check failed: " #condition); } while (false)
template<class F> void rejects(F f) {
    bool threw = false;
    try { f(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}
inline int run_tests(const std::vector<std::pair<std::string, std::function<void()>>>& tests) {
    int failed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    std::cout << tests.size() - static_cast<std::size_t>(failed) << '/' << tests.size() << " passed\n";
    return failed ? 1 : 0;
}
