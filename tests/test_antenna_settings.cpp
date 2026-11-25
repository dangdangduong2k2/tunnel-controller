#include "AntennaSettings.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace antenna;
struct Wire : Transport {
    struct Command { uint8_t id; std::vector<uint8_t> data; };
    std::vector<Command> sent;
    bool grant=true, permission=true, idle=true, accept=true, response=false;
    unsigned releases=0, acquisitions=0;
    uint8_t error=0;
    std::vector<uint8_t> bytes;
    bool acquire() override { ++acquisitions; return grant; }
    bool allowed() override { return permission; }
    bool ready() override { return idle; }
    bool begin(uint8_t c,const uint8_t *d,uint8_t n) override {
        if (!accept) return false;
        sent.push_back({c,{d,d+n}}); idle=false; return true;
    }
    bool reply(uint8_t *d,uint8_t *n,uint8_t *s) override {
        if (!response) return false;
        *n=uint8_t(bytes.size()); *s=error;
        memcpy(d,bytes.data(),bytes.size()); response=false; return true;
    }
    void release() override { ++releases; }
    void deliver(std::vector<uint8_t> b,uint8_t status=0) {
        bytes=b; error=status; response=true; idle=true;
    }
};
static std::vector<uint8_t> info(uint8_t mask) {
    std::vector<uint8_t> b(14); b[0]=0x21; b[10]=mask; return b;
}
static void readback(Job &j,Wire &w,uint32_t &t,const Values &v) {
    j.tick(++t); assert(w.sent.back().id==0x94);
    w.deliver({0x94,0,v.power[0],v.power[1],v.power[2],v.power[3]}); j.tick(++t);
    j.tick(++t); assert(w.sent.back().id==0x21);
    w.deliver(info(v.mask)); j.tick(++t);
}
int main() {
    const Values target={{0,15,22,30},5};
    for (bool persist : {false,true}) {
        Wire w; Job j(w); uint32_t t=100;
        assert(j.start(t,&target,persist)); assert(!j.start(t));
        j.tick(++t); assert(w.sent.size()==1 && w.sent[0].id==0x2f);
        assert(w.sent[0].data.size()==4);
        for (unsigned i=0;i<4;++i) assert(w.sent[0].data[i]==(target.power[i]|(persist?0:0x80)));
        w.deliver({0x2f,0}); j.tick(++t);
        j.tick(++t); assert(w.sent.size()==2 && w.sent[1].id==0x3f);
        assert(w.sent[1].data==std::vector<uint8_t>{uint8_t(persist?5:0x85)});
        w.deliver({0x3f,0}); j.tick(++t);
        assert(!j.state().valid && j.state().busy && w.releases==0);
        readback(j,w,t,target);
        assert(j.state().valid && j.state().result==SAVED && same(j.state().values,target));
        assert(!j.state().busy && w.releases==1 && w.sent.size()==4);
        assert(j.start(++t)); readback(j,w,t,target);
        assert(j.state().result==LOADED && j.state().revision==2 && w.sent.size()==6);
    }
    for (auto bad : {std::vector<uint8_t>{}, {0x2f,0}, {0x94,0,1}, {0x94,0,31,2,3,4}, {0x94,1}}) {
        Wire w; Job j(w); assert(j.start(0)); j.tick(1);
        w.deliver(bad); j.tick(2);
        assert(!j.state().valid && !j.state().busy && w.releases==1 && w.sent.size()==1);
    }
    for (unsigned reason=0;reason<4;++reason) {
        Wire w; Job j(w); uint32_t t=0;
        assert(j.start(t,&target)); j.tick(++t);
        if (reason==0) { w.deliver({0x2f,0},4); j.tick(++t); assert(j.state().result==READER_ERROR); }
        else {
            if (reason==1) { j.tick(3001); t=3001; }
            if (reason==2) j.cancel();
            if (reason==3) w.permission=false;
            j.tick(++t);
            assert(j.state().busy && w.releases==0); // keep ownership until in-flight reply drained
            w.deliver({0x2f,0}); j.tick(++t);
            assert(j.state().result==(reason==1?TIMEOUT:CANCELLED));
        }
        assert(!j.state().valid && !j.state().busy && w.releases==1 && w.sent.size()==1);
        j.tick(++t); assert(w.sent.size()==1); // no automatic repeat or second write
    }
    {
        Wire w; Job j(w); uint32_t t=0;
        assert(j.start(t,&target)); j.tick(++t); w.deliver({0x2f,0}); j.tick(++t);
        j.tick(++t); w.deliver({0x3f,0}); j.tick(++t);
        Values changed=target; changed.power[0]=1;
        readback(j,w,t,changed);
        assert(j.state().result==MISMATCH && !j.state().valid);
    }
    for (bool shortInfo : {false,true}) {
        Wire w; Job j(w); assert(j.start(0)); j.tick(1);
        w.deliver({0x94,0,1,2,3,4}); j.tick(2); j.tick(3);
        w.deliver(shortInfo?std::vector<uint8_t>{0x21,0}:info(0)); j.tick(4);
        assert(j.state().result==BAD_REPLY && !j.state().valid);
    }
    {
        Wire w; Job j(w); Values bad=target; bad.mask=0;
        assert(!j.start(0,&bad) && w.acquisitions==0);
        bad=target; bad.power[3]=31; assert(!j.start(0,&bad));
        w.grant=false; assert(!j.start(0) && !j.state().busy);
        w.grant=true; w.accept=false; assert(j.start(0)); j.tick(1);
        assert(j.state().result==REJECTED && w.sent.empty());
    }
    {
        Wire w; Job j(w); w.idle=false;
        assert(j.start(0xfffffff0)); j.tick(0x10); assert(j.state().busy && w.sent.empty());
        j.tick(0xbb0); j.tick(0xbb1);
        assert(j.state().result==TIMEOUT && w.sent.empty() && w.releases==1);
    }
    puts("PASS antenna transactions: read/write/RAM/persistent/readback, errors, cancellation, timeout, no write retry");
}
