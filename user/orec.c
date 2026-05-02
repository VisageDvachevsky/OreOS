#ifdef OREC_HOST
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include "ore_abi.h"

static void puts(const char *s);
#else
#include "user.h"
#endif

#define IMAGE_MAX 65536
#define TEXT_MAX 32768
#define DATA_MAX 32768
#define MAX_VARS 64
#define MAX_FUNCS 96
#define MAX_CALLS 128
#define MAX_PARAMS 6
#define MAX_STRUCTS 16
#define MAX_FIELDS 32
#define MAX_DATA_REFS 512
#define MAX_LOOP_DEPTH 16
#define MAX_BREAKS_PER_LOOP 64
#define IMPORT_BUF_SIZE 8192

typedef enum {
    TY_VOID,
    TY_BOOL,
    TY_CHAR,
    TY_U8,
    TY_I32,
    TY_U32,
    TY_INT,
    TY_U64,
    TY_PTR,
    TY_ARRAY,
    TY_STRUCT
} TypeKind;

typedef struct {
    TypeKind kind;
    uint32_t size;
    uint32_t align;
    uint32_t base;
    uint32_t count;
    uint32_t struc;
} Type;

typedef struct {
    char name[32];
    int32_t offset;
    Type type;
} Var;

typedef struct {
    char name[32];
    Type type;
    uint32_t offset;
} Field;

typedef struct {
    char name[32];
    Field fields[MAX_FIELDS];
    uint32_t field_count;
    uint32_t size;
    uint32_t align;
    uint32_t packed;
} StructDef;

typedef struct {
    char name[32];
    const char *body;
    uint64_t addr;
    uint32_t argc;
    char params[MAX_PARAMS][32];
    Type param_types[MAX_PARAMS];
    Type ret_type;
    uint32_t is_extern;
    uint32_t is_unsafe;
    uint32_t attr_kernel;
    uint32_t attr_user;
    uint32_t attr_export;
} Function;

typedef struct {
    uint64_t patch;
    uint32_t fn;
} CallPatch;

typedef struct {
    uint64_t patch;
    uint32_t fn;
} FramePatch;

typedef struct {
    uint64_t patch;
    uint64_t data_off;
} DataRef;

typedef struct {
    uint64_t breaks[MAX_BREAKS_PER_LOOP];
    uint32_t break_count;
} LoopContext;

static uint8_t image[IMAGE_MAX];
static uint8_t text[TEXT_MAX];
static uint8_t data[DATA_MAX];
static uint64_t text_len;
static uint64_t data_len;
static char source[24576];
static char expanded_source[32768];
static Var vars[MAX_VARS];
static Function funcs[MAX_FUNCS];
static CallPatch calls[MAX_CALLS];
static FramePatch frame_patches[MAX_FUNCS];
static DataRef data_refs[MAX_DATA_REFS];
static LoopContext loop_stack[MAX_LOOP_DEPTH];
static StructDef structs[MAX_STRUCTS];
static char import_sources[9][IMPORT_BUF_SIZE];
static uint32_t var_count;
static uint32_t func_count;
static uint32_t call_count;
static uint32_t data_ref_count;
static uint32_t loop_depth;
static uint32_t struct_count;
static uint32_t current_func;
static uint32_t compile_kmod_target;
static uint32_t current_function_unsafe;
static int32_t stack_size;

static int name_eq(const char *a, const char *b);
static int parse_expr_t(const char **p, Type *out);

uint64_t strlen(const char *s) { uint64_t n = 0; while (s[n]) n++; return n; }
#ifdef OREC_HOST
static void puts(const char *s) { (void)write(1, s, strlen(s)); }
static void write_one(const char *s) { (void)write(1, s, 1); }
#else
void puts(const char *s) { sys_write(1, s, strlen(s)); }
static void write_one(const char *s) { sys_write(1, s, 1); }
#endif

static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alnum(char c) { return is_alpha(c) || is_digit(c); }

static int keyword_at(const char *s, const char *kw) {
    uint64_t i = 0;
    while (kw[i]) {
        if (s[i] != kw[i]) return 0;
        i++;
    }
    return !is_alnum(s[i]);
}

#ifndef OREC_HOST
static void putu(uint64_t v) {
    char b[24];
    uint32_t i = sizeof(b);
    b[--i] = 0;
    if (!v) { puts("0"); return; }
    while (v && i) { b[--i] = (char)('0' + (v % 10)); v /= 10; }
    puts(&b[i]);
}
#endif

static void emit8(uint8_t v) { if (text_len < TEXT_MAX) text[text_len++] = v; }
static void emit32(uint32_t v) { emit8((uint8_t)v); emit8((uint8_t)(v >> 8)); emit8((uint8_t)(v >> 16)); emit8((uint8_t)(v >> 24)); }
static void emit64(uint64_t v) { for (uint32_t i = 0; i < 8; ++i) emit8((uint8_t)(v >> (i * 8))); }

static void patch32(uint64_t at, int32_t value) {
    text[at] = (uint8_t)value;
    text[at + 1] = (uint8_t)(value >> 8);
    text[at + 2] = (uint8_t)(value >> 16);
    text[at + 3] = (uint8_t)(value >> 24);
}

static int add_data_ref(uint64_t patch, uint64_t data_off) {
    if (data_ref_count >= MAX_DATA_REFS) {
        puts("orec: too many data references\n");
        return -1;
    }
    data_refs[data_ref_count].patch = patch;
    data_refs[data_ref_count].data_off = data_off;
    data_ref_count++;
    return 0;
}

static int push_loop(void) {
    if (loop_depth >= MAX_LOOP_DEPTH) {
        puts("orec: loop nesting too deep\n");
        return -1;
    }
    loop_stack[loop_depth].break_count = 0;
    loop_depth++;
    return 0;
}

static LoopContext *current_loop(void) {
    if (!loop_depth) return 0;
    return &loop_stack[loop_depth - 1];
}

static int add_break_patch(uint64_t patch) {
    LoopContext *loop = current_loop();
    if (!loop) {
        puts("orec: break outside loop\n");
        return -1;
    }
    if (loop->break_count >= MAX_BREAKS_PER_LOOP) {
        puts("orec: too many breaks in loop\n");
        return -1;
    }
    loop->breaks[loop->break_count++] = patch;
    return 0;
}

static void patch_loop_breaks(uint64_t target) {
    LoopContext *loop = current_loop();
    if (!loop) return;
    for (uint32_t i = 0; i < loop->break_count; ++i) {
        uint64_t patch = loop->breaks[i];
        patch32(patch, (int32_t)(target - (patch + 4)));
    }
    loop_depth--;
}

static uint64_t add_data(const char *s, uint64_t len) {
    uint64_t off = data_len;
    for (uint64_t i = 0; i < len && data_len < DATA_MAX; ++i) data[data_len++] = (uint8_t)s[i];
    return off;
}

static void emit_mov_rax(uint64_t v) { emit8(0x48); emit8(0xB8); emit64(v); }
static void emit_mov_rdi(uint64_t v) { emit8(0x48); emit8(0xBF); emit64(v); }
static void emit_mov_rdx(uint64_t v) { emit8(0x48); emit8(0xBA); emit64(v); }
static void emit_syscall(void) { emit8(0x0F); emit8(0x05); }
static void emit_push_rax(void) { emit8(0x50); }
static void emit_pop_rcx(void) { emit8(0x59); }
static void emit_pop_rax(void) { emit8(0x58); }
static void emit_pop_rdi(void) { emit8(0x5F); }
static void emit_pop_rsi(void) { emit8(0x5E); }
static void emit_pop_rdx(void) { emit8(0x5A); }
static void emit_pop_r8(void) { emit8(0x41); emit8(0x58); }
static void emit_pop_r9(void) { emit8(0x41); emit8(0x59); }
static void emit_pop_r10(void) { emit8(0x41); emit8(0x5A); }

static uint64_t emit_prologue(void) {
    emit8(0x55);                         /* push rbp */
    emit8(0x48); emit8(0x89); emit8(0xE5); /* mov rbp,rsp */
    emit8(0x48); emit8(0x81); emit8(0xEC);
    uint64_t patch = text_len;
    emit32(0);                           /* patched after function compile */
    emit8(0x48); emit8(0x89); emit8(0x9D); emit32(0xFFFFFFF8U); /* mov [rbp-8],rbx */
    return patch;
}

static void emit_epilogue_ret(void) {
    emit8(0x48); emit8(0x8B); emit8(0x9D); emit32(0xFFFFFFF8U); /* mov rbx,[rbp-8] */
    emit8(0x48); emit8(0x89); emit8(0xEC); /* mov rsp,rbp */
    emit8(0x5D);                           /* pop rbp */
    emit8(0xC3);                           /* ret */
}

static void emit_load_var(int32_t off) {
    emit8(0x48); emit8(0x8B); emit8(0x85); emit32((uint32_t)-off); /* mov rax,[rbp-off] */
}

static void emit_store_var(int32_t off) {
    emit8(0x48); emit8(0x89); emit8(0x85); emit32((uint32_t)-off); /* mov [rbp-off],rax */
}

