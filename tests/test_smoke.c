#include "t.h"
#include "core/version.h"
static int smoke(void) { OK(strcmp(CRAFTBOOT_VERSION, "2.1") == 0); return 0; }
int main(void) { RUN(smoke); return 0; }
