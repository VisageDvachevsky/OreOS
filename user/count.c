#include "user.h"

uint64_t strlen(const char *s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

void puts(const char *s) {
    sys_write(1, s, strlen(s));
}

static void put_digit(uint64_t value) {
    char c = (char)('0' + (value % 10));
    sys_write(1, &c, 1);
}

int main(void) {
    for (uint64_t i = 0; i < 5; ++i) {
        puts("count ");
        put_digit(i);
        puts("\n");
        for (volatile uint64_t spin = 0; spin < 8000000ULL; ++spin) {
        }
    }
    return 0;
}