static void emit_addr_var(int32_t off) {
    emit8(0x48); emit8(0x8D); emit8(0x85); emit32((uint32_t)-off); /* lea rax,[rbp-off] */
}

static void emit_load_ptr_rax(void) {
    emit8(0x48); emit8(0x8B); emit8(0x00); /* mov rax,[rax] */
}

static void emit_load_ptr_t(Type t) {
    if (t.size == 1) {
        emit8(0x48); emit8(0x0F); emit8(0xB6); emit8(0x00); /* movzx rax,byte [rax] */
    } else if (t.size == 4) {
        emit8(0x8B); emit8(0x00); /* mov eax,dword [rax] */
    } else {
        emit_load_ptr_rax();
    }
}

static void emit_store_ptr_rcx_rax(void) {
    emit8(0x48); emit8(0x89); emit8(0x01); /* mov [rcx],rax */
}

static void emit_store_ptr_t(Type t) {
    if (t.size == 1) {
        emit8(0x88); emit8(0x01); /* mov [rcx],al */
    } else if (t.size == 4) {
        emit8(0x89); emit8(0x01); /* mov [rcx],eax */
    } else {
        emit_store_ptr_rcx_rax();
    }
}

static void emit_scale_rax_8(void) {
    emit8(0x48); emit8(0x6B); emit8(0xC0); emit8(0x08); /* imul rax,rax,8 */
}

static void emit_lea_rcx_var(int32_t off) {
    emit8(0x48); emit8(0x8D); emit8(0x8D); emit32((uint32_t)-off); /* lea rcx,[rbp-off] */
}

static void emit_add_rax_rcx(void) {
    emit8(0x48); emit8(0x01); emit8(0xC8); /* add rax,rcx */
}

static void emit_store_arg_reg(int32_t off, uint32_t arg) {
    emit8(0x48); emit8(0x89);
    if (arg == 0) emit8(0xBD);          /* rdi */
    else if (arg == 1) emit8(0xB5);     /* rsi */
    else if (arg == 2) emit8(0x95);     /* rdx */
    else if (arg == 3) emit8(0x8D);     /* rcx */
    else if (arg == 4) { emit8(0x85); } /* rax fallback, not used */
    else { emit8(0x85); }
    emit32((uint32_t)-off);
}

static void emit_load_var_t(int32_t off, Type t) {
    if (t.size == 1) {
        emit8(0x0F); emit8(0xB6); emit8(0x85); emit32((uint32_t)-off); /* movzx eax,byte [rbp-off] */
    } else if (t.size == 4) {
        emit8(0x8B); emit8(0x85); emit32((uint32_t)-off); /* mov eax,[rbp-off] */
    } else {
        emit_load_var(off);
    }
}

static void emit_store_var_t(int32_t off, Type t) {
    if (t.size == 1) {
        emit8(0x88); emit8(0x85); emit32((uint32_t)-off); /* mov [rbp-off],al */
    } else if (t.size == 4) {
        emit8(0x89); emit8(0x85); emit32((uint32_t)-off); /* mov [rbp-off],eax */
    } else {
        emit_store_var(off);
    }
}

static void emit_store_r8_arg(int32_t off) {
    emit8(0x4C); emit8(0x89); emit8(0x85); emit32((uint32_t)-off);
}

static void emit_store_r9_arg(int32_t off) {
    emit8(0x4C); emit8(0x89); emit8(0x8D); emit32((uint32_t)-off);
}

static void emit_lea_rsi_data(uint64_t data_off) {
    emit8(0x48); emit8(0x8D); emit8(0x35); /* lea rsi,[rip+disp32] */
    uint64_t patch = text_len;
    emit32(0);
    (void)add_data_ref(patch, data_off);
}

static void emit_lea_rax_data(uint64_t data_off) {
    emit8(0x48); emit8(0x8D); emit8(0x05); /* lea rax,[rip+disp32] */
    uint64_t patch = text_len;
    emit32(0);
    (void)add_data_ref(patch, data_off);
}

static void emit_write_data(uint64_t data_off, uint64_t len) {
    emit_mov_rax(ORE_SYS_WRITE);
    emit_mov_rdi(1);
    emit_lea_rsi_data(data_off);
    emit_mov_rdx(len);
    emit_syscall();
}

static void emit_exit_rax(void) {
    emit8(0x48); emit8(0x89); emit8(0xC7); /* mov rdi,rax */
    emit_mov_rax(ORE_SYS_EXIT);
    emit_syscall();
}

static void emit_return_rax(void) {
    if (current_func < func_count && name_eq(funcs[current_func].name, "main")) {
        emit_exit_rax();
    } else {
        emit_epilogue_ret();
    }
}

static void emit_exit_const(uint64_t code) {
    emit_mov_rax(ORE_SYS_EXIT);
    emit_mov_rdi(code);
    emit_syscall();
}

static void emit_puti_rax(void) {
    emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);       /* sub rsp,32 */
    emit8(0x48); emit8(0x8D); emit8(0x74); emit8(0x24); emit8(0x1F); /* lea rsi,[rsp+31] */
    emit8(0x48); emit8(0xBB); emit64(10);                     /* mov rbx,10 */
    emit8(0x48); emit8(0x83); emit8(0xF8); emit8(0x00);       /* cmp rax,0 */
    emit8(0x0F); emit8(0x85); uint64_t jne = text_len; emit32(0);
    emit8(0xC6); emit8(0x44); emit8(0x24); emit8(0x1E); emit8('0'); /* mov byte [rsp+30],'0' */
    emit8(0x48); emit8(0x8D); emit8(0x74); emit8(0x24); emit8(0x1E); /* lea rsi,[rsp+30] */
    emit_mov_rdx(1);
    emit8(0xE9); uint64_t jwrite = text_len; emit32(0);
    uint64_t loop = text_len;
    patch32(jne, (int32_t)(loop - (jne + 4)));
    emit8(0x48); emit8(0x31); emit8(0xD2);                   /* xor rdx,rdx */
    emit8(0x48); emit8(0xF7); emit8(0xF3);                   /* div rbx */
    emit8(0x80); emit8(0xC2); emit8('0');                    /* add dl,'0' */
    emit8(0x48); emit8(0xFF); emit8(0xCE);                   /* dec rsi */
    emit8(0x88); emit8(0x16);                                /* mov [rsi],dl */
    emit8(0x48); emit8(0x83); emit8(0xF8); emit8(0x00);       /* cmp rax,0 */
    emit8(0x0F); emit8(0x85); emit32((uint32_t)(int32_t)(loop - (text_len + 4)));
    emit8(0x48); emit8(0x8D); emit8(0x54); emit8(0x24); emit8(0x1F); /* lea rdx,[rsp+31] */
    emit8(0x48); emit8(0x29); emit8(0xF2);                   /* sub rdx,rsi */
    uint64_t write = text_len;
    patch32(jwrite, (int32_t)(write - (jwrite + 4)));
    emit_mov_rax(ORE_SYS_WRITE);
    emit_mov_rdi(1);
    emit_syscall();
    emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);       /* add rsp,32 */
}

static const char *skip_ws(const char *p) {
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }
        return p;
    }
}

static int parse_ident(const char **p, char out[32]) {
    const char *s = skip_ws(*p);
    if (!is_alpha(*s)) return -1;
    uint32_t n = 0;
    while (is_alnum(*s)) {
        if (n + 1 < 32) out[n++] = *s;
        s++;
    }
    out[n] = 0;
    *p = s;
    return 0;
}

static int name_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static Type ty_int(void) { Type t = { TY_INT, 8, 8, 0, 0, 0 }; return t; }
static Type ty_char(void) { Type t = { TY_CHAR, 1, 1, 0, 0, 0 }; return t; }
static Type ty_void(void) { Type t = { TY_VOID, 0, 1, 0, 0, 0 }; return t; }
static Type ty_bool(void) { Type t = { TY_BOOL, 1, 1, 0, 0, 0 }; return t; }
static Type ty_ptr(Type base) {
    Type t = { TY_PTR, 8, 8, 0, 0, 0 };
    if (base.kind == TY_CHAR || base.kind == TY_U8) t.base = 1;
    else if (base.kind == TY_I32 || base.kind == TY_U32) t.base = 4;
    else if (base.kind == TY_VOID) t.base = 1;
    else t.base = base.size ? base.size : 8;
    return t;
}

static Type ty_array_element(Type array) {
    Type elem = ty_int();
    elem.size = array.base ? array.base : 1;
    elem.align = elem.size < 8 ? elem.size : 8;
    if (elem.size == 1) elem.kind = TY_U8;
    else if (elem.size == 4) elem.kind = TY_U32;
    return elem;
}

static uint32_t align_up_u32(uint32_t v, uint32_t a) {
    if (!a) return v;
    return (v + a - 1) / a * a;
}

static int type_is_ptr(Type t) { return t.kind == TY_PTR || t.kind == TY_ARRAY; }
static int type_is_int_like(Type t) {
    return t.kind == TY_INT || t.kind == TY_U64 || t.kind == TY_I32 || t.kind == TY_U32 ||
           t.kind == TY_CHAR || t.kind == TY_U8 || t.kind == TY_BOOL;
}
static uint32_t type_pointee_size(Type t) {
    if (t.kind == TY_ARRAY) return t.base ? t.base : 1;
    if (t.kind == TY_PTR) return t.base ? t.base : 1;
    return 1;
}

