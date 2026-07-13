#ifndef DEV
#include "platform/display.h"
#include "platform/input.h"
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAXKBD 8
struct input { int fd[MAXKBD]; int n; };

static int is_keyboard(int fd) {
    unsigned long bits[(KEY_MAX + 63) / 64] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0) return 0;
    return !!(bits[KEY_ENTER / 64] >> (KEY_ENTER % 64) & 1);
}
input_t *input_open(display_t *d) {
    (void)d;
    input_t *in = calloc(1, sizeof *in);
    if (!in) return NULL;
    DIR *dir = opendir("/dev/input");
    if (!dir) return in;
    struct dirent *e;
    while ((e = readdir(dir)) && in->n < MAXKBD) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char p[PATH_MAX]; snprintf(p, sizeof p, "/dev/input/%s", e->d_name);
        int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (is_keyboard(fd)) in->fd[in->n++] = fd; else close(fd);
    }
    closedir(dir);
    fprintf(stderr, "[craftboot] evdev: %d keyboard(s)\n", in->n);
    return in;
}
action_t input_poll(input_t *in) {
    if (!in) return ACT_NONE;
    struct input_event ev;
    for (int i = 0; i < in->n; i++) {
        while (read(in->fd[i], &ev, sizeof ev) == sizeof ev) {
            if (ev.type != EV_KEY || ev.value != 1) continue;
            switch (ev.code) {
            case KEY_UP: case KEY_W:                 return ACT_UP;
            case KEY_DOWN: case KEY_S:               return ACT_DOWN;
            case KEY_ENTER: case KEY_KPENTER: case KEY_SPACE: return ACT_SELECT;
            case KEY_ESC:                            return ACT_BACK;
            }
        }
    }
    return ACT_NONE;
}
void input_close(input_t *in) {
    if (!in) return;
    for (int i = 0; i < in->n; i++) close(in->fd[i]);
    free(in);
}
#endif
