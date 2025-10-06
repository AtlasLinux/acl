#include "acl.h"
#include <stdio.h>

int main(void) {
    acl_init();
    AclBlock *root = acl_parse_file("test/07");
    if (!root) return 1;
    acl_resolve_all(root);
    acl_print(root, stdout);
    acl_free(root);
    acl_shutdown();
    return 0;
}
