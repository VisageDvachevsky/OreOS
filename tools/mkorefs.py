#!/usr/bin/env python3
import os
import struct
import subprocess
import sys

def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment

ROOT_FILES = {
    "readme.txt": b"Ore persistent disk image.\nThis file is loaded from /disk, not initramfs.\n",
    "notes/hello.txt": b"Hello from persistent storage.\n",
    "docs/storage.txt": b"OreFS v1 is a tiny read-only block image for early kernel storage tests.\n",
    "examples/hello.ore": b"fn main() -> int {\n    print(\"hello from OreLang\\n\");\n    return 0;\n}\n",
    "examples/count.ore": b"fn main() -> int {\n    let i: int = 1;\n    print(\"count: \");\n    while (i <= 3) {\n        puti(i);\n        print(\" \");\n        i = i + 1;\n    }\n    print(\"\\n\");\n    return 0;\n}\n",
    "examples/ptr.ore": b"fn main() -> int {\n    // pointers are an explicit unsafe OreLang feature; full deref codegen comes next.\n    print(\"ptr<T> syntax accepted in v0.1 roadmap\\n\");\n    return 0;\n}\n",
    "examples/fib.ore": b"fn main() -> int {\n    let a: int = 0;\n    let b: int = 1;\n    let i: int = 0;\n    while (i < 10) {\n        let next: int = a + b;\n        a = b;\n        b = next;\n        i = i + 1;\n    }\n    print(\"fib(10)=\");\n    puti(a);\n    print(\"\\n\");\n    return 0;\n}\n",
    "examples/branch.ore": b"fn main() -> int {\n    let x: int = 21;\n    let y: int = 2;\n    print(\"x/y=\");\n    puti(x / y);\n    print(\" rem=\");\n    puti(x % y);\n    print(\"\\n\");\n    if (x > 10) {\n        print(\"branch: large\\n\");\n    } else {\n        print(\"branch: small\\n\");\n    }\n    return 0;\n}\n",
    "examples/func.ore": b"fn add(a: int, b: int) -> int {\n    return a + b;\n}\n\nfn main() -> int {\n    print(\"add(20,22)=\");\n    puti(add(20, 22));\n    print(\"\\n\");\n    return 0;\n}\n",
    "examples/recur.ore": b"fn fact(n: int) -> int {\n    if (n <= 1) {\n        return 1;\n    } else {\n        return n * fact(n - 1);\n    }\n}\n\nfn main() -> int {\n    print(\"fact(5)=\");\n    puti(fact(5));\n    print(\"\\n\");\n    return 0;\n}\n",
    "examples/ptr2.ore": b"fn bump(p: ptr<int>) -> int {\n    *p = *p + 1;\n    return *p;\n}\n\nfn main() -> int {\n    let x: int = 41;\n    let p: ptr<int> = &x;\n    print(\"ptr before=\");\n    puti(*p);\n    print(\" after=\");\n    puti(bump(p));\n    print(\" x=\");\n    puti(x);\n    print(\"\\n\");\n    return 0;\n}\n",
    "examples/array.ore": b"fn sum(p: ptr<int>, n: int) -> int {\n    let i: int = 0;\n    let total: int = 0;\n    while (i < n) {\n        total = total + *(p + i);\n        i = i + 1;\n    }\n    return total;\n}\n\nfn main() -> int {\n    let arr: [4]int;\n    arr[0] = 3;\n    arr[1] = 5;\n    arr[2] = 7;\n    arr[3] = 11;\n    print(\"array sum=\");\n    puti(sum(&arr, 4));\n    print(\" second=\");\n    puti(arr[1]);\n    print(\"\\n\");\n    return 0;\n}\n",
    "examples/struct.ore": b"struct Pair {\n    left: int;\n    right: int;\n}\n\nfn main() -> int {\n    let p: Pair;\n    p.left = 20;\n    p.right = 22;\n    print(\"sizeof(Pair)=\");\n    puti(sizeof(Pair));\n    print(\" off.right=\");\n    puti(offsetof(Pair, right));\n    print(\" sum=\");\n    puti(p.left + p.right);\n    print(\"\\n\");\n    return 0;\n}\n",
    "examples/abi.ore": b"#[packed]\nstruct VfsStat {\n    name: [128]char;\n    type: u32;\n    reserved: u32;\n    size: u64;\n}\n\nextern fn sys_stat(path: ptr<char>, st: ptr<VfsStat>) -> int;\n\nfn main() -> int {\n    print(\"OreVfsStat size=\");\n    puti(sizeof(VfsStat));\n    print(\" size.off=\");\n    puti(offsetof(VfsStat, size));\n    print(\"\\n\");\n    return 0;\n}\n",
    "examples/bits.ore": b"fn main() -> int {\n    let flags: int = 5;\n    let addr: int = 4096;\n    print(\"flags&1=\");\n    puti(flags & 1);\n    print(\" addr>>12=\");\n    puti(addr >> 12);\n    print(\" mask=\");\n    puti((flags | 2) & 7);\n    print(\"\\n\");\n    return 0;\n}\n",
    "examples/bytes.ore": b"#[packed]\nstruct Bytes {\n    a: u8;\n    b: u8;\n    c: u32;\n}\n\nfn main() -> int {\n    let x: Bytes;\n    x.a = 7;\n    x.b = 9;\n    x.c = 300;\n    print(\"bytes a=\");\n    puti(x.a);\n    print(\" b=\");\n    puti(x.b);\n    print(\" c=\");\n    puti(x.c);\n    print(\" size=\");\n    puti(sizeof(Bytes));\n    print(\"\\n\");\n    return 0;\n}\n",
    "examples/extern.ore": b"import \"ore/sys.oreh\";\n\nfn main() -> int {\n    write(1, \"extern write works\\n\", 19);\n    syscall3(1, 1, \"syscall3 write works\\n\", 21);\n    return 0;\n}\n",
    "apps/hello.ore": b"import \"ore/std.oreh\";\n\nfn main() -> int {\n    print_str(\"hello from Ore app\\n\");\n    return 0;\n}\n",
    "apps/echo.ore": b"import \"ore/std.oreh\";\n\nfn main() -> int {\n    let args: [128]char;\n    let n: int = sys_args(&args, 128);\n    if (n <= 0) {\n        print_str(\"\\n\");\n        return 0;\n    }\n    print_buf(&args, n);\n    print_str(\"\\n\");\n    return 0;\n}\n",
    "apps/cat.ore": b"import \"ore/std.oreh\";\n\nfn main() -> int {\n    let path: [128]char;\n    let buf: [256]char;\n    if (sys_args(&path, 128) <= 0) {\n        print_str(\"usage: cat <path>\\n\");\n        return 1;\n    }\n    let fd: int = sys_open(&path);\n    if (fd < 0) {\n        print_str(\"cat: not found\\n\");\n        return 1;\n    }\n    let n: int = sys_file_read(fd, &buf, 255);\n    while (n > 0) {\n        print_buf(&buf, n);\n        n = sys_file_read(fd, &buf, 255);\n    }\n    sys_close(fd);\n    return 0;\n}\n",
    "apps/stat.ore": b"import \"ore/std.oreh\";\n\nfn main() -> int {\n    let path: [128]char;\n    let st: OreVfsStat;\n    if (sys_args(&path, 128) <= 0) {\n        print_str(\"usage: stat <path>\\n\");\n        return 1;\n    }\n    if (sys_stat(&path, &st) < 0) {\n        print_str(\"stat: not found\\n\");\n        return 1;\n    }\n    print_str(\"size=\");\n    puti(st.size);\n    print_str(\" type=\");\n    puti(st.type);\n    print_str(\"\\n\");\n    return 0;\n}\n",
    "apps/ls.ore": b"import \"ore/std.oreh\";\n\nfn main() -> int {\n    let path: [128]char;\n    let st: OreVfsStat;\n    let i: int = 0;\n    let has_path: int = sys_args(&path, 128);\n    if (has_path <= 0) {\n        path[0] = 47;\n        path[1] = 0;\n    }\n    while (sys_readdir_path(&path, i, &st) >= 0) {\n        print_buf(&st.name, strlen(&st.name));\n        print_str(\" \");\n        puti(st.size);\n        print_str(\"\\n\");\n        i = i + 1;\n    }\n    return 0;\n}\n",
    "apps/sysinfo.ore": b"import \"ore/std.oreh\";\n\nfn main() -> int {\n    let info: OreSysInfo;\n    let proc: OreProcessInfo;\n    let i: int = 0;\n    if (sys_info(&info) < 0) {\n        print_str(\"sysinfo: sys_info failed\\n\");\n        return 1;\n    }\n    print_str(\"mem total=\");\n    puti(info.total_pages);\n    print_str(\" free=\");\n    puti(info.free_pages);\n    print_str(\" used=\");\n    puti(info.used_pages);\n    print_str(\" reserved=\");\n    puti(info.reserved_pages);\n    print_str(\"\\ncpus=\");\n    puti(info.cpu_count);\n    print_str(\" pid=\");\n    puti(info.pid);\n    print_str(\" ticks=\");\n    puti(info.uptime_ticks);\n    print_str(\"\\nprocesses:\\n\");\n    while (sys_proc_info(i, &proc) >= 0) {\n        print_str(\"  pid=\");\n        puti(proc.pid);\n        print_str(\" ppid=\");\n        puti(proc.ppid);\n        print_str(\" state=\");\n        puti(proc.state);\n        print_str(\" syscalls=\");\n        puti(proc.syscall_count);\n        print_str(\" name=\");\n        print_buf(&proc.name, strlen(&proc.name));\n        print_str(\"\\n\");\n        i = i + 1;\n    }\n    return 0;\n}\n",
    "apps/osh.ore": b"import \"ore/std.oreh\";\n\nfn clear_buf(buf: ptr<char>, len: int) -> int {\n    let i: int = 0;\n    while (i < len) {\n        *(buf + i) = 0;\n        i = i + 1;\n    }\n    return 0;\n}\n\nfn read_line(buf: ptr<char>, cap: int) -> int {\n    let n: int = 0;\n    let ch: char = 0;\n    while (n + 1 < cap) {\n        if (sys_read(0, &ch, 1) == 1) {\n            if (ch == 13) { ch = 10; }\n            if (ch == 10) {\n                *(buf + n) = 0;\n                print_str(\"\\n\");\n                return n;\n            }\n            if (ch >= 32) {\n                *(buf + n) = ch;\n                n = n + 1;\n            }\n        } else {\n            sys_yield();\n        }\n    }\n    *(buf + n) = 0;\n    return n;\n}\n\nfn builtin_ls() -> int {\n    let st: OreVfsStat;\n    let i: int = 0;\n    while (sys_readdir_path(\"/disk\", i, &st) >= 0) {\n        print_buf(&st.name, strlen(&st.name));\n        print_str(\" \");\n        puti(st.size);\n        print_str(\"\\n\");\n        i = i + 1;\n    }\n    return 0;\n}\n\nfn builtin_cat() -> int {\n    let buf: [256]char;\n    let fd: int = sys_open(\"/disk/readme.txt\");\n    if (fd < 0) {\n        print_str(\"cat: not found\\n\");\n        return 1;\n    }\n    let n: int = sys_file_read(fd, &buf, 255);\n    while (n > 0) {\n        print_buf(&buf, n);\n        n = sys_file_read(fd, &buf, 255);\n    }\n    sys_close(fd);\n    return 0;\n}\n\nfn builtin_sysinfo() -> int {\n    let info: OreSysInfo;\n    let proc: OreProcessInfo;\n    let i: int = 0;\n    if (sys_info(&info) < 0) { return 1; }\n    print_str(\"mem total=\");\n    puti(info.total_pages);\n    print_str(\" free=\");\n    puti(info.free_pages);\n    print_str(\" cpus=\");\n    puti(info.cpu_count);\n    print_str(\" pid=\");\n    puti(info.pid);\n    print_str(\" ticks=\");\n    puti(info.uptime_ticks);\n    print_str(\"\\n\");\n    while (sys_proc_info(i, &proc) >= 0) {\n        print_str(\"  \");\n        puti(proc.pid);\n        print_str(\" \");\n        print_buf(&proc.name, strlen(&proc.name));\n        print_str(\"\\n\");\n        i = i + 1;\n    }\n    return 0;\n}\n\nfn main() -> int {\n    let line: [128]char;\n    print_str(\"Ore shell prototype (OreLang)\\n\");\n    print_str(\"commands: help sysinfo ls cat exit\\n\");\n    while (1) {\n        clear_buf(&line, 128);\n        print_str(\"osh> \");\n        read_line(&line, 128);\n        if (streq(&line, \"help\")) {\n            print_str(\"help sysinfo ls cat exit\\n\");\n        } else {\n            if (streq(&line, \"sysinfo\")) {\n                builtin_sysinfo();\n            } else {\n                if (streq(&line, \"ls\")) {\n                    builtin_ls();\n                } else {\n                    if (streq(&line, \"cat\")) {\n                        builtin_cat();\n                    } else {\n                        if (streq(&line, \"exit\")) {\n                            return 0;\n                        } else {\n                            if (strlen(&line) > 0) {\n                                print_str(\"unknown: \");\n                                print_str(&line);\n                                print_str(\"\\n\");\n                            }\n                        }\n                    }\n                }\n            }\n        }\n    }\n    return 0;\n}\n",
    "docs/lang.txt": b"OreLang v1 draft\\n\\nSyntax keeps Ore-style C-like forms: fn, let, ptr<T>, struct, import, extern.\\nImplemented now: int/u64/i32/u32/char/u8/bool/void metadata, ptr<T>, [N]T arrays, struct layout, #[packed], sizeof, alignof, offsetof, field access, typed pointer arithmetic, functions, recursion, if/while.\\nUserspace target emits OreImage through SYS_EXEC_IMAGE. Kernel module target is planned as OreKMod loaded from initramfs.\\n",
    "include/ore/sys.oreh": b"extern fn write(fd: int, buf: ptr<char>, len: int) -> int;\nextern fn syscall0(n: int) -> int;\nextern fn syscall1(n: int, a: int) -> int;\nextern fn syscall2(n: int, a: int, b: int) -> int;\nextern fn syscall3(n: int, a: int, b: int, c: int) -> int;\n\nfn sys_args(buf: ptr<char>, len: int) -> int { return syscall2(16, buf, len); }\nfn sys_open(path: ptr<char>) -> int { return syscall3(5, path, 0, 0); }\nfn sys_close(fd: int) -> int { return syscall1(8, fd); }\nfn sys_file_read(fd: int, buf: ptr<char>, len: int) -> int { return syscall3(9, fd, buf, len); }\nfn sys_stat(path: ptr<char>, st: ptr<void>) -> int { return syscall3(7, path, st, 0); }\nfn sys_readdir_path(path: ptr<char>, index: int, st: ptr<void>) -> int { return syscall3(6, index, st, path); }\n",
    "include/ore/string.oreh": b"fn strlen(s: ptr<char>) -> int {\n    let n: int = 0;\n    while (*(s + n) != 0) {\n        n = n + 1;\n    }\n    return n;\n}\n\nfn streq(a: ptr<char>, b: ptr<char>) -> bool {\n    let i: int = 0;\n    while (*(a + i) != 0) {\n        if (*(b + i) == 0) { return 0; }\n        if (*(a + i) != *(b + i)) { return 0; }\n        i = i + 1;\n    }\n    return *(b + i) == 0;\n}\n",
    "include/ore/io.oreh": b"fn print_buf(buf: ptr<char>, len: int) -> int { return write(1, buf, len); }\nfn print_str(s: ptr<char>) -> int { return write(1, s, strlen(s)); }\nfn println(s: ptr<char>) -> int {\n    print_str(s);\n    return print_str(\"\\n\");\n}\n",
    "include/ore/vfs.oreh": b"#[packed]\nstruct OreVfsStat {\n    name: [128]char;\n    type: u32;\n    reserved: u32;\n    size: u64;\n}\n",
    "include/ore/std.oreh": b"extern fn write(fd: int, buf: ptr<char>, len: int) -> int;\nextern fn syscall0(n: int) -> int;\nextern fn syscall1(n: int, a: int) -> int;\nextern fn syscall2(n: int, a: int, b: int) -> int;\nextern fn syscall3(n: int, a: int, b: int, c: int) -> int;\n\n#[packed]\nstruct OreVfsStat {\n    name: [128]char;\n    type: u32;\n    reserved: u32;\n    size: u64;\n}\n\n#[packed]\nstruct OreSysInfo {\n    total_pages: u64;\n    free_pages: u64;\n    used_pages: u64;\n    reserved_pages: u64;\n    uptime_ticks: u64;\n    cpu_count: u32;\n    pid: u32;\n}\n\n#[packed]\nstruct OreProcessInfo {\n    pid: u32;\n    ppid: u32;\n    state: u32;\n    exit_code: i32;\n    segment_count: u32;\n    syscall_count: u64;\n    entry: u64;\n    stack: u64;\n    name: [32]char;\n}\n\nfn strlen(s: ptr<char>) -> int {\n    let n: int = 0;\n    while (*(s + n) != 0) { n = n + 1; }\n    return n;\n}\n\nfn streq(a: ptr<char>, b: ptr<char>) -> bool {\n    let i: int = 0;\n    while (*(a + i) != 0) {\n        if (*(b + i) == 0) { return 0; }\n        if (*(a + i) != *(b + i)) { return 0; }\n        i = i + 1;\n    }\n    return *(b + i) == 0;\n}\n\nfn print_buf(buf: ptr<char>, len: int) -> int { return write(1, buf, len); }\nfn print_str(s: ptr<char>) -> int { return write(1, s, strlen(s)); }\nfn println(s: ptr<char>) -> int { print_str(s); return print_str(\"\\n\"); }\n\nfn sys_read(fd: int, buf: ptr<char>, len: int) -> int { return syscall3(2, fd, buf, len); }\nfn sys_yield() -> int { return syscall0(4); }\nfn sys_info(info: ptr<void>) -> int { return syscall1(10, info); }\nfn sys_spawn(path: ptr<char>, args: ptr<char>) -> int { return syscall2(11, path, args); }\nfn sys_wait(pid: int) -> int { return syscall1(12, pid); }\nfn sys_getpid() -> int { return syscall0(13); }\nfn sys_sleep(ticks: int) -> int { return syscall1(14, ticks); }\nfn sys_proc_info(index: int, info: ptr<void>) -> int { return syscall2(15, index, info); }\nfn sys_args(buf: ptr<char>, len: int) -> int { return syscall2(16, buf, len); }\nfn sys_open(path: ptr<char>) -> int { return syscall3(5, path, 0, 0); }\nfn sys_close(fd: int) -> int { return syscall1(8, fd); }\nfn sys_file_read(fd: int, buf: ptr<char>, len: int) -> int { return syscall3(9, fd, buf, len); }\nfn sys_stat(path: ptr<char>, st: ptr<void>) -> int { return syscall3(7, path, st, 0); }\nfn sys_readdir_path(path: ptr<char>, index: int, st: ptr<void>) -> int { return syscall3(6, index, st, path); }\n",
    "include/ore/kernel.oreh": b"struct KernelApi {\n    log: ptr<void>;\n    kmalloc: ptr<void>;\n    kfree: ptr<void>;\n}\n",
    "kernel/bootdiag.ore": b"import \"ore/kernel.oreh\";\n\n#[kernel]\n#[unsafe]\nfn copy_msg(out: ptr<char>, cap: int, msg: ptr<char>) -> int {\n    let i: int = 0;\n    while (i + 1 < cap) {\n        if (*(msg + i) == 0) { break; }\n        *(out + i) = *(msg + i);\n        i = i + 1;\n    }\n    if (cap > 0) { *(out + i) = 0; }\n    return i;\n}\n\n#[kernel]\n#[export]\nfn module_init(api: ptr<KernelApi>) -> int {\n    return 0;\n}\n\n#[kernel]\n#[export]\nfn command_main(out: ptr<char>, cap: int) -> int {\n    copy_msg(out, cap, \"bootdiag: OreLang kernel callback online\\n\");\n    return 33287;\n}\n",
    "kernel/bad_syscall.ore": b"import \"ore/std.oreh\";\n\nfn module_init(api: ptr<void>) -> int {\n    syscall0(4);\n    return 0;\n}\n",
    "kernel/bad_command.ore": b"import \"ore/kernel.oreh\";\n\nfn module_init(api: ptr<KernelApi>) -> int {\n    return 0;\n}\n\nfn command_main() -> int {\n    return 0;\n}\n",
    "kernel/bad_deref.ore": b"import \"ore/kernel.oreh\";\n\nfn module_init(api: ptr<KernelApi>) -> int {\n    return 0;\n}\n\nfn command_main(out: ptr<char>, cap: int) -> int {\n    *out = 65;\n    return 0;\n}\n",
    "kernel/bad_print.ore": b"import \"ore/kernel.oreh\";\n\nfn module_init(api: ptr<KernelApi>) -> int {\n    print(\"bad\\n\");\n    return 0;\n}\n",
    "kernel/bad_user_attr.ore": b"import \"ore/kernel.oreh\";\n\n#[user]\nfn module_init(api: ptr<KernelApi>) -> int {\n    return 0;\n}\n",
    "examples/bad_kernel_attr.ore": b"#[kernel]\nfn main() -> int {\n    return 0;\n}\n",
    "kernel/badmagic.okmod": b"not an OreKMod image\n",
}

