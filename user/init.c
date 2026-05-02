#include "user.h"
#include <stdarg.h>

#define LINE_MAX 256
#define ARGV_MAX 8
#define HISTORY_MAX 8

#define C_RESET "\033[0m"
#define C_DIM "\033[90m"
#define C_RED "\033[91m"
#define C_GREEN "\033[92m"
#define C_YELLOW "\033[93m"
#define C_BLUE "\033[94m"
#define C_MAGENTA "\033[95m"
#define C_CYAN "\033[96m"
#define C_WHITE "\033[97m"

static char history[HISTORY_MAX][LINE_MAX];
static uint32_t history_count;

uint64_t strlen(const char *s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

static int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void *memcpy(void *dst, const void *src, uint64_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (uint64_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

static void *memset(void *dst, int value, uint64_t n) {
    uint8_t *d = dst;
    for (uint64_t i = 0; i < n; ++i) d[i] = (uint8_t)value;
    return dst;
}

static char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return c == 0 ? (char *)s : 0;
}

static int atoi(const char *s) {
    int value = 0;
    while (*s >= '0' && *s <= '9') value = value * 10 + (*s++ - '0');
    return value;
}

void puts(const char *s) {
    sys_write(1, s, strlen(s));
}

static void putu_base(uint64_t value, uint32_t base, int sign) {
    char buf[32];
    const char *digits = "0123456789abcdef";
    uint32_t i = sizeof(buf);
    buf[--i] = 0;
    if (sign && (int64_t)value < 0) {
        puts("-");
        value = (uint64_t)(-(int64_t)value);
    }
    if (!value) {
        puts("0");
        return;
    }
    while (value && i) {
        buf[--i] = digits[value % base];
        value /= base;
    }
    puts(&buf[i]);
}

void printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; ++fmt) {
        if (*fmt != '%') {
            sys_write(1, fmt, 1);
            continue;
        }
        fmt++;
        if (*fmt == 's') puts(va_arg(ap, const char *));
        else if (*fmt == 'u') putu_base(va_arg(ap, uint64_t), 10, 0);
        else if (*fmt == 'd') putu_base((uint64_t)va_arg(ap, int64_t), 10, 1);
        else if (*fmt == 'x') putu_base(va_arg(ap, uint64_t), 16, 0);
        else sys_write(1, fmt, 1);
    }
    va_end(ap);
}

static const char *normalize_path(const char *path, char out[160]) {
    if (!path || !path[0]) return 0;
    if (path[0] == '/') return path;
    out[0] = '/';
    uint64_t i = 0;
    for (; path[i] && i < 158; ++i) out[i + 1] = path[i];
    out[i + 1] = 0;
    return out;
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
    return s;
}

static int split(char *line, char **argv, int max) {
    int argc = 0;
    char *p = trim(line);
    while (*p && argc < max) {
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = 0;
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
        }
        if (!*p) break;
        *p++ = 0;
        while (*p == ' ' || *p == '\t') p++;
    }
    return argc;
}

static void cmd_help(void) {
    puts(C_CYAN "commands" C_RESET "\n");
    puts("  help about lang history clear theme demo\n");
    puts("  ls [path] cat <path> echo <text> stat <path> sysinfo osh mounts ore <file>\n");
    puts("  ping dns http gfxtest inputtest terraintest skyrun [--smoke] raycast [--smoke]\n");
    puts("  mem cpu uptime ps mods kcmd <name> sleep <ticks>\n");
    puts("  spawn <path> yield run <path> preempt fault exit\n");
    puts("  default file tools are OreImage apps from /disk/bin/*.oimg\n");
}

static void cmd_about(void) {
    puts(C_CYAN "Ore" C_RESET " is a small x86_64 OS built from its own UEFI loader up.\n");
    puts(C_DIM "kernel:" C_RESET " SMP, VMM, ring3, syscalls, initramfs, /disk\n");
    puts(C_DIM "shell: " C_RESET "single foreground environment with small user binaries\n");
}

static void cmd_lang(void) {
    puts(C_CYAN "OreLang v0.1\n" C_RESET);
    puts("  native C-like language compiled by /bin/orec\n");
    puts("  examples: hello.ore, count.ore, fib.ore, branch.ore, func.ore, recur.ore, ptr2.ore, array.ore, struct.ore, abi.ore, bytes.ore, bits.ore, extern.ore\n");
    puts("  apps: /disk/apps/hello.ore, /disk/apps/echo.ore, /disk/apps/stat.ore\n");
    puts("  syntax: let x: int = 1; while (x <= 3) { puti(x); x = x + 1; }\n");
    puts("  supported: typed ptr arithmetic, [N]T arrays, struct, #[packed], sizeof, alignof, offsetof, extern declarations\n");
    puts("  run: ore /disk/examples/hello.ore\n");
}

