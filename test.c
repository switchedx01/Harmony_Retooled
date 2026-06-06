#include <stdio.h>
#define HARMONY_VERSION "v2.0.01-beta (Moonlight)"
int main() {
    char cmd_buf[256];
    snprintf(cmd_buf, sizeof(cmd_buf), "python3 scripts/updater.py \"%s\"", HARMONY_VERSION);
    printf("CMD: %s\n", cmd_buf);
    return 0;
}