static StructDef *find_struct(const char *name, uint32_t *index) {
    for (uint32_t i = 0; i < struct_count; ++i) {
        if (name_eq(structs[i].name, name)) {
            if (index) *index = i;
            return &structs[i];
        }
    }
    return 0;
}

static Field *find_field(StructDef *st, const char *name) {
    if (!st) return 0;
    for (uint32_t i = 0; i < st->field_count; ++i) {
        if (name_eq(st->fields[i].name, name)) return &st->fields[i];
    }
    return 0;
}

static Var *find_var(const char *name) {
    for (uint32_t i = 0; i < var_count; ++i) if (name_eq(vars[i].name, name)) return &vars[i];
    return 0;
}

static Var *add_var_type(const char *name, Type type) {
    if (var_count >= MAX_VARS) return 0;
    Var *v = &vars[var_count++];
    uint32_t i = 0;
    for (; i + 1 < sizeof(v->name) && name[i]; ++i) v->name[i] = name[i];
    v->name[i] = 0;
    uint32_t bytes = type.size ? type.size : 8;
    bytes = align_up_u32(bytes, 8);
    stack_size += (int32_t)bytes;
    v->offset = stack_size;
    v->type = type;
    return v;
}

static Function *find_func(const char *name, uint32_t *index) {
    for (uint32_t i = 0; i < func_count; ++i) {
        if (name_eq(funcs[i].name, name)) {
            if (index) *index = i;
            return &funcs[i];
        }
    }
    return 0;
}