static void cmd_mem(void) {
    SysInfo info;
    if (sys_info(&info) < 0) {
        puts(C_RED "mem: sys_info failed\n" C_RESET);
        return;
    }
    printf("pages total=%u free=%u used=%u reserved=%u\n",
           info.total_pages, info.free_pages, info.used_pages, info.reserved_pages);
}

static void cmd_cpu(void) {
    SysInfo info;
    if (sys_info(&info) < 0) {
        puts(C_RED "cpu: sys_info failed\n" C_RESET);
        return;
    }
    printf("cpus=%u pid=%u\n", info.cpu_count, info.pid);
}

static void cmd_mods(void) {
    puts(C_DIM "name state abi rc text size path\n" C_RESET);
    uint32_t shown = 0;
    for (uint32_t i = 0;; ++i) {
        ModuleInfo mod;
        if (sys_mod_info(i, &mod) < 0) break;
        printf("%s %u %u %d 0x%x 0x%x cmd=%u %s\n",
               mod.name,
               (uint64_t)mod.state,
               (uint64_t)mod.abi_version,
               (int64_t)mod.init_rc,
               mod.text_base,
               mod.text_size,
               (uint64_t)mod.has_command,
               mod.path);
        shown++;
    }
    if (!shown) puts(C_YELLOW "mods: none loaded\n" C_RESET);
}

static void cmd_uptime(void) {
    SysInfo info;
    if (sys_info(&info) < 0) {
        puts(C_RED "uptime: sys_info failed\n" C_RESET);
        return;
    }
    printf("ticks=%u\n", info.uptime_ticks);
}

static void cmd_run(const char *path, const char *args) {
    char normalized[160];
    const char *p = normalize_path(path, normalized);
    if (!p) {
        puts(C_YELLOW "usage: run <path>\n" C_RESET);
        return;
    }
    int64_t pid = sys_spawn_args(p, args ? args : "");
    if (pid < 0) {
        puts(C_RED "run: not found or bad executable\n" C_RESET);
        return;
    }
    int64_t rc = sys_wait((uint64_t)pid);
    printf("run: pid=%u exit=%d\n", (uint64_t)pid, rc);
}

static void cmd_exec_wait(const char *path, const char *args) {
    int64_t pid = sys_spawn_args(path, args ? args : "");
    if (pid < 0) {
        puts(C_RED "exec: not found or bad executable\n" C_RESET);
        return;
    }
    int64_t rc = sys_wait((uint64_t)pid);
    if (rc < 0) {
        puts(C_RED "exec: wait failed\n" C_RESET);
    }
}

static void cmd_exec_ore_app(const char *name, const char *args) {
    char path[160];
    const char *prefix = "/disk/bin/";
    uint64_t pos = 0;
    for (uint64_t i = 0; prefix[i] && pos + 1 < sizeof(path); ++i) path[pos++] = prefix[i];
    for (uint64_t i = 0; name[i] && pos + 6 < sizeof(path); ++i) path[pos++] = name[i];
    path[pos++] = '.';
    path[pos++] = 'o';
    path[pos++] = 'i';
    path[pos++] = 'm';
    path[pos++] = 'g';
    path[pos] = 0;
    cmd_exec_wait(path, args);
}

static int cmd_try_disk_bin(int argc, char **argv) {
    char path[160];
    char args[160];
    const char *prefix = "/disk/bin/";
    uint64_t pos = 0;
    for (uint64_t i = 0; prefix[i] && pos + 1 < sizeof(path); ++i) path[pos++] = prefix[i];
    for (uint64_t i = 0; argv[0][i] && pos + 6 < sizeof(path); ++i) path[pos++] = argv[0][i];
    path[pos++] = '.';
    path[pos++] = 'o';
    path[pos++] = 'i';
    path[pos++] = 'm';
    path[pos++] = 'g';
    path[pos] = 0;

    uint64_t apos = 0;
    for (int i = 1; i < argc && apos + 1 < sizeof(args); ++i) {
        if (i > 1 && apos + 1 < sizeof(args)) args[apos++] = ' ';
        for (uint64_t j = 0; argv[i][j] && apos + 1 < sizeof(args); ++j) args[apos++] = argv[i][j];
    }
    args[apos] = 0;

    int64_t pid = sys_spawn_args(path, args);
    if (pid < 0) return 0;
    int64_t rc = sys_wait((uint64_t)pid);
    printf("run: pid=%u exit=%d\n", (uint64_t)pid, rc);
    return 1;
}

