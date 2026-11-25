#include "dwin_panel_fake.h"
#include <cstdio>
using namespace dwinpatch;
int main() {
    for (bool current:{false,true}) {
        PanelFake p; PatchJournal j; p.journal=&j;
        if(current) p.panelCrc=dwinpatchdata::IMAGE.newCrc;
        Updater u(p,j,dwinpatchdata::IMAGE);
        assert(u.run()==(current?ALREADY_CURRENT:UPDATED));
        assert(p.flashWrites==(current?0U:1U) && !j.tag && p.page==1);
        assert(p.crcRequests>=13 && p.reboots==(current?0U:1U));
    }
    for(unsigned fault=0;fault<7;++fault) {
        PanelFake p; PatchJournal j; p.journal=&j;
        Result expected=IO_ERROR;
        switch(fault) {
        case 0:p.offline=true;expected=NO_REPLY;break;
        case 1:p.wrongKernel=true;expected=WRONG_PANEL;break;
        case 2:p.wrongLayout=true;expected=WRONG_LAYOUT;break;
        case 3:p.panelCrc=0x1234;expected=WRONG_LAYOUT;break;
        case 4:p.corruptRam=true;expected=RAM_MISMATCH;break;
        case 5:j.failArm=true;expected=JOURNAL_ERROR;break;
        case 6:p.failUpload=true;break;
        }
        Updater u(p,j,dwinpatchdata::IMAGE); assert(u.run()==expected);
        assert(!p.flashWrites && !j.tag && !u.wroteFlash());
        assert(p.clock<40000);
    }
    for(unsigned fault=0;fault<5;++fault) {
        PanelFake p; PatchJournal j; p.journal=&j;
        Result expected=IO_ERROR;
        switch(fault) {
        case 0:p.cutFlash=true;break;
        case 1:p.corruptFlash=true;expected=FLASH_MISMATCH;break;
        case 2:p.lostFlashAck=true;break;
        case 3:p.slowFlash=true;break;
        case 4:j.failClear=true;expected=JOURNAL_ERROR;break;
        }
        Updater u(p,j,dwinpatchdata::IMAGE); assert(u.run()==expected);
        assert(p.flashWrites==1 && j.pending(dwinpatchdata::IMAGE.journalTag) && u.wroteFlash());
        assert(p.reboots==(fault==4?1U:0U)); // only restart after verified flash
        assert(p.page==(fault==4?1U:0U));
        p.cutFlash=p.corruptFlash=p.lostFlashAck=p.slowFlash=false; j.failClear=false;
        Updater next(p,j,dwinpatchdata::IMAGE);
        const bool damaged=p.panelCrc!=dwinpatchdata::IMAGE.newCrc;
        assert(next.run()==(damaged?UPDATED:ALREADY_CURRENT));
        assert(p.flashWrites==(damaged?2U:1U) && !j.tag);
    }
    { // Journal alone cannot authorize a different screen layout.
        PanelFake p; PatchJournal j; j.arm(dwinpatchdata::IMAGE.journalTag);
        p.panelCrc=0x1234; p.wrongLayout=true;
        Updater u(p,j,dwinpatchdata::IMAGE);
        assert(u.run()==WRONG_LAYOUT && !p.flashWrites && j.tag);
    }
    { // Millisecond wrap must not bypass deadlines.
        PanelFake p; PatchJournal j; p.clock=0xffffff00;
        Updater u(p,j,dwinpatchdata::IMAGE); assert(u.run()==UPDATED);
    }
    { // Emergency during a transaction must abort before any flash command.
        struct StopPanel:PanelFake {
            bool send(const uint8_t *b,size_t n) override {
                if(b[3]==0x83) { reply({0x83,0x10,0x40,1,0,1}); return true; }
                return PanelFake::send(b,n);
            }
        } p;
        PatchJournal j; Updater u(p,j,dwinpatchdata::IMAGE);
        assert(u.run()==ABORTED && !p.flashWrites && p.emergencies==1);
    }
    puts("PASS DWIN patch: exact block, 13 slot CRCs, RAM readback, current/unknown/offline, bounded timeout, STOP, journal and interrupted-write recovery without same-boot retry");
}
