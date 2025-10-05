// parser.c
// Single-file ACL2-like visual spec parser (no expressions).
// Reliable sub-block detection via safe stateful lookahead snapshot.
// Supports nested sub-blocks with optional string labels, typed and inferred fields,
// literal values: int, bool, string, char, comments, improved errors.
//
// Build: gcc -std=c11 -O2 -Wall -Wextra -o parser parser.c

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------- small helpers ---------- */

static char *str_dup_local(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *r = malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    return r;
}
static char *substr_dup(const char *s, size_t a, size_t b) {
    size_t n = b - a;
    char *r = malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s + a, n);
    r[n] = '\0';
    return r;
}

/* ---------- lexer ---------- */

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_INT_LITERAL,
    TOK_STRING,
    TOK_CHAR,
    TOK_BOOL_LITERAL,

    TOK_LBRACE,
    TOK_RBRACE,
    TOK_EQ,
    TOK_SEMI,

    TOK_TYPE_INT,
    TOK_TYPE_FLOAT,
    TOK_TYPE_BOOL,
    TOK_TYPE_STRING,

    TOK_UNKNOWN
} TokenKind;

typedef struct {
    TokenKind kind;
    char *text;    /* owned for identifiers/strings */
    long  ival;
    int   bval;
    int   cval;
    size_t pos;
    int line;
    int col;
} Token;

/* source buffer and reading state */
static const char *SRC = NULL;
static size_t SRC_POS = 0;
static size_t SRC_LEN = 0;
static int LINE = 1;
static int COL = 1;

static void adv_pos(char c) {
    if (c == '\n') { LINE++; COL = 1; } else COL++;
}
static char peekc(void) { return SRC_POS < SRC_LEN ? SRC[SRC_POS] : '\0'; }
static char getc_src(void) { char c = peekc(); if (c) { adv_pos(c); SRC_POS++; } return c; }

static void skip_spaces_and_comments(void) {
    while (SRC_POS < SRC_LEN) {
        char c = SRC[SRC_POS];
        if (c == '\r') { adv_pos(c); SRC_POS++; continue; } /* tolerate CR */
        if (isspace((unsigned char)c)) { adv_pos(c); SRC_POS++; continue; }
        if (c == '/' && SRC_POS+1 < SRC_LEN && SRC[SRC_POS+1] == '/') {
            getc_src(); getc_src();
            while (peekc() && peekc() != '\n') getc_src();
            continue;
        }
        if (c == '/' && SRC_POS+1 < SRC_LEN && SRC[SRC_POS+1] == '*') {
            getc_src(); getc_src();
            while (SRC_POS+1 < SRC_LEN && !(SRC[SRC_POS]=='*' && SRC[SRC_POS+1]=='/')) getc_src();
            if (SRC_POS+1 < SRC_LEN) { getc_src(); getc_src(); }
            continue;
        }
        break;
    }
}

static int parse_escape_char(void) {
    if (SRC_POS >= SRC_LEN) return '\\';
    char esc = getc_src();
    switch (esc) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        case '0': return '\0';
        default: return (unsigned char)esc;
    }
}