static void cmd_spawn(const char *path) {
    char normalized[160];
    const char *p = normalize_path(path, normalized);
    if (!p) {
        puts(C_YELLOW "usage: spawn <path>\n" C_RESET);
        return;
    }
    int64_t pid = sys_spawn(p);
    if (pid < 0) {
        puts(C_RED "spawn: not found or bad executable\n" C_RESET);
        return;
    }
    printf("spawn: pid=%u ready\n", (uint64_t)pid);
}

static const char *proc_state_name(uint32_t state) {
    if (state == ORE_PROC_READY) return "ready";
    if (state == ORE_PROC_RUNNING) return "running";
    if (state == ORE_PROC_EXITED) return "exited";
    if (state == ORE_PROC_BLOCKED) return "blocked";
    return "unused";
}

static void cmd_ps(void) {
    puts(C_DIM "pid ppid state   syscalls entry stack segments name\n" C_RESET);
    for (uint32_t i = 0;; ++i) {
        ProcessInfo info;
        if (sys_proc_info(i, &info) < 0) break;
        printf("%u %u %s %u 0x%x 0x%x %u %s\n",
               (uint64_t)info.pid,
               (uint64_t)info.ppid,
               proc_state_name(info.state),
               info.syscall_count,
               info.entry,
               info.stack,
               (uint64_t)info.segment_count,
               info.name);
    }
}

static void run_command(char *line) {
    char *argv[ARGV_MAX];
    int argc = split(line, argv, ARGV_MAX);
    if (argc == 0) return;
    if (strcmp(argv[0], "help") == 0) cmd_help();
    else if (strcmp(argv[0], "about") == 0) cmd_about();
    else if (strcmp(argv[0], "lang") == 0) cmd_lang();
    else if (strcmp(argv[0], "history") == 0) {
        uint32_t start = history_count > HISTORY_MAX ? history_count - HISTORY_MAX : 0;
        for (uint32_t i = start; i < history_count; ++i) {
            printf("%u %s\n", (uint64_t)i, history[i % HISTORY_MAX]);
        }
    }
    else if (strcmp(argv[0], "ls") == 0) cmd_exec_ore_app("ls", argc >= 2 ? argv[1] : "/");
    else if (strcmp(argv[0], "cat") == 0) cmd_exec_ore_app("cat", argc >= 2 ? argv[1] : "");
    else if (strcmp(argv[0], "echo") == 0) {
        char args[160];
        uint64_t pos = 0;
        for (int i = 1; i < argc && pos + 1 < sizeof(args); ++i) {
            if (i > 1 && pos + 1 < sizeof(args)) args[pos++] = ' ';
            for (uint64_t j = 0; argv[i][j] && pos + 1 < sizeof(args); ++j) args[pos++] = argv[i][j];
        }
        args[pos] = 0;
        cmd_exec_ore_app("echo", args);
    } else if (strcmp(argv[0], "stat") == 0) cmd_exec_ore_app("stat", argc >= 2 ? argv[1] : "");
    else if (strcmp(argv[0], "mem") == 0) cmd_mem();
    else if (strcmp(argv[0], "cpu") == 0) cmd_cpu();
    else if (strcmp(argv[0], "uptime") == 0 || strcmp(argv[0], "ticks") == 0) cmd_uptime();
    else if (strcmp(argv[0], "ps") == 0) cmd_ps();
    else if (strcmp(argv[0], "mods") == 0) cmd_mods();
    else if (strcmp(argv[0], "kcmd") == 0) {
        if (argc < 2) puts(C_YELLOW "usage: kcmd <name>\n" C_RESET);
        else {
            char out[512];
            memset(out, 0, sizeof(out));
            int64_t rc = sys_kcmd(argv[1], out, sizeof(out));
            if (rc < 0) puts(C_RED "kcmd: not found\n" C_RESET);
            else {
                if (out[0]) puts(out);
                printf("kcmd: %s rc=%u\n", argv[1], (uint64_t)rc);
            }
        }
    }
    else if (strcmp(argv[0], "sleep") == 0) {
        uint64_t ticks = argc >= 2 ? (uint64_t)atoi(argv[1]) : 1;
        sys_sleep(ticks);
    } else if (strcmp(argv[0], "spawn") == 0) cmd_spawn(argc >= 2 ? argv[1] : 0);
    else if (strcmp(argv[0], "yield") == 0) sys_yield();
    else if (strcmp(argv[0], "preempt") == 0) {
        cmd_spawn("/bin/count");
        cmd_spawn("/bin/count");
        sys_yield();
    }
    else if (strcmp(argv[0], "run") == 0) {
        char args[192];
        uint64_t pos = 0;
        for (int i = 2; i < argc && pos + 1 < sizeof(args); ++i) {
            if (i > 2 && pos + 1 < sizeof(args)) args[pos++] = ' ';
            for (uint64_t j = 0; argv[i][j] && pos + 1 < sizeof(args); ++j) args[pos++] = argv[i][j];
        }
        args[pos] = 0;
        cmd_run(argc >= 2 ? argv[1] : 0, args);
    }
    else if (strcmp(argv[0], "ore") == 0) {
        char args[192];
        uint64_t pos = 0;
        for (int i = 1; i < argc && pos + 1 < sizeof(args); ++i) {
            if (i > 1 && pos + 1 < sizeof(args)) args[pos++] = ' ';
            for (uint64_t j = 0; argv[i][j] && pos + 1 < sizeof(args); ++j) args[pos++] = argv[i][j];
        }
        args[pos] = 0;
        cmd_exec_wait("/bin/orec", args);
    }
    else if (strcmp(argv[0], "mounts") == 0) puts(C_GREEN "/ram" C_RESET " initramfs ro\n" C_GREEN "/disk" C_RESET " orefs ro\n");
    else if (strcmp(argv[0], "theme") == 0) {
        puts(C_RED "red " C_GREEN "green " C_YELLOW "yellow " C_BLUE "blue " C_MAGENTA "magenta " C_CYAN "cyan " C_WHITE "white\n" C_RESET);
    }
    else if (strcmp(argv[0], "demo") == 0) {
        puts(C_CYAN "+------------------------+\n" C_RESET);
        puts(C_CYAN "|" C_RESET " Ore interactive shell  " C_CYAN "|\n" C_RESET);
        puts(C_CYAN "+------------------------+\n" C_RESET);
    }
    else if (strcmp(argv[0], "fault") == 0) {
        volatile uint64_t *bad = (uint64_t *)0xdeadbeef000ULL;
        *bad = 1;
    }
    else if (strcmp(argv[0], "clear") == 0) puts("\033[2J\033[H");
    else if (strcmp(argv[0], "exit") == 0) sys_exit(0);
    else {
        if (cmd_try_disk_bin(argc, argv)) return;
        puts(C_RED "unknown command: " C_RESET);
        puts(argv[0]);
        puts("\n");
    }
}

