// parser.c
// ACL2-like visual spec parser with nested sub-blocks and literals.
// Compile: gcc -std=c11 -O2 -o parser parser.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------- lexer ---------- */

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_INT,
    TOK_STRING,
    TOK_CHAR,
    TOK_BOOL,
    TOK_LBRACE, // {
    TOK_RBRACE, // }
    TOK_EQ,     // =
    TOK_SEMI,   // ;
    TOK_TYPE_INT,
    TOK_TYPE_FLOAT,
    TOK_TYPE_BOOL,
    TOK_TYPE_STRING,
    TOK_UNKNOWN
} TokenKind;

typedef struct {
    TokenKind kind;
    char *text;   // for ident/string
    long  ival;   // for int
    int   bval;   // for bool
    int   cval;   // for char
} Token;

static const char *src;
static size_t srcpos;
static size_t srclen;

static void skip_spaces_and_comments() {
    while (srcpos < srclen) {
        char c = src[srcpos];
        if (isspace((unsigned char)c)) { srcpos++; continue; }
        if (c == '/' && srcpos+1 < srclen && src[srcpos+1] == '/') {
            srcpos += 2;
            while (srcpos < srclen && src[srcpos] != '\n') srcpos++;
            continue;
        }
        if (c == '/' && srcpos+1 < srclen && src[srcpos+1] == '*') {
            srcpos += 2;
            while (srcpos+1 < srclen && !(src[srcpos]=='*' && src[srcpos+1]=='/')) srcpos++;
            if (srcpos+1 < srclen) srcpos += 2;
            continue;
        }
        break;
    }
}

static char peekc() {
    if (srcpos >= srclen) return '\0';
    return src[srcpos];
}

static char getc_src() {
    if (srcpos >= srclen) return '\0';
    return src[srcpos++];
}

static char *substr_range(size_t a, size_t b) {
    size_t n = b - a;
    char *s = malloc(n+1);
    if (!s) exit(1);
    memcpy(s, src + a, n);
    s[n] = '\0';
    return s;
}

static int parse_escape_char() {
    if (srcpos >= srclen) return '\\';
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

static Token next_token() {
    skip_spaces_and_comments();
    Token tk = { TOK_EOF, NULL, 0, 0, 0 };
    if (srcpos >= srclen) { tk.kind = TOK_EOF; return tk; }
    char c = peekc();

    if (c == '{') { srcpos++; tk.kind = TOK_LBRACE; return tk; }
    if (c == '}') { srcpos++; tk.kind = TOK_RBRACE; return tk; }
    if (c == '=') { srcpos++; tk.kind = TOK_EQ; return tk; }
    if (c == ';') { srcpos++; tk.kind = TOK_SEMI; return tk; }

    if (c == '"') {
        srcpos++; // skip "
        size_t cap = 128, len = 0;
        char *buf = malloc(cap);
        if (!buf) exit(1);
        while (srcpos < srclen) {
            char ch = getc_src();
            if (ch == '"') break;
            if (ch == '\\') {
                int ec = parse_escape_char();
                ch = (char)ec;
            }
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); if (!buf) exit(1); }
            buf[len++] = ch;
        }
        buf[len] = '\0';
        tk.kind = TOK_STRING;
        tk.text = buf;
        return tk;
    }

    if (c == '\'') {
        srcpos++; // skip '
        int ch;
        if (peekc() == '\\') {
            srcpos++; // skip backslash
            ch = parse_escape_char();
        } else {
            ch = getc_src();
        }
        if (peekc() == '\'') srcpos++;
        tk.kind = TOK_CHAR;
        tk.cval = ch;
        return tk;
    }

    if (isalpha((unsigned char)c) || c == '_') {
        size_t a = srcpos;
        srcpos++;
        while (srcpos < srclen && (isalnum((unsigned char)src[srcpos]) || src[srcpos] == '_')) srcpos++;
        char *s = substr_range(a, srcpos);
        if (strcmp(s, "int") == 0) { free(s); tk.kind = TOK_TYPE_INT; return tk; }
        if (strcmp(s, "float") == 0) { free(s); tk.kind = TOK_TYPE_FLOAT; return tk; }
        if (strcmp(s, "bool") == 0) { free(s); tk.kind = TOK_TYPE_BOOL; return tk; }
        if (strcmp(s, "string") == 0) { free(s); tk.kind = TOK_TYPE_STRING; return tk; }
        if (strcmp(s, "true") == 0) { free(s); tk.kind = TOK_BOOL; tk.bval = 1; return tk; }
        if (strcmp(s, "false") == 0) { free(s); tk.kind = TOK_BOOL; tk.bval = 0; return tk; }
        tk.kind = TOK_IDENT;
        tk.text = s;
        return tk;
    }

    if (isdigit((unsigned char)c) || (c=='-' && srcpos+1 < srclen && isdigit((unsigned char)src[srcpos+1]))) {
        size_t a = srcpos;
        if (src[srcpos] == '-') srcpos++;
        while (srcpos < srclen && isdigit((unsigned char)src[srcpos])) srcpos++;
        char *s = substr_range(a, srcpos);
        tk.kind = TOK_INT;
        tk.ival = strtol(s, NULL, 10);
        free(s);
        return tk;
    }

    srcpos++;
    tk.kind = TOK_UNKNOWN;
    return tk;
}

