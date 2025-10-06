#include <stdio.h>
#include <stdlib.h>
#include "acl.h"

int main(void) {
    const char *text =
        "Modules {\n"
        "  string[] load = { \"virtio\", \"e1000\", \"synth\" };\n"
        "}\n"
        "Network {\n"
        "  interface \"eth0\" {\n"
        "    string[] addresses = { \"10.0.0.1\", \"10.0.0.2\" };\n"
        "  }\n"
        "}\n";

    acl_init();
    AclBlock *root = acl_parse_string(text);
    if (!root) { fprintf(stderr, "parse failed\n"); return 1; }

    /* resolve refs (no refs here but call for completeness) */
    if (!acl_resolve_all(root)) { fprintf(stderr, "resolve failed\n"); acl_free(root); return 1; }

    /* example: get Modules.load[1] */
    char *s = NULL;
    if (acl_get_string(root, "Modules.load[1]", &s)) {
        printf("Modules.load[1] = %s\n", s);
        free(s);
    } else {
        printf("Modules.load[1] not found or not string\n");
    }

    /* example: get Network.interface["eth0"].addresses[0] */
    if (acl_get_string(root, "Network.interface[\"eth0\"].addresses[0]", &s)) {
        printf("eth0 addr 0 = %s\n", s);
        free(s);
    } else {
        printf("address[0] not found\n");
    }

    /* print whole tree */
    acl_print(root, stdout);

    acl_free(root);
    acl_shutdown();
    return 0;
}
