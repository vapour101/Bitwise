#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX(x, y) ((x) >= (y) ? (x) : (y))

void* xrealloc(void* ptr, size_t num_bytes)
{
    ptr = realloc(ptr, num_bytes);
    if (!ptr) {
        perror("xrealloc failed");
        exit(1);
    }
    return ptr;
}

void* xmalloc(size_t num_bytes)
{
    void* ptr = malloc(num_bytes);
    if (!ptr) {
        perror("xmalloc failed");
        exit(1);
    }
    return ptr;
}

void fatal(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf("Fatal: ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
    exit(1);
}

// stretchy buffers

typedef struct BufHdr {
    size_t len;
    size_t cap;
    char buf[];
} BufHdr;

#define buf__hdr(b) ((BufHdr*)((char*)b - offsetof(BufHdr, buf)))
#define buf__fits(b, n) (buf_len(b) + (n) <= buf_cap(b))
#define buf__fit(b, n) (buf__fits((b), (n)) ? 0 : ((b) = buf__grow((b), buf_len(b) + (n), sizeof(*(b)))))

#define buf_len(b) ((b) ? buf__hdr(b)->len : 0)
#define buf_cap(b) ((b) ? buf__hdr(b)->cap : 0)
#define buf_end(b) ((b) + buf_len(b))
#define buf_free(b) ((b) ? (free(buf__hdr(b)), (b) = NULL) : 0)
#define buf_push(b, ...) (buf__fit(b, 1), (b)[buf__hdr(b)->len++] = (__VA_ARGS__))

void* buf__grow(const void* buf, size_t new_len, size_t elem_size)
{
    assert(buf_cap(buf) <= (SIZE_MAX - 1) / 2);
    size_t new_cap = MAX(1 + 2 * buf_cap(buf), new_len);
    assert(new_len <= new_cap);
    assert(new_cap <= (SIZE_MAX - offsetof(BufHdr, buf) / elem_size));
    size_t new_size = offsetof(BufHdr, buf) + new_cap * elem_size;

    BufHdr* new_hdr;
    if (buf) {
        new_hdr = xrealloc(buf__hdr(buf), new_size);
    } else {
        new_hdr = xmalloc(new_size);
        new_hdr->len = 0;
    }

    new_hdr->cap = new_cap;
    return new_hdr->buf;
}

void buf_test()
{
    int* test_buffer = NULL;
    assert(buf_len(test_buffer) == 0);

    int n = 1024;
    for (int i = 0; i < n; i++) {
        buf_push(test_buffer, i);
    }

    assert(buf_len(test_buffer) == n);

    for (int i = 0; i < buf_len(test_buffer); i++) {
        assert(test_buffer[i] == i);
    }

    buf_free(test_buffer);
    assert(test_buffer == NULL);
    assert(buf_len(test_buffer) == 0);
}

// string interning

typedef struct Intern {
    size_t len;
    const char* str;
} Intern;

static Intern* interns;

const char* str_intern_range(const char* start, const char* end)
{
    size_t len = end - start;

    for (Intern* it = interns; it != buf_end(interns); it++) {
        if (it->len == len && strncmp(it->str, start, len) == 0) {
            return it->str;
        }
    }

    char* str = xmalloc(len + 1);
    memcpy(str, start, len);
    str[len] = 0;
    buf_push(interns, (Intern){ len, str });
    return str;
}

const char* str_intern(const char* str)
{
    return str_intern_range(str, str + strlen(str));
}

void str_intern_test()
{
    char a[] = "hello";

    assert(strcmp(a, str_intern(a)) == 0);
    assert(str_intern(a) == str_intern(a));
    assert(str_intern(str_intern(a)) == str_intern(a));

    char b[] = "hello";

    assert(a != b);
    assert(str_intern(a) == str_intern(b));

    char c[] = "hello!";

    assert(str_intern(a) != str_intern(c));

    char d[] = "hell";

    assert(str_intern(a) != str_intern(d));
}

// lexer

typedef enum TokenKind {
    TOKEN_EOL = 0,
    TOKEN_LAST_CHAR = 127,
    TOKEN_INT,
    TOKEN_NAME,
} TokenKind;

size_t copy_token_kind_str(char* dest, size_t dest_size, TokenKind kind)
{
    size_t n = 0;

    switch (kind) {
    case TOKEN_EOL:
        n = snprintf(dest, dest_size, "end of file");
        break;
    case TOKEN_INT:
        n = snprintf(dest, dest_size, "integer");
        break;
    case TOKEN_NAME:
        n = snprintf(dest, dest_size, "name");
        break;
    default:
        if (kind < 128 && isprint(kind))
            n = snprintf(dest, dest_size, "%c", kind);
        else
            n = snprintf(dest, dest_size, "<ASCII %d>", kind);
        break;
    }

    return n;
}

// Warning: Return value is volatile
const char* temp_token_kind_str(TokenKind kind)
{
    static char buf[256];
    size_t n = copy_token_kind_str(buf, sizeof(buf), kind);
    assert(n + 1 <= sizeof(buf));
    return buf;
}

typedef struct Token {
    TokenKind kind;
    const char* start;
    const char* end;
    union {
        int val;
        const char* name;
    };
} Token;

Token token;
const char* stream;

const char* keyword_if;
const char* keyword_for;
const char* keyword_while;

void init_keywords()
{
    keyword_if = str_intern("if");
    keyword_for = str_intern("for");
    keyword_while = str_intern("while");
}

void next_token()
{
    token.start = stream;
    switch (*stream) {
        // clang-format off
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': {
        // clang-format on
        int val = 0;
        while (isdigit(*stream)) {
            val *= 10;
            val += *stream - '0';
            stream++;
        }
        token.kind = TOKEN_INT;
        token.val = val;
        break;
    }
        // clang-format off
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
    case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
    case 'v': case 'w': case 'x': case 'y': case 'z': case 'A': case 'B':
    case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I':
    case 'J': case 'K': case 'L': case 'M': case 'N': case 'O': case 'P':
    case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W':
    case 'X': case 'Y': case 'Z': case '_':
        // clang-format on
        while (isalnum(*stream) || *stream == '_') {
            stream++;
        }
        token.kind = TOKEN_NAME;
        token.name = str_intern_range(token.start, stream);
        break;
    default:
        token.kind = *stream++;
        break;
    }
    token.end = stream;
}

