#include "user.h"

uint64_t strlen(const char *s) { uint64_t n = 0; while (s[n]) n++; return n; }
void puts(const char *s) { sys_write(1, s, strlen(s)); }

int main(void) {
    char args[128];
    sys_args(args, sizeof(args));
    puts(args);
    puts("\n");
    return 0;
}