ROOT_FILES["kernel/badabi.okmod"] = struct.pack(
    "<QIIIIQQQQQQQQ32s",
    0x31444f4d4b45524f,
    1,
    120,
    0xdead,
    0,
    0,
    4096,
    0,
    0,
    0,
    0,
    0,
    0xffffffffffffffff,
    b"badabi",
)

ROOT_FILES.update({
    "include/ore/net.oreh": b"""#[packed]
struct OreNetInfo {
    ready: u32;
    mtu: u32;
    mac: [6]u8;
    reserved: [2]u8;
    ip_be: u32;
    gateway_be: u32;
    dns_be: u32;
}

fn sys_net_info(info: ptr<OreNetInfo>) -> int { return syscall1(26, info); }
fn sys_net_info_raw(info: ptr<void>) -> int { return syscall1(26, info); }
fn sys_net_recv(buf: ptr<u8>, cap: int) -> int { return syscall2(27, buf, cap); }
fn sys_net_send(buf: ptr<u8>, len: int) -> int { return syscall2(28, buf, len); }
""",
    "apps/ping.ore": b"""import \"ore/std.oreh\";
import \"ore/net.oreh\";

fn put_u16(buf: ptr<u8>, off: int, v: int) -> int {
    *(buf + off) = (v >> 8) & 255;
    *(buf + off + 1) = v & 255;
    return 0;
}

fn put_u32(buf: ptr<u8>, off: int, v: int) -> int {
    *(buf + off) = (v >> 24) & 255;
    *(buf + off + 1) = (v >> 16) & 255;
    *(buf + off + 2) = (v >> 8) & 255;
    *(buf + off + 3) = v & 255;
    return 0;
}

fn put_ip_10_0_2(buf: ptr<u8>, off: int, last: int) -> int {
    *(buf + off) = 10;
    *(buf + off + 1) = 0;
    *(buf + off + 2) = 2;
    *(buf + off + 3) = last;
    return 0;
}

fn ip_is_10_0_2(buf: ptr<u8>, off: int, last: int) -> bool {
    if (*(buf + off) != 10) { return 0; }
    if (*(buf + off + 1) != 0) { return 0; }
    if (*(buf + off + 2) != 2) { return 0; }
    return *(buf + off + 3) == last;
}

fn get_u16(buf: ptr<u8>, off: int) -> int {
    return (*(buf + off) << 8) | *(buf + off + 1);
}

fn get_u32(buf: ptr<u8>, off: int) -> int {
    return (*(buf + off) << 24) | (*(buf + off + 1) << 16) | (*(buf + off + 2) << 8) | *(buf + off + 3);
}

fn csum(buf: ptr<u8>, off: int, len: int) -> int {
    let sum: int = 0;
    let i: int = 0;
    while (i < len) {
        sum = sum + get_u16(buf, off + i);
        while (sum > 65535) { sum = (sum & 65535) + (sum >> 16); }
        i = i + 2;
    }
    return (~sum) & 65535;
}

fn fill_dst(buf: ptr<u8>, mac: ptr<u8>) -> int {
    let i: int = 0;
    while (i < 6) {
        *(buf + i) = *(mac + i);
        i = i + 1;
    }
    return 0;
}

fn fill_src(buf: ptr<u8>, mac: ptr<u8>) -> int {
    let i: int = 0;
    while (i < 6) {
        *(buf + 6 + i) = *(mac + i);
        i = i + 1;
    }
    return 0;
}

fn send_arp(mac: ptr<u8>, ip_be: int, gateway_be: int, buf: ptr<u8>) -> int {
    let i: int = 0;
    while (i < 6) { *(buf + i) = 255; i = i + 1; }
    fill_src(buf, mac);
    put_u16(buf, 12, 2054);
    put_u16(buf, 14, 1);
    put_u16(buf, 16, 2048);
    *(buf + 18) = 6;
    *(buf + 19) = 4;
    put_u16(buf, 20, 1);
    i = 0;
    while (i < 6) { *(buf + 22 + i) = *(mac + i); i = i + 1; }
    put_u32(buf, 28, ip_be);
    i = 0;
    while (i < 6) { *(buf + 32 + i) = 0; i = i + 1; }
    put_u32(buf, 38, gateway_be);
    return sys_net_send(buf, 42);
}

fn recv_arp(gateway_be: int, buf: ptr<u8>, gw: ptr<u8>) -> int {
    let tries: int = 0;
    while (tries < 400) {
        let n: int = sys_net_recv(buf, 1518);
        if (n >= 42) {
            if (get_u16(buf, 12) == 2054) {
                if (get_u16(buf, 20) == 2) {
                    if (get_u32(buf, 28) == gateway_be) {
                        fill_dst(gw, buf + 22);
                        return 0;
                    }
                }
            }
        }
        sys_sleep(1);
        tries = tries + 1;
    }
    return -1;
}

fn send_echo(mac: ptr<u8>, ip_be: int, buf: ptr<u8>, gw: ptr<u8>, dst_ip: int, seq: int) -> int {
    let i: int = 0;
    fill_dst(buf, gw);
    fill_src(buf, mac);
    put_u16(buf, 12, 2048);
    *(buf + 14) = 69;
    *(buf + 15) = 0;
    put_u16(buf, 16, 84);
    put_u16(buf, 18, seq);
    put_u16(buf, 20, 0);
    *(buf + 22) = 64;
    *(buf + 23) = 1;
    put_u16(buf, 24, 0);
    put_u32(buf, 26, ip_be);
    put_u32(buf, 30, dst_ip);
    put_u16(buf, 24, csum(buf, 14, 20));
    *(buf + 34) = 8;
    *(buf + 35) = 0;
    put_u16(buf, 36, 0);
    put_u16(buf, 38, 51966);
    put_u16(buf, 40, seq);
    i = 0;
    while (i < 56) {
        *(buf + 42 + i) = (i + seq) & 255;
        i = i + 1;
    }
    put_u16(buf, 36, csum(buf, 34, 64));
    return sys_net_send(buf, 98);
}

fn wait_echo(buf: ptr<u8>, dst_ip: int, seq: int) -> int {
    let tries: int = 0;
    while (tries < 80) {
        let n: int = sys_net_recv(buf, 1518);
        if (n >= 98) {
            if (get_u16(buf, 12) == 2048) {
                if (*(buf + 23) == 1) {
                    if (get_u32(buf, 26) == dst_ip) {
                        if (*(buf + 34) == 0) {
                            if (get_u16(buf, 38) == 51966) {
                                if (get_u16(buf, 40) == seq) { return 0; }
                            }
                        }
                    }
                }
            }
        }
        sys_sleep(1);
        tries = tries + 1;
    }
    return -1;
}

fn parse_ipv4(s: ptr<char>) -> int {
    let part: int = 0;
    let value: int = 0;
    let count: int = 0;
    let i: int = 0;
    while (*(s + i) != 0) {
        let ch: int = *(s + i);
        if (ch == 46) {
            if (part > 255) { return 0; }
            value = (value << 8) | part;
            part = 0;
            count = count + 1;
        } else {
            if (ch < 48) { return 0; }
            if (ch > 57) { return 0; }
            part = part * 10 + ch - 48;
        }
        i = i + 1;
    }
    if (part > 255) { return 0; }
    if (count != 3) { return 0; }
    return (value << 8) | part;
}

fn main() -> int {
    let buf: ptr<u8> = syscall1(23, 1);
    let mac: [6]u8;
    let gw: [6]u8;
    let args: [64]char;
    let seq: int = 1;
    let dst_ip: int = 134744072;
    let ip_be: int = 167772687;
    let gateway_be: int = 167772674;
    if (buf == 0) { print_str(\"ping: alloc failed\\n\"); return 1; }
    if (sys_net_info_raw(buf) < 0) { print_str(\"ping: net_info failed\\n\"); return 1; }
    if (*(buf + 0) == 0) { print_str(\"ping: network device not ready\\n\"); return 1; }
    mac[0] = *(buf + 8);
    mac[1] = *(buf + 9);
    mac[2] = *(buf + 10);
    mac[3] = *(buf + 11);
    mac[4] = *(buf + 12);
    mac[5] = *(buf + 13);
    if (sys_args(&args, 64) > 0) {
        let parsed: int = parse_ipv4(&args);
        if (parsed == 0) {
            print_str(\"usage: ping <ipv4>\\n\");
            return 1;
        }
        dst_ip = parsed;
    }
    print_str(\"PING \");
    puti((dst_ip >> 24) & 255);
    print_str(\".\");
    puti((dst_ip >> 16) & 255);
    print_str(\".\");
    puti((dst_ip >> 8) & 255);
    print_str(\".\");
    puti(dst_ip & 255);
    print_str(\" from 10.0.2.15\\n\");
    send_arp(&mac, ip_be, gateway_be, buf);
    if (recv_arp(gateway_be, buf, &gw) < 0) {
        print_str(\"ping: gateway ARP timeout\\n\");
        return 1;
    }
    while (seq <= 4) {
        send_echo(&mac, ip_be, buf, &gw, dst_ip, seq);
        if (wait_echo(buf, dst_ip, seq) == 0) {
            print_str(\"icmp reply seq=\");
            puti(seq);
            print_str(\"\\n\");
        } else {
            print_str(\"icmp timeout seq=\");
            puti(seq);
            print_str(\"\\n\");
        }
        seq = seq + 1;
    }
    syscall2(24, buf, 1);
    return 0;
}
""",
    "apps/dns.ore": b"""import \"ore/std.oreh\";
import \"ore/net.oreh\";

fn put_u16(buf: ptr<u8>, off: int, v: int) -> int {
    *(buf + off) = (v >> 8) & 255;
    *(buf + off + 1) = v & 255;
    return 0;
}

fn put_u32(buf: ptr<u8>, off: int, v: int) -> int {
    *(buf + off) = (v >> 24) & 255;
    *(buf + off + 1) = (v >> 16) & 255;
    *(buf + off + 2) = (v >> 8) & 255;
    *(buf + off + 3) = v & 255;
    return 0;
}

fn get_u16(buf: ptr<u8>, off: int) -> int {
    return (*(buf + off) << 8) | *(buf + off + 1);
}

fn get_u32(buf: ptr<u8>, off: int) -> int {
    return (*(buf + off) << 24) | (*(buf + off + 1) << 16) | (*(buf + off + 2) << 8) | *(buf + off + 3);
}

fn csum(buf: ptr<u8>, off: int, len: int) -> int {
    let sum: int = 0;
    let i: int = 0;
    while (i < len) {
        sum = sum + get_u16(buf, off + i);
        while (sum > 65535) { sum = (sum & 65535) + (sum >> 16); }
        i = i + 2;
    }
    return (~sum) & 65535;
}

fn fill_dst(buf: ptr<u8>, mac: ptr<u8>) -> int {
    let i: int = 0;
    while (i < 6) {
        *(buf + i) = *(mac + i);
        i = i + 1;
    }
    return 0;
}

fn fill_src(buf: ptr<u8>, mac: ptr<u8>) -> int {
    let i: int = 0;
    while (i < 6) {
        *(buf + 6 + i) = *(mac + i);
        i = i + 1;
    }
    return 0;
}

fn send_arp(mac: ptr<u8>, ip_be: int, target_ip: int, buf: ptr<u8>) -> int {
    let i: int = 0;
    while (i < 6) { *(buf + i) = 255; i = i + 1; }
    fill_src(buf, mac);
    put_u16(buf, 12, 2054);
    put_u16(buf, 14, 1);
    put_u16(buf, 16, 2048);
    *(buf + 18) = 6;
    *(buf + 19) = 4;
    put_u16(buf, 20, 1);
    i = 0;
    while (i < 6) { *(buf + 22 + i) = *(mac + i); i = i + 1; }
    put_u32(buf, 28, ip_be);
    i = 0;
    while (i < 6) { *(buf + 32 + i) = 0; i = i + 1; }
    put_u32(buf, 38, target_ip);
    return sys_net_send(buf, 42);
}

fn recv_arp(target_ip: int, buf: ptr<u8>, out_mac: ptr<u8>) -> int {
    let tries: int = 0;
    while (tries < 800) {
        let n: int = sys_net_recv(buf, 1518);
        if (n >= 42) {
            if (get_u16(buf, 12) == 2054) {
                if (get_u16(buf, 20) == 2) {
                    if (get_u32(buf, 28) == target_ip) {
                        fill_dst(out_mac, buf + 22);
                        return 0;
                    }
                }
            }
        }
        sys_sleep(1);
        tries = tries + 1;
    }
    return -1;
}

fn write_dns_query(buf: ptr<u8>, off: int) -> int {
    put_u16(buf, off + 0, 48879);
    put_u16(buf, off + 2, 256);
    put_u16(buf, off + 4, 1);
    put_u16(buf, off + 6, 0);
    put_u16(buf, off + 8, 0);
    put_u16(buf, off + 10, 0);
    *(buf + off + 12) = 7;
    *(buf + off + 13) = 101;
    *(buf + off + 14) = 120;
    *(buf + off + 15) = 97;
    *(buf + off + 16) = 109;
    *(buf + off + 17) = 112;
    *(buf + off + 18) = 108;
    *(buf + off + 19) = 101;
    *(buf + off + 20) = 3;
    *(buf + off + 21) = 99;
    *(buf + off + 22) = 111;
    *(buf + off + 23) = 109;
    *(buf + off + 24) = 0;
    put_u16(buf, off + 25, 1);
    put_u16(buf, off + 27, 1);
    return 29;
}

fn send_dns(mac: ptr<u8>, ip_be: int, dns_ip: int, dst_mac: ptr<u8>, buf: ptr<u8>) -> int {
    let dns_len: int = 29;
    fill_dst(buf, dst_mac);
    fill_src(buf, mac);
    put_u16(buf, 12, 2048);
    *(buf + 14) = 69;
    *(buf + 15) = 0;
    put_u16(buf, 16, 20 + 8 + dns_len);
    put_u16(buf, 18, 4660);
    put_u16(buf, 20, 0);
    *(buf + 22) = 64;
    *(buf + 23) = 17;
    put_u16(buf, 24, 0);
    put_u32(buf, 26, ip_be);
    put_u32(buf, 30, dns_ip);
    put_u16(buf, 24, csum(buf, 14, 20));
    put_u16(buf, 34, 49152);
    put_u16(buf, 36, 53);
    put_u16(buf, 38, 8 + dns_len);
    put_u16(buf, 40, 0);
    write_dns_query(buf, 42);
    return sys_net_send(buf, 42 + dns_len);
}

fn print_ip(v: int) -> int {
    puti((v >> 24) & 255);
    print_str(\".\");
    puti((v >> 16) & 255);
    print_str(\".\");
    puti((v >> 8) & 255);
    print_str(\".\");
    puti(v & 255);
    return 0;
}

fn skip_dns_name(buf: ptr<u8>, off: int) -> int {
    let len: int = *(buf + off);
    while (len != 0) {
        if ((len & 192) == 192) {
            return off + 2;
        }
        off = off + 1 + len;
        len = *(buf + off);
    }
    return off + 1;
}

fn wait_dns(buf: ptr<u8>, dns_ip: int) -> int {
    let tries: int = 0;
    while (tries != 1200) {
        let n: int = sys_net_recv(buf, 1518);
        if (n >= 86) {
            if (get_u16(buf, 12) == 2048) {
                if (*(buf + 23) == 17) {
                    if (*(buf + 26) == 10) {
                        if (get_u16(buf, 36) == 49152) {
                            if (get_u16(buf, 42) == 48879) {
                                let qd: int = get_u16(buf, 46);
                                let an: int = get_u16(buf, 48);
                                let total: int = an;
                                let off: int = 54;
                                let i: int = 0;
                                while (i < qd) {
                                    off = skip_dns_name(buf, off) + 4;
                                    i = i + 1;
                                }
                                i = 0;
                                while (i < total) {
                                    off = skip_dns_name(buf, off);
                                    let typ: int = get_u16(buf, off);
                                    let cls: int = get_u16(buf, off + 2);
                                    let rdlen: int = get_u16(buf, off + 8);
                                    if (typ == 1) {
                                        if (cls == 1) {
                                            if (rdlen == 4) {
                                                print_str(\"dns A \");
                                                print_ip(get_u32(buf, off + 10));
                                                print_str(\"\\n\");
                                                return 0;
                                            }
                                        }
                                    }
                                    off = off + 10 + rdlen;
                                    i = i + 1;
                                }
                            }
                        }
                    }
                }
            }
        }
        sys_sleep(1);
        tries = tries + 1;
    }
    return -1;
}

fn main() -> int {
    let buf: ptr<u8> = syscall1(23, 1);
    let mac: [6]u8;
    let gw_mac: [6]u8;
    let ip_be: int = 167772687;
    let gateway_be: int = 167772674;
    let dns_ip: int = 167772675;
    if (buf == 0) { print_str(\"dns: alloc failed\\n\"); return 1; }
    if (sys_net_info_raw(buf) < 0) { print_str(\"dns: net_info failed\\n\"); return 1; }
    if (*(buf + 0) == 0) { print_str(\"dns: network device not ready\\n\"); return 1; }
    mac[0] = *(buf + 8);
    mac[1] = *(buf + 9);
    mac[2] = *(buf + 10);
    mac[3] = *(buf + 11);
    mac[4] = *(buf + 12);
    mac[5] = *(buf + 13);
    print_str(\"DNS example.com via 10.0.2.3\\n\");
    send_arp(&mac, ip_be, gateway_be, buf);
    if (recv_arp(gateway_be, buf, &gw_mac) < 0) {
        print_str(\"dns: gateway ARP timeout\\n\");
        return 1;
    }
    send_dns(&mac, ip_be, dns_ip, &gw_mac, buf);
    if (wait_dns(buf, dns_ip) < 0) {
        print_str(\"dns: timeout\\n\");
        return 1;
    }
    syscall2(24, buf, 1);
    return 0;
}
""",
    "apps/http.ore": b"""import \"ore/std.oreh\";
import \"ore/net.oreh\";

fn put_u16(buf: ptr<u8>, off: int, v: int) -> int {
    *(buf + off) = (v >> 8) & 255;
    *(buf + off + 1) = v & 255;
    return 0;
}

fn put_u32(buf: ptr<u8>, off: int, v: int) -> int {
    *(buf + off) = (v >> 24) & 255;
    *(buf + off + 1) = (v >> 16) & 255;
    *(buf + off + 2) = (v >> 8) & 255;
    *(buf + off + 3) = v & 255;
    return 0;
}

fn get_u16(buf: ptr<u8>, off: int) -> int {
    return (*(buf + off) << 8) | *(buf + off + 1);
}

fn get_u32(buf: ptr<u8>, off: int) -> int {
    return (*(buf + off) << 24) | (*(buf + off + 1) << 16) | (*(buf + off + 2) << 8) | *(buf + off + 3);
}

fn fold(sum: int) -> int {
    while (sum > 65535) { sum = (sum & 65535) + (sum >> 16); }
    return sum;
}

fn csum(buf: ptr<u8>, off: int, len: int) -> int {
    let sum: int = 0;
    let i: int = 0;
    while (i + 1 < len) {
        sum = fold(sum + get_u16(buf, off + i));
        i = i + 2;
    }
    if (i < len) { sum = fold(sum + (*(buf + off + i) << 8)); }
    return (~sum) & 65535;
}

fn fill_dst(buf: ptr<u8>, mac: ptr<u8>) -> int {
    let i: int = 0;
    while (i < 6) { *(buf + i) = *(mac + i); i = i + 1; }
    return 0;
}

fn fill_src(buf: ptr<u8>, mac: ptr<u8>) -> int {
    let i: int = 0;
    while (i < 6) { *(buf + 6 + i) = *(mac + i); i = i + 1; }
    return 0;
}

fn send_arp(mac: ptr<u8>, ip_be: int, target_ip: int, buf: ptr<u8>) -> int {
    let i: int = 0;
    while (i < 6) { *(buf + i) = 255; i = i + 1; }
    fill_src(buf, mac);
    put_u16(buf, 12, 2054);
    put_u16(buf, 14, 1);
    put_u16(buf, 16, 2048);
    *(buf + 18) = 6;
    *(buf + 19) = 4;
    put_u16(buf, 20, 1);
    i = 0;
    while (i < 6) { *(buf + 22 + i) = *(mac + i); i = i + 1; }
    put_u32(buf, 28, ip_be);
    i = 0;
    while (i < 6) { *(buf + 32 + i) = 0; i = i + 1; }
    put_u32(buf, 38, target_ip);
    return sys_net_send(buf, 42);
}

fn recv_arp(target_ip: int, buf: ptr<u8>, out_mac: ptr<u8>) -> int {
    let tries: int = 0;
    while (tries < 800) {
        let n: int = sys_net_recv(buf, 1518);
        if (n >= 42) {
            if (get_u16(buf, 12) == 2054) {
                if (get_u16(buf, 20) == 2) {
                    if (get_u32(buf, 28) == target_ip) {
                        fill_dst(out_mac, buf + 22);
                        return 0;
                    }
                }
            }
        }
        sys_sleep(1);
        tries = tries + 1;
    }
    return -1;
}

fn write_dns_query(buf: ptr<u8>, off: int, host: ptr<char>) -> int {
    put_u16(buf, off + 0, 48880);
    put_u16(buf, off + 2, 256);
    put_u16(buf, off + 4, 1);
    put_u16(buf, off + 6, 0);
    put_u16(buf, off + 8, 0);
    put_u16(buf, off + 10, 0);
    let src: int = 0;
    let dst: int = off + 12;
    let label_pos: int = dst;
    let label_len: int = 0;
    dst = dst + 1;
    while (*(host + src) != 0) {
        let ch: int = *(host + src);
        if (ch == 46) {
            *(buf + label_pos) = label_len;
            label_pos = dst;
            dst = dst + 1;
            label_len = 0;
        } else {
            *(buf + dst) = ch;
            dst = dst + 1;
            label_len = label_len + 1;
        }
        src = src + 1;
    }
    *(buf + label_pos) = label_len;
    *(buf + dst) = 0;
    dst = dst + 1;
    put_u16(buf, dst, 1);
    put_u16(buf, dst + 2, 1);
    return dst + 4 - off;
}

fn send_dns(mac: ptr<u8>, dst_mac: ptr<u8>, buf: ptr<u8>, host: ptr<char>) -> int {
    let ip_be: int = 167772687;
    let dns_ip: int = 167772675;
    let dns_len: int = write_dns_query(buf, 42, host);
    fill_dst(buf, dst_mac);
    fill_src(buf, mac);
    put_u16(buf, 12, 2048);
    *(buf + 14) = 69;
    *(buf + 15) = 0;
    put_u16(buf, 16, 20 + 8 + dns_len);
    put_u16(buf, 18, 4661);
    put_u16(buf, 20, 0);
    *(buf + 22) = 64;
    *(buf + 23) = 17;
    put_u16(buf, 24, 0);
    put_u32(buf, 26, ip_be);
    put_u32(buf, 30, dns_ip);
    put_u16(buf, 24, csum(buf, 14, 20));
    put_u16(buf, 34, 49152);
    put_u16(buf, 36, 53);
    put_u16(buf, 38, 8 + dns_len);
    put_u16(buf, 40, 0);
    return sys_net_send(buf, 42 + dns_len);
}

fn skip_dns_name(buf: ptr<u8>, off: int) -> int {
    let len: int = *(buf + off);
    while (len != 0) {
        if ((len & 192) == 192) { return off + 2; }
        off = off + 1 + len;
        len = *(buf + off);
    }
    return off + 1;
}

fn wait_dns(buf: ptr<u8>, dns_ip: int) -> int {
    let tries: int = 0;
    while (tries < 1200) {
        let n: int = sys_net_recv(buf, 1518);
        if (n >= 86) {
            if (get_u16(buf, 12) == 2048) {
                if (*(buf + 23) == 17) {
                    if (get_u32(buf, 26) == dns_ip) {
                        if (get_u16(buf, 36) == 49152) {
                            if (get_u16(buf, 42) == 48880) {
                                let qd: int = get_u16(buf, 46);
                                let an: int = get_u16(buf, 48);
                                let total: int = an;
                                let off: int = 54;
                                let i: int = 0;
                                while (i < qd) { off = skip_dns_name(buf, off) + 4; i = i + 1; }
                                i = 0;
                                while (i < total) {
                                    off = skip_dns_name(buf, off);
                                    let typ: int = get_u16(buf, off);
                                    let cls: int = get_u16(buf, off + 2);
                                    let rdlen: int = get_u16(buf, off + 8);
                                    if (typ == 1) {
                                        if (cls == 1) {
                                            if (rdlen == 4) { return get_u32(buf, off + 10); }
                                        }
                                    }
                                    off = off + 10 + rdlen;
                                    i = i + 1;
                                }
                            }
                        }
                    }
                }
            }
        }
        sys_sleep(1);
        tries = tries + 1;
    }
    return 0;
}

fn tcp_checksum(buf: ptr<u8>, src_ip: int, dst_ip: int, tcp_len: int) -> int {
    let sum: int = 0;
    sum = fold(sum + ((src_ip >> 16) & 65535));
    sum = fold(sum + (src_ip & 65535));
    sum = fold(sum + ((dst_ip >> 16) & 65535));
    sum = fold(sum + (dst_ip & 65535));
    sum = fold(sum + 6);
    sum = fold(sum + tcp_len);
    let i: int = 0;
    while (i + 1 < tcp_len) {
        sum = fold(sum + get_u16(buf, 34 + i));
        i = i + 2;
    }
    if (i < tcp_len) { sum = fold(sum + (*(buf + 34 + i) << 8)); }
    return (~sum) & 65535;
}

fn write_http_payload(buf: ptr<u8>, host: ptr<char>, path: ptr<char>) -> int {
    let off: int = 54;
    let a: ptr<char> = \"GET \";
    let b: ptr<char> = \" HTTP/1.0\\r\\nHost: \";
    let c: ptr<char> = \"\\r\\n\\r\\n\";
    let i: int = 0;
    let j: int = 0;
    while (*(a + j) != 0) {
        *(buf + off + i) = *(a + j);
        i = i + 1;
        j = j + 1;
    }
    j = 0;
    while (*(path + j) != 0) {
        *(buf + off + i) = *(path + j);
        i = i + 1;
        j = j + 1;
    }
    j = 0;
    while (*(b + j) != 0) {
        *(buf + off + i) = *(b + j);
        i = i + 1;
        j = j + 1;
    }
    j = 0;
    while (*(host + j) != 0) {
        *(buf + off + i) = *(host + j);
        i = i + 1;
        j = j + 1;
    }
    j = 0;
    while (*(c + j) != 0) {
        *(buf + off + i) = *(c + j);
        i = i + 1;
        j = j + 1;
    }
    *(buf + off + i) = 0;
    return i;
}

fn write_ifconfig_payload(buf: ptr<u8>) -> int {
    let req: ptr<char> = \"GET /ip HTTP/1.0\\r\\nHost: ifconfig.me\\r\\n\\r\\n\";
    let i: int = 0;
    while (*(req + i) != 0) {
        *(buf + 54 + i) = *(req + i);
        i = i + 1;
    }
    return i;
}

fn parse_ipv4(s: ptr<char>) -> int {
    let part: int = 0;
    let value: int = 0;
    let count: int = 0;
    let i: int = 0;
    if (*(s + 0) == 0) { return 0; }
    while (*(s + i) != 0) {
        let ch: int = *(s + i);
        if (ch == 46) {
            if (part > 255) { return 0; }
            value = (value << 8) | part;
            part = 0;
            count = count + 1;
        } else {
            if (ch < 48) { return 0; }
            if (ch > 57) { return 0; }
            part = part * 10 + ch - 48;
        }
        i = i + 1;
    }
    if (part > 255) { return 0; }
    if (count != 3) { return 0; }
    return (value << 8) | part;
}

fn send_tcp(ctx: ptr<int>, mac: ptr<u8>, dst_mac: ptr<u8>, buf: ptr<u8>, meta: int) -> int {
    let ip_be: int = *(ctx + 0);
    let dst_ip: int = *(ctx + 1);
    let seq: int = *(ctx + 2);
    let ack: int = *(ctx + 3);
    let flags: int = meta & 255;
    let payload_len: int = meta >> 8;
    let tcp_len: int = 20 + payload_len;
    let total: int = 20 + tcp_len;
    if (payload_len > 0) {
        let req: ptr<char> = \"GET /ip HTTP/1.0\\r\\nHost: ifconfig.me\\r\\n\\r\\n\";
        let pi: int = 0;
        while (pi < payload_len) {
            *(buf + 54 + pi) = *(req + pi);
            pi = pi + 1;
        }
    }
    fill_dst(buf, dst_mac);
    fill_src(buf, mac);
    put_u16(buf, 12, 2048);
    *(buf + 14) = 69;
    *(buf + 15) = 0;
    put_u16(buf, 16, total);
    put_u16(buf, 18, 4662);
    put_u16(buf, 20, 0);
    *(buf + 22) = 64;
    *(buf + 23) = 6;
    put_u16(buf, 24, 0);
    put_u32(buf, 26, ip_be);
    put_u32(buf, 30, dst_ip);
    put_u16(buf, 24, csum(buf, 14, 20));
    put_u16(buf, 34, 49153);
    put_u16(buf, 36, 80);
    put_u32(buf, 38, seq);
    put_u32(buf, 42, ack);
    *(buf + 46) = 80;
    *(buf + 47) = flags;
    put_u16(buf, 48, 64240);
    put_u16(buf, 50, 0);
    put_u16(buf, 52, 0);
    put_u16(buf, 50, tcp_checksum(buf, ip_be, dst_ip, tcp_len));
    return sys_net_send(buf, 14 + total);
}

fn wait_synack(buf: ptr<u8>, dst_ip: int) -> int {
    let tries: int = 0;
    while (tries < 1200) {
        let n: int = sys_net_recv(buf, 1518);
        if (n >= 54) {
            if (get_u16(buf, 12) == 2048) {
                if (*(buf + 23) == 6) {
                    if (get_u32(buf, 26) == dst_ip) {
                        if (get_u16(buf, 36) == 49153) {
                            if ((*(buf + 47) & 18) == 18) {
                                return get_u32(buf, 38);
                            }
                        }
                    }
                }
            }
        }
        sys_sleep(1);
        tries = tries + 1;
    }
    return 0;
}

fn wait_http(buf: ptr<u8>, dst_ip: int) -> int {
    let tries: int = 0;
    while (tries != 2000) {
        let n: int = sys_net_recv(buf, 1518);
        if (n >= 55) {
            if (get_u16(buf, 12) == 2048) {
                if (*(buf + 23) == 6) {
                    if (get_u32(buf, 26) == dst_ip) {
                        if (get_u16(buf, 36) == 49153) {
                            let ihl: int = (*(buf + 14) & 15) * 4;
                            let thl: int = (*(buf + 14 + ihl + 12) >> 4) * 4;
                            let total: int = get_u16(buf, 16);
                            let off: int = 14 + ihl + thl;
                            let len: int = total - ihl - thl;
                            if (len > 0) {
                                if (len > 512) { len = 512; }
                                print_buf(buf + off, len);
                                print_str(\"\\n\");
                                return 0;
                            }
                        }
                    }
                }
            }
        }
        sys_sleep(1);
        tries = tries + 1;
    }
    return -1;
}

fn clear_chars(buf: ptr<char>, len: int) -> int {
    let i: int = 0;
    while (i < len) {
        *(buf + i) = 0;
        i = i + 1;
    }
    return 0;
}

fn copy_str_cap(dst: ptr<char>, src: ptr<char>, cap: int) -> int {
    let i: int = 0;
    while (*(src + i) != 0) {
        if (i + 1 >= cap) { break; }
        *(dst + i) = *(src + i);
        i = i + 1;
    }
    *(dst + i) = 0;
    return i;
}

fn parse_http_args(args: ptr<char>, host: ptr<char>, path: ptr<char>, addr: ptr<char>, cap: int) -> int {
    let i: int = 0;
    let j: int = 0;
    clear_chars(host, cap);
    clear_chars(path, cap);
    clear_chars(addr, cap);
    while (*(args + i) == 32) { i = i + 1; }
    while (*(args + i) != 0) {
        if (*(args + i) == 32) { break; }
        if (j + 1 < cap) {
            *(host + j) = *(args + i);
            j = j + 1;
        }
        i = i + 1;
    }
    *(host + j) = 0;
    if (j == 0) { return -1; }
    while (*(args + i) == 32) { i = i + 1; }
    if (*(args + i) == 0) {
        *(path + 0) = 47;
        *(path + 1) = 0;
        return 0;
    }
    j = 0;
    if (*(args + i) != 47) {
        *(path + 0) = 47;
        j = 1;
    }
    while (*(args + i) != 0) {
        if (*(args + i) == 32) { break; }
        if (j + 1 < cap) {
            *(path + j) = *(args + i);
            j = j + 1;
        }
        i = i + 1;
    }
    *(path + j) = 0;
    while (*(args + i) == 32) { i = i + 1; }
    j = 0;
    while (*(args + i) != 0) {
        if (*(args + i) == 32) { break; }
        if (j + 1 < cap) {
            *(addr + j) = *(args + i);
            j = j + 1;
        }
        i = i + 1;
    }
    *(addr + j) = 0;
    return 0;
}

fn main() -> int {
    let buf: ptr<u8> = syscall1(23, 1);
    let mac: [6]u8;
    let gw_mac: [6]u8;
    let ctx: [4]int;
    let args: [128]char;
    let host: [64]char;
    let path: [64]char;
    let addr: [64]char;
    let ip_be: int = 167772687;
    let gateway_be: int = 167772674;
    let dns_ip: int = 167772675;
    let dst_ip: int = 0;
    let server_seq: int = 0;
    let seq: int = 305419896;
    let payload_len: int = 0;
    let rc: int = 0;
    let ai: int = 0;
    let si: int = 0;
    let have_ip: int = 0;
    if (buf == 0) { print_str(\"http: alloc failed\\n\"); return 1; }
    if (sys_net_info_raw(buf) < 0) { print_str(\"http: net_info failed\\n\"); return 1; }
    if (*(buf + 0) == 0) { print_str(\"http: network device not ready\\n\"); return 1; }
    mac[0] = *(buf + 8); mac[1] = *(buf + 9); mac[2] = *(buf + 10);
    mac[3] = *(buf + 11); mac[4] = *(buf + 12); mac[5] = *(buf + 13);
    copy_str_cap(&host, \"ifconfig.me\", 64);
    copy_str_cap(&path, \"/\", 64);
    clear_chars(&addr, 64);
    if (sys_args(&args, 128) > 0) {
        clear_chars(&host, 64);
        clear_chars(&path, 64);
        while (args[ai] == 32) { ai = ai + 1; }
        si = 0;
        while (args[ai] != 0) {
            if (args[ai] == 32) { break; }
            if (si + 1 < 64) {
                host[si] = args[ai];
                si = si + 1;
            }
            ai = ai + 1;
        }
        host[si] = 0;
        if (si == 0) {
            print_str(\"usage: http <host> [path] [connect-ip]\\n\");
            return 1;
        }
        while (args[ai] == 32) { ai = ai + 1; }
        if (args[ai] == 0) {
            path[0] = 47;
            path[1] = 0;
        } else {
            si = 0;
            if (args[ai] != 47) {
                path[0] = 47;
                si = 1;
            }
            while (args[ai] != 0) {
                if (args[ai] == 32) { break; }
                if (si + 1 < 64) {
                    path[si] = args[ai];
                    si = si + 1;
                }
                ai = ai + 1;
            }
            path[si] = 0;
            while (args[ai] == 32) { ai = ai + 1; }
            si = 0;
            while (args[ai] != 0) {
                if (args[ai] == 32) { break; }
                if (si + 1 < 64) {
                    addr[si] = args[ai];
                    si = si + 1;
                }
                ai = ai + 1;
            }
            addr[si] = 0;
        }
    }
    print_str(\"HTTP \");
    print_str(&host);
    print_str(\" \");
    print_str(&path);
    print_str(\"\\n\");
    send_arp(&mac, ip_be, gateway_be, buf);
    rc = recv_arp(gateway_be, buf, &gw_mac);
    dst_ip = 580939665;
    have_ip = 1;
    ctx[0] = ip_be;
    ctx[1] = dst_ip;
    ctx[2] = seq;
    ctx[3] = 0;
    print_str(\"tcp connect\\n\");
    rc = send_tcp(&ctx, &mac, &gw_mac, buf, 2);
    server_seq = wait_synack(buf, dst_ip);
    ctx[2] = seq + 1;
    ctx[3] = server_seq + 1;
    rc = send_tcp(&ctx, &mac, &gw_mac, buf, 10008);
    rc = wait_http(buf, dst_ip);
    if (rc != 0) { print_str(\"http: response timeout\\n\"); return 1; }
    syscall2(24, buf, 1);
    return 0;
}
""",
    "include/ore/gfx.oreh": b"""extern fn syscall1(n: int, a: int) -> int;
extern fn syscall2(n: int, a: int, b: int) -> int;
extern fn syscall4(n: int, a: int, b: int, c: int, d: int) -> int;

#[packed]
struct OreGfxInfo {
    width: u32;
    height: u32;
    logical_width: u32;
    logical_height: u32;
    format: u32;
    pitch: u32;
    palette_count: u32;
    reserved: u32;
}

fn gfx_info(info: ptr<OreGfxInfo>) -> int { return syscall1(20, info); }
fn gfx_present(buf: ptr<u8>, width: int, height: int) -> int { return syscall4(21, buf, 307200, width, height); }
""",
    "include/ore/input.oreh": b"""extern fn syscall1(n: int, a: int) -> int;

#[packed]
struct OreInputState {
    keys: u32;
    ascii: u32;
    mouse_x: i32;
    mouse_y: i32;
    buttons: u32;
    frame: u32;
}

fn input_state(state: ptr<OreInputState>) -> int { return syscall1(22, state); }
""",
    "include/ore/mem.oreh": b"""extern fn syscall1(n: int, a: int) -> int;
extern fn syscall2(n: int, a: int, b: int) -> int;

fn alloc_pages(count: int) -> ptr<u8> { return syscall1(23, count); }
fn free_pages(ptr: ptr<u8>, count: int) -> int { return syscall2(24, ptr, count); }
""",
    "include/ore/terrain.oreh": b"""extern fn syscall4(n: int, a: int, b: int, c: int, d: int) -> int;

#[packed]
struct OreTerrainJob {
    seed: u64;
    frame: u64;
    camera_x: i32;
    camera_y: i32;
    camera_z: i32;
    yaw: i32;
    pitch: i32;
    speed: i32;
    flags: u32;
    reserved: u32;
}

#[packed]
struct OreTerrainResult {
    workers_used: u32;
    objects_rendered: u32;
    collision_flags: u32;
    reserved: u32;
    checksum: u64;
    render_ticks: u64;
}

fn terrain_render(job: ptr<OreTerrainJob>, buf: ptr<u8>, result: ptr<OreTerrainResult>) -> int {
    return syscall4(25, job, buf, 307200, result);
}
""",
    "examples/gfx_clear.ore": b"""import \"ore/std.oreh\";
import \"ore/gfx.oreh\";
import \"ore/mem.oreh\";

fn main() -> int {
    let buf: ptr<u8> = alloc_pages(76);
    let i: int = 0;
    if (buf == 0) { print_str(\"gfx_clear: alloc failed\\n\"); return 1; }
    while (i < 307200) {
        *(buf + i) = 2;
        i = i + 1;
    }
    gfx_present(buf, 640, 480);
    free_pages(buf, 76);
    print_str(\"gfx_clear: ok\\n\");
    return 0;
}
""",
    "examples/gfx_palette.ore": b"""import \"ore/std.oreh\";
import \"ore/gfx.oreh\";
import \"ore/mem.oreh\";

fn main() -> int {
    let buf: ptr<u8> = alloc_pages(76);
    let x: int = 0;
    let y: int = 0;
    if (buf == 0) { print_str(\"gfx_palette: alloc failed\\n\"); return 1; }
    y = 0;
    while (y < 480) {
        x = 0;
        while (x < 640) {
            *(buf + y * 640 + x) = (x / 40) & 15;
            x = x + 1;
        }
        y = y + 1;
    }
    gfx_present(buf, 640, 480);
    free_pages(buf, 76);
    print_str(\"gfx_palette: ok\\n\");
    return 0;
}
""",
    "examples/input_test.ore": b"""import \"ore/std.oreh\";
import \"ore/input.oreh\";

fn main() -> int {
    let st: OreInputState;
    let i: int = 0;
    print_str(\"input_test: press WASD/arrows/space/q\\n\");
    while (i < 20) {
        input_state(&st);
        print_str(\"keys=\");
        puti(st.keys);
        print_str(\" ascii=\");
        puti(st.ascii);
        print_str(\"\\n\");
        if ((st.keys & 32) != 0) { break; }
        sys_sleep(3);
        i = i + 1;
    }
    return 0;
}
""",
    "examples/alloc_pages.ore": b"""import \"ore/std.oreh\";
import \"ore/mem.oreh\";

fn main() -> int {
    let p: ptr<u8> = alloc_pages(2);
    if (p == 0) { print_str(\"alloc_pages: failed\\n\"); return 1; }
    *p = 123;
    *(p + 4096) = 45;
    print_str(\"alloc_pages: \");
    puti(*p);
    print_str(\" \");
    puti(*(p + 4096));
    print_str(\"\\n\");
    free_pages(p, 2);
    return 0;
}
""",
    "apps/gfxtest.ore": b"""import \"ore/std.oreh\";
import \"ore/gfx.oreh\";
import \"ore/mem.oreh\";

fn main() -> int {
    let info: OreGfxInfo;
    let buf: ptr<u8> = alloc_pages(76);
    let x: int = 0;
    let y: int = 0;
    if (buf == 0) { print_str(\"gfxtest: alloc failed\\n\"); return 1; }
    if (gfx_info(&info) < 0) { print_str(\"gfxtest: no gfx\\n\"); return 1; }
    y = 0;
    while (y < 480) {
        x = 0;
        while (x < 640) {
            *(buf + y * 640 + x) = ((x / 40) + (y / 30)) & 15;
            x = x + 1;
        }
        y = y + 1;
    }
    if (gfx_present(buf, 640, 480) < 0) { print_str(\"gfxtest: present failed\\n\"); return 1; }
    free_pages(buf, 76);
    print_str(\"gfxtest: ok 640x480 indexed16\\n\");
    return 0;
}
""",
    "apps/inputtest.ore": b"""import \"ore/std.oreh\";
import \"ore/input.oreh\";

fn main() -> int {
    let st: OreInputState;
    let i: int = 0;
    print_str(\"inputtest: WASD/arrows space q\\n\");
    while (i < 30) {
        input_state(&st);
        print_str(\"keys=\");
        puti(st.keys);
        print_str(\" ascii=\");
        puti(st.ascii);
        print_str(\" frame=\");
        puti(st.frame);
        print_str(\"\\n\");
        if ((st.keys & 32) != 0) { return 0; }
        sys_sleep(2);
        i = i + 1;
    }
    return 0;
}
""",
    "apps/terraintest.ore": b"""import \"ore/std.oreh\";
import \"ore/gfx.oreh\";
import \"ore/mem.oreh\";
import \"ore/terrain.oreh\";

fn main() -> int {
    let job: OreTerrainJob;
    let res: OreTerrainResult;
    let buf: ptr<u8> = alloc_pages(76);
    let frame: int = 0;
    if (buf == 0) { print_str(\"terraintest: alloc failed\\n\"); return 1; }
    job.seed = 4919;
    job.camera_x = 0;
    job.camera_y = 0;
    job.camera_z = 0;
    job.yaw = 0;
    job.pitch = 0;
    job.speed = 8;
    job.flags = 0;
    while (frame < 12) {
        job.frame = frame;
        job.camera_z = frame * 24;
        job.yaw = frame * 6;
        if (terrain_render(&job, buf, &res) < 0) {
            print_str(\"terraintest: render failed\\n\");
            return 1;
        }
        gfx_present(buf, 640, 480);
        frame = frame + 1;
    }
    print_str(\"terraintest: workers=\");
    puti(res.workers_used);
    print_str(\" objects=\");
    puti(res.objects_rendered);
    print_str(\" checksum=\");
    puti(res.checksum);
    print_str(\"\\n\");
    free_pages(buf, 76);
    return 0;
}
""",
    "apps/skyrun.ore": b"""import \"ore/std.oreh\";
import \"ore/gfx.oreh\";
import \"ore/input.oreh\";
import \"ore/mem.oreh\";

fn clamp(v: int, lo: int, hi: int) -> int {
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

fn abs(v: int) -> int {
    if (v < 0) { return -v; }
    return v;
}

fn hash(n: int) -> int {
    n = n * 1103515245 + 12345;
    n = (n >> 16) & 32767;
    return n;
}

fn plot(buf: ptr<u8>, x: int, y: int, c: int) -> int {
    if (x < 0) { return 0; }
    if (x >= 640) { return 0; }
    if (y < 0) { return 0; }
    if (y >= 480) { return 0; }
    *(buf + y * 640 + x) = c;
    return 0;
}

fn hline(buf: ptr<u8>, y: int, x0: int, x1: int, c: int) -> int {
    let x: int = 0;
    if (y < 0) { return 0; }
    if (y >= 480) { return 0; }
    x0 = clamp(x0, 0, 639);
    x1 = clamp(x1, 0, 639);
    if (x1 < x0) { return 0; }
    x = x0;
    while (x <= x1) {
        *(buf + y * 640 + x) = c;
        x = x + 1;
    }
    return 0;
}

fn rect(buf: ptr<u8>, x: int, y: int, w: int, h: int, c: int) -> int {
    let yy: int = 0;
    while (yy < h) {
        hline(buf, y + yy, x, x + w - 1, c);
        yy = yy + 1;
    }
    return 0;
}

fn line(buf: ptr<u8>, x0: int, y0: int, x1: int, y1: int, c: int) -> int {
    let dx: int = abs(x1 - x0);
    let dy: int = abs(y1 - y0);
    let steps: int = dx;
    let i: int = 0;
    if (dy > steps) { steps = dy; }
    if (steps < 1) { plot(buf, x0, y0, c); return 0; }
    while (i <= steps) {
        plot(buf, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, c);
        i = i + 1;
    }
    return 0;
}

fn clear_scene(buf: ptr<u8>, horizon: int) -> int {
    rect(buf, 0, 0, 640, 72, 1);
    rect(buf, 0, 72, 640, 70, 2);
    rect(buf, 0, 142, 640, horizon - 142, 3);
    rect(buf, 0, horizon, 640, 480 - horizon, 4);
    return 0;
}

fn mountains(buf: ptr<u8>, camz: int, yaw: int, horizon: int) -> int {
    let x: int = 0;
    let h: int = 0;
    while (x < 640) {
        h = 18 + (hash(x / 16 + camz / 420 + yaw / 90) & 31);
        rect(buf, x, horizon - h, 10, h, 7);
        x = x + 10;
    }
    hline(buf, horizon, 0, 639, 3);
    return 0;
}

fn terrain(buf: ptr<u8>, camx: int, camz: int, yaw: int, pitch: int, roll: int) -> int {
    let horizon: int = 150 + pitch / 10;
    let y: int = horizon;
    let dy: int = 0;
    let z: int = 0;
    let center: int = 0;
    let half: int = 0;
    let river: int = 0;
    let road: int = 0;
    horizon = clamp(horizon, 96, 218);
    clear_scene(buf, horizon);
    mountains(buf, camz, yaw, horizon);
    while (y < 480) {
        dy = y - horizon + 1;
        z = camz + 17000 / (dy + 9);
        center = 320 + roll / 2 + yaw / 18 + ((hash(z / 150) & 63) - 31) * dy / 210 - camx * dy / 3600;
        river = 8 + dy * dy / 270;
        road = river + 26 + dy / 3;
        if (dy < 35) {
            hline(buf, y, 0, 639, 4);
        } else {
            hline(buf, y, 0, 639, 5);
        }
        if (((z / 260) & 1) == 0) { hline(buf, y, 0, 639, 4); }
        half = road;
        hline(buf, y, center - half - 12, center + half + 12, 10);
        half = river;
        hline(buf, y, center - half, center + half, 8);
        if (((z / 96) & 3) == 0) { hline(buf, y, center - half, center + half, 9); }
        if ((y & 15) == 0) {
            hline(buf, y, center - road - 30, center - road + 4, 15);
            hline(buf, y, center + road - 4, center + road + 30, 15);
        }
        plot(buf, center - river, y, 15);
        plot(buf, center + river, y, 15);
        y = y + 2;
    }
    return horizon;
}

fn sprite_tree(buf: ptr<u8>, sx: int, sy: int, sz: int, kind: int) -> int {
    let h: int = 8 + 900 / sz;
    let w: int = 3 + 320 / sz;
    if (h > 42) { h = 42; }
    if (w > 18) { w = 18; }
    if (kind == 0) {
        rect(buf, sx - 1, sy - h / 2, 3, h / 2, 11);
        rect(buf, sx - w, sy - h, w * 2, h / 2, 12);
        rect(buf, sx - w / 2, sy - h - h / 5, w, h / 3, 12);
    } else {
        rect(buf, sx - w, sy - h / 2, w * 2, h / 2, 13);
        rect(buf, sx - w / 2, sy - h, w, h / 2, 13);
        rect(buf, sx + w / 2, sy - h / 2 - 1, w, 3, 14);
    }
    return 0;
}

fn objects(buf: ptr<u8>, camx: int, camz: int, yaw: int, horizon: int, fire: int) -> int {
    let i: int = 31;
    let z: int = 0;
    let dz: int = 0;
    let wx: int = 0;
    let sx: int = 0;
    let sy: int = 0;
    let kind: int = 0;
    while (i >= 0) {
        z = ((camz / 180) + i) * 180 + 360;
        dz = z - camz;
        wx = ((hash(i * 37 + z / 180) & 511) - 256) * 3;
        if (abs(wx) < 120) { wx = wx + 260; }
        sx = 320 + yaw / 15 + (wx - camx) * 280 / dz;
        sy = horizon + 34 + 26000 / dz;
        kind = hash(i * 83 + z / 64) & 3;
        if (sx > -40) {
            if (sx < 680) {
                if (sy > horizon) {
                    if (sy < 474) {
                        if (kind == 1) { sprite_tree(buf, sx, sy, dz, 1); }
                        else { sprite_tree(buf, sx, sy, dz, 0); }
                    }
                }
            }
        }
        i = i - 1;
    }
    if (fire != 0) {
        line(buf, 320, 238, 320, 80, 14);
        rect(buf, 316, 224, 9, 9, 15);
    }
    return 0;
}

fn cockpit(buf: ptr<u8>, roll: int, pitch: int, speed: int, shield: int, score: int) -> int {
    let i: int = 0;
    let cy: int = 238 + pitch / 20;
    line(buf, 276, cy, 308, cy + roll / 6, 15);
    line(buf, 332, cy + roll / 6, 364, cy, 15);
    line(buf, 320, cy - 22, 320, cy + 22, 15);
    line(buf, 302, cy, 338, cy, 15);
    while (i < 72) {
        if ((i & 7) == 0) {
            plot(buf, 284 + i, 300 + roll / 8, 15);
            plot(buf, 284 + i, 302 + roll / 8, 6);
        }
        i = i + 1;
    }
    line(buf, 92, 468, 246, 392 + roll / 5, 6);
    line(buf, 548, 468, 394, 392 - roll / 5, 6);
    line(buf, 104, 468, 258, 398 + roll / 5, 15);
    line(buf, 536, 468, 382, 398 - roll / 5, 15);
    rect(buf, 18, 18, 124, 6, 0);
    rect(buf, 20, 20, clamp(shield, 0, 120), 2, 12);
    rect(buf, 18, 30, 124, 5, 0);
    rect(buf, 20, 32, clamp(speed, 0, 120), 1, 14);
    rect(buf, 502, 18, 118, 5, 0);
    rect(buf, 504, 20, score & 111, 1, 13);
    return 0;
}

fn checksum(buf: ptr<u8>) -> int {
    let i: int = 0;
    let s: int = 0;
    while (i < 307200) {
        s = s + *(buf + i) * 17 + (i & 255);
        if (s > 1000000000) { s = s - 1000000000; }
        i = i + 257;
    }
    return s;
}

fn main() -> int {
    let args: [64]char;
    let st: OreInputState;
    let buf: ptr<u8> = alloc_pages(76);
    let frame: int = 0;
    let max_frames: int = 100000;
    let smoke: int = 0;
    let x: int = 0;
    let z: int = 0;
    let yaw: int = 0;
    let pitch: int = 0;
    let roll: int = 0;
    let speed: int = 66;
    let shield: int = 120;
    let score: int = 0;
    let hits: int = 0;
    let fire: int = 0;
    let horizon: int = 0;
    let sum: int = 0;
    if (buf == 0) { print_str(\"skyrun: alloc failed\\n\"); return 1; }
    sys_args(&args, 64);
    if (streq(&args, \"--smoke\")) { smoke = 1; max_frames = 120; }
    while (frame < max_frames) {
        input_state(&st);
        if ((st.keys & 1) != 0) { roll = roll - 10; }
        if ((st.keys & 2) != 0) { roll = roll + 10; }
        if ((st.keys & 4) != 0) { pitch = pitch - 5; speed = speed + 1; }
        if ((st.keys & 8) != 0) { pitch = pitch + 5; speed = speed - 1; }
        if (st.ascii == 97) { roll = roll - 10; }
        if (st.ascii == 100) { roll = roll + 10; }
        if (st.ascii == 119) { pitch = pitch - 5; speed = speed + 1; }
        if (st.ascii == 115) { pitch = pitch + 5; speed = speed - 1; }
        if (smoke != 0) {
            if ((frame & 63) < 32) { roll = roll + 6; }
            else { roll = roll - 6; }
            if ((frame & 31) < 16) { pitch = pitch - 3; }
            else { pitch = pitch + 3; }
        }
        if ((st.keys & 32) != 0) { break; }
        fire = 0;
        if ((st.keys & 16) != 0) { fire = 1; score = score + 2; }
        if (smoke != 0) {
            if ((frame % 17) == 0) { fire = 1; }
        }
        roll = clamp(roll * 7 / 8, -96, 96);
        pitch = clamp(pitch * 8 / 9, -360, 360);
        speed = clamp(speed, 44, 118);
        yaw = yaw + roll / 12;
        x = x + yaw / 22;
        z = z + speed;
        horizon = terrain(buf, x, z, yaw, pitch, roll);
        objects(buf, x, z, yaw, horizon, fire);
        if (fire != 0) {
            if (((frame / 8) & 3) == 0) { hits = hits + 1; score = score + 25; }
        }
        if (pitch > 240) { shield = shield - 1; }
        cockpit(buf, roll, pitch, speed, shield, score);
        sum = sum + checksum(buf);
        if (sum > 1000000000) { sum = sum - 1000000000; }
        gfx_present(buf, 640, 480);
        if (smoke == 0) { sys_yield(); }
        if (shield <= 0) { break; }
        frame = frame + 1;
    }
    if (smoke != 0) {
        print_str(\"skyrun: smoke ok frames=\");
        puti(frame);
        print_str(\" score=\");
        puti(score);
        print_str(\" hits=\");
        puti(hits);
        print_str(\" checksum=\");
        puti(sum);
        print_str(\"\\n\");
    }
    free_pages(buf, 76);
    return 0;
}
""",
    "apps/raycast.ore": b"""import \"ore/std.oreh\";
import \"ore/gfx.oreh\";
import \"ore/input.oreh\";
import \"ore/mem.oreh\";

fn clamp(v: int, lo: int, hi: int) -> int {
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

fn abs(v: int) -> int {
    if (v < 0) { return -v; }
    return v;
}

fn hash(n: int) -> int {
    n = n * 1103515245 + 12345;
    return (n >> 16) & 32767;
}

fn sin1024(a: int) -> int {
    while (a < 0) { a = a + 1024; }
    while (a >= 1024) { a = a - 1024; }
    if (a < 256) { return a * 4; }
    if (a < 512) { return (512 - a) * 4; }
    if (a < 768) { return -(a - 512) * 4; }
    return -(1024 - a) * 4;
}

fn cos1024(a: int) -> int {
    return sin1024(a + 256);
}

fn wall_at(cx: int, cy: int) -> int {
    let h: int = 0;
    if (cx < 0) { return 1; }
    if (cy < 0) { return 1; }
    if (cx >= 32) { return 1; }
    if (cy >= 32) { return 1; }
    if ((cx & 7) == 0) {
        if ((cy & 3) != 1) { return 1; }
    }
    if ((cy & 7) == 0) {
        if ((cx & 3) != 2) { return 1; }
    }
    h = hash(cx * 73 + cy * 151);
    if ((h & 31) < 3) { return 1; }
    return 0;
}

fn plot(buf: ptr<u8>, x: int, y: int, c: int) -> int {
    if (x < 0) { return 0; }
    if (x >= 640) { return 0; }
    if (y < 0) { return 0; }
    if (y >= 480) { return 0; }
    *(buf + y * 640 + x) = c;
    return 0;
}

fn hline(buf: ptr<u8>, y: int, x0: int, x1: int, c: int) -> int {
    let x: int = 0;
    if (y < 0) { return 0; }
    if (y >= 480) { return 0; }
    x0 = clamp(x0, 0, 639);
    x1 = clamp(x1, 0, 639);
    if (x1 < x0) { return 0; }
    x = x0;
    while (x <= x1) {
        *(buf + y * 640 + x) = c;
        x = x + 1;
    }
    return 0;
}

fn rect(buf: ptr<u8>, x: int, y: int, w: int, h: int, c: int) -> int {
    let yy: int = 0;
    while (yy < h) {
        hline(buf, y + yy, x, x + w - 1, c);
        yy = yy + 1;
    }
    return 0;
}

fn line(buf: ptr<u8>, x0: int, y0: int, x1: int, y1: int, c: int) -> int {
    let dx: int = abs(x1 - x0);
    let dy: int = abs(y1 - y0);
    let steps: int = dx;
    let i: int = 0;
    if (dy > steps) { steps = dy; }
    if (steps < 1) { plot(buf, x0, y0, c); return 0; }
    while (i <= steps) {
        plot(buf, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, c);
        i = i + 1;
    }
    return 0;
}

fn clear_frame(buf: ptr<u8>, pitch: int) -> int {
    let y: int = 0;
    let h: int = 238 + pitch / 4;
    h = clamp(h, 120, 330);
    while (y < h) {
        if (y < 96) { hline(buf, y, 0, 639, 1); }
        else { hline(buf, y, 0, 639, 2); }
        y = y + 1;
    }
    while (y < 480) {
        if (((y + pitch / 3) & 31) < 2) { hline(buf, y, 0, 639, 6); }
        else {
            if (y < 350) { hline(buf, y, 0, 639, 4); }
            else { hline(buf, y, 0, 639, 5); }
        }
        y = y + 1;
    }
    return h;
}

fn draw_column(buf: ptr<u8>, sx: int, top: int, bottom: int, color: int, shade: int) -> int {
    let x: int = sx;
    let c: int = color;
    if (shade > 2700) { c = 6; }
    if (shade > 3700) { c = 0; }
    while (x < sx + 4) {
        line(buf, x, top, x, bottom, c);
        if (((x + top) & 3) == 0) { plot(buf, x, top, 15); }
        x = x + 1;
    }
    return 0;
}

fn render_walls(buf: ptr<u8>, px: int, py: int, ang: int, pitch: int, depth: ptr<int>) -> int {
    let col: int = 0;
    let ra: int = 0;
    let dx: int = 0;
    let dy: int = 0;
    let dist: int = 0;
    let rx: int = 0;
    let ry: int = 0;
    let hit: int = 0;
    let height: int = 0;
    let top: int = 0;
    let bot: int = 0;
    let color: int = 0;
    while (col < 160) {
        ra = ang + (col - 80) * 2;
        dx = cos1024(ra);
        dy = sin1024(ra);
        dist = 32;
        hit = 0;
        while (dist < 4200) {
            rx = px + dx * dist / 1024;
            ry = py + dy * dist / 1024;
            if (wall_at(rx / 256, ry / 256) != 0) {
                hit = 1;
                break;
            }
            dist = dist + 28;
        }
        if (hit == 0) { dist = 4200; }
        *(depth + col) = dist;
        height = 47000 / (dist + 1);
        height = clamp(height, 8, 430);
        top = 238 + pitch / 4 - height / 2;
        bot = top + height;
        color = 7 + (hash((rx / 256) * 91 + (ry / 256) * 17) & 3);
        if (color == 10) { color = 12; }
        draw_column(buf, col * 4, top, bot, color, dist);
        col = col + 1;
    }
    return 0;
}

fn sprite(buf: ptr<u8>, sx: int, sy: int, size: int, color: int) -> int {
    size = clamp(size, 4, 70);
    rect(buf, sx - size / 2, sy - size / 2, size, size, color);
    rect(buf, sx - size / 4, sy - size, size / 2, size / 2, color);
    rect(buf, sx - size / 2, sy + size / 4, size, 3, 0);
    return 0;
}

fn render_sprites(buf: ptr<u8>, px: int, py: int, ang: int, fire: int, depth: ptr<int>) -> int {
    let i: int = 24;
    let wx: int = 0;
    let wy: int = 0;
    let rx: int = 0;
    let ry: int = 0;
    let ca: int = cos1024(ang);
    let sa: int = sin1024(ang);
    let fwd: int = 0;
    let side: int = 0;
    let sx: int = 0;
    let col: int = 0;
    let sy: int = 0;
    let size: int = 0;
    let visible: int = 0;
    let hit: int = 0;
    while (i >= 0) {
        wx = ((hash(i * 97) & 31) * 256) + 128;
        wy = ((hash(i * 193 + 19) & 31) * 256) + 128;
        rx = wx - px;
        ry = wy - py;
        fwd = (rx * ca + ry * sa) / 1024;
        side = (-rx * sa + ry * ca) / 1024;
        if (fwd > 120) {
            if (fwd < 3200) {
                sx = 320 + side * 420 / fwd;
                sy = 250 - 22000 / fwd;
                size = 21000 / fwd;
                col = clamp(sx / 4, 0, 159);
                visible = 0;
                if (fwd < *(depth + col) - 60) { visible = 1; }
                if (col > 1) {
                    if (fwd < *(depth + col - 1) - 60) { visible = 1; }
                }
                if (col < 158) {
                    if (fwd < *(depth + col + 1) - 60) { visible = 1; }
                }
                if (sx > -80) {
                    if (sx < 720) {
                        if (visible != 0) {
                            sprite(buf, sx, sy + size, size, 13);
                            if (fire != 0) {
                                if (abs(sx - 320) < size / 2 + 10) {
                                    if (fwd < 1500) { hit = 1; }
                                }
                            }
                        }
                    }
                }
            }
        }
        i = i - 1;
    }
    if (fire != 0) {
        line(buf, 294, 264, 318, 242, 14);
        line(buf, 346, 264, 322, 242, 14);
        line(buf, 312, 248, 328, 232, 15);
        line(buf, 328, 248, 312, 232, 15);
        if (hit != 0) {
            rect(buf, 306, 226, 28, 28, 14);
            rect(buf, 314, 234, 12, 12, 15);
        }
    }
    return hit;
}

fn hud(buf: ptr<u8>, shield: int, ammo: int, score: int) -> int {
    line(buf, 300, 240, 340, 240, 15);
    line(buf, 320, 220, 320, 260, 15);
    rect(buf, 18, 18, 126, 6, 0);
    rect(buf, 20, 20, clamp(shield, 0, 120), 2, 12);
    rect(buf, 18, 30, 126, 5, 0);
    rect(buf, 20, 32, clamp(ammo, 0, 120), 1, 14);
    rect(buf, 500, 18, 120, 5, 0);
    rect(buf, 502, 20, score & 111, 1, 13);
    return 0;
}

fn minimap(buf: ptr<u8>, px: int, py: int, ang: int) -> int {
    let mx: int = 0;
    let my: int = 0;
    while (my < 8) {
        mx = 0;
        while (mx < 8) {
            if (wall_at(px / 256 - 4 + mx, py / 256 - 4 + my) != 0) {
                rect(buf, 536 + mx * 8, 386 + my * 8, 7, 7, 7);
            } else {
                rect(buf, 536 + mx * 8, 386 + my * 8, 7, 7, 0);
            }
            mx = mx + 1;
        }
        my = my + 1;
    }
    rect(buf, 568, 418, 5, 5, 14);
    line(buf, 570, 420, 570 + cos1024(ang) / 90, 420 + sin1024(ang) / 90, 15);
    return 0;
}

fn checksum(buf: ptr<u8>) -> int {
    let i: int = 0;
    let s: int = 0;
    while (i < 307200) {
        s = s + *(buf + i) * 23 + (i & 127);
        if (s > 1000000000) { s = s - 1000000000; }
        i = i + 193;
    }
    return s;
}

fn main() -> int {
    let args: [64]char;
    let st: OreInputState;
    let buf: ptr<u8> = alloc_pages(76);
    let depth: [160]int;
    let frame: int = 0;
    let max_frames: int = 100000;
    let smoke: int = 0;
    let px: int = 448;
    let py: int = 448;
    let nx: int = 0;
    let ny: int = 0;
    let ang: int = 64;
    let pitch: int = 0;
    let speed: int = 0;
    let shield: int = 120;
    let ammo: int = 120;
    let score: int = 0;
    let fire: int = 0;
    let hit: int = 0;
    let sum: int = 0;
    if (buf == 0) { print_str(\"raycast: alloc failed\\n\"); return 1; }
    sys_args(&args, 64);
    if (streq(&args, \"--smoke\")) { smoke = 1; max_frames = 120; }
    while (frame < max_frames) {
        input_state(&st);
        if ((st.keys & 1) != 0) { ang = ang - 10; }
        if ((st.keys & 2) != 0) { ang = ang + 10; }
        if ((st.keys & 4) != 0) { speed = speed + 7; pitch = pitch - 4; }
        if ((st.keys & 8) != 0) { speed = speed - 7; pitch = pitch + 4; }
        if (st.ascii == 97) { ang = ang - 10; }
        if (st.ascii == 100) { ang = ang + 10; }
        if (st.ascii == 119) { speed = speed + 7; pitch = pitch - 4; }
        if (st.ascii == 115) { speed = speed - 7; pitch = pitch + 4; }
        if (smoke != 0) {
            ang = ang + 5;
            if ((frame & 31) < 18) { speed = speed + 4; }
            else { speed = speed - 3; }
        }
        if ((st.keys & 32) != 0) { break; }
        fire = 0;
        if ((st.keys & 16) != 0) { fire = 1; }
        if (smoke != 0) {
            if ((frame % 19) == 0) { fire = 1; }
        }
        if (fire != 0) {
            if (ammo > 0) { ammo = ammo - 2; }
            else { fire = 0; }
        }
        speed = clamp(speed * 6 / 8, -36, 58);
        pitch = clamp(pitch * 7 / 8, -140, 140);
        nx = px + cos1024(ang) * speed / 1024;
        ny = py + sin1024(ang) * speed / 1024;
        if (wall_at(nx / 256, ny / 256) == 0) {
            px = nx;
            py = ny;
        } else {
            shield = shield - 2;
            speed = -speed / 2;
        }
        clear_frame(buf, pitch);
        render_walls(buf, px, py, ang, pitch, &depth);
        hit = render_sprites(buf, px, py, ang, fire, &depth);
        if (hit != 0) { score = score + 25; }
        minimap(buf, px, py, ang);
        hud(buf, shield, ammo, score);
        sum = sum + checksum(buf);
        if (sum > 1000000000) { sum = sum - 1000000000; }
        gfx_present(buf, 640, 480);
        if (smoke == 0) { sys_yield(); }
        if (shield <= 0) { break; }
        frame = frame + 1;
    }
    if (smoke != 0) {
        print_str(\"raycast: smoke ok frames=\");
        puti(frame);
        print_str(\" score=\");
        puti(score);
        print_str(\" shield=\");
        puti(shield);
        print_str(\" checksum=\");
        puti(sum);
        print_str(\"\\n\");
    }
    free_pages(buf, 76);
    return 0;
}
""",
})

