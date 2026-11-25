#pragma once
#include "DwinPatch.h"
#include "DwinScanPatchData.h"
#include <cassert>
#include <cstring>
#include <deque>
#include <vector>
struct PatchJournal : dwinpatch::Journal {
    uint32_t tag=0; bool failArm=false,failClear=false;
    bool pending(uint32_t v) override { return tag==v; }
    bool arm(uint32_t v) override { if (failArm) return false; tag=v; return true; }
    bool clear() override { if (failClear) return false; tag=0; return true; }
};
struct PanelFake : dwinpatch::Port {
    std::vector<uint8_t> ram=std::vector<uint8_t>(131072);
    std::deque<uint8_t> incoming;
    uint32_t clock=1; uint16_t panelCrc=dwinpatchdata::IMAGE.oldCrc;
    bool cancel=false,offline=false,wrongKernel=false,wrongLayout=false,corruptRam=false,
         corruptFlash=false,lostFlashAck=false,slowFlash=false,cutFlash=false,failUpload=false;
    unsigned flashWrites=0,ramWrites=0,reboots=0,crcRequests=0,emergencies=0;
    uint16_t page=1; uint8_t crcResult[4]={},flashResult[12]={};
    PatchJournal *journal=nullptr;
    uint32_t now() override { return clock; }
    void pause(uint32_t ms) override { clock+=ms; }
    bool aborted() override { return cancel; }
    void emergency() override { cancel=true; ++emergencies; }
    int receive() override { if(incoming.empty()) return -1; int b=incoming.front(); incoming.pop_front(); return b; }
    void reply(const std::vector<uint8_t> &body) {
        // Noise and an unrelated ACK exercise resynchronization/read matching.
        incoming.push_back(0x11); incoming.push_back(0x5a); incoming.push_back(0x5a);
        incoming.push_back(0xa5); incoming.push_back(uint8_t(body.size()));
        incoming.insert(incoming.end(),body.begin(),body.end());
    }
    bool send(const uint8_t *b,size_t n) override {
        assert(n>=7 && b[0]==0x5a && b[1]==0xa5 && b[2]+3==n);
        if (offline) return true;
        const uint16_t vp=uint16_t(b[4]<<8)|b[5];
        if (b[3]==0x83) {
            assert(n==7 && b[6]>=1 && b[6]<=120);
            std::vector<uint8_t> response={0x83,b[4],b[5],b[6]};
            std::vector<uint8_t> data(b[6]*2);
            if (vp==0xf) { data[0]=wrongKernel?0x60:0x61; data[1]=0x21; }
            else if(vp==0x7a) { data={4,0,2,0x58}; }
            else if(vp==0x80) data={0,0,0,0x20};
            else if(vp==0x14) { data[0]=uint8_t(page>>8); data[1]=uint8_t(page); }
            else if(vp==0xe0) data.assign(crcResult,crcResult+4);
            else if(vp==0xaa) data.assign(flashResult,flashResult+12);
            else {
                assert(vp>=0x8000 && size_t(vp)*2+data.size()<=ram.size());
                memcpy(data.data(),ram.data()+vp*2,data.size());
                if(corruptRam) data[0]^=1;
            }
            assert(data.size()==b[6]*2);
            response.insert(response.end(),data.begin(),data.end()); reply(response); return true;
        }
        assert(b[3]==0x82 && n>=8 && n<=246);
        const uint8_t *d=b+6; const size_t size=n-6;
        if(vp==0xe0) {
            assert(size==4 && d[0]==0x5a && d[1]==0 && d[3]==64); ++crcRequests;
            uint16_t crc=panelCrc;
            if(d[2]!=13) {
                bool found=false;
                for(auto f:dwinpatchdata::REST) if(f.slot==d[2]) {crc=f.crc; found=true;}
                assert(found); if(wrongLayout) crc^=1;
            }
            crcResult[0]=crcResult[1]=0; crcResult[2]=uint8_t(crc); crcResult[3]=uint8_t(crc>>8);
        } else if(vp==0xaa) {
            const uint8_t expected[]={0x5a,2,0,104,0x80,0,0,0,0,0,0,0};
            assert(size==12 && !memcmp(d,expected,12));
            assert(page==0 && !memcmp(ram.data()+0x10000,dwinpatchdata::BLOCK,32768));
            if(journal) assert(journal->pending(dwinpatchdata::IMAGE.journalTag));
            ++flashWrites;
            panelCrc=(corruptFlash||cutFlash)?0x1234:dwinpatchdata::IMAGE.newCrc;
            memset(flashResult,0,12); if(slowFlash) flashResult[0]=0x5a;
            if(lostFlashAck || cutFlash) return true;
        } else if(vp==0x84) {
            assert(size==4 && d[0]==0x5a && d[1]==1); page=uint16_t(d[2]<<8)|d[3];
        } else if(vp==4) {
            assert(size==4 && !memcmp(d,"\x55\xaa\x5a\xa5",4)); ++reboots; page=0;
        } else {
            assert(vp>=0x8000 && size_t(vp)*2+size<=ram.size());
            if(failUpload) return false;
            memcpy(ram.data()+vp*2,d,size); ++ramWrites;
        }
        reply({0x82,'O','K'}); return true;
    }
};
