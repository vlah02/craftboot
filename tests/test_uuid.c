#include "t.h"
#include "init/init.h"
static int parses(void) {
    unsigned char u[16];
    OK(uuid_parse16("c36a4c56-487b-4aee-946b-f7fa2dc7f001", u) == 0);
    OK(u[0] == 0xc3 && u[1] == 0x6a && u[15] == 0x01);
    OK(uuid_parse16("not-a-uuid", u) != 0);
    return 0;
}
int main(void) { RUN(parses); return 0; }
