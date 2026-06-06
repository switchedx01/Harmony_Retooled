#include <stdio.h>
#include <stddef.h>
#include "include/core/app_context.h"
int main() {
    printf("offsetof(library_input_buffer)=%zu\n", offsetof(PlayerContext, library_input_buffer));
    printf("offsetof(input_clipboard_buffer)=%zu\n", offsetof(PlayerContext, input_clipboard_buffer));
    return 0;
}