/* ---------- parser structures ---------- */

typedef enum { VAL_INT, VAL_BOOL, VAL_STRING, VAL_CHAR } ValKind;

typedef struct Value {
    ValKind kind;
    long  ival;
    int   bval;
    char *sval;
    int   cval;
} Value;

typedef struct Field {
    char *type;   // NULL for inferred
    char *name;
    Value val;
    struct Field *next;
} Field;

typedef struct Block {
    char *name;           // block name (identifier)
    char *label;          // optional label (string), NULL if absent
    Field *fields;
    struct Block *children; // linked list of child blocks
    struct Block *next;     // next sibling block in same list
    struct Block *parent;   // parent block or NULL
} Block;

typedef struct {
    Token cur;
    int have_cur;
} Parser;

static Parser P;

static void tk_free(Token *t) {
    if (!t) return;
    if (t->text) free(t->text);
    t->text = NULL;
}

static void advance() {
    if (P.have_cur) {
        tk_free(&P.cur);
        P.have_cur = 0;
    }
    P.cur = next_token();
    P.have_cur = 1;
}

static int accept(TokenKind k) {
    if (!P.have_cur) advance();
    return P.cur.kind == k;
}

static int expect(TokenKind k, const char *msg) {
    if (!P.have_cur) advance();
    if (P.cur.kind == k) return 1;
    fprintf(stderr, "Parse error: expected token %d but got %d. %s\n", (int)k, (int)P.cur.kind, msg?msg:"");
    exit(1);
}

static char *dupstr(const char *s) { return s ? strdup(s) : NULL; }

/* ---------- literal expression parsing ---------- */

static Value parse_literal_expr() {
    if (!P.have_cur) advance();
    Token tk = P.cur;
    Value v;
    memset(&v,0,sizeof(v));
    if (tk.kind == TOK_INT) {
        v.kind = VAL_INT;
        v.ival = tk.ival;
        advance();
        return v;
    } else if (tk.kind == TOK_BOOL) {
        v.kind = VAL_BOOL;
        v.bval = tk.bval;
        advance();
        return v;
    } else if (tk.kind == TOK_STRING) {
        v.kind = VAL_STRING;
        v.sval = strdup(tk.text);
        advance();
        return v;
    } else if (tk.kind == TOK_CHAR) {
        v.kind = VAL_CHAR;
        v.cval = tk.cval;
        advance();
        return v;
    } else {
        fprintf(stderr, "Parse error: expected literal expression, got token kind %d\n", (int)tk.kind);
        exit(1);
    }
}

/* ---------- field parsing ---------- */

