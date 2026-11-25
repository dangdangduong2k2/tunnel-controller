#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace fota {
// Stable little-endian record embedded once in the application image.
struct ImageIdentity {
    char magic[16];
    uint32_t schema, version;
    char target[32];
};
static_assert(sizeof(ImageIdentity) == 56, "OTA identity layout changed");
extern const ImageIdentity imageIdentity;

class ImageScanner {
  public:
    void feed(const uint8_t *data, size_t size) {
        for (size_t i=0; i<size; ++i) {
            uint8_t b=data[i];
            if (used >= sizeof(imageIdentity.magic)) {
                buffer[used++]=b;
                if (used==sizeof(buffer)) {
                    ++count;
                    memcpy(&found,buffer,sizeof(found));
                    used=0;
                }
            } else if (b==uint8_t(imageIdentity.magic[used])) {
                buffer[used++]=b;
            } else {
                used=b==uint8_t(imageIdentity.magic[0]) ? 1 : 0;
                if (used) buffer[0]=b;
            }
        }
    }
    bool matches(uint32_t version) const {
        return count==1 && found.schema==1 && found.version==version &&
            !memcmp(found.target,imageIdentity.target,sizeof(found.target));
    }
  private:
    uint8_t buffer[sizeof(ImageIdentity)]={};
    size_t used=0;
    uint32_t count=0;
    ImageIdentity found={};
};
} // namespace fota
