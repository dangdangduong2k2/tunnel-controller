#include "DwinPatch.h"
#include <string.h>

namespace dwinpatch {
bool Updater::wait(uint8_t cmd, uint16_t vp, uint8_t words, uint8_t *out) {
    uint8_t state=0, length=0, body[255]; unsigned used=0;
    const uint32_t start=port.now();
    while (uint32_t(port.now()-start)<1500) {
        if (port.aborted()) return false;
        for (unsigned n=0;n<512;++n) {
            int c=port.receive(); if (c<0) break;
            uint8_t b=uint8_t(c);
            if (state==0) { if (b==0x5a) state=1; continue; }
            if (state==1) { state=b==0xa5?2:b==0x5a?1:0; continue; }
            if (state==2) { length=b; used=0; state=length?3:0; continue; }
            body[used++]=b;
            if (used<length) continue;
            state=0;
            if (length==6 && body[0]==0x83 && body[1]==0x10 && body[2]==0x40 &&
                body[3]==1 && body[4]==0 && body[5]==1) {
                port.emergency(); return false;
            }
            if (cmd==0x82 && length==3 && !memcmp(body,"\x82OK",3)) return true;
            if (cmd==0x83 && length==4+2*words && body[0]==0x83 &&
                body[1]==uint8_t(vp>>8) && body[2]==uint8_t(vp) && body[3]==words) {
                memcpy(out,body+4,2*words); return true;
            }
        }
        port.pause(1);
    }
    return false;
}
bool Updater::read(uint16_t vp, uint8_t words, uint8_t *out) {
    const uint8_t bytes[]={0x5a,0xa5,4,0x83,uint8_t(vp>>8),uint8_t(vp),words};
    return !port.aborted() && port.send(bytes,sizeof(bytes)) && wait(0x83,vp,words,out);
}
bool Updater::write(uint16_t vp, const uint8_t *data, size_t size) {
    if (size>240 || (size&1) || port.aborted()) return false;
    uint8_t bytes[246]={0x5a,0xa5,uint8_t(size+3),0x82,uint8_t(vp>>8),uint8_t(vp)};
    memcpy(bytes+6,data,size);
    return port.send(bytes,size+6) && wait(0x82,0,0,nullptr);
}
bool Updater::operation(uint16_t vp, const uint8_t *data, uint8_t size, uint8_t *out) {
    if (!write(vp,data,size)) return false;
    const uint32_t start=port.now();
    while (uint32_t(port.now()-start)<30000) {
        port.pause(50);
        if (!read(vp,size/2,out)) return false;
        if (out[0]!=0x5a) return out[0]==0;
    }
    return false;
}
bool Updater::crc(uint8_t slot, uint16_t &value) {
    const uint8_t cmd[]={0x5a,0,slot,64}; uint8_t result[4];
    if (!operation(0xe0,cmd,sizeof(cmd),result)) return false;
    // DGUS CRC bytes are returned little-endian (same as deployed Python loader).
    value=uint16_t(result[2]) | (uint16_t(result[3])<<8); return true;
}
Result Updater::failed() {
    // After a flash trigger its outcome may be unknown. Keep page 0 and journal;
    // never try to execute potentially damaged touch records in this boot.
    if (movedToBoot && !flashTriggered && !port.aborted()) {
        const uint8_t home[]={0x5a,1,0,1}; (void)write(0x84,home,sizeof(home));
    }
    return port.aborted()?ABORTED:IO_ERROR;
}
bool Updater::restartHome() {
    uint8_t data[2];
    const uint8_t reboot[]={0x55,0xaa,0x5a,0xa5};
    // Reset may suppress its own ACK; identity/page reads must still succeed.
    (void)write(4,reboot,sizeof(reboot));
    port.pause(2000);
    if (!read(0x0f,1,data) || data[0]!=0x61 || data[1]!=0x21) return false;
    const uint8_t home[]={0x5a,1,0,1};
    return write(0x84,home,sizeof(home)) && read(0x14,1,data) && data[0]==0 && data[1]==1;
}
Result Updater::run() {
    uint8_t data[240]; uint16_t actual=0;
    movedToBoot=flashTriggered=false;
    phase="probe";
    // Give a concurrently powered DWIN time to boot. Only the identity read is
    // retried; never retry writes/flash automatically after uncertain ACKs.
    bool found=false;
    for (unsigned attempt=0;attempt<3 && !port.aborted();++attempt) {
        port.pause(1000);
        for (unsigned n=0;n<1024 && port.receive()>=0;++n) {}
        if (read(0x0f,1,data)) { found=true; break; }
    }
    if (!found) return port.aborted()?ABORTED:NO_REPLY;
    if (data[0]!=0x61 || data[1]!=0x21) return WRONG_PANEL;
    if (!read(0x7a,2,data)) return failed();
    if (memcmp(data,"\x04\x00\x02\x58",4)) return WRONG_PANEL;
    if (!read(0x80,2,data)) return failed();
    if (!(data[3]&0x20)) return WRONG_PANEL;
    phase="touch_crc";
    if (!crc(13,actual)) return failed();
    const bool resume=journal.pending(image.journalTag);
    if (actual!=image.oldCrc && actual!=image.newCrc && !resume) return WRONG_LAYOUT;
    phase="layout_crc";
    for (size_t i=0;i<image.restCount;++i) {
        uint16_t value=0;
        if (!crc(image.rest[i].slot,value)) return failed();
        if (value!=image.rest[i].crc) return WRONG_LAYOUT;
    }
    if (actual==image.newCrc) {
        // An interrupted run may have committed flash without restarting the
        // panel. Reload touch records before clearing that recovery marker.
        if (resume && !restartHome()) return failed();
        if (resume && !journal.clear()) return JOURNAL_ERROR;
        const uint8_t home[]={0x5a,1,0,1};
        if (!write(0x84,home,sizeof(home))) return failed();
        phase="current"; return ALREADY_CURRENT;
    }
    phase="boot_page";
    const uint8_t boot[]={0x5a,1,0,0};
    if (!write(0x84,boot,sizeof(boot))) return failed();
    movedToBoot=true;
    if (!read(0x14,1,data) || data[0] || data[1]) return failed();
    phase="upload_ram";
    for (unsigned off=0;off<32768;off+=240) {
        const unsigned n=32768-off<240?32768-off:240;
        if (!write(uint16_t(0x8000+off/2),image.block+off,n)) return failed();
    }
    phase="verify_ram";
    for (unsigned off=0;off<32768;off+=240) {
        const unsigned n=32768-off<240?32768-off:240;
        if (!read(uint16_t(0x8000+off/2),uint8_t(n/2),data)) return failed();
        if (memcmp(data,image.block+off,n)) { (void)failed(); return RAM_MISMATCH; }
    }
    phase="journal";
    if (!journal.arm(image.journalTag)) { (void)failed(); return JOURNAL_ERROR; }
    if (port.aborted()) return ABORTED;
    phase="write_flash";
    // DGUS 0x00AA, mode 2: 32KiB block 104 == slot13/block0, RAM VP8000.
    const uint8_t flash[]={0x5a,2,0,104,0x80,0,0,0,0,0,0,0};
    flashTriggered=true; // set before sending: even a lost ACK may have executed
    if (!operation(0xaa,flash,sizeof(flash),data)) return failed();
    phase="verify_flash";
    if (!crc(13,actual)) return failed();
    if (actual!=image.newCrc) return FLASH_MISMATCH;
    phase="restart_panel";
    if (!restartHome()) return failed();
    if (!journal.clear()) return JOURNAL_ERROR;
    phase="complete"; return UPDATED;
}
const char *name(Result r) {
    switch(r) {
    case ALREADY_CURRENT:return "already_current"; case UPDATED:return "updated";
    case NO_REPLY:return "no_reply"; case WRONG_PANEL:return "wrong_panel";
    case WRONG_LAYOUT:return "wrong_layout"; case IO_ERROR:return "io_error";
    case ABORTED:return "aborted"; case JOURNAL_ERROR:return "journal_error";
    case RAM_MISMATCH:return "ram_mismatch"; case FLASH_MISMATCH:return "flash_mismatch";
    default:return "not_run";
    }
}
} // namespace dwinpatch