static void copy_name(char dst[32], const char *src) {
    uint32_t i = 0;
    for (; i + 1 < 32 && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static int consume(const char **p, char c) {
    const char *s = skip_ws(*p);
    if (*s != c) return -1;
    *p = s + 1;
    return 0;
}

static uint64_t parse_uint(const char **p) {
    uint64_t v = 0;
    while (is_digit(**p)) { v = v * 10 + (uint64_t)(**p - '0'); (*p)++; }
    return v;
}

static int parse_expr(const char **p) {
    Type ignored;
    return parse_expr_t(p, &ignored);
}

static int parse_type(const char **p, Type *out) {
    const char *s = skip_ws(*p);
    if (*s == '[') {
        s++;
        uint32_t count = (uint32_t)parse_uint(&s);
        if (consume(&s, ']') < 0) { puts("orec: expected ] in array type\n"); return -1; }
        Type elem;
        if (parse_type(&s, &elem) < 0) return -1;
        *out = elem;
        out->kind = TY_ARRAY;
        out->count = count;
        out->base = elem.size ? elem.size : 1;
        out->size = out->base * count;
        out->align = elem.align ? elem.align : 1;
        *p = s;
        return 0;
    }
    char name[32];
    if (parse_ident(&s, name) < 0) return -1;
    if (name_eq(name, "void")) *out = ty_void();
    else if (name_eq(name, "bool")) { Type t = { TY_BOOL, 1, 1, 0, 0, 0 }; *out = t; }
    else if (name_eq(name, "char")) *out = ty_char();
    else if (name_eq(name, "u8")) { Type t = { TY_U8, 1, 1, 0, 0, 0 }; *out = t; }
    else if (name_eq(name, "i32")) { Type t = { TY_I32, 4, 4, 0, 0, 0 }; *out = t; }
    else if (name_eq(name, "u32")) { Type t = { TY_U32, 4, 4, 0, 0, 0 }; *out = t; }
    else if (name_eq(name, "int")) *out = ty_int();
    else if (name_eq(name, "u64")) { Type t = { TY_U64, 8, 8, 0, 0, 0 }; *out = t; }
    else if (name_eq(name, "ptr")) {
        if (consume(&s, '<') < 0) { puts("orec: expected < in ptr<T>\n"); return -1; }
        Type base;
        if (parse_type(&s, &base) < 0) return -1;
        if (consume(&s, '>') < 0) { puts("orec: expected > in ptr<T>\n"); return -1; }
        *out = ty_ptr(base);
    } else {
        uint32_t idx = 0;
        StructDef *st = find_struct(name, &idx);
        if (!st) { puts("orec: unknown type "); puts(name); puts("\n"); return -1; }
        Type t = { TY_STRUCT, st->size, st->align, 0, 0, idx };
        *out = t;
    }
    s = skip_ws(s);
    if (*s == '[') {
        s++;
        uint32_t count = (uint32_t)parse_uint(&s);
        if (consume(&s, ']') < 0) { puts("orec: expected ] in array type\n"); return -1; }
        Type elem = *out;
        out->kind = TY_ARRAY;
        out->count = count;
        out->base = elem.size ? elem.size : 1;
        out->size = out->base * count;
        out->align = elem.align ? elem.align : 1;
    }
    *p = s;
    return 0;
}

static int emit_index_address(Var *v, const char **p) {
    const char *s = skip_ws(*p);
    if (*s != '[') return -1;
    s++;
    if (parse_expr(&s) < 0) return -1;
    if (consume(&s, ']') < 0) { puts("orec: expected ]\n"); return -1; }
    uint32_t scale = v->type.kind == TY_ARRAY ? type_pointee_size(v->type) : 8;
    if (scale == 8) emit_scale_rax_8();
    else if (scale != 1) { emit8(0x48); emit8(0x6B); emit8(0xC0); emit8((uint8_t)scale); }
    emit_lea_rcx_var(v->offset);
    emit_add_rax_rcx();
    *p = s;
    return 0;
}

static int parse_primary_t(const char **p, Type *out) {
    const char *s = skip_ws(*p);
    if (*s == '"') {
        s++;
        char str[512];
        uint64_t len = 0;
        while (*s && *s != '"' && len + 1 < sizeof(str)) {
            if (*s == '\\' && s[1] == 'n') { str[len++] = '\n'; s += 2; }
            else if (*s == '\\' && s[1] == 'r') { str[len++] = '\r'; s += 2; }
            else if (*s == '\\' && s[1] == 't') { str[len++] = '\t'; s += 2; }
            else str[len++] = *s++;
        }
        if (*s != '"') { puts("orec: unterminated string\n"); return -1; }
        s++;
        str[len++] = 0;
        emit_lea_rax_data(add_data(str, len));
        *out = ty_ptr(ty_char());
        *p = s;
        return 0;
    }
    if (is_digit(*s)) {
        uint64_t v = parse_uint(&s);
        emit_mov_rax(v);
        *out = ty_int();
        *p = s;
        return 0;
    }
    if (*s == '(') {
        s++;
        if (parse_expr_t(&s, out) < 0) return -1;
        if (consume(&s, ')') < 0) { puts("orec: expected )\n"); return -1; }
        *p = s;
        return 0;
    }
    char name[32];
    if (parse_ident(&s, name) == 0) {
        const char *after_name = skip_ws(s);
        if (name_eq(name, "sizeof")) {
            s = after_name;
            if (consume(&s, '(') < 0) { puts("orec: expected (\n"); return -1; }
            Type t;
            if (parse_type(&s, &t) < 0) return -1;
            if (consume(&s, ')') < 0) { puts("orec: expected )\n"); return -1; }
            emit_mov_rax(t.size);
            *out = ty_int();
            *p = s;
            return 0;
        }
        if (name_eq(name, "alignof")) {
            s = after_name;
            if (consume(&s, '(') < 0) { puts("orec: expected (\n"); return -1; }
            Type t;
            if (parse_type(&s, &t) < 0) return -1;
            if (consume(&s, ')') < 0) { puts("orec: expected )\n"); return -1; }
            emit_mov_rax(t.align);
            *out = ty_int();
            *p = s;
            return 0;
        }
        if (name_eq(name, "offsetof")) {
            s = after_name;
            if (consume(&s, '(') < 0) { puts("orec: expected (\n"); return -1; }
            Type t;
            if (parse_type(&s, &t) < 0 || t.kind != TY_STRUCT) { puts("orec: offsetof expects struct type\n"); return -1; }
            if (consume(&s, ',') < 0) { puts("orec: expected ,\n"); return -1; }
            char field_name[32];
            if (parse_ident(&s, field_name) < 0) return -1;
            if (consume(&s, ')') < 0) { puts("orec: expected )\n"); return -1; }
            Field *f = find_field(&structs[t.struc], field_name);
            if (!f) { puts("orec: unknown field "); puts(field_name); puts("\n"); return -1; }
            emit_mov_rax(f->offset);
            *out = ty_int();
            *p = s;
            return 0;
        }
        if (*after_name == '(') {
            uint32_t fn_index = 0;
            Function *fn = find_func(name, &fn_index);
            if (!fn) { puts("orec: unknown function "); puts(name); puts("\n"); return -1; }
            s = after_name + 1;
            uint32_t argc = 0;
            s = skip_ws(s);
            if (*s != ')') {
                for (;;) {
                    if (argc >= MAX_PARAMS) { puts("orec: too many args\n"); return -1; }
                    if (parse_expr(&s) < 0) return -1;
                    emit_push_rax();
                    argc++;
                    s = skip_ws(s);
                    if (*s == ')') break;
                    if (*s != ',') { puts("orec: expected ,\n"); return -1; }
                    s++;
                }
            }
            if (*s != ')') { puts("orec: expected )\n"); return -1; }
            s++;
            if (argc != fn->argc) { puts("orec: bad arg count for "); puts(name); puts("\n"); return -1; }
            if (compile_kmod_target && fn->is_extern) {
                puts("orec: kernel target forbids unresolved/user extern ");
                puts(name);
                puts("\n");
                return -1;
            }
            if (fn->is_extern && (name_eq(name, "write") || name_eq(name, "sys_write"))) {
                if (argc != 3) { puts("orec: write expects 3 args\n"); return -1; }
                emit_pop_rdx();
                emit_pop_rsi();
                emit_pop_rdi();
                emit_mov_rax(ORE_SYS_WRITE);
                emit_syscall();
                *out = ty_int();
                *p = s;
                return 0;
            }
            if (fn->is_extern && name_eq(name, "syscall3")) {
                if (argc != 4) { puts("orec: syscall3 expects 4 args\n"); return -1; }
                emit_pop_rdx();
                emit_pop_rsi();
                emit_pop_rdi();
                emit_pop_rax();
                emit_syscall();
                *out = ty_int();
                *p = s;
                return 0;
            }
            if (fn->is_extern && name_eq(name, "syscall4")) {
                if (argc != 5) { puts("orec: syscall4 expects 5 args\n"); return -1; }
                emit_pop_r10();
                emit_pop_rdx();
                emit_pop_rsi();
                emit_pop_rdi();
                emit_pop_rax();
                emit_syscall();
                *out = ty_int();
                *p = s;
                return 0;
            }
            if (fn->is_extern && name_eq(name, "syscall0")) {
                if (argc != 1) { puts("orec: syscall0 expects 1 arg\n"); return -1; }
                emit_pop_rax();
                emit_syscall();
                *out = ty_int();
                *p = s;
                return 0;
            }
            if (fn->is_extern && name_eq(name, "syscall1")) {
                if (argc != 2) { puts("orec: syscall1 expects 2 args\n"); return -1; }
                emit_pop_rdi();
                emit_pop_rax();
                emit_syscall();
                *out = ty_int();
                *p = s;
                return 0;
            }
            if (fn->is_extern && name_eq(name, "syscall2")) {
                if (argc != 3) { puts("orec: syscall2 expects 3 args\n"); return -1; }
                emit_pop_rsi();
                emit_pop_rdi();
                emit_pop_rax();
                emit_syscall();
                *out = ty_int();
                *p = s;
                return 0;
            }
            if (fn->is_extern) { puts("orec: unresolved extern "); puts(name); puts("\n"); return -1; }
            for (uint32_t i = argc; i > 0; --i) {
                uint32_t a = i - 1;
                if (a == 0) emit_pop_rdi();
                else if (a == 1) emit_pop_rsi();
                else if (a == 2) emit_pop_rdx();
                else if (a == 3) emit_pop_rcx();
                else if (a == 4) emit_pop_r8();
                else if (a == 5) emit_pop_r9();
            }
            emit8(0xE8);
            uint64_t patch = text_len;
            emit32(0);
            if (call_count < MAX_CALLS) {
                calls[call_count].patch = patch;
                calls[call_count].fn = fn_index;
                call_count++;
            }
            *out = fn->ret_type;
            *p = s;
            return 0;
        }
        Var *v = find_var(name);
        if (!v) { puts("orec: unknown variable "); puts(name); puts("\n"); return -1; }
        after_name = skip_ws(s);
        if (*after_name == '[') {
            s = after_name;
            if (emit_index_address(v, &s) < 0) return -1;
            Type elem = v->type;
            elem.kind = TY_INT;
            elem.size = type_pointee_size(v->type);
            elem.align = elem.size < 8 ? elem.size : 8;
            emit_load_ptr_t(elem);
            *out = elem;
            *p = s;
            return 0;
        }
        if (*after_name == '.') {
            s = after_name + 1;
            char field_name[32];
            if (parse_ident(&s, field_name) < 0) return -1;
            if (v->type.kind != TY_STRUCT) { puts("orec: field access on non-struct\n"); return -1; }
            Field *f = find_field(&structs[v->type.struc], field_name);
            if (!f) { puts("orec: unknown field "); puts(field_name); puts("\n"); return -1; }
            emit_addr_var(v->offset);
            if (f->offset) { emit8(0x48); emit8(0x05); emit32(f->offset); }
            emit_load_ptr_t(f->type);
            *out = f->type;
            *p = s;
            return 0;
        }
        emit_load_var_t(v->offset, v->type);
        *out = v->type;
        *p = s;
        return 0;
    }
    puts("orec: expected expression\n");
    return -1;
}

static int parse_unary_t(const char **p, Type *out) {
    const char *s = skip_ws(*p);
    if (*s == '!') {
        s++;
        if (parse_unary_t(&s, out) < 0) return -1;
        emit8(0x48); emit8(0x83); emit8(0xF8); emit8(0x00); /* cmp rax,0 */
        emit8(0x0F); emit8(0x94); emit8(0xC0);             /* sete al */
        emit8(0x48); emit8(0x0F); emit8(0xB6); emit8(0xC0);
        *out = ty_bool();
        *p = s;
        return 0;
    }
    if (*s == '~') {
        s++;
        if (parse_unary_t(&s, out) < 0) return -1;
        emit8(0x48); emit8(0xF7); emit8(0xD0); /* not rax */
        *p = s;
        return 0;
    }
    if (*s == '-') {
        s++;
        if (parse_unary_t(&s, out) < 0) return -1;
        emit8(0x48); emit8(0xF7); emit8(0xD8); /* neg rax */
        *out = ty_int();
        *p = s;
        return 0;
    }
    if (*s == '&') {
        s++;
        char name[32];
        if (parse_ident(&s, name) < 0) { puts("orec: & expects variable\n"); return -1; }
        Var *v = find_var(name);
        if (!v) { puts("orec: unknown variable "); puts(name); puts("\n"); return -1; }
        const char *after_name = skip_ws(s);
        Type addr_type = v->type;
        if (*after_name == '[') {
            s = after_name;
            if (emit_index_address(v, &s) < 0) return -1;
            addr_type = ty_array_element(v->type);
        } else {
            if (*after_name == '.') {
                s = after_name + 1;
                char field_name[32];
                if (parse_ident(&s, field_name) < 0) return -1;
                if (v->type.kind != TY_STRUCT) { puts("orec: &field on non-struct\n"); return -1; }
                Field *f = find_field(&structs[v->type.struc], field_name);
                if (!f) { puts("orec: unknown field "); puts(field_name); puts("\n"); return -1; }
                emit_addr_var(v->offset);
                if (f->offset) { emit8(0x48); emit8(0x05); emit32(f->offset); }
                addr_type = f->type;
            } else {
                emit_addr_var(v->offset);
            }
        }
        if (addr_type.kind == TY_ARRAY) *out = ty_ptr(ty_array_element(addr_type));
        else *out = ty_ptr(addr_type);
        *p = s;
        return 0;
    }
    if (*s == '*') {
        s++;
        if (compile_kmod_target && !current_function_unsafe) {
            puts("orec: kernel pointer deref requires #[unsafe] or unsafe block\n");
            return -1;
        }
        Type ptr_type;
        if (parse_unary_t(&s, &ptr_type) < 0) return -1;
        Type val = ty_int();
        if (ptr_type.kind == TY_PTR) {
            val.size = ptr_type.base ? ptr_type.base : 8;
            val.align = val.size < 8 ? val.size : 8;
            if (val.size == 1) val.kind = TY_U8;
            else if (val.size == 4) val.kind = TY_U32;
        }
        emit_load_ptr_t(val);
        *out = val;
        *p = s;
        return 0;
    }
    return parse_primary_t(p, out);
}

static int parse_mul_t(const char **p, Type *out) {
    if (parse_unary_t(p, out) < 0) return -1;
    for (;;) {
        const char *s = skip_ws(*p);
        char op = *s;
        if (op != '*' && op != '/' && op != '%') break;
        s++;
        emit_push_rax();
        Type rhs;
        if (parse_unary_t(&s, &rhs) < 0) return -1;
        emit_pop_rcx();
        if (op == '*') {
            emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xC1); /* imul rax,rcx */
        } else {
            emit8(0x48); emit8(0x89); emit8(0xC3);             /* mov rbx,rax */
            emit8(0x48); emit8(0x89); emit8(0xC8);             /* mov rax,rcx */
            emit8(0x48); emit8(0x99);                           /* cqo */
            emit8(0x48); emit8(0xF7); emit8(0xFB);             /* idiv rbx */
            if (op == '%') {
                emit8(0x48); emit8(0x89); emit8(0xD0);         /* mov rax,rdx */
            }
        }
        *out = ty_int();
        *p = s;
    }
    return 0;
}

static int parse_add_t(const char **p, Type *out) {
    if (parse_mul_t(p, out) < 0) return -1;
    for (;;) {
        const char *s = skip_ws(*p);
        char op = *s;
        if (op != '+' && op != '-') break;
        s++;
        Type lhs = *out;
        emit_push_rax();
        Type rhs;
        if (parse_mul_t(&s, &rhs) < 0) return -1;
        if (type_is_ptr(lhs) && !type_is_ptr(rhs)) {
            uint32_t scale = type_pointee_size(lhs);
            if (scale == 8) emit_scale_rax_8();
            else if (scale != 1) { emit8(0x48); emit8(0x6B); emit8(0xC0); emit8((uint8_t)scale); }
        }
        emit_pop_rcx();
        if (op == '+') {
            emit8(0x48); emit8(0x01); emit8(0xC8); /* add rax,rcx */
        } else {
            emit8(0x48); emit8(0x29); emit8(0xC1); /* sub rcx,rax */
            emit8(0x48); emit8(0x89); emit8(0xC8); /* mov rax,rcx */
        }
        *out = lhs;
        *p = s;
    }
    return 0;
}

static int parse_shift_t(const char **p, Type *out) {
    if (parse_add_t(p, out) < 0) return -1;
    for (;;) {
        const char *s = skip_ws(*p);
        int right = 0;
        if (s[0] == '<' && s[1] == '<') right = 0;
        else if (s[0] == '>' && s[1] == '>') right = 1;
        else break;
        s += 2;
        emit_push_rax();
        Type rhs;
        if (parse_add_t(&s, &rhs) < 0) return -1;
        emit8(0x48); emit8(0x89); emit8(0xC1); /* mov rcx,rax */
        emit8(0x58);                           /* pop rax */
        if (right) { emit8(0x48); emit8(0xD3); emit8(0xF8); } /* sar rax,cl */
        else { emit8(0x48); emit8(0xD3); emit8(0xE0); }      /* shl rax,cl */
        *out = ty_int();
        *p = s;
    }
    return 0;
}

static int parse_bitand_t(const char **p, Type *out) {
    if (parse_shift_t(p, out) < 0) return -1;
    for (;;) {
        const char *s = skip_ws(*p);
        if (*s != '&' || s[1] == '&') break;
        s++;
        emit_push_rax();
        Type rhs;
        if (parse_shift_t(&s, &rhs) < 0) return -1;
        emit_pop_rcx();
        emit8(0x48); emit8(0x21); emit8(0xC8); /* and rax,rcx */
        *out = ty_int();
        *p = s;
    }
    return 0;
}

static int parse_bitor_t(const char **p, Type *out) {
    if (parse_bitand_t(p, out) < 0) return -1;
    for (;;) {
        const char *s = skip_ws(*p);
        if (*s != '|') break;
        s++;
        emit_push_rax();
        Type rhs;
        if (parse_bitand_t(&s, &rhs) < 0) return -1;
        emit_pop_rcx();
        emit8(0x48); emit8(0x09); emit8(0xC8); /* or rax,rcx */
        *out = ty_int();
        *p = s;
    }
    return 0;
}

static void emit_cmp_set(const char *op) {
    emit_pop_rcx();
    emit8(0x48); emit8(0x39); emit8(0xC1); /* cmp rcx,rax */
    emit8(0x0F);
    if (op[0] == '=' && op[1] == '=') emit8(0x94);
    else if (op[0] == '!' && op[1] == '=') emit8(0x95);
    else if (op[0] == '<' && op[1] == '=') emit8(0x9E);
    else if (op[0] == '>' && op[1] == '=') emit8(0x9D);
    else if (op[0] == '<') emit8(0x9C);
    else emit8(0x9F);
    emit8(0xC0);                         /* setcc al */
    emit8(0x48); emit8(0x0F); emit8(0xB6); emit8(0xC0); /* movzx rax,al */
}

static int parse_expr_t(const char **p, Type *out) {
    if (parse_bitor_t(p, out) < 0) return -1;
    const char *s = skip_ws(*p);
    char op[3] = {0,0,0};
    if ((s[0] == '=' && s[1] == '=') || (s[0] == '!' && s[1] == '=') ||
        (s[0] == '<' && s[1] == '=') || (s[0] == '>' && s[1] == '=')) {
        op[0] = s[0]; op[1] = s[1]; s += 2;
    } else if (s[0] == '<' || s[0] == '>') {
        op[0] = s[0]; s++;
    } else {
        *p = s;
        return 0;
    }
    emit_push_rax();
    Type rhs;
    if (parse_bitor_t(&s, &rhs) < 0) return -1;
    emit_cmp_set(op);
    *out = ty_bool();
    *p = s;
    return 0;
}

static int compile_block(const char **p);

static int compile_statement(const char **p) {
    const char *s = skip_ws(*p);
    if (!*s || *s == '}') { *p = s; return 0; }
    if (*s == '*') {
        s++;
        if (compile_kmod_target && !current_function_unsafe) {
            puts("orec: kernel pointer store requires #[unsafe] or unsafe block\n");
            return -1;
        }
        Type ptr_type;
        if (parse_expr_t(&s, &ptr_type) < 0) return -1;
        emit_push_rax();
        if (consume(&s, '=') < 0) { puts("orec: expected =\n"); return -1; }
        if (parse_expr(&s) < 0) return -1;
        emit_pop_rcx();
        Type val = ty_int();
        if (ptr_type.kind == TY_PTR) {
            val.size = ptr_type.base ? ptr_type.base : 8;
            val.align = val.size < 8 ? val.size : 8;
            if (val.size == 1) val.kind = TY_U8;
            else if (val.size == 4) val.kind = TY_U32;
        }
        emit_store_ptr_t(val);
        if (consume(&s, ';') < 0) { puts("orec: expected ;\n"); return -1; }
        *p = s;
        return 1;
    }
    if (keyword_at(s, "unsafe")) {
        s += 6;
        if (consume(&s, '{') < 0) { puts("orec: expected unsafe block\n"); return -1; }
        uint32_t old_unsafe = current_function_unsafe;
        current_function_unsafe = 1;
        if (compile_block(&s) < 0) return -1;
        current_function_unsafe = old_unsafe;
        *p = s;
        return 1;
    }
    if (keyword_at(s, "let")) {
        s += 3;
        char name[32];
        if (parse_ident(&s, name) < 0) { puts("orec: expected variable name\n"); return -1; }
        Type vtype = ty_int();
        if (consume(&s, ':') == 0) {
            if (parse_type(&s, &vtype) < 0) return -1;
        }
        Var *v = find_var(name);
        if (!v) v = add_var_type(name, vtype);
        if (!v) return -1;
        s = skip_ws(s);
        if (*s == '=') {
            s++;
            if (parse_expr(&s) < 0) return -1;
            emit_store_var_t(v->offset, v->type);
        } else {
            emit_mov_rax(0);
            uint32_t slots = align_up_u32(vtype.size ? vtype.size : 8, 8) / 8;
            for (uint32_t i = 0; i < slots; ++i) emit_store_var(v->offset - (int32_t)(i * 8));
        }
        if (consume(&s, ';') < 0) { puts("orec: expected ;\n"); return -1; }
        *p = s;
        return 1;
    }
    if (keyword_at(s, "while")) {
        s += 5;
        if (consume(&s, '(') < 0) { puts("orec: expected (\n"); return -1; }
        uint64_t loop = text_len;
        if (parse_expr(&s) < 0) return -1;
        if (consume(&s, ')') < 0) { puts("orec: expected )\n"); return -1; }
        emit8(0x48); emit8(0x83); emit8(0xF8); emit8(0x00); /* cmp rax,0 */
        emit8(0x0F); emit8(0x84); uint64_t je = text_len; emit32(0);
        if (consume(&s, '{') < 0) { puts("orec: expected {\n"); return -1; }
        if (push_loop() < 0) return -1;
        if (compile_block(&s) < 0) return -1;
        emit8(0xE9); emit32((uint32_t)(int32_t)(loop - (text_len + 4)));
        patch32(je, (int32_t)(text_len - (je + 4)));
        patch_loop_breaks(text_len);
        *p = s;
        return 1;
    }
    if (keyword_at(s, "break")) {
        s += 5;
        if (consume(&s, ';') < 0) { puts("orec: expected ; after break\n"); return -1; }
        emit8(0xE9);
        uint64_t jend = text_len;
        emit32(0);
        if (add_break_patch(jend) < 0) return -1;
        *p = s;
        return 1;
    }
    if (keyword_at(s, "if")) {
        s += 2;
        if (consume(&s, '(') < 0) { puts("orec: expected (\n"); return -1; }
        if (parse_expr(&s) < 0) return -1;
        if (consume(&s, ')') < 0) { puts("orec: expected )\n"); return -1; }
        emit8(0x48); emit8(0x83); emit8(0xF8); emit8(0x00); /* cmp rax,0 */
        emit8(0x0F); emit8(0x84); uint64_t je = text_len; emit32(0);
        if (consume(&s, '{') < 0) { puts("orec: expected {\n"); return -1; }
        if (compile_block(&s) < 0) return -1;
        s = skip_ws(s);
        if (keyword_at(s, "else")) {
            emit8(0xE9); uint64_t jend = text_len; emit32(0);
            patch32(je, (int32_t)(text_len - (je + 4)));
            s += 4;
            if (consume(&s, '{') < 0) { puts("orec: expected {\n"); return -1; }
            if (compile_block(&s) < 0) return -1;
            patch32(jend, (int32_t)(text_len - (jend + 4)));
        } else {
            patch32(je, (int32_t)(text_len - (je + 4)));
        }
        *p = s;
        return 1;
    }
    if (*s == '{') {
        s++;
        if (compile_block(&s) < 0) return -1;
        *p = s;
        return 1;
    }
    if (keyword_at(s, "return")) {
        s += 6;
        if (parse_expr(&s) < 0) return -1;
        emit_return_rax();
        if (consume(&s, ';') < 0) { puts("orec: expected ;\n"); return -1; }
        *p = s;
        return 1;
    }
    if (keyword_at(s, "print")) {
        if (compile_kmod_target) {
            puts("orec: kernel target forbids print/sys_write\n");
            return -1;
        }
        s += 5;
        if (consume(&s, '(') < 0) { puts("orec: expected (\n"); return -1; }
        s = skip_ws(s);
        if (*s != '"') { puts("orec: print expects string literal\n"); return -1; }
        s++;
        char str[512];
        uint64_t len = 0;
        while (*s && *s != '"' && len < sizeof(str)) {
            if (*s == '\\' && s[1] == 'n') { str[len++] = '\n'; s += 2; }
            else if (*s == '\\' && s[1] == 'r') { str[len++] = '\r'; s += 2; }
            else if (*s == '\\' && s[1] == 't') { str[len++] = '\t'; s += 2; }
            else str[len++] = *s++;
        }
        if (*s != '"') { puts("orec: unterminated string\n"); return -1; }
        s++;
        if (consume(&s, ')') < 0 || consume(&s, ';') < 0) { puts("orec: expected );\n"); return -1; }
        emit_write_data(add_data(str, len), len);
        *p = s;
        return 1;
    }
    if (keyword_at(s, "puti")) {
        if (compile_kmod_target) {
            puts("orec: kernel target forbids puti/sys_write\n");
            return -1;
        }
        s += 4;
        if (consume(&s, '(') < 0) { puts("orec: expected (\n"); return -1; }
        if (parse_expr(&s) < 0) return -1;
        if (consume(&s, ')') < 0 || consume(&s, ';') < 0) { puts("orec: expected );\n"); return -1; }
        emit_puti_rax();
        *p = s;
        return 1;
    }
    if (is_alpha(*s)) {
        const char *expr_start = s;
        char maybe_call[32];
        if (parse_ident(&s, maybe_call) == 0) {
            const char *after_name = skip_ws(s);
            if (*after_name == '(') {
                s = expr_start;
                Type ignored;
                if (parse_expr_t(&s, &ignored) < 0) return -1;
                if (consume(&s, ';') < 0) { puts("orec: expected ;\n"); return -1; }
                *p = s;
                return 1;
            }
        }
        s = expr_start;
    }
    if (is_alpha(*s)) {
        char name[32];
        if (parse_ident(&s, name) < 0) return -1;
        Var *v = find_var(name);
        if (!v) { puts("orec: unknown variable "); puts(name); puts("\n"); return -1; }
        s = skip_ws(s);
        if (*s == '[') {
            if (emit_index_address(v, &s) < 0) return -1;
            emit_push_rax();
            if (consume(&s, '=') < 0) { puts("orec: expected assignment\n"); return -1; }
            if (parse_expr(&s) < 0) return -1;
            emit_pop_rcx();
            Type elem = v->type;
            elem.size = type_pointee_size(v->type);
            elem.align = elem.size < 8 ? elem.size : 8;
            emit_store_ptr_t(elem);
            if (consume(&s, ';') < 0) { puts("orec: expected ;\n"); return -1; }
            *p = s;
            return 1;
        }
        if (*s == '.') {
            s++;
            char field_name[32];
            if (parse_ident(&s, field_name) < 0) return -1;
            if (v->type.kind != TY_STRUCT) { puts("orec: assignment field on non-struct\n"); return -1; }
            Field *f = find_field(&structs[v->type.struc], field_name);
            if (!f) { puts("orec: unknown field "); puts(field_name); puts("\n"); return -1; }
            emit_addr_var(v->offset);
            if (f->offset) { emit8(0x48); emit8(0x05); emit32(f->offset); }
            emit_push_rax();
            if (consume(&s, '=') < 0) { puts("orec: expected assignment\n"); return -1; }
            if (parse_expr(&s) < 0) return -1;
            emit_pop_rcx();
            emit_store_ptr_t(f->type);
            if (consume(&s, ';') < 0) { puts("orec: expected ;\n"); return -1; }
            *p = s;
            return 1;
        }
        if (consume(&s, '=') < 0) { puts("orec: expected assignment\n"); return -1; }
        if (parse_expr(&s) < 0) return -1;
        emit_store_var_t(v->offset, v->type);
        if (consume(&s, ';') < 0) { puts("orec: expected ;\n"); return -1; }
        *p = s;
        return 1;
    }
    puts("orec: unsupported syntax near: ");
    for (uint32_t i = 0; s[i] && i < 24; ++i) write_one(&s[i]);
    puts("\n");
    return -1;
}

static int compile_block(const char **p) {
    const char *s = *p;
    while (*s) {
        s = skip_ws(s);
        if (*s == '}') { *p = s + 1; return 0; }
        if (compile_statement(&s) < 0) return -1;
    }
    puts("orec: unterminated block\n");
    return -1;
}

static const char *find_matching_brace(const char *p) {
    uint32_t depth = 1;
    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p += 2;
                else p++;
            }
        }
        if (*p == '{') depth++;
        else if (*p == '}') {
            depth--;
            if (!depth) return p;
        }
        if (*p) p++;
    }
    return 0;
}

