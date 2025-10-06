#ifndef ACL_H
#define ACL_H

#include <stdio.h>

/* Opaque types (mirror internal structures) */
typedef struct AclValue AclValue;
typedef struct AclField AclField;
typedef struct AclBlock AclBlock;
typedef struct AclError AclError;

/* Lifecycle (no-op for now) */
int acl_init(void);
void acl_shutdown(void);

/* Parse from file or in-memory string.
   Returns a heap-allocated AclBlock* (linked list of top-level blocks) on success,
   or NULL on failure (in which case an error may have been printed to stderr). */
AclBlock *acl_parse_file(const char *path);
AclBlock *acl_parse_string(const char *text);

/* Resolve references in-place. Returns 1 on success, 0 on failure. */
int acl_resolve_all(AclBlock *root);

/* Utilities */
void acl_print(AclBlock *root, FILE *out);

/* Free tree returned by parser */
void acl_free(AclBlock *root);

/* Error structure and helpers (placeholder; parser currently prints to stderr) */
struct AclError {
    int code;
    char *message;
    int line;
    int col;
    size_t pos;
};
void acl_error_free(AclError *err);

#endif
