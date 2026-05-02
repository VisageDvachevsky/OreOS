#include "user.h"

uint64_t strlen(const char *s) { uint64_t n = 0; while (s[n]) n++; return n; }
void puts(const char *s) { sys_write(1, s, strlen(s)); }

int main(void) {
    char path[128];
    if (sys_args(path, sizeof(path)) <= 0) {
        puts("usage: cat <path>\n");
        return 1;
    }
    int64_t fd = sys_open(path);
    if (fd < 0) {
        puts("cat: not found\n");
        return 1;
    }
    char buf[256];
    int64_t last = 0;
    for (;;) {
        int64_t n = sys_file_read((uint64_t)fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            puts("cat: read failed\n");
            sys_close((uint64_t)fd);
            return 1;
        }
        if (n == 0) break;
        buf[n] = 0;
        puts(buf);
        last = n;
    }
    if (last > 0 && buf[last - 1] != '\n') puts("\n");
    sys_close((uint64_t)fd);
    return 0;
}
