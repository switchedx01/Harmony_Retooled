#include "path_utils.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

Result get_data_path(char *buffer, size_t max_len) {
    if (!buffer || max_len == 0) return RESULT_ERROR_INVALID_PARAMETER;
    
    const char *home = getenv("HOME");
    if (!home) {
        // Fallback to /tmp if no HOME
        home = "/tmp";
    }
    
    // Use ~/.local/share/harmony_player
    int ret = snprintf(buffer, max_len, "%s/.local/share/harmony_player", home);
    if (ret < 0 || (size_t)ret >= max_len) return RESULT_ERROR_BUFFER_OVERFLOW;
    
    // Create directory if it doesn't exist
    struct stat st = {0};
    if (stat(buffer, &st) == -1) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", buffer);
        system(cmd);
    }
    
    return RESULT_SUCCESS;
}

Result resolve_data_path(const char *rel_path, char *buffer, size_t max_len) {
    if (!buffer || max_len == 0 || !rel_path) return RESULT_ERROR_INVALID_PARAMETER;
    
    char base_path[MAX_PATH_LENGTH];
    if (get_data_path(base_path, sizeof(base_path)) != RESULT_SUCCESS) {
        return RESULT_ERROR_GENERIC;
    }
    
    int ret = snprintf(buffer, max_len, "%s/%s", base_path, rel_path);
    if (ret < 0 || (size_t)ret >= max_len) return RESULT_ERROR_BUFFER_OVERFLOW;
    
    return RESULT_SUCCESS;
}

Result resolve_asset_path(const char *rel_path, char *buffer, size_t max_len) {
    if (!buffer || max_len == 0 || !rel_path) return RESULT_ERROR_INVALID_PARAMETER;
    
    // Try to resolve using standard data directory
    if (resolve_data_path(rel_path, buffer, max_len) == RESULT_SUCCESS) {
        if (access(buffer, F_OK) == 0) {
            return RESULT_SUCCESS;
        }
    }
    
    // Fallback: just copy rel_path to buffer
    strncpy(buffer, rel_path, max_len - 1);
    buffer[max_len - 1] = '\0';
    return RESULT_ERROR_FILE_IO;
}