static Token next_token_internal(void) {
    skip_spaces_and_comments();
    Token tk; memset(&tk,0,sizeof(tk));
    tk.pos = SRC_POS; tk.line = LINE; tk.col = COL;
    if (SRC_POS >= SRC_LEN) { tk.kind = TOK_EOF; return tk; }
    char c = peekc();

    if (c == '{') { getc_src(); tk.kind = TOK_LBRACE; return tk; }
    if (c == '}') { getc_src(); tk.kind = TOK_RBRACE; return tk; }
    if (c == '=') { getc_src(); tk.kind = TOK_EQ; return tk; }
    if (c == ';') { getc_src(); tk.kind = TOK_SEMI; return tk; }

    if (c == '"') {
        getc_src();
        size_t cap = 128, len = 0;
        char *buf = malloc(cap);
        while (SRC_POS < SRC_LEN) {
            char ch = getc_src();
            if (ch == '"') break;
            if (ch == '\\') {
                int ec = parse_escape_char();
                ch = (char)ec;
            }
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = ch;
        }
        buf[len] = '\0';
        tk.kind = TOK_STRING; tk.text = buf;
        return tk;
    }

    if (c == '\'') {
        getc_src();
        int ch;
        if (peekc() == '\\') { getc_src(); ch = parse_escape_char(); } else ch = getc_src();
        if (peekc() == '\'') getc_src();
        tk.kind = TOK_CHAR; tk.cval = ch;
        return tk;
    }

    if (isalpha((unsigned char)c) || c == '_') {
        size_t a = SRC_POS; getc_src();
        while (SRC_POS < SRC_LEN && (isalnum((unsigned char)peekc()) || peekc() == '_')) getc_src();
        size_t b = SRC_POS;
        char *id = substr_dup(SRC, a, b);

        if (strcmp(id, "int") == 0) { free(id); tk.kind = TOK_TYPE_INT; return tk; }
        if (strcmp(id, "float") == 0) { free(id); tk.kind = TOK_TYPE_FLOAT; return tk; }
        if (strcmp(id, "bool") == 0) { free(id); tk.kind = TOK_TYPE_BOOL; return tk; }
        if (strcmp(id, "string") == 0) { free(id); tk.kind = TOK_TYPE_STRING; return tk; }
        if (strcmp(id, "true") == 0) { free(id); tk.kind = TOK_BOOL_LITERAL; tk.bval = 1; return tk; }
        if (strcmp(id, "false") == 0) { free(id); tk.kind = TOK_BOOL_LITERAL; tk.bval = 0; return tk; }

        tk.kind = TOK_IDENT; tk.text = id; return tk;
    }

    if (isdigit((unsigned char)c) || (c == '-' && SRC_POS+1 < SRC_LEN && isdigit((unsigned char)SRC[SRC_POS+1]))) {
        size_t a = SRC_POS;
        if (peekc() == '-') getc_src();
        while (SRC_POS < SRC_LEN && isdigit((unsigned char)peekc())) getc_src();
        size_t b = SRC_POS;
        char *num = substr_dup(SRC, a, b);
        tk.kind = TOK_INT_LITERAL;
        tk.ival = strtol(num, NULL, 10);
        free(num);
        return tk;
    }

    getc_src();
    tk.kind = TOK_UNKNOWN;
    return tk;
}

static void token_free(Token *t) { if (!t) return; if (t->text) free(t->text); t->text = NULL; }

/* ---------- parser shared buffer ----------
   We'll use a one-token BUF (current) and a SAVED slot.
   For lookahead we snapshot the entire lexer/parser state (positions, BUF, SAVED)
   and call next_token_internal as much as needed, then restore the snapshot.
*/

static Token BUF = {0};
static int HAVE_BUF = 0;
static Token SAVED = {0};
static int HAVE_SAVED = 0;

/* read next token from underlying lexer, respecting SAVED if present */
static Token get_token_shared(void) {
    if (HAVE_SAVED) {
        Token t = SAVED;
        HAVE_SAVED = 0;
        return t;
    }
    return next_token_internal();
}

/* push one token into SAVED (ownership transferred) */
static void push_token_shared(Token t) {
    if (HAVE_SAVED) token_free(&SAVED);
    SAVED = t;
    HAVE_SAVED = 1;
}

/* current token accessors */
static Token cur_token(void) {
    if (HAVE_BUF) return BUF;
    BUF = get_token_shared();
    HAVE_BUF = 1;
    return BUF;
}
static void consume_token(void) {
    if (HAVE_BUF) { token_free(&BUF); HAVE_BUF = 0; }
    else { Token t = get_token_shared(); token_free(&t); }
}
/* take a token (returns an owned copy) */
static Token take_token(void) {
    Token t = cur_token();
    Token c = t;
    if (c.text) c.text = str_dup_local(c.text);
    consume_token();
    return c;
}

/* ---------- safe lookahead via full snapshot ----------
   Save all lexer/parser state (SRC_POS, LINE, COL, HAVE_SAVED/SAVED, HAVE_BUF/BUF)
   then call next_token_internal as many times as needed to peek, collecting owned copies.
   Afterwards restore saved state and free temporaries created while peeking.
*/

typedef struct {
    size_t src_pos;
    int line;
    int col;
    int have_saved;
    Token saved_copy; /* owned copy of saved */
    int have_buf;
    Token buf_copy;   /* owned copy of buf */
} Snapshot;

