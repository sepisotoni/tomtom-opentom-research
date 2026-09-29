// Tiny dependency-free unit-test harness.
#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace testing {

struct Case {
    const char* name;
    void (*fn)();
};
std::vector<Case>& registry();
int& failures();
struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

}  // namespace testing

#define TEST(name)                                          \
    static void test_##name();                              \
    static testing::Registrar registrar_##name(#name, test_##name); \
    static void test_##name()

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++testing::failures();                                                       \
        }                                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        auto va_ = (a);                                                                  \
        auto vb_ = (b);                                                                  \
        if (!(va_ == vb_)) {                                                             \
            std::printf("    FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);       \
            ++testing::failures();                                                       \
        }                                                                                \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                                 \
    do {                                                                                 \
        std::string h_ = (haystack);                                                     \
        if (h_.find(needle) == std::string::npos) {                                      \
            std::printf("    FAIL %s:%d: \"%s\" not found in \"%s\"\n", __FILE__, __LINE__, needle, h_.c_str()); \
            ++testing::failures();                                                       \
        }                                                                                \
    } while (0)
