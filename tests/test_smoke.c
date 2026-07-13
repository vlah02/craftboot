#include "t.h"
#include "core/version.h"
/* Version comes from `git describe --tags --always --dirty` at build time
 * (tag "v2.1" -> "v2.1", commits after a tag -> "v2.1-12-gabc1234", no tags
 * -> bare hash), so assert its shape rather than an exact string. */
static int smoke(void) {
    const char v[] = CRAFTBOOT_VERSION;
    OK(v[0] != 0);                                       /* non-empty */
    OK(v[0] == 'v' || (v[0] >= '0' && v[0] <= '9') ||
       (v[0] >= 'a' && v[0] <= 'f'));                    /* tag or bare hash */
    OK(strlen(v) < 48);                                  /* fits the footer */
    return 0;
}
int main(void) { RUN(smoke); return 0; }