static Field *parse_field(int type_expected) {
    char *type = NULL;
    if (type_expected) {
        if (P.cur.kind == TOK_TYPE_INT) type = dupstr("int");
        else if (P.cur.kind == TOK_TYPE_FLOAT) type = dupstr("float");
        else if (P.cur.kind == TOK_TYPE_BOOL) type = dupstr("bool");
        else if (P.cur.kind == TOK_TYPE_STRING) type = dupstr("string");
        else { fprintf(stderr, "internal parser error: unexpected type token\n"); exit(1); }
        advance();
    }

    if (!P.have_cur) advance();
    if (P.cur.kind != TOK_IDENT) {
        fprintf(stderr, "Parse error: expected identifier for field name\n");
        exit(1);
    }
    char *name = strdup(P.cur.text);
    advance();

    expect(TOK_EQ, "after field name");
    advance();

    Value v = parse_literal_expr();

    expect(TOK_SEMI, "after field expression");
    advance();

    Field *f = malloc(sizeof(Field));
    memset(f,0,sizeof(Field));
    f->type = type;
    f->name = name;
    f->val = v;
    f->next = NULL;
    return f;
}

/* ---------- block parsing (with sub-blocks) ---------- */

static Block *make_block(const char *name, const char *label, Block *parent) {
    Block *b = malloc(sizeof(Block));
    memset(b,0,sizeof(Block));
    b->name = dupstr(name);
    b->label = dupstr(label);
    b->fields = NULL;
    b->children = NULL;
    b->next = NULL;
    b->parent = parent;
    return b;
}

/* parse a single block starting at an identifier (the current token should be IDENT) */
static Block *parse_block_recursive(Block *parent) {
    if (!P.have_cur) advance();
    if (P.cur.kind != TOK_IDENT) {
        fprintf(stderr, "Parse error: expected block name identifier\n");
        exit(1);
    }
    char *bname = strdup(P.cur.text);
    advance();

    /* optional label: a string literal immediately after the identifier */
    char *label = NULL;
    if (!P.have_cur) advance();
    if (P.cur.kind == TOK_STRING) {
        label = strdup(P.cur.text);
        advance();
    }

    expect(TOK_LBRACE, "after block name/label");
    advance();

    Block *blk = make_block(bname, label, parent);
    free(bname); if (label) free(label);

    Field *lastf = NULL;
    Block *lastchild = NULL;

    while (1) {
        if (!P.have_cur) advance();
        if (P.cur.kind == TOK_RBRACE) {
            advance();
            break;
        }
        if (P.cur.kind == TOK_EOF) {
            fprintf(stderr, "Parse error: unexpected EOF inside block\n");
            exit(1);
        }

        /* detect typed field */
        if (P.cur.kind == TOK_TYPE_INT || P.cur.kind == TOK_TYPE_FLOAT ||
            P.cur.kind == TOK_TYPE_BOOL || P.cur.kind == TOK_TYPE_STRING) {
            Field *f = parse_field(1);
            if (!blk->fields) blk->fields = f; else lastf->next = f;
            lastf = f;
            continue;
        }

        /* detect inferred field (ident = expr;) */
        if (P.cur.kind == TOK_IDENT) {
            /* but IDENT followed by STRING then LBRACE indicates a child block with label */
            Token look = P.cur;
            // peek ahead without consuming by saving state
            size_t save_pos = srcpos;
            TokenKind save_kind = P.cur.kind;
            char *save_text = P.cur.text ? strdup(P.cur.text) : NULL;
            int have_save = P.have_cur;

            // to determine if it's a child block, need to check next token(s)
            advance(); // consume IDENT
            if (!P.have_cur) advance();
            int is_child = 0;
            if (P.cur.kind == TOK_STRING) {
                // could be labeled child; check next token
                advance();
                if (!P.have_cur) advance();
                if (P.cur.kind == TOK_LBRACE) is_child = 1;
            } else if (P.cur.kind == TOK_LBRACE) {
                is_child = 1;
            }

            // restore parser state to before IDENT
            // free current token text and restore P.cur
            if (P.have_cur) tk_free(&P.cur);
            P.have_cur = have_save;
            srcpos = save_pos;
            if (P.cur.text) { free(P.cur.text); P.cur.text = NULL; }
            P.cur.kind = save_kind;
            P.cur.text = save_text;

            if (is_child) {
                Block *child = parse_block_recursive(blk); // parse child with parent
                if (!blk->children) blk->children = child; else lastchild->next = child;
                lastchild = child;
                continue;
            } else {
                // treat as inferred field (identifier regained)
                Field *f = parse_field(0);
                if (!blk->fields) blk->fields = f; else lastf->next = f;
                lastf = f;
                continue;
            }
        }

        fprintf(stderr, "Parse error: unexpected token inside block (kind %d)\n", (int)P.cur.kind);
        exit(1);
    }

    return blk;
}