static int scan_structs(const char *p) {
    struct_count = 0;
    while (*p) {
        p = skip_ws(p);
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p += 2;
                else p++;
            }
            if (*p == '"') p++;
            continue;
        }
        uint32_t packed = 0;
        if (p[0] == '#' && p[1] == '[') {
            const char *a = p + 2;
            char attr[32];
            if (parse_ident(&a, attr) == 0 && name_eq(attr, "packed") && *skip_ws(a) == ']') {
                p = skip_ws(a) + 1;
                packed = 1;
            }
        }
        if (!keyword_at(p, "struct")) {
            if (*p) p++;
            continue;
        }
        p += 6;
        if (struct_count >= MAX_STRUCTS) return -1;
        StructDef *st = &structs[struct_count];
        if (parse_ident(&p, st->name) < 0) return -1;
        st->field_count = 0;
        st->size = 0;
        st->align = 1;
        st->packed = packed;
        if (consume(&p, '{') < 0) return -1;
        while (*p) {
            p = skip_ws(p);
            if (*p == '}') { p++; break; }
            if (st->field_count >= MAX_FIELDS) return -1;
            Field *f = &st->fields[st->field_count++];
            if (parse_ident(&p, f->name) < 0) return -1;
            if (consume(&p, ':') < 0) return -1;
            if (parse_type(&p, &f->type) < 0) return -1;
            uint32_t align = packed ? 1 : (f->type.align ? f->type.align : 1);
            st->size = align_up_u32(st->size, align);
            f->offset = st->size;
            st->size += f->type.size;
            if (align > st->align) st->align = align;
            if (consume(&p, ';') < 0) return -1;
        }
        st->size = align_up_u32(st->size, st->align);
        p = skip_ws(p);
        if (*p == ';') p++;
        struct_count++;
    }
    return 0;
}

