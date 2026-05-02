#include "user.h"

uint64_t strlen(const char *s) { uint64_t n = 0; while (s[n]) n++; return n; }
void puts(const char *s) { sys_write(1, s, strlen(s)); }

static void putu(uint64_t v) {
    char b[24];
    uint32_t i = sizeof(b);
    b[--i] = 0;
    if (!v) { puts("0"); return; }
    while (v && i) { b[--i] = (char)('0' + (v % 10)); v /= 10; }
    puts(&b[i]);
}

int main(void) {
    char path[128];
    if (sys_args(path, sizeof(path)) <= 0) {
        puts("usage: stat <path>\n");
        return 1;
    }
    VfsStat st;
    if (sys_stat(path, &st) < 0) {
        puts("stat: not found\n");
        return 1;
    }
    puts(st.name);
    puts(" type=");
    putu(st.type);
    puts(" size=");
    putu(st.size);
    puts("\n");
    return 0;
}