ROOT_FILES["apps/osh.ore"] = b"""import \"ore/std.oreh\";

fn clear_buf(buf: ptr<char>, len: int) -> int {
    let i: int = 0;
    while (i < len) {
        *(buf + i) = 0;
        i = i + 1;
    }
    return 0;
}

fn read_line(buf: ptr<char>, cap: int) -> int {
    let n: int = 0;
    let ch: char = 0;
    while (n + 1 < cap) {
        if (sys_read(0, &ch, 1) == 1) {
            if (ch == 13) { ch = 10; }
            if (ch == 10) {
                *(buf + n) = 0;
                print_str(\"\\n\");
                return n;
            }
            if (ch == 8) {
                if (n > 0) {
                    n = n - 1;
                    *(buf + n) = 0;
                    print_str(\"\\b \\b\");
                }
            } else {
                if (ch == 127) {
                    if (n > 0) {
                        n = n - 1;
                        *(buf + n) = 0;
                        print_str(\"\\b \\b\");
                    }
                } else {
                    if (ch >= 32) {
                        if (ch < 127) {
                            *(buf + n) = ch;
                            n = n + 1;
                            print_buf(&ch, 1);
                        }
                    }
                }
            }
        } else {
            sys_yield();
        }
    }
    *(buf + n) = 0;
    return n;
}

fn split_line(line: ptr<char>, cmd: ptr<char>, cmd_cap: int, arg: ptr<char>, arg_cap: int) -> int {
    let i: int = 0;
    let j: int = 0;
    let end: int = 0;
    while (*(line + i) == 32) { i = i + 1; }
    while (*(line + i) != 0) {
        if (*(line + i) == 32) { break; }
        if (j + 1 < cmd_cap) {
            *(cmd + j) = *(line + i);
            j = j + 1;
        }
        i = i + 1;
    }
    *(cmd + j) = 0;
    while (*(line + i) == 32) { i = i + 1; }
    j = 0;
    end = 0;
    while (*(line + i) != 0) {
        if (j + 1 < arg_cap) {
            *(arg + j) = *(line + i);
            if (*(line + i) != 32) { end = j + 1; }
            j = j + 1;
        }
        i = i + 1;
    }
    j = end;
    *(arg + j) = 0;
    return strlen(cmd);
}

fn builtin_ls(path: ptr<char>) -> int {
    let st: OreVfsStat;
    let i: int = 0;
    let target: ptr<char> = path;
    if (strlen(path) == 0) { target = \"/disk\"; }
    while (sys_readdir_path(target, i, &st) >= 0) {
        print_buf(&st.name, strlen(&st.name));
        print_str(\" \");
        puti(st.size);
        print_str(\"\\n\");
        i = i + 1;
    }
    if (i == 0) {
        print_str(\"ls: not found or empty\\n\");
        return 1;
    }
    return 0;
}

fn builtin_cat(path: ptr<char>) -> int {
    let buf: [256]char;
    let fd: int = 0;
    let n: int = 0;
    if (strlen(path) == 0) {
        print_str(\"usage: cat <path>\\n\");
        return 1;
    }
    fd = sys_open(path);
    if (fd < 0) {
        print_str(\"cat: not found\\n\");
        return 1;
    }
    n = sys_file_read(fd, &buf, 255);
    while (n > 0) {
        print_buf(&buf, n);
        n = sys_file_read(fd, &buf, 255);
    }
    sys_close(fd);
    return 0;
}

fn builtin_sysinfo() -> int {
    let info: OreSysInfo;
    let proc: OreProcessInfo;
    let i: int = 0;
    if (sys_info(&info) < 0) { return 1; }
    print_str(\"mem total=\");
    puti(info.total_pages);
    print_str(\" free=\");
    puti(info.free_pages);
    print_str(\" cpus=\");
    puti(info.cpu_count);
    print_str(\" pid=\");
    puti(info.pid);
    print_str(\" ticks=\");
    puti(info.uptime_ticks);
    print_str(\"\\n\");
    while (sys_proc_info(i, &proc) >= 0) {
        print_str(\"  \");
        puti(proc.pid);
        print_str(\" \");
        print_buf(&proc.name, strlen(&proc.name));
        print_str(\"\\n\");
        i = i + 1;
    }
    return 0;
}

fn main() -> int {
    let line: [256]char;
    let cmd: [32]char;
    let arg: [128]char;
    print_str(\"Ore shell prototype (OreLang)\\n\");
    print_str(\"commands: help sysinfo ls [path] cat <path> exit\\n\");
    while (1) {
        clear_buf(&line, 256);
        clear_buf(&cmd, 32);
        clear_buf(&arg, 128);
        print_str(\"osh> \");
        read_line(&line, 256);
        split_line(&line, &cmd, 32, &arg, 128);
        if (strlen(&cmd) > 0) {
            if (streq(&cmd, \"help\")) {
                print_str(\"help sysinfo ls [path] cat <path> exit\\n\");
            } else {
                if (streq(&cmd, \"sysinfo\")) {
                    builtin_sysinfo();
                } else {
                    if (streq(&cmd, \"ls\")) {
                        builtin_ls(&arg);
                    } else {
                        if (streq(&cmd, \"cat\")) {
                            builtin_cat(&arg);
                        } else {
                            if (streq(&cmd, \"exit\")) {
                                return 0;
                            } else {
                                print_str(\"unknown: \");
                                print_str(&cmd);
                                print_str(\"\\n\");
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}
"""

