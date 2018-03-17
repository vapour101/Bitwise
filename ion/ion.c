#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

// stretchy buffers

typedef struct BufHdr {
    size_t len;
    size_t cap;
    char buf[0];
} BufHdr;

#define buf__hdr(b) ((BufHdr*)((char*)b - offsetof(BufHdr, buf)))
#define buf__fits(b, n) (buf_len(b) + (n) <= buf_cap(b))
#define buf__fit(b, n) (buf__fits((b), (n)) ? 0 : ((b) = buf__grow((b), buf_len(b) + (n), sizeof(*(b)))))

#define buf_len(b) ((b) ? buf__hdr(b)->len : 0)
#define buf_cap(b) ((b) ? buf__hdr(b)->cap : 0)
#define buf_push(b, x) (buf__fit(b, 1), (b)[buf__hdr(b)->len++] = (x))
#define buf_free(b) ((b) ? (free(buf__hdr(b)), (b) = NULL) : 0)

void* buf__grow(const void* buf, size_t new_len, size_t elem_size)
{
    size_t new_cap = MAX(1 + 2 * buf_cap(buf), new_len);
    assert(new_len <= new_cap);
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

    enum {
        N = 1024
    };

    for (int i = 0; i < N; i++) {
        buf_push(test_buffer, i);
    }

    assert(buf_len(test_buffer) == N);

    for (int i = 0; i < N; i++) {
        assert(test_buffer[i] == i);
    }

    buf_free(test_buffer);
    assert(test_buffer == NULL);
    assert(buf_len(test_buffer) == 0);
}

// lexer

typedef enum TokenKind {
    TOKEN_LAST_CHAR = 127,
    TOKEN_INT,
    TOKEN_NAME,
} TokenKind;

typedef struct Token {
    TokenKind kind;
    const char* start;
    const char* end;
    union {
        uint64_t val;
    };
} Token;

Token token;
const char* stream;

void next_token()
{
    token.start = stream;
    switch (*stream) {
        // clang-format off
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': {
        // clang-format on
        uint64_t val = 0;
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
        break;
    default:
        token.kind = *stream++;
        break;
    }
    token.end = stream;
}

void print_token(Token token)
{
    switch (token.kind) {
    case TOKEN_INT:
        printf("TOKEN INT:\t%lu\n", token.val);
        break;
    case TOKEN_NAME:
        printf("TOKEN NAME:\t%.*s\n", (int)(token.end - token.start), token.start);
        break;
    default:
        printf("TOKEN '%c'\n", token.kind);
        break;
    }
}

void lex_test()
{
    char* source = "+()_HELLO1,234+FOO|994";
    stream = source;
    next_token();

    while (token.kind) {
        print_token(token);
        next_token();
    }
}

int main(int argc, char** argv)
{
    buf_test();
    lex_test();
    return 0;
}