static void snapshot_begin(Snapshot *S) {
    S->src_pos = SRC_POS;
    S->line = LINE;
    S->col = COL;
    S->have_saved = HAVE_SAVED;
    if (S->have_saved) {
        /* duplicate SAVED into saved_copy */
        S->saved_copy = SAVED;
        if (S->saved_copy.text) S->saved_copy.text = str_dup_local(SAVED.text);
    } else { memset(&S->saved_copy,0,sizeof(Token)); }
    S->have_buf = HAVE_BUF;
    if (S->have_buf) {
        S->buf_copy = BUF;
        if (S->buf_copy.text) S->buf_copy.text = str_dup_local(BUF.text);
    } else { memset(&S->buf_copy,0,sizeof(Token)); }
}

static void snapshot_restore(Snapshot *S) {
    /* free any current BUF or SAVED we might have */
    if (HAVE_BUF) token_free(&BUF);
    if (HAVE_SAVED) token_free(&SAVED);
    /* restore lexer positions */
    SRC_POS = S->src_pos;
    LINE = S->line;
    COL = S->col;
    /* restore SAVED */
    if (S->have_saved) {
        SAVED = S->saved_copy; HAVE_SAVED = 1;
    } else { HAVE_SAVED = 0; memset(&SAVED,0,sizeof(Token)); }
    /* restore BUF */
    if (S->have_buf) {
        BUF = S->buf_copy; HAVE_BUF = 1;
    } else { HAVE_BUF = 0; memset(&BUF,0,sizeof(Token)); }
}

/* peek n tokens ahead (n>=1), return owned copy of that token (caller must token_free) */
static Token peek_n_safe(int n) {
    Snapshot S; snapshot_begin(&S);
    Token out = {0};
    for (int i = 0; i < n; ++i) {
        Token t = get_token_shared();
        /* we must duplicate text to return an owned copy; but don't free original yet */
        out = t;
        if (out.text) out.text = str_dup_local(out.text);
        /* for interim tokens (not last), free the duplicate so we don't leak */
        if (i < n-1) token_free(&out);
        /* continue to next */
    }
    snapshot_restore(&S);
    return out;
}

/* convenience: peek next token (n=1) */
static Token peek_token_safe(void) { return peek_n_safe(1); }

/* ---------- error reporting ---------- */

static void show_line_context(size_t pos, int line, int col) {
    size_t i = pos;
    while (i > 0 && SRC[i-1] != '\n') i--;
    size_t j = i;
    while (j < SRC_LEN && SRC[j] != '\n') j++;
    size_t len = j - i;
    char *buf = malloc(len + 1);
    memcpy(buf, SRC + i, len); buf[len] = '\0';
    fprintf(stderr, "  %s\n", buf);
    fprintf(stderr, "  ");
    for (int k = 0; k < col-1 && k < (int)len; ++k) fputc((buf[k]=='\t')?'\t':' ', stderr);
    fprintf(stderr, "^\n");
    free(buf);
}

static void parse_error_token(const Token *t, const char *expect) {
    fprintf(stderr, "Parse error at %d:%d: unexpected token", t->line, t->col);
    if (t->text) fprintf(stderr, " '%s'", t->text);
    if (t->kind == TOK_INT_LITERAL) fprintf(stderr, " (int=%ld)", t->ival);
    fprintf(stderr, ", expected %s\n", expect ? expect : "valid construct");
    show_line_context(t->pos, t->line, t->col);
    exit(1);
}

/* ---------- values and AST ---------- */

typedef enum { VAL_INT, VAL_BOOL, VAL_STRING, VAL_CHAR } ValKind;
typedef struct {
    ValKind kind;
    long  ival;
    int   bval;
    char *sval;
    int   cval;
} Value;

static Value make_int(long x) { Value v; memset(&v,0,sizeof(v)); v.kind = VAL_INT; v.ival = x; return v; }
static Value make_bool(int b) { Value v; memset(&v,0,sizeof(v)); v.kind = VAL_BOOL; v.bval = b?1:0; return v; }
static Value make_string_owned(char *s) { Value v; memset(&v,0,sizeof(v)); v.kind = VAL_STRING; v.sval = s; return v; }
static Value make_char(int c) { Value v; memset(&v,0,sizeof(v)); v.kind = VAL_CHAR; v.cval = c; return v; }
static void value_free(Value *v) { if (!v) return; if (v->kind == VAL_STRING && v->sval) { free(v->sval); v->sval = NULL; } }

/* ---------- literal parsing (no expressions) ---------- */

