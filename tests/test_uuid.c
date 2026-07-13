#include "t.h"
#include "init/init.h"
#include <stdio.h>
#include <unistd.h>

static int parses(void) {
    unsigned char u[16];
    OK(uuid_parse16("c36a4c56-487b-4aee-946b-f7fa2dc7f001", u) == 0);
    OK(u[0] == 0xc3 && u[1] == 0x6a && u[15] == 0x01);
    OK(uuid_parse16("not-a-uuid", u) != 0);
    return 0;
}

/* ext4_uuid_matches() reads a device/file, checks the ext superblock magic
 * 0xEF53 at 1024+0x38 (LE) and compares the 16-byte UUID at 1024+0x68. Craft
 * a minimal fake "superblock" in a temp file and exercise match / mismatch /
 * bad-magic / missing-file paths -- this is boot-critical (root-fs
 * selection) and was previously untested. */
static int write_fake_superblock(const char *path, int good_magic,
                                  const unsigned char uuid[16]) {
    unsigned char buf[1024 + 0x80] = {0};
    if (good_magic) { buf[1024 + 0x38] = 0x53; buf[1024 + 0x39] = 0xEF; }
    if (uuid) memcpy(buf + 1024 + 0x68, uuid, 16);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(buf, 1, sizeof buf, f);
    fclose(f);
    return n == sizeof buf ? 0 : -1;
}

static int uuid_matching(void) {
    unsigned char want[16], other[16];
    OK(uuid_parse16("c36a4c56-487b-4aee-946b-f7fa2dc7f001", want) == 0);
    OK(uuid_parse16("11111111-2222-3333-4444-555555555555", other) == 0);

    char path[] = "/tmp/craftboot_test_uuid_XXXXXX";
    int fd = mkstemp(path);
    OK(fd >= 0);
    close(fd);

    OK(write_fake_superblock(path, 1, want) == 0);
    OK(ext4_uuid_matches(path, want) == 1);
    OK(ext4_uuid_matches(path, other) == 0);

    OK(write_fake_superblock(path, 0, want) == 0);   /* bad magic */
    OK(ext4_uuid_matches(path, want) == 0);

    OK(ext4_uuid_matches("/nonexistent/craftboot-test-path", want) == 0);

    unlink(path);
    return 0;
}

int main(void) {
    RUN(parses);
    RUN(uuid_matching);
    return 0;
}