static void remember(const char *line) {
    if (!line[0]) return;
    uint32_t slot = history_count % HISTORY_MAX;
    uint64_t n = strlen(line);
    if (n >= LINE_MAX) n = LINE_MAX - 1;
    memcpy(history[slot], line, n);
    history[slot][n] = 0;
    history_count++;
}

static void prompt(void) {
    puts(C_GREEN "ore" C_RESET C_DIM ":" C_RESET C_CYAN "/" C_RESET C_YELLOW " > " C_RESET);
}

int main(void) {
    (void)memset;
    (void)strchr;
    (void)atoi;
    puts("\033[2J\033[H");
    puts(C_CYAN "ORE USERSYSTEM v0.2\n" C_RESET);
    puts(C_DIM "ring3 shell / initramfs / persistent disk / SMP kernel\n\n" C_RESET);
    puts(C_GREEN "ready" C_RESET "  type " C_YELLOW "help" C_RESET ", " C_YELLOW "lang" C_RESET ", or " C_YELLOW "theme" C_RESET "\n\n");
    puts(C_DIM "$ help\n" C_RESET);
    char smoke1[] = "help";
    run_command(smoke1);
    puts(C_DIM "$ ls\n" C_RESET);
    char smoke2[] = "ls";
    run_command(smoke2);
    puts(C_DIM "$ cat /init.txt\n" C_RESET);
    char smoke3[] = "cat /init.txt";
    run_command(smoke3);
    prompt();

    char line[LINE_MAX];
    uint32_t len = 0;
    for (;;) {
        char c = 0;
        if (sys_read(0, &c, 1) != 1) {
            sys_yield();
            continue;
        }
        if (c == '\r') c = '\n';
        if (c == '\n') {
            line[len] = 0;
            puts("\n");
            remember(line);
            run_command(line);
            len = 0;
            prompt();
            continue;
        }
        if (c == 8 || c == 127) {
            if (len) {
                len--;
                puts("\b \b");
            }
            continue;
        }
        if ((unsigned char)c < 32) continue;
        if ((unsigned char)c >= 128) {
            c = '?';
        }
        if (len + 1 < sizeof(line)) {
            line[len++] = c;
            sys_write(1, &c, 1);
        }
    }
}