static Value parse_literal_value(void) {
    Token t = cur_token();
    if (t.kind == TOK_INT_LITERAL) {
        Token tk = take_token();
        Value v = make_int(tk.ival);
        token_free(&tk);
        return v;
    }
    if (t.kind == TOK_BOOL_LITERAL) {
        Token tk = take_token();
        Value v = make_bool(tk.bval);
        token_free(&tk);
        return v;
    }
    if (t.kind == TOK_STRING) {
        Token tk = take_token(); /* owns text */
        Value v = make_string_owned(tk.text);
        return v;
    }
    if (t.kind == TOK_CHAR) {
        Token tk = take_token();
        Value v = make_char(tk.cval);
        token_free(&tk);
        return v;
    }
    parse_error_token(&t, "literal (int, bool, string, or char)");
    return make_int(0);
}

/* ---------- field parsing ---------- */

typedef struct Field Field;
typedef struct Block Block;
struct Field { char *type; char *name; Value value; Field *next; };
struct Block { char *name; char *label; Field *fields; Block *children; Block *next; Block *parent; };

static Field *parse_field_with_type(const char *type_name) {
    Token t = cur_token();
    if (t.kind != TOK_IDENT) parse_error_token(&t, "field name (identifier)");
    Token name_tok = take_token();

    Token eq = cur_token();
    if (eq.kind != TOK_EQ) parse_error_token(&eq, "'=' after field name");
    consume_token();

    Value v = parse_literal_value();

    Token semi = cur_token();
    if (semi.kind != TOK_SEMI) parse_error_token(&semi, "';' after field value");
    consume_token();

    Field *f = malloc(sizeof(Field)); memset(f,0,sizeof(Field));
    f->type = type_name ? str_dup_local(type_name) : NULL;
    f->name = name_tok.text;
    f->value = v;
    f->next = NULL;
    return f;
}

static Field *parse_field_from_type_token(TokenKind tk_type) {
    const char *type_name = NULL;
    if (tk_type == TOK_TYPE_INT) type_name = "int";
    else if (tk_type == TOK_TYPE_FLOAT) type_name = "float";
    else if (tk_type == TOK_TYPE_BOOL) type_name = "bool";
    else if (tk_type == TOK_TYPE_STRING) type_name = "string";
    consume_token(); /* consume type token */
    return parse_field_with_type(type_name);
}

/* ---------- block parsing with robust lookahead ---------- */

static Block *parse_block_recursive(Block *parent) {
    Token t = cur_token();
    if (t.kind != TOK_IDENT) parse_error_token(&t, "block name (identifier)");
    Token name_tok = take_token();

    /* optional immediate string label */
    char *label = NULL;
    Token after_name = cur_token();
    if (after_name.kind == TOK_STRING) {
        Token lab = take_token();
        label = lab.text;
        after_name = cur_token();
    }

    if (after_name.kind != TOK_LBRACE) parse_error_token(&after_name, "'{' after block name/label");
    consume_token(); /* consume '{' */

    Block *blk = malloc(sizeof(Block)); memset(blk,0,sizeof(Block));
    blk->name = name_tok.text;
    blk->label = label;
    blk->fields = NULL;
    blk->children = NULL;
    blk->next = NULL;
    blk->parent = parent;

    Field *lastf = NULL;
    Block *lastchild = NULL;

    for (;;) {
        Token cur = cur_token();
        if (cur.kind == TOK_RBRACE) { consume_token(); break; }
        if (cur.kind == TOK_EOF) parse_error_token(&cur, "unexpected EOF in block");

        /* typed field start */
        if (cur.kind == TOK_TYPE_INT || cur.kind == TOK_TYPE_FLOAT || cur.kind == TOK_TYPE_BOOL || cur.kind == TOK_TYPE_STRING) {
            Field *f = parse_field_from_type_token(cur.kind);
            if (!blk->fields) blk->fields = f; else lastf->next = f;
            lastf = f;
            continue;
        }

        /* identifier: could be inferred field or child block (with optional label) */
        if (cur.kind == TOK_IDENT) {
            /* safe peek next two tokens */
            Token n1 = peek_n_safe(1);
            Token n2 = peek_n_safe(2);

            int handled = 0;
            if (n1.kind == TOK_EQ) {
                token_free(&n1); token_free(&n2);
                Field *f = parse_field_with_type(NULL);
                if (!blk->fields) blk->fields = f; else lastf->next = f;
                lastf = f;
                handled = 1;
            } else if (n1.kind == TOK_LBRACE) {
                token_free(&n1); token_free(&n2);
                Block *child = parse_block_recursive(blk);
                if (!blk->children) blk->children = child; else lastchild->next = child;
                lastchild = child;
                handled = 1;
            } else if (n1.kind == TOK_STRING && n2.kind == TOK_LBRACE) {
                token_free(&n1); token_free(&n2);
                Block *child = parse_block_recursive(blk);
                if (!blk->children) blk->children = child; else lastchild->next = child;
                lastchild = child;
                handled = 1;
            } else {
                token_free(&n1); token_free(&n2);
            }

            if (handled) continue;
            parse_error_token(&cur, "expected '=' for field or '{' for child block");
        }

        parse_error_token(&cur, "typed field, inferred field, or child block");
    }

    return blk;
}