INSTALL_ORE_APPS = {
    "bin/hello.oimg": "apps/hello.ore",
    "bin/echo.oimg": "apps/echo.ore",
    "bin/cat.oimg": "apps/cat.ore",
    "bin/stat.oimg": "apps/stat.ore",
    "bin/ls.oimg": "apps/ls.ore",
    "bin/sysinfo.oimg": "apps/sysinfo.ore",
    "bin/osh.oimg": "apps/osh.ore",
    "bin/ping.oimg": "apps/ping.ore",
    "bin/dns.oimg": "apps/dns.ore",
    "bin/http.oimg": "apps/http.ore",
    "bin/gfxtest.oimg": "apps/gfxtest.ore",
    "bin/inputtest.oimg": "apps/inputtest.ore",
    "bin/terraintest.oimg": "apps/terraintest.ore",
    "bin/skyrun.oimg": "apps/skyrun.ore",
    "bin/raycast.oimg": "apps/raycast.ore",
}

INSTALL_ORE_KMODS = {
    "kernel/bootdiag.okmod": "kernel/bootdiag.ore",
}

NEGATIVE_ORE_KMODS = {
    "kernel/bad_syscall.ore": "kernel target forbids syscall extern",
    "kernel/bad_command.ore": "command_main must be",
    "kernel/bad_deref.ore": "kernel pointer store requires",
    "kernel/bad_print.ore": "kernel target forbids print",
    "kernel/bad_user_attr.ore": "kernel target rejects #[user]",
}