static int scan_functions(const char *p) {
    func_count = 0;
    while (*p) {
        p = skip_ws(p);
        if (!*p) break;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p += 2;
                else p++;
            }
            if (*p == '"') p++;
            continue;
        }
        if (keyword_at(p, "import")) {
            while (*p && *p != ';') p++;
            if (*p == ';') p++;
            continue;
        }
        uint32_t attr_unsafe = 0;
        uint32_t attr_kernel = 0;
        uint32_t attr_user = 0;
        uint32_t attr_export = 0;
        while (p[0] == '#' && p[1] == '[') {
            const char *attr_p = p + 2;
            char attr[32];
            if (parse_ident(&attr_p, attr) == 0) {
                if (name_eq(attr, "unsafe")) attr_unsafe = 1;
                else if (name_eq(attr, "kernel")) attr_kernel = 1;
                else if (name_eq(attr, "user")) attr_user = 1;
                else if (name_eq(attr, "export")) attr_export = 1;
                else if (name_eq(attr, "packed")) { }
                else { puts("orec: unknown attribute "); puts(attr); puts("\n"); return -1; }
            }
            while (*p && *p != ']') p++;
            if (*p == ']') p++;
            p = skip_ws(p);
        }
        if (keyword_at(p, "struct")) {
            while (*p && *p != '}') p++;
            if (*p == '}') p++;
            if (*p == ';') p++;
            continue;
        }
        uint32_t is_extern = 0;
        if (keyword_at(p, "extern")) {
            is_extern = 1;
            p += 6;
            p = skip_ws(p);
        }
        if (!keyword_at(p, "fn")) { p++; continue; }
        p += 2;
        if (func_count >= MAX_FUNCS) { puts("orec: too many functions\n"); return -1; }
        Function *fn = &funcs[func_count];
        if (parse_ident(&p, fn->name) < 0) { puts("orec: expected function name\n"); return -1; }
        fn->argc = 0;
        fn->addr = 0;
        fn->is_extern = is_extern;
        fn->is_unsafe = attr_unsafe;
        fn->attr_kernel = attr_kernel;
        fn->attr_user = attr_user;
        fn->attr_export = attr_export;
        fn->ret_type = ty_int();
        if (consume(&p, '(') < 0) { puts("orec: expected function (\n"); return -1; }
        p = skip_ws(p);
        if (*p != ')') {
            for (;;) {
                if (fn->argc >= MAX_PARAMS) { puts("orec: too many parameters\n"); return -1; }
                char param[32];
                if (parse_ident(&p, param) < 0) { puts("orec: expected parameter name in "); puts(fn->name); puts("\n"); return -1; }
                copy_name(fn->params[fn->argc], param);
                fn->param_types[fn->argc] = ty_int();
                p = skip_ws(p);
                if (*p == ':') {
                    p++;
                    if (parse_type(&p, &fn->param_types[fn->argc]) < 0) { puts("orec: bad parameter type in "); puts(fn->name); puts("\n"); return -1; }
                }
                fn->argc++;
                p = skip_ws(p);
                if (*p == ')') break;
                if (*p != ',') { puts("orec: expected , in parameters for "); puts(fn->name); puts("\n"); return -1; }
                p++;
            }
        }
        if (*p != ')') { puts("orec: expected ) in "); puts(fn->name); puts("\n"); return -1; }
        p++;
        p = skip_ws(p);
        if (p[0] == '-' && p[1] == '>') {
            p += 2;
            if (parse_type(&p, &fn->ret_type) < 0) { puts("orec: bad return type in "); puts(fn->name); puts("\n"); return -1; }
        }
        if (is_extern) {
            const char *after_sig = skip_ws(p);
            if (*after_sig != ';') {
                is_extern = 0;
                fn->is_extern = 0;
            }
        }
        if (is_extern) {
            if (consume(&p, ';') < 0) { puts("orec: expected ; after extern "); puts(fn->name); puts("\n"); return -1; }
            func_count++;
            continue;
        }
        while (*p && *p != '{') p++;
        if (*p != '{') { puts("orec: expected body for "); puts(fn->name); puts("\n"); return -1; }
        fn->body = p + 1;
        const char *end = find_matching_brace(p + 1);
        if (!end) { puts("orec: unmatched body for "); puts(fn->name); puts("\n"); return -1; }
        func_count++;
        p = end + 1;
    }
    if (compile_kmod_target) return find_func("module_init", 0) ? 0 : -1;
    return find_func("main", 0) ? 0 : -1;
}

