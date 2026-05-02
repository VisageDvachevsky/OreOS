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
        path[0] = '/';
        path[1] = 0;
    }
    VfsStat st;
    uint32_t shown = 0;
    for (uint32_t i = 0;; ++i) {
        if (sys_readdir_path(path, i, &st) < 0) break;
        puts(st.name);
        if (st.type == ORE_VFS_DIR) puts("/");
        puts(" ");
        putu(st.size);
        puts("\n");
        shown++;
    }
    if (!shown) puts("ls: empty or not found\n");
    return 0;
}
