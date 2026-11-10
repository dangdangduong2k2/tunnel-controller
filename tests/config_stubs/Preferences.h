#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
inline std::map<std::string, std::vector<unsigned char>> testNvs;
inline bool testNvsFailure = false;
class Preferences {
  public:
    bool begin(const char *, bool) {
        return true;
    }
    size_t getBytesLength(const char *k) {
        auto i = testNvs.find(k);
        return i == testNvs.end() ? 0 : i->second.size();
    }
    size_t getBytes(const char *k, void *p, size_t n) {
        auto i = testNvs.find(k);
        if (i == testNvs.end() || i->second.size() != n)
            return 0;
        memcpy(p, i->second.data(), n);
        return n;
    }
    size_t putBytes(const char *k, const void *p, size_t n) {
        if (testNvsFailure)
            return 0;
        const auto *b = (const unsigned char *)p;
        testNvs[k] = std::vector<unsigned char>(b, b + n);
        return n;
    }
};