/* parse top-level sequence of blocks */
static Block *parse_all() {
    Block *head = NULL, *last = NULL;
    while (1) {
        if (!P.have_cur) advance();
        if (P.cur.kind == TOK_EOF) break;
        if (P.cur.kind == TOK_IDENT) {
            Block *b = parse_block_recursive(NULL);
            if (!head) head = b; else last->next = b;
            last = b;
            continue;
        }
        fprintf(stderr, "Parse error: expected top-level block name\n");
        exit(1);
    }
    return head;
}

/* ---------- printing ---------- */

static void print_value(const Value *v) {
    switch (v->kind) {
        case VAL_INT: printf("%ld", v->ival); break;
        case VAL_BOOL: printf(v->bval ? "true" : "false"); break;
        case VAL_STRING: printf("\"%s\"", v->sval ? v->sval : ""); break;
        case VAL_CHAR:
            if (v->cval == '\n') printf("'\\n'");
            else if (v->cval == '\t') printf("'\\t'");
            else if (v->cval == '\r') printf("'\\r'");
            else if (v->cval == '\\') printf("'\\\\'");
            else if (v->cval == '\'') printf("'\\''");
            else printf("'%c'", (char)v->cval);
            break;
        default: printf("<unknown>"); break;
    }
}

static void print_block_recursive(const Block *blk, int indent) {
    for (int i = 0; i < indent; ++i) printf("  ");
    if (blk->label)
        printf("Block: %s  label: \"%s\"\n", blk->name, blk->label);
    else
        printf("Block: %s\n", blk->name);

    for (const Field *f = blk->fields; f; f = f->next) {
        for (int i = 0; i < indent; ++i) printf("  ");
        printf("  Field: %s  ", f->name);
        if (f->type) printf("(type: %s)  ", f->type); else printf("(type: inferred)  ");
        printf("value: ");
        print_value(&f->val);
        printf("\n");
    }

    for (const Block *c = blk->children; c; c = c->next) {
        print_block_recursive(c, indent + 1);
    }
}

static void print_blocks(const Block *b) {
    for (const Block *blk = b; blk; blk = blk->next) {
        print_block_recursive(blk, 0);
        printf("\n");
    }
}

/* ---------- utilities and freeing ---------- */

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    if (fread(buf,1,sz,fp) != (size_t)sz) { free(buf); fclose(fp); return NULL; }
    buf[sz] = '\0';
    fclose(fp);
    return buf;
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
            if (f->val.kind == VAL_STRING && f->val.sval) free(f->val.sval);
            free(f);
            f = nf;
        }
        if (b->children) {
            free_blocks(b->children);
        }
        free(b);
        b = nb;
    }
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
    char *text = NULL;
    if (argc >= 2) {
        text = read_file(argv[1]);
        if (!text) { fprintf(stderr, "Failed to read %s\n", argv[1]); return 1; }
    } else {
        size_t cap = 4096; size_t len = 0;
        text = malloc(cap);
        if (!text) return 1;
        int c;
        while ((c = getchar()) != EOF) {
            if (len+2 >= cap) { cap *= 2; text = realloc(text, cap); if (!text) return 1; }
            text[len++] = (char)c;
        }
        text[len] = '\0';
    }

    src = text;
    srcpos = 0;
    srclen = strlen(src);
    P.have_cur = 0;

    Block *root = parse_all();

    print_blocks(root);

    free_blocks(root);

    if (P.have_cur) tk_free(&P.cur);
    free(text);
    return 0;
}