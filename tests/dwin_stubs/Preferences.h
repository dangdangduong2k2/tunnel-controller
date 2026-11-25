#pragma once
#include <stdint.h>
#include <stddef.h>
extern uint32_t testDwinJournal;
class Preferences {
  public:
    bool begin(const char *,bool) { return true; }
    void end() {}
    uint32_t getUInt(const char *,uint32_t) { return testDwinJournal; }
    size_t putUInt(const char *,uint32_t value) { testDwinJournal=value; return 4; }
};
