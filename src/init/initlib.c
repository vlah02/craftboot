#include "init/init.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
int uuid_parse16(const char *s, unsigned char out[16]) {
    int bi = 0;
    for (int i = 0; s[i] && bi < 16; i++) {
        if (s[i] == '-') continue;
        int h = hex(s[i]), l = hex(s[i + 1]);
        if (h < 0 || l < 0) return -1;
        out[bi++] = (unsigned char)(h << 4 | l);
        i++;
    }
    return bi == 16 ? 0 : -1;
}
int ext4_uuid_matches(const char *blkdev, const unsigned char want[16]) {
    int fd = open(blkdev, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    unsigned char sb[1024 + 0x80];
    ssize_t n = pread(fd, sb, sizeof sb, 0);
    close(fd);
    if (n < 1024 + 0x78) return 0;
    /* ext superblock at 1024: magic 0xEF53 at +0x38, uuid at +0x68 */
    if (!(sb[1024 + 0x38] == 0x53 && sb[1024 + 0x39] == 0xEF)) return 0;
    return memcmp(sb + 1024 + 0x68, want, 16) == 0;
}