void init_stream(const char* str)
{
    stream = str;
    next_token();
}

void print_token(Token token)
{
    switch (token.kind) {
    case TOKEN_INT:
        printf("TOKEN INT:\t%d\n", token.val);
        break;
    case TOKEN_NAME:
        printf("TOKEN NAME:\t%.*s\n", (int)(token.end - token.start), token.start);
        break;
    default:
        printf("TOKEN '%c'\n", token.kind);
        break;
    }
}

static inline bool is_token(TokenKind kind)
{
    return token.kind == kind;
}

static inline bool is_token_name(const char* name)
{
    return token.kind == TOKEN_NAME && token.name == name;
}

static inline bool match_token(TokenKind kind)
{
    if (is_token(kind)) {
        next_token();
        return true;
    }

    return false;
}

static inline bool expect_token(TokenKind kind)
{
    if (is_token(kind)) {
        next_token();
        return true;
    }

    char buf[256];
    copy_token_kind_str(buf, sizeof(buf), kind);
    fatal("expected token %s, got %s", buf, temp_token_kind_str(token.kind));
    return false;
}

#define assert_token(x) assert(match_token(x))
#define assert_token_name(x) assert(token.name == str_intern(x) && match_token(TOKEN_NAME))
#define assert_token_int(x) assert(token.val == (x) && match_token(TOKEN_INT))
#define assert_token_eof() assert(is_token(0))

void lex_test()
{
    const char* str = "XY+(XY)_HELLO1,234+994";
    init_stream(str);
    assert_token_name("XY");
    assert_token('+');
    assert_token('(');
    assert_token_name("XY");
    assert_token(')');
    assert_token_name("_HELLO1");
    assert_token(',');
    assert_token_int(234);
    assert_token('+');
    assert_token_int(994);
    assert_token_eof();
}

#undef assert_token_eof
#undef assert_token_int
#undef assert_token_name
#undef assert_token

int parse_expr();

int parse_expr3()
{
    if (is_token(TOKEN_INT)) {
        int val = token.val;
        next_token();
        return val;
    } else if (match_token('(')) {
        int val = parse_expr();
        expect_token(')');
        return val;
    } else {
        fatal("expected integer or (, got %s", temp_token_kind_str(token.kind));
        return 0;
    }
}

int parse_expr2()
{
    if (match_token('-'))
        return -parse_expr2();
    else if (match_token('+'))
        return parse_expr2();
    else
        return parse_expr3();
}

int parse_expr1()
{
    int val = parse_expr2();
    while (is_token('*') || is_token('/')) {
        char op = token.kind;
        next_token();
        int rval = parse_expr2();
        if (op == '*')
            val *= rval;
        else {
            assert(op == '/');
            assert(rval != 0);
            val /= rval;
        }
    }
    return val;
}

int parse_expr0()
{
    int val = parse_expr1();
    while (is_token('+') || is_token('-')) {
        char op = token.kind;
        next_token();
        int rval = parse_expr1();
        if (op == '+')
            val += rval;
        else {
            assert(op == '-');
            val -= rval;
        }
    }
    return val;
}

int parse_expr()
{
    return parse_expr0();
}

int parse_expr_str(const char* str)
{
    init_stream(str);
    return parse_expr();
}

#define assert_expr(x) assert(parse_expr_str(#x) == (x))

void parse_test()
{
    // clang-format off
    assert_expr(1);
    assert_expr((1));
    assert_expr(-+1);
    assert_expr(1-2-3);
    assert_expr(2*3+4*5);
    assert_expr(2*(3+4)*5);
    assert_expr(2+-3);
    // clang-format on
}

#undef assert_expr

#define POP() (*--top)
#define PUSH(x) (*top++ = (x))
#define POPS(n) assert(top - stack >= (n))
#define PUSHES(n) assert(top + (n) <= stack + MAX_STACK)

