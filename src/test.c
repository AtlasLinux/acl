#include "acl.h"
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input.acl>\n", argv[0]);
        return 1;
    }
    acl_init();
    AclBlock *root = acl_parse_file(argv[1]);
    if (!root) return 1;
    acl_resolve_all(root);
    acl_print(root, stdout);
    acl_free(root);
    acl_shutdown();
    return 0;
}