static int compile_function(uint32_t index) {
    current_func = index;
    Function *fn = &funcs[index];
    fn->addr = text_len;
    var_count = 0;
    current_function_unsafe = fn->is_unsafe;
    stack_size = 8; /* [rbp-8] is reserved for the saved SysV callee-saved rbx. */
    frame_patches[index].patch = emit_prologue();
    frame_patches[index].fn = index;
    for (uint32_t i = 0; i < fn->argc; ++i) {
        Var *v = add_var_type(fn->params[i], fn->param_types[i]);
        if (!v) return -1;
        if (i < 4) emit_store_arg_reg(v->offset, i);
        else if (i == 4) emit_store_r8_arg(v->offset);
        else if (i == 5) emit_store_r9_arg(v->offset);
    }
    const char *p = fn->body;
    if (compile_block(&p) < 0) return -1;
    if (name_eq(fn->name, "main")) emit_exit_const(0);
    else {
        emit_mov_rax(0);
        emit_epilogue_ret();
    }
    uint32_t frame = align_up_u32((uint32_t)stack_size + 32, 16);
    patch32(frame_patches[index].patch, (int32_t)frame);
    current_function_unsafe = 0;
    return 0;
}

static int validate_kernel_target(void) {
    uint32_t init_index = 0;
    Function *init = find_func("module_init", &init_index);
    if (!init || init->is_extern) {
        puts("orec: kernel target requires module_init\n");
        return -1;
    }
    if (init->argc != 1 || init->param_types[0].kind != TY_PTR || !type_is_int_like(init->ret_type)) {
        puts("orec: module_init must be fn module_init(api: ptr<KernelApi>) -> int\n");
        return -1;
    }
    uint32_t cmd_index = 0;
    Function *cmd = find_func("command_main", &cmd_index);
    if (cmd) {
        if (cmd->is_extern || cmd->argc != 2 ||
            cmd->param_types[0].kind != TY_PTR ||
            !type_is_int_like(cmd->param_types[1]) ||
            !type_is_int_like(cmd->ret_type)) {
            puts("orec: command_main must be fn command_main(out: ptr<char>, cap: int) -> int\n");
            return -1;
        }
    }
    for (uint32_t i = 0; i < func_count; ++i) {
        if (funcs[i].attr_user) {
            puts("orec: kernel target rejects #[user] function ");
            puts(funcs[i].name);
            puts("\n");
            return -1;
        }
        if (!funcs[i].is_extern) continue;
        if (name_eq(funcs[i].name, "write") || name_eq(funcs[i].name, "sys_write") ||
            name_eq(funcs[i].name, "syscall0") || name_eq(funcs[i].name, "syscall1") ||
            name_eq(funcs[i].name, "syscall2") || name_eq(funcs[i].name, "syscall3") ||
            name_eq(funcs[i].name, "syscall4")) {
            puts("orec: kernel target forbids syscall extern ");
            puts(funcs[i].name);
            puts("\n");
            return -1;
        }
    }
    return 0;
}

static int validate_user_target(void) {
    for (uint32_t i = 0; i < func_count; ++i) {
        if (funcs[i].attr_kernel) {
            puts("orec: user target rejects #[kernel] function ");
            puts(funcs[i].name);
            puts("\n");
            return -1;
        }
    }
    return 0;
}

static int compile_source(const char *p) {
    text_len = data_len = 0;
    call_count = 0;
    data_ref_count = 0;
    loop_depth = 0;
    if (scan_structs(p) < 0) { puts("orec: struct scan failed\n"); return -1; }
    if (scan_functions(p) < 0) { puts("orec: function scan failed\n"); return -1; }
    if (compile_kmod_target && validate_kernel_target() < 0) return -1;
    if (!compile_kmod_target && validate_user_target() < 0) return -1;
    uint32_t main_index = 0;
    uint64_t main_patch = 0;
    if (!compile_kmod_target) {
        (void)find_func("main", &main_index);
        emit8(0xE8);
        main_patch = text_len;
        emit32(0);
        emit_exit_rax();
    }
    for (uint32_t i = 0; i < func_count; ++i) {
        if (funcs[i].is_extern) continue;
        if (compile_function(i) < 0) return -1;
    }
    if (!compile_kmod_target) {
        patch32(main_patch, (int32_t)(funcs[main_index].addr - (main_patch + 4)));
    }
    for (uint32_t i = 0; i < call_count; ++i) {
        uint64_t patch = calls[i].patch;
        uint64_t target = funcs[calls[i].fn].addr;
        patch32(patch, (int32_t)(target - (patch + 4)));
    }
    return 0;
}

static void append_char(char *dst, uint64_t *out, uint64_t cap, char c) {
    if (*out + 1 < cap) dst[(*out)++] = c;
}

static void append_str_n(char *dst, uint64_t *out, uint64_t cap, const char *s, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) append_char(dst, out, cap, s[i]);
}

static int read_file_into(const char *path, char *buf, uint64_t cap, uint64_t *out_len) {
#ifdef OREC_HOST
    char host_path[256];
    const char *root = getenv("ORE_ROOT");
    if (!root || !root[0]) root = ".";
    uint64_t pos = 0;
    for (uint64_t i = 0; root[i] && pos + 1 < sizeof(host_path); ++i) host_path[pos++] = root[i];
    if (pos && host_path[pos - 1] != '/' && pos + 1 < sizeof(host_path)) host_path[pos++] = '/';
    const char *rel = path;
    if (rel[0] == '/') rel++;
    if (rel[0] == 'd' && rel[1] == 'i' && rel[2] == 's' && rel[3] == 'k' && rel[4] == '/') rel += 5;
    for (uint64_t i = 0; rel[i] && pos + 1 < sizeof(host_path); ++i) host_path[pos++] = rel[i];
    host_path[pos] = 0;

    int fd = open(host_path, O_RDONLY);
    if (fd < 0) return -1;
    uint64_t total = 0;
    while (total + 1 < cap) {
        ssize_t n = read(fd, buf + total, cap - 1 - total);
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) break;
        total += (uint64_t)n;
    }
    close(fd);
    buf[total] = 0;
    if (out_len) *out_len = total;
    return 0;
