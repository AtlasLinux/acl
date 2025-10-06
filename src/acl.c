// parser.c
// Single-file ACL2-like visual spec parser with reference parsing and resolution.
// References parsed: $Path.to.field, $.local.field, ^field (with repeated '^'), and ["name"] indexing.
// After parsing the AST, resolve_all_refs(root) is called to replace resolvable VAL_REF
// values with deep-copied resolved literal values. Ambiguous matches favor the first.
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
    TOK_COMMA,
    TOK_LBRACK,
    TOK_RBRACK,

    TOK_DOLLAR,
    TOK_DOT,
    TOK_CARET,

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

    /* punctuation */
    if (c == '{') { getc_src(); tk.kind = TOK_LBRACE; return tk; }
    if (c == '}') { getc_src(); tk.kind = TOK_RBRACE; return tk; }
    if (c == '=') { getc_src(); tk.kind = TOK_EQ; return tk; }
    if (c == ';') { getc_src(); tk.kind = TOK_SEMI; return tk; }
    if (c == ',') { getc_src(); tk.kind = TOK_COMMA; return tk; }
    if (c == '[') { getc_src(); tk.kind = TOK_LBRACK; return tk; }
    if (c == ']') { getc_src(); tk.kind = TOK_RBRACK; return tk; }
    if (c == '$') { getc_src(); tk.kind = TOK_DOLLAR; return tk; }
    if (c == '.') { getc_src(); tk.kind = TOK_DOT; return tk; }
    if (c == '^') { getc_src(); tk.kind = TOK_CARET; return tk; }

    /* string literal */
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

    /* char literal */
    if (c == '\'') {
        getc_src();
        int ch;
        if (peekc() == '\\') { getc_src(); ch = parse_escape_char(); } else ch = getc_src();
        if (peekc() == '\'') getc_src();
        tk.kind = TOK_CHAR; tk.cval = ch;
        return tk;
    }

    /* identifier or type keyword or bool literal */
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

    /* integer literal (simple) */
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

    /* unknown char */
    getc_src();
    tk.kind = TOK_UNKNOWN;
    return tk;
}

static void token_free(Token *t) { if (!t) return; if (t->text) free(t->text); t->text = NULL; }

/* ---------- parser buffer + safe snapshot lookahead ---------- */

static Token BUF = {0};
static int HAVE_BUF = 0;
static Token SAVED = {0};
static int HAVE_SAVED = 0;

static Token get_token_shared(void) {
    if (HAVE_SAVED) {
        Token t = SAVED;
        HAVE_SAVED = 0;
        return t;
    }
    return next_token_internal();
}

static void push_token_shared(Token t) {
    if (HAVE_SAVED) token_free(&SAVED);
    SAVED = t;
    HAVE_SAVED = 1;
}

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
static Token take_token(void) {
    Token t = cur_token();
    Token c = t;
    if (c.text) c.text = str_dup_local(c.text);
    consume_token();
    return c;
}

typedef struct {
    size_t src_pos;
    int line;
    int col;
    int have_saved;
    Token saved_copy;
    int have_buf;
    Token buf_copy;
} Snapshot;