enum VM_OPS {
    ADD,
    SUB,
    MUL,
    DIV,
    POS,
    NEG,
    NOT,
    LIT,
    HLT
};

int32_t vm_exec(const uint8_t* code)
{
    enum { MAX_STACK = 1024 };
    int32_t stack[MAX_STACK];
    int32_t* top = stack;

    while (42) {
        uint8_t op = *code++;
        switch (op) {
        case ADD: {
            POPS(2);
            int32_t right = POP();
            int32_t left = POP();
            PUSHES(1);
            PUSH(left + right);
            break;
        }
        case SUB: {
            POPS(2);
            int32_t right = POP();
            int32_t left = POP();
            PUSHES(1);
            PUSH(left - right);
            break;
        }
        case MUL: {
            POPS(2);
            int32_t right = POP();
            int32_t left = POP();
            PUSHES(1);
            PUSH(left * right);
            break;
        }
        case DIV: {
            POPS(2);
            int32_t right = POP();
            int32_t left = POP();
            PUSHES(1);
            assert(right != 0);
            PUSH(left / right);
            break;
        }
        case POS: {
            POPS(1);
            int32_t val = POP();
            PUSHES(1);
            PUSH(val);
            break;
        }
        case NEG: {
            POPS(1);
            int32_t val = POP();
            PUSHES(1);
            PUSH(-val);
            break;
        }
        case NOT: {
            POPS(1);
            int32_t val = POP();
            PUSHES(1);
            PUSH(~val);
            break;
        }
        case LIT: {
            uint32_t val = 0;
            for (int i = 0; i < 4; i++)
                val += (1 << (8 * i)) * (*code++);

            PUSHES(1);
            PUSH(val);
            break;
        }
        case HLT: {
            POPS(1);
            return POP();
        }
        default:
            fatal("vm_exec: illegal opcode");
            return 0;
        }
    }
}

#undef PUSHES
#undef POPS
#undef PUSH
#undef POP

#define push_lit(b, x)                  \
    buf_push((b), LIT);                 \
    buf_push((b), (uint8_t)(x));        \
    buf_push((b), (uint8_t)((x) >> 8)); \
    buf_push((b), (uint8_t)(x >> 16));  \
    buf_push((b), (uint8_t)(x >> 24))

uint8_t* parse_vm_expr0(uint8_t* output);

uint8_t* parse_vm_expr3(uint8_t* output)
{
    if (is_token(TOKEN_INT)) {
        push_lit(output, token.val);
        next_token();
        return output;
    } else if (match_token('(')) {
        output = parse_vm_expr0(output);
        expect_token(')');
        return output;
    } else {
        fatal("expected integer or (, got %s", temp_token_kind_str(token.kind));
        return output;
    }
}

uint8_t* parse_vm_expr2(uint8_t* output)
{
    if (match_token('-')) {
        output = parse_vm_expr2(output);
        buf_push(output, NEG);
        return output;
    } else if (match_token('+')) {
        output = parse_vm_expr2(output);
        buf_push(output, POS);
        return output;
    } else if (match_token('~')) {
        output = parse_vm_expr2(output);
        buf_push(output, NOT);
        return output;
    } else {
        output = parse_vm_expr3(output);
        return output;
    }
}

uint8_t* parse_vm_expr1(uint8_t* output)
{
    output = parse_vm_expr2(output);
    while (is_token('*') || is_token('/')) {
        char op = token.kind;
        next_token();
        output = parse_vm_expr2(output);
        if (op == '*')
            buf_push(output, MUL);
        else {
            assert(op == '/');
            buf_push(output, DIV);
        }
    }
    return output;
}

uint8_t* parse_vm_expr0(uint8_t* output)
{
    output = parse_vm_expr1(output);
    while (is_token('+') || is_token('-')) {
        char op = token.kind;
        next_token();
        output = parse_vm_expr1(output);
        if (op == '+')
            buf_push(output, ADD);
        else {
            assert(op == '-');
            buf_push(output, SUB);
        }
    }
    return output;
}

uint8_t* parse_vm_expr(uint8_t* output)
{
    output = parse_vm_expr0(output);
    buf_push(output, HLT);
    return output;
}

#undef push_lit

int vm_evaluate(const char* str)
{
    init_stream(str);
    uint8_t* buffer = NULL;
    buffer = parse_vm_expr(buffer);

    return vm_exec(buffer);
}

#define assert_expr(x) assert(vm_evaluate(#x) == (x))

void vm_test()
{
    // clang-format off
    assert_expr(1);
    assert_expr((1));
    assert_expr(-+1);
    assert_expr(1-2-3);
    assert_expr(2*3+4*5);
    assert_expr(2*(3+4)*5);
    assert_expr(2+-3);
    assert_expr(~1+1);
    assert_expr(12*34+45/56+~25);
    // clang-format on
}

#undef assert_expr

void run_tests()
{
    buf_test();
    lex_test();
    str_intern_test();
    parse_test();
    vm_test();
}

int main(int argc, char** argv)
{
    run_tests();
    return 0;
}