#else
    int64_t fd = sys_open(path);
    if (fd < 0) return -1;
    uint64_t total = 0;
    for (;;) {
        int64_t n = sys_file_read((uint64_t)fd, buf + total, cap - 1 - total);
        if (n < 0) {
            sys_close((uint64_t)fd);
            return -1;
        }
        if (n == 0) break;
        total += (uint64_t)n;
        if (total + 1 >= cap) break;
    }
    sys_close((uint64_t)fd);
    buf[total] = 0;
    if (out_len) *out_len = total;
    return 0;
#endif
}

static int expand_imports_depth(const char *src, char *dst, uint64_t cap, uint32_t depth) {
    if (depth > 8) {
        puts("orec: import depth exceeded\n");
        return -1;
    }
    const char *p = src;
    uint64_t out = 0;
    while (*p) {
        const char *s = skip_ws(p);
        if (s != p) {
            append_str_n(dst, &out, cap, p, (uint64_t)(s - p));
            p = s;
        }
        if (!keyword_at(p, "import")) {
            append_char(dst, &out, cap, *p++);
            continue;
        }
        p += 6;
        p = skip_ws(p);
        if (*p != '"') {
            puts("orec: import expects string path near: ");
            for (uint32_t i = 0; p[i] && i < 24; ++i) write_one(&p[i]);
            puts("\n");
            return -1;
        }
        p++;
        char imp[128];
        uint32_t n = 0;
        while (*p && *p != '"' && n + 1 < sizeof(imp)) imp[n++] = *p++;
        imp[n] = 0;
        if (*p != '"') { puts("orec: unterminated import path\n"); return -1; }
        p++;
        p = skip_ws(p);
        if (*p == ';') p++;
        char full[160];
        const char *prefix = "/disk/include/";
        uint32_t k = 0;
        for (; prefix[k] && k + 1 < sizeof(full); ++k) full[k] = prefix[k];
        for (uint32_t i = 0; imp[i] && k + 1 < sizeof(full); ++i) full[k++] = imp[i];
        full[k] = 0;
        char *imported = import_sources[depth];
        uint64_t imported_len = 0;
        if (read_file_into(full, imported, IMPORT_BUF_SIZE, &imported_len) < 0) {
            puts("orec: import not found ");
            puts(full);
            puts("\n");
            return -1;
        }
        append_str_n(dst, &out, cap, imported, imported_len);
        append_char(dst, &out, cap, '\n');
    }
    dst[out] = 0;
    return 0;
}

static int expand_imports(const char *src, char *dst, uint64_t cap) {
    return expand_imports_depth(src, dst, cap, 0);
}

static void patch_data_refs(uint64_t image_base, uint64_t text_image_size) {
    for (uint32_t i = 0; i < data_ref_count; ++i) {
        uint64_t patch = data_refs[i].patch;
        uint64_t target = image_base + text_image_size + data_refs[i].data_off;
        uint64_t next = image_base + patch + 4;
        patch32(patch, (int32_t)((int64_t)target - (int64_t)next));
    }
}

static uint64_t build_image(void) {
    OreImageHeader *hdr = (OreImageHeader *)image;
    uint64_t text_image_size = (text_len + 4095ULL) & ~4095ULL;
    patch_data_refs(0x0000008000000000ULL, text_image_size);
    hdr->magic = ORE_IMAGE_MAGIC;
    hdr->version = ORE_IMAGE_VERSION;
    hdr->header_size = sizeof(OreImageHeader);
    hdr->entry = 0;
    hdr->text_size = text_image_size;
    hdr->data_size = data_len;
    hdr->bss_size = 0;
    hdr->reloc_count = 0;
    uint64_t off = sizeof(OreImageHeader);
    for (uint64_t i = 0; i < text_len; ++i) image[off++] = text[i];
    while (off < sizeof(OreImageHeader) + text_image_size) image[off++] = 0;
    for (uint64_t i = 0; i < data_len; ++i) image[off++] = data[i];
    return off;
}

#ifdef OREC_HOST
static void copy_kmod_name(char dst[32], const char *path) {
    const char *base = path;
    for (uint64_t i = 0; path[i]; ++i) {
        if (path[i] == '/' || path[i] == '\\') base = path + i + 1;
    }
    uint64_t i = 0;
    for (; i + 1 < 32 && base[i] && base[i] != '.'; ++i) dst[i] = base[i];
    dst[i] = 0;
}

static uint64_t build_kmod_image(uint32_t entry_fn, const char *source_path) {
    OreKModHeader *hdr = (OreKModHeader *)image;
    uint64_t text_image_size = (text_len + 4095ULL) & ~4095ULL;
    patch_data_refs(0, text_image_size);
    hdr->magic = ORE_KMOD_MAGIC;
    hdr->version = ORE_KMOD_VERSION;
    hdr->header_size = sizeof(OreKModHeader);
    hdr->kernel_abi_version = ORE_KERNEL_ABI_VERSION;
    hdr->flags = 0;
    hdr->entry = funcs[entry_fn].addr;
    hdr->text_size = text_image_size;
    hdr->data_size = data_len;
    hdr->bss_size = 0;
    hdr->reloc_count = 0;
    hdr->import_count = 0;
    hdr->export_count = 0;
    uint32_t command_fn = 0;
    hdr->command_entry = find_func("command_main", &command_fn) && !funcs[command_fn].is_extern
        ? funcs[command_fn].addr
        : 0xffffffffffffffffULL;
    copy_kmod_name(hdr->name, source_path);
    uint64_t off = sizeof(OreKModHeader);
    for (uint64_t i = 0; i < text_len; ++i) image[off++] = text[i];
    while (off < sizeof(OreKModHeader) + text_image_size) image[off++] = 0;
    for (uint64_t i = 0; i < data_len; ++i) image[off++] = data[i];
    return off;
}
#endif

#ifdef OREC_HOST
int main(int argc, char **argv) {
    if (argc < 3) {
        puts("usage: orec-host [--kmod] <input.ore> <output>\n");
        return 1;
    }
    int kmod_target = 0;
    int argi = 1;
    if (name_eq(argv[argi], "--kmod")) {
        kmod_target = 1;
        argi++;
    }
    if (argc - argi < 2) {
        puts("usage: orec-host [--kmod] <input.ore> <output>\n");
        return 1;
    }
    const char *path = argv[argi];
    if (read_file_into(path, source, sizeof(source), 0) < 0) {
        puts("orec-host: source not found ");
        puts(path);
        puts("\n");
        return 1;
    }
    puts("orec-host: compiling ");
    puts(path);
    puts("\n");
    if (expand_imports(source, expanded_source, sizeof(expanded_source)) < 0) return 1;
#ifdef OREC_HOST
    const char *dump = getenv("ORE_DUMP_EXPANDED");
    if (dump && dump[0]) {
        int dfd = open(dump, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (dfd >= 0) {
            (void)write(dfd, expanded_source, strlen(expanded_source));
            close(dfd);
        }
    }
#endif
    compile_kmod_target = (uint32_t)kmod_target;
    if (compile_source(expanded_source) < 0) return 1;
    uint64_t image_len = 0;
    if (kmod_target) {
        uint32_t entry_fn = 0;
        if (!find_func("module_init", &entry_fn) || funcs[entry_fn].is_extern) {
            puts("orec-host: module target requires module_init\n");
            return 1;
        }
        image_len = build_kmod_image(entry_fn, path);
    } else {
        image_len = build_image();
    }
    int fd = open(argv[argi + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        puts("orec-host: cannot open output\n");
        return 1;
    }
    uint64_t done = 0;
    while (done < image_len) {
        ssize_t n = write(fd, image + done, image_len - done);
        if (n <= 0) {
            close(fd);
            puts("orec-host: write failed\n");
            return 1;
        }
        done += (uint64_t)n;
    }
    close(fd);
    puts("orec-host: wrote image\n");
    return 0;
}
#else
int main(void) {
    char args[160];
    char path[128];
    char run_args[128];
    if (sys_args(args, sizeof(args)) <= 0) {
        puts("usage: orec <file.ore>\n");
        return 1;
    }
    uint32_t i = 0;
    while (args[i] == ' ' || args[i] == '\t') i++;
    uint32_t p = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && p + 1 < sizeof(path)) path[p++] = args[i++];
    path[p] = 0;
    while (args[i] == ' ' || args[i] == '\t') i++;
    uint32_t r = 0;
    while (args[i] && r + 1 < sizeof(run_args)) run_args[r++] = args[i++];
    run_args[r] = 0;
    if (!path[0]) {
        puts("usage: orec <file.ore>\n");
        return 1;
    }
    if (read_file_into(path, source, sizeof(source), 0) < 0) {
        puts("orec: source not found\n");
        return 1;
    }
    puts("orec: compiling ");
    puts(path);
    puts("\n");
    if (expand_imports(source, expanded_source, sizeof(expanded_source)) < 0) return 1;
    if (compile_source(expanded_source) < 0) return 1;
    uint64_t image_len = build_image();
    puts("orec: text=");
    putu(text_len);
    puts(" data=");
    putu(data_len);
    puts(" running\n");
    int64_t rc = sys_exec_image(image, image_len, run_args);
    puts("orec: program exited ");
    putu((uint64_t)rc);
    puts("\n");
    return rc < 0 ? 1 : (int)rc;
}
#endif
