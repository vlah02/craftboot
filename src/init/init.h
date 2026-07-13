#pragma once
int uuid_parse16(const char *s36, unsigned char out[16]);
int ext4_uuid_matches(const char *blkdev, const unsigned char want[16]);