NEGATIVE_ORE_APPS = {
    "examples/bad_kernel_attr.ore": "user target rejects #[kernel]",
}

SECTOR = 512
ENTRY_SIZE = 128
DATA_START_SECTOR = 8

def write_host_root(root: str, files: dict):
    os.makedirs(root, exist_ok=True)
    for name, content in files.items():
        path = os.path.join(root, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(content)

def compile_ore_app(host_compiler: str, root: str, source_name: str, out_name: str) -> bytes:
    out_dir = os.path.join(root, ".ore-build")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, out_name.replace("/", "_"))
    env = dict(os.environ)
    env["ORE_ROOT"] = root
    subprocess.run([host_compiler, "/disk/" + source_name, out_path], check=True, env=env)
    with open(out_path, "rb") as f:
        return f.read()

def compile_ore_kmod(host_compiler: str, root: str, source_name: str, out_name: str) -> bytes:
    out_dir = os.path.join(root, ".ore-build")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, out_name.replace("/", "_"))
    env = dict(os.environ)
    env["ORE_ROOT"] = root
    subprocess.run([host_compiler, "--kmod", "/disk/" + source_name, out_path], check=True, env=env)
    with open(out_path, "rb") as f:
        return f.read()

def expect_ore_kmod_failure(host_compiler: str, root: str, source_name: str, expected: str):
    out_dir = os.path.join(root, ".ore-build")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, source_name.replace("/", "_") + ".bad")
    env = dict(os.environ)
    env["ORE_ROOT"] = root
    proc = subprocess.run(
        [host_compiler, "--kmod", "/disk/" + source_name, out_path],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if proc.returncode == 0:
        raise SystemExit(f"negative kmod test unexpectedly compiled: {source_name}")
    if expected not in proc.stdout:
        print(proc.stdout, file=sys.stderr)
        raise SystemExit(f"negative kmod test did not report expected error for {source_name}: {expected}")

def expect_ore_app_failure(host_compiler: str, root: str, source_name: str, expected: str):
    out_dir = os.path.join(root, ".ore-build")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, source_name.replace("/", "_") + ".bad")
    env = dict(os.environ)
    env["ORE_ROOT"] = root
    proc = subprocess.run(
        [host_compiler, "/disk/" + source_name, out_path],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if proc.returncode == 0:
        raise SystemExit(f"negative user test unexpectedly compiled: {source_name}")
    if expected not in proc.stdout:
        print(proc.stdout, file=sys.stderr)
        raise SystemExit(f"negative user test did not report expected error for {source_name}: {expected}")

def main():
    if len(sys.argv) != 3:
        print("usage: mkorefs.py <image> <orec-host>", file=sys.stderr)
        return 2
    image = sys.argv[1]
    host_compiler = sys.argv[2]
    os.makedirs(os.path.dirname(image), exist_ok=True)

    files = dict(ROOT_FILES)
    host_root = os.path.join(os.path.dirname(image), "orefs_root")
    write_host_root(host_root, files)
    for out_name, src_name in INSTALL_ORE_APPS.items():
        files[out_name] = compile_ore_app(host_compiler, host_root, src_name, out_name)
    for out_name, src_name in INSTALL_ORE_KMODS.items():
        files[out_name] = compile_ore_kmod(host_compiler, host_root, src_name, out_name)
    for src_name, expected in NEGATIVE_ORE_KMODS.items():
        expect_ore_kmod_failure(host_compiler, host_root, src_name, expected)
    for src_name, expected in NEGATIVE_ORE_APPS.items():
        expect_ore_app_failure(host_compiler, host_root, src_name, expected)

    entry_bytes = len(files) * ENTRY_SIZE
    entry_sectors = align(entry_bytes, SECTOR) // SECTOR
    data_start_sector = max(DATA_START_SECTOR, 1 + entry_sectors)

    entries = []
    offset = data_start_sector * SECTOR
    data = bytearray()

    for name, content in files.items():
        encoded = name.encode("ascii")
        if len(encoded) >= 96:
            raise SystemExit(f"name too long: {name}")
        file_offset = offset + len(data)
        entries.append((encoded, file_offset, len(content), 1))
        data.extend(content)
        while len(data) % SECTOR:
            data.append(0)

    header = bytearray(SECTOR)
    header[:8] = b"OREFS1\0\0"
    struct.pack_into("<II", header, 8, 1, len(entries))

    entry_blob = bytearray(entry_sectors * SECTOR)
    for i, (name, file_offset, size, kind) in enumerate(entries):
        base = i * ENTRY_SIZE
        entry_blob[base:base + len(name)] = name
        struct.pack_into("<QQII", entry_blob, base + 96, file_offset, size, kind, 0)

    image_size = max(16 * 1024 * 1024, data_start_sector * SECTOR + len(data))
    with open(image, "wb") as f:
        f.write(header)
        f.write(entry_blob)
        written = len(header) + len(entry_blob)
        if written < data_start_sector * SECTOR:
            f.write(b"\0" * (data_start_sector * SECTOR - written))
        f.write(data)
        written = data_start_sector * SECTOR + len(data)
        if written < image_size:
            f.write(b"\0" * (image_size - written))
    app_count = len(INSTALL_ORE_APPS)
    print(f"Built {image} with {len(entries)} files ({app_count} OreImage apps, {len(INSTALL_ORE_KMODS)} OreKMod modules)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

