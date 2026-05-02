#include "user.h"

uint64_t strlen(const char *s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

void puts(const char *s) {
    sys_write(1, s, strlen(s));
}

int main(void) {
    puts("hello from /bin/hello\n");
    return 0;
}