/* ---------- top-level parse ---------- */

static Block *parse_all(const char *text) {
    SRC = text;
    SRC_POS = 0;
    /* skip UTF-8 BOM if present */
    if (SRC_LEN == 0) SRC_LEN = strlen(SRC);
    if (SRC_POS+3 <= SRC_LEN && (unsigned char)SRC[0]==0xEF && (unsigned char)SRC[1]==0xBB && (unsigned char)SRC[2]==0xBF) SRC_POS = 3;
    SRC_LEN = strlen(SRC);
    LINE = 1; COL = 1;
    HAVE_BUF = 0; HAVE_SAVED = 0;

    Block *head = NULL, *last = NULL;
    for (;;) {
        Token t = cur_token();
        if (t.kind == TOK_EOF) break;
        if (t.kind == TOK_IDENT) {
            Block *b = parse_block_recursive(NULL);
            if (!head) head = b; else last->next = b;
            last = b;
            continue;
        }
        parse_error_token(&t, "top-level block name (identifier)");
    }
    return head;
}

/* ---------- printing/freeing ---------- */

static void print_value(const Value *v) {
    switch (v->kind) {
        case VAL_INT: printf("%ld", v->ival); break;
        case VAL_BOOL: printf(v->bval ? "true" : "false"); break;
        case VAL_STRING: printf("\"%s\"", v->sval ? v->sval : ""); break;
        case VAL_CHAR:
            if (v->cval == '\n') printf("'\\n'"); else if (v->cval == '\t') printf("'\\t'");
            else if (v->cval == '\\') printf("'\\\\'"); else if (v->cval == '\'') printf("'\\''");
            else printf("'%c'", (char)v->cval);
            break;
    }
}

static void print_block(const Block *b, int indent) {
    for (int i = 0; i < indent; ++i) printf("  ");
    if (b->label) printf("Block: %s  label: \"%s\"\n", b->name, b->label);
    else printf("Block: %s\n", b->name);
    for (const Field *f = b->fields; f; f = f->next) {
        for (int i = 0; i < indent; ++i) printf("  ");
        printf("  Field: %s  ", f->name);
        if (f->type) printf("(type: %s)  ", f->type); else printf("(type: inferred)  ");
        printf("value: ");
        print_value(&f->value);
        printf("\n");
    }
    for (const Block *c = b->children; c; c = c->next) print_block(c, indent+1);
}

static void print_all(const Block *root) {
    for (const Block *b = root; b; b = b->next) {
        print_block(b, 0);
        printf("\n");
    }
}

static void free_blocks(Block *b) {
    while (b) {
        Block *nb = b->next;
        if (b->name) free(b->name);
        if (b->label) free(b->label);
        for (Field *f = b->fields; f; ) {
            Field *nf = f->next;
            if (f->type) free(f->type);
            if (f->name) free(f->name);
            value_free(&f->value);
            free(f);
            f = nf;
        }
        if (b->children) free_blocks(b->children);
        free(b);
        b = nb;
    }
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
    char *text = NULL;
    if (argc >= 2) {
        const char *fn = argv[1];
        FILE *f = fopen(fn, "rb");
        if (!f) { perror("fopen"); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        text = malloc(sz+1);
        if (!text) { fclose(f); return 1; }
        if (fread(text, 1, sz, f) != (size_t)sz) { fclose(f); free(text); return 1; }
        text[sz] = '\0';
        fclose(f);
    } else {
        size_t cap = 4096, len = 0;
        text = malloc(cap);
        if (!text) return 1;
        int c;
        while ((c = getchar()) != EOF) {
            if (len+2 >= cap) { cap *= 2; text = realloc(text, cap); if (!text) return 1; }
            text[len++] = (char)c;
        }
        text[len] = '\0';
    }

    Block *root = parse_all(text);
    print_all(root);
    free_blocks(root);
    free(text);
    return 0;
}