static void snapshot_begin(Snapshot *S) {
    S->src_pos = SRC_POS;
    S->line = LINE;
    S->col = COL;
    S->have_saved = HAVE_SAVED;
    if (S->have_saved) {
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
    if (HAVE_BUF) token_free(&BUF);
    if (HAVE_SAVED) token_free(&SAVED);
    SRC_POS = S->src_pos;
    LINE = S->line;
    COL = S->col;
    if (S->have_saved) {
        SAVED = S->saved_copy; HAVE_SAVED = 1;
    } else { HAVE_SAVED = 0; memset(&SAVED,0,sizeof(Token)); }
    if (S->have_buf) {
        BUF = S->buf_copy; HAVE_BUF = 1;
    } else { HAVE_BUF = 0; memset(&BUF,0,sizeof(Token)); }
}

/* peek n tokens ahead (n>=1), return owned copy (caller must token_free) */
static Token peek_n_safe(int n) {
    Snapshot S; snapshot_begin(&S);
    Token out = {0};
    for (int i = 0; i < n; ++i) {
        Token t = get_token_shared();
        out = t;
        if (out.text) out.text = str_dup_local(out.text);
        if (i < n-1) token_free(&out);
    }
    snapshot_restore(&S);
    return out;
}
static Token peek1(void) { return peek_n_safe(1); }
static Token peek2(void) { return peek_n_safe(2); }

/* ---------- error reporting ---------- */

static void show_line_context(size_t pos, int line, int col) {
    (void)line; /* unused */
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

/* ---------- values, references, and AST ---------- */

/* Reference representation */
typedef enum { REF_GLOBAL, REF_LOCAL, REF_PARENT } RefScope;
typedef struct RefSeg { char *name; int is_index; char *index; struct RefSeg *next; } RefSeg;
typedef struct {
    RefScope scope;
    int parent_levels; /* number of ^ prefixes for parent refs (>=1) */
    RefSeg *head;      /* linked list of segments (name or index) */
} Ref;

/* Value kinds (extended with VAL_REF and VAL_ARRAY) */
typedef enum { VAL_INT, VAL_BOOL, VAL_STRING, VAL_CHAR, VAL_ARRAY, VAL_REF } ValKind;

typedef struct ValueItem ValueItem;
typedef struct Value {
    ValKind kind;
    long  ival;
    int   bval;
    char *sval;
    int   cval;

    /* arrays */
    ValueItem *arr;
    size_t arr_len;

    /* ref */
    Ref *ref;
} Value;
typedef struct ValueItem { Value v; struct ValueItem *next; } ValueItem;


static Value make_int(long x) { Value v; memset(&v,0,sizeof(v)); v.kind = VAL_INT; v.ival = x; return v; }
static Value make_bool(int b) { Value v; memset(&v,0,sizeof(v)); v.kind = VAL_BOOL; v.bval = b?1:0; return v; }
static Value make_string_owned(char *s) { Value v; memset(&v,0,sizeof(v)); v.kind = VAL_STRING; v.sval = s; return v; }
static Value make_char(int c) { Value v; memset(&v,0,sizeof(v)); v.kind = VAL_CHAR; v.cval = c; return v; }
static Value make_array(void) { Value v; memset(&v,0,sizeof(v)); v.kind = VAL_ARRAY; v.arr = NULL; v.arr_len = 0; return v; }
static Value make_ref(Ref *r) { Value v; memset(&v,0,sizeof(v)); v.kind = VAL_REF; v.ref = r; return v; }

/* helpers for ref segments */
static RefSeg *refseg_create_name(const char *name) {
    RefSeg *s = malloc(sizeof(*s));
    s->name = name ? str_dup_local(name) : NULL;
    s->is_index = 0;
    s->index = NULL;
    s->next = NULL;
    return s;
}
static RefSeg *refseg_create_index(const char *idx) {
    RefSeg *s = malloc(sizeof(*s));
    s->name = NULL;
    s->is_index = 1;
    s->index = idx ? str_dup_local(idx) : NULL;
    s->next = NULL;
    return s;
}
static void refseg_free(RefSeg *s) {
    while (s) {
        RefSeg *n = s->next;
        if (s->name) free(s->name);
        if (s->index) free(s->index);
        free(s);
        s = n;
    }
}

/* ref creation / free */
static Ref *ref_create(RefScope scope) {
    Ref *r = malloc(sizeof(*r));
    r->scope = scope;
    r->parent_levels = 0;
    r->head = NULL;
    return r;
}
static void ref_free(Ref *r) {
    if (!r) return;
    if (r->head) refseg_free(r->head);
    free(r);
}

/* free Value (deep) */
static void value_free(Value *v) {
    if (!v) return;
    if (v->kind == VAL_STRING && v->sval) { free(v->sval); v->sval = NULL; }
    if (v->kind == VAL_ARRAY) {
        ValueItem *it = v->arr;
        while (it) {
            ValueItem *n = it->next;
            value_free(&it->v);
            free(it);
            it = n;
        }
        v->arr = NULL;
        v->arr_len = 0;
    }
    if (v->kind == VAL_REF) {
        if (v->ref) { ref_free(v->ref); v->ref = NULL; }
    }
}

/* append to array value (takes ownership of item) */
static void array_append(Value *arrv, Value item) {
    if (!arrv || arrv->kind != VAL_ARRAY) return;
    ValueItem *node = malloc(sizeof(*node));
    node->v = item;
    node->next = NULL;
    if (!arrv->arr) arrv->arr = node;
    else {
        ValueItem *it = arrv->arr;
        while (it->next) it = it->next;
        it->next = node;
    }
    arrv->arr_len++;
}

/* ---------- printing values (including refs and arrays) ---------- */

static void print_ref(const Ref *r) {
    if (!r) { printf("<ref:null>"); return; }
    if (r->scope == REF_GLOBAL) printf("$");
    else if (r->scope == REF_LOCAL) printf("$."); 
    else if (r->scope == REF_PARENT) {
        for (int i = 0; i < r->parent_levels; ++i) printf("^");
    }
    RefSeg *s = r->head;
    int first = 1;
    while (s) {
        if (!first && (s->is_index == 0)) {
            printf(".");
        }
        if (s->is_index) {
            printf("[\"%s\"]", s->index ? s->index : "");
        } else {
            printf("%s", s->name ? s->name : "");
        }
        first = 0;
        s = s->next;
    }
}

static void print_value(const Value *v) {
    if (!v) return;
    switch (v->kind) {
        case VAL_INT: printf("%ld", v->ival); break;
        case VAL_BOOL: printf(v->bval ? "true" : "false"); break;
        case VAL_STRING: printf("\"%s\"", v->sval ? v->sval : ""); break;
        case VAL_CHAR:
            if (v->cval == '\n') printf("'\\n'"); else if (v->cval == '\t') printf("'\\t'");
            else if (v->cval == '\\') printf("'\\\\'"); else if (v->cval == '\'') printf("'\\''");
            else printf("'%c'", (char)v->cval);
            break;
        case VAL_ARRAY: {
            printf("[");
            ValueItem *it = v->arr;
            int first = 1;
            while (it) {
                if (!first) printf(", ");
                print_value(&it->v);
                first = 0;
                it = it->next;
            }
            printf("]");
            break;
        }
        case VAL_REF:
            print_ref(v->ref);
            break;
    }
}

/* ---------- AST: fields and blocks ---------- */

typedef struct Field { char *type; char *name; Value value; struct Field *next; } Field;
typedef struct Block { char *name; char *label; Field *fields; struct Block *children; struct Block *next; struct Block *parent; } Block;

/* ---------- reference parsing helpers ---------- */

/* parse path tail: .ident or ["index"] repeated; returns head RefSeg list (owned) */
static RefSeg *parse_ref_path_segments(void) {
    RefSeg *head = NULL;
    RefSeg **tail = &head;
    while (1) {
        Token t = cur_token();
        if (t.kind == TOK_DOT) {
            consume_token(); /* consume '.' */
            Token id = cur_token();
            if (id.kind != TOK_IDENT) parse_error_token(&id, "identifier after '.' in reference");
            Token idtok = take_token(); /* owned copy of ident */
            RefSeg *s = refseg_create_name(idtok.text);
            /* idtok.text transferred into s->name, do not free idtok.text below */
            *tail = s; tail = &s->next;
            token_free(&idtok);
            continue;
        } else if (t.kind == TOK_LBRACK) {
            consume_token(); /* consume '[' */
            Token idx = cur_token();
            if (idx.kind != TOK_STRING) parse_error_token(&idx, "string index in reference [\"name\"]");
            Token idxtok = take_token(); /* owns string */
            Token rb = cur_token();
            if (rb.kind != TOK_RBRACK) parse_error_token(&rb, "']' after string index in reference");
            consume_token(); /* consume ']' */
            RefSeg *s = refseg_create_index(idxtok.text);
            *tail = s; tail = &s->next;
            continue;
        }
        break;
    }
    return head;
}

/* parse a reference value starting at current token (handles $, $., and ^) */
static Value parse_reference_value(void) {
    Token t = cur_token();
    if (t.kind == TOK_DOLLAR) {
        consume_token(); /* consume $ */
        Token next = cur_token();
        Ref *r;
        if (next.kind == TOK_DOT) {
            /* local: $.field.path */
            consume_token(); /* consume '.' */
            r = ref_create(REF_LOCAL);
            Token id = cur_token();
            if (id.kind != TOK_IDENT) parse_error_token(&id, "identifier after '$.'");
            Token idtok = take_token();
            RefSeg *head = refseg_create_name(idtok.text);
            token_free(&idtok);
            RefSeg *rest = parse_ref_path_segments();
            if (rest) head->next = rest;
            r->head = head;
            return make_ref(r);
        } else {
            /* global: $Name.path or $Name["label"].field... */
            r = ref_create(REF_GLOBAL);
            Token id = cur_token();
            if (id.kind != TOK_IDENT) parse_error_token(&id, "identifier after '$'");
            Token idtok = take_token();
            RefSeg *head = refseg_create_name(idtok.text);
            token_free(&idtok);
            RefSeg *rest = parse_ref_path_segments();
            if (rest) head->next = rest;
            r->head = head;
            return make_ref(r);
        }
    } else if (t.kind == TOK_CARET) {
        int levels = 0;
        while (cur_token().kind == TOK_CARET) { consume_token(); levels++; }
        Ref *r = ref_create(REF_PARENT);
        r->parent_levels = levels;
        Token id = cur_token();
        if (id.kind != TOK_IDENT) parse_error_token(&id, "identifier after '^' in parent reference");
        Token idtok = take_token();
        RefSeg *head = refseg_create_name(idtok.text);
        token_free(&idtok);
        RefSeg *rest = parse_ref_path_segments();
        if (rest) head->next = rest;
        r->head = head;
        return make_ref(r);
    }
    parse_error_token(&t, "reference starting with '$' or '^'");
    return make_int(0);
}

/* ---------- literal parsing (with arrays and refs) ---------- */

static Value parse_literal_value_final(void); /* forward */

static Value parse_array_literal_final(void) {
    consume_token(); /* consume '{' */
    Value arr = make_array();
    Token next = cur_token();
    if (next.kind == TOK_RBRACE) { consume_token(); return arr; }
    while (1) {
        Value item = parse_literal_value_final();
        array_append(&arr, item);
        Token sep = cur_token();
        if (sep.kind == TOK_COMMA) { consume_token(); continue; }
        if (sep.kind == TOK_RBRACE) { consume_token(); break; }
        parse_error_token(&sep, "',' or '}' in array literal");
    }
    return arr;
}

static Value parse_literal_value_final(void) {
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
    if (t.kind == TOK_LBRACE) return parse_array_literal_final();
    if (t.kind == TOK_DOLLAR || t.kind == TOK_CARET) return parse_reference_value();
    /* support local $. handled in parse_reference_value */

    parse_error_token(&t, "literal (int, bool, string, char, array, or reference)");
    return make_int(0);
}

/* ---------- field parsing ---------- */

static Field *parse_field_with_type(const char *type_name) {
    Token t = cur_token();
    if (t.kind != TOK_IDENT) parse_error_token(&t, "field name (identifier)");
    Token name_tok = take_token();

    Token eq = cur_token();
    if (eq.kind != TOK_EQ) parse_error_token(&eq, "'=' after field name");
    consume_token();

    Value v = parse_literal_value_final();

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

/* typed field that optionally supports array type with [] after type token */
static Field *parse_field_from_type_token(TokenKind tk_type) {
    const char *type_name = NULL;
    if (tk_type == TOK_TYPE_INT) type_name = "int";
    else if (tk_type == TOK_TYPE_FLOAT) type_name = "float";
    else if (tk_type == TOK_TYPE_BOOL) type_name = "bool";
    else if (tk_type == TOK_TYPE_STRING) type_name = "string";
    consume_token(); /* consume type token */

    /* optional [] after type token */
    Token nxt = cur_token();
    if (nxt.kind == TOK_LBRACK) {
        consume_token();
        Token r = cur_token();
        if (r.kind != TOK_RBRACK) parse_error_token(&r, "']' after '[' in type[]");
        consume_token();
    }

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
            Token n1 = peek1();
            Token n2 = peek2();

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
    SRC_LEN = strlen(SRC);
    /* skip UTF-8 BOM if present */
    if (SRC_POS+3 <= SRC_LEN && (unsigned char)SRC[0]==0xEF && (unsigned char)SRC[1]==0xBB && (unsigned char)SRC[2]==0xBF) SRC_POS = 3;
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

/* ---------- resolution helpers ---------- */

/* deep copy value (owned copy) */
static Value value_deep_copy(const Value *v) {
    Value r; memset(&r,0,sizeof(r));
    r.kind = v->kind;
    r.ival = v->ival;
    r.bval = v->bval;
    r.cval = v->cval;
    r.sval = NULL;
    r.arr = NULL;
    r.arr_len = 0;
    r.ref = NULL;
    if (v->kind == VAL_STRING && v->sval) r.sval = str_dup_local(v->sval);
    if (v->kind == VAL_ARRAY) {
        ValueItem *it = v->arr;
        while (it) {
            array_append(&r, value_deep_copy(&it->v));
            it = it->next;
        }
    }
    if (v->kind == VAL_REF && v->ref) {
        /* copy ref structure so unresolved refs remain independent */
        Ref *rf = ref_create(v->ref->scope);
        rf->parent_levels = v->ref->parent_levels;
        RefSeg *src = v->ref->head;
        RefSeg **tail = &rf->head;
        while (src) {
            if (src->is_index) *tail = refseg_create_index(src->index);
            else *tail = refseg_create_name(src->name);
            tail = &(*tail)->next;
            src = src->next;
        }
        r.ref = rf;
    }
    return r;
}

/* find first child block of `blk` with given name (favor first) */
static Block *find_child_by_name(Block *blk, const char *name) {
    if (!blk) return NULL;
    for (Block *c = blk->children; c; c = c->next) {
        if (c->name && name && strcmp(c->name, name) == 0) return c;
    }
    return NULL;
}

/* find first child block of `blk` with given name and label */
static Block *find_child_by_name_and_label(Block *blk, const char *name, const char *label) {
    if (!blk) return NULL;
    for (Block *c = blk->children; c; c = c->next) {
        if (c->name && name && strcmp(c->name, name) == 0) {
            if (c->label && label && strcmp(c->label, label) == 0) return c;
        }
    }
    return NULL;
}

/* find field by name in block (favor first) */
static Field *find_field_in_block(Block *blk, const char *name) {
    if (!blk) return NULL;
    for (Field *f = blk->fields; f; f = f->next) {
        if (f->name && name && strcmp(f->name, name) == 0) return f;
    }
    return NULL;
}

/* resolve a Ref into a Value copy, given root list and current block context.
   Returns 1 on success and stores copied value in out (caller must value_free), 0 on failure.
   Ambiguities favor first match. depth limits prevent runaway recursion. */
static int resolve_ref_to_value(const Block *root_list, const Block *current_block, const Ref *r, Value *out, int depth) {
    if (!r || !out) return 0;
    if (depth > 64) return 0;

    memset(out, 0, sizeof(*out));

    /* helper: copy field value into out if found */
    int try_field_copy(const Block *blk, const char *field_name) {
        if (!blk || !field_name) return 0;
        Field *f = find_field_in_block((Block *)blk, field_name);
        if (!f) return 0;
        *out = value_deep_copy(&f->value);
        return 1;
    }

    /* start position */
    const Block *pos = NULL;
    RefSeg *seg = r->head;

    if (r->scope == REF_GLOBAL) {
        /* global: first segment must be a name (not index) */
        if (!seg || seg->is_index) return 0;
        /* find top-level block with that name (scan root_list linked list) */
        const Block *b = root_list;
        while (b) {
            if (b->name && seg->name && strcmp(b->name, seg->name) == 0) { pos = b; break; }
            b = b->next;
        }
        if (!pos) return 0;
        seg = seg->next; /* consumed first name */
    } else if (r->scope == REF_LOCAL) {
        if (!current_block) return 0;
        pos = current_block;
    } else { /* REF_PARENT */
        if (!current_block) return 0;
        pos = current_block;
        for (int i = 0; i < r->parent_levels; ++i) {
            if (!pos->parent) { pos = NULL; break; }
            pos = pos->parent;
        }
        if (!pos) return 0;
    }

    /* Walk remaining segments */
    while (seg) {
        if (!pos) return 0;

        if (seg->is_index) {
            /* index selects first child with matching label */
            const Block *found = NULL;
            for (const Block *c = pos->children; c; c = c->next) {
                if (c->label && seg->index && strcmp(c->label, seg->index) == 0) { found = c; break; }
            }
            pos = found;
            seg = seg->next;
            continue;
        }

        /* seg is a name */
        RefSeg *next = seg->next;
        if (next && next->is_index) {
            /* name + index: select child by (name, label) */
            const char *lbl = next->index;
            const Block *found = NULL;
            for (const Block *c = pos->children; c; c = c->next) {
                if (c->name && seg->name && strcmp(c->name, seg->name) == 0) {
                    if (c->label && lbl && strcmp(c->label, lbl) == 0) { found = c; break; }
                }
            }
            pos = found;
            seg = next->next; /* consumed both */
            continue;
        } else {
            /* name only: pick first child block with that name */
            const Block *child = NULL;
            for (const Block *c = pos->children; c; c = c->next) {
                if (c->name && seg->name && strcmp(c->name, seg->name) == 0) { child = c; break; }
            }
            if (child) {
                pos = child;
                seg = seg->next;
                continue;
            }
            /* no child found; if this is final segment, treat as field name in pos */
            if (seg->next == NULL) {
                if (try_field_copy(pos, seg->name)) return 1;
                return 0;
            }
            /* intermediate named segment not found -> fail */
            return 0;
        }
    }

    /* If we consumed all segments and ended on a block, that's not a field value -> fail */
    return 0;
}

/* attempt to resolve a single Value if it's VAL_REF; uses block context (the block that owns the field).
   Returns 1 if replaced (and out value is set into field), 0 if nothing changed. */
static int try_resolve_value_for_field(const Block *root_list, Block *field_block, Value *v, int depth) {
    if (!v) return 0;
    if (v->kind != VAL_REF) return 0;
    Value resolved;
    if (resolve_ref_to_value(root_list, field_block, v->ref, &resolved, depth+1)) {
        /* replace v with resolved (take ownership of resolved) */
        /* free original ref */
        value_free(v);
        *v = resolved;
        return 1;
    }
    return 0;
}

/* Walk tree and resolve all field VAL_REF values, using containing block as context.
   This is iterative but will attempt to resolve nested references by multiple passes up to a limit. */
static void resolve_all_refs(Block *root) {
    if (!root) return;
    /* gather list of blocks for easy traversal (top-down) */
    /* We'll perform multiple passes until no changes or max passes reached */
    const int MAX_PASSES = 16;
    for (int pass = 0; pass < MAX_PASSES; ++pass) {
        int any_changed = 0;
        /* traverse all blocks */
        Block *b = root;
        while (b) {
            /* traverse recursively children via stack */
            /* We'll implement simple DFS using array stack */
            size_t stack_cap = 64;
            Block **stack = malloc(sizeof(Block*) * stack_cap);
            size_t stack_len = 0;
            stack[stack_len++] = b;
            while (stack_len) {
                Block *cur = stack[--stack_len];
                /* push children */
                for (Block *c = cur->children; c; c = c->next) {
                    if (stack_len + 1 > stack_cap) {
                        stack_cap *= 2;
                        stack = realloc(stack, sizeof(Block*) * stack_cap);
                    }
                    stack[stack_len++] = c;
                }
                /* resolve fields in cur */
                for (Field *f = cur->fields; f; f = f->next) {
                    if (f->value.kind == VAL_REF) {
                        int changed = try_resolve_value_for_field(root, cur, &f->value, 0);
                        any_changed |= changed;
                    } else if (f->value.kind == VAL_ARRAY) {
                        /* arrays may contain refs: resolve elements */
                        int changed = 0;
                        ValueItem *it = f->value.arr;
                        while (it) {
                            if (it->v.kind == VAL_REF) {
                                changed |= try_resolve_value_for_field(root, cur, &it->v, 0);
                            }
                            it = it->next;
                        }
                        any_changed |= changed;
                    }
                }
            }
            free(stack);
            b = b->next;
        }
        if (!any_changed) break;
    }
}

/* ---------- printing/freeing ---------- */

static void print_block(const Block *b, int indent);
static void print_all(const Block *root) {
    for (const Block *b = root; b; b = b->next) {
        print_block(b, 0);
        printf("\n");
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
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input-file>\n", argv[0]);
        return 1;
    }
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

    Block *root = parse_all(text);

    /* resolve references in-place */
    resolve_all_refs(root);

    print_all(root);
    free_blocks(root);
    free(text);
    return 0;
}
