#include "core/hub_client.h"
#include "init.h"
#include <zmq.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void* zmq_ctx = NULL;
static void* zmq_sock = NULL;
static void* zmq_cmd_sock = NULL;
static const char* HUB_ENDPOINT = "ipc:///tmp/hub_socket.ipc";
static const char* HUB_CMD_ENDPOINT = "ipc:///tmp/hub_cmd.ipc";

// Helper function to create or recreate the socket
static void reset_socket(void) {
    if (zmq_sock) {
        zmq_close(zmq_sock);
        zmq_sock = NULL;
    }
    
    if (!zmq_ctx) return;
    
    zmq_sock = zmq_socket(zmq_ctx, ZMQ_REQ);
    if (!zmq_sock) return;
    
    // Set send and receive timeouts to prevent blocking the main thread (5ms)
    int timeout_ms = 5;
    zmq_setsockopt(zmq_sock, ZMQ_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
    zmq_setsockopt(zmq_sock, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    
    // Set linger to 0 so zmq_close/zmq_ctx_destroy doesn't block if hub is offline
    int linger = 0;
    zmq_setsockopt(zmq_sock, ZMQ_LINGER, &linger, sizeof(linger));
    
    // Connect to the hub
    zmq_connect(zmq_sock, HUB_ENDPOINT);

    if (zmq_cmd_sock) zmq_close(zmq_cmd_sock);
    zmq_cmd_sock = zmq_socket(zmq_ctx, ZMQ_SUB);
    zmq_setsockopt(zmq_cmd_sock, ZMQ_SUBSCRIBE, "", 0);
    int timeout = 10;
    zmq_setsockopt(zmq_cmd_sock, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Connect to local GUI
    zmq_connect(zmq_cmd_sock, HUB_CMD_ENDPOINT);
}

int init_hub_connection(void) {
    if (zmq_ctx) return 0; // Already initialized
    
    zmq_ctx = zmq_ctx_new();
    if (!zmq_ctx) {
        fprintf(stderr, "[Hub Client] Failed to create ZeroMQ context\n");
        return -1;
    }
    
    reset_socket();
    if (!zmq_sock) {
        fprintf(stderr, "[Hub Client] Failed to create ZeroMQ socket\n");
        return -1;
    }
    
    return 0;
}

void send_to_hub(const char* json_message) {
    if (!zmq_sock || !json_message) return;
    
    // Send the message
    int len = strlen(json_message);
    int rc = zmq_send(zmq_sock, json_message, len, 0);
    
    if (rc < 0) {
        // If send fails (e.g. timeout if high water mark is reached), reset socket
        // to ensure it doesn't get stuck in a bad state.
        reset_socket();
        return;
    }
    
    // Wait for ACK
    char buffer[256];
    rc = zmq_recv(zmq_sock, buffer, sizeof(buffer) - 1, 0);
    
    if (rc < 0) {
        // Timeout or other error occurred
        // The REQ socket state machine is now broken (must receive a reply before sending again).
        // Standard ZeroMQ practice is to destroy and recreate the socket.
        reset_socket();
    } else {
        buffer[rc] = '\0';
        // printf("[Hub Client] Received from hub: %s\n", buffer);
    }
}

void close_hub_connection(void) {
    if (zmq_sock) {
        zmq_close(zmq_sock);
        zmq_sock = NULL;
    }
    if (zmq_cmd_sock) {
        zmq_close(zmq_cmd_sock);
        zmq_cmd_sock = NULL;
    }
    if (zmq_ctx) {
        zmq_ctx_destroy(zmq_ctx);
        zmq_ctx = NULL;
    }
}

#include "app_context.h"
#include "command_dispatch.h"
#include "player.h"

void poll_hub_commands(void) {
    if (!zmq_cmd_sock) return;
    
    char buffer[1024];
    int rc = zmq_recv(zmq_cmd_sock, buffer, sizeof(buffer) - 1, ZMQ_DONTWAIT);
    if (rc > 0) {
        buffer[rc] = '\0';
        AppContext *app = app_get_context();
        
        if (strcmp(buffer, "PLAY") == 0) {
            dispatch_command(app, "play_force", 0);
        } else if (strcmp(buffer, "PAUSE") == 0) {
            dispatch_command(app, "pause_force", 0);
        } else if (strcmp(buffer, "SHOW_GUI") == 0) {
            app_enable_gui(app);
        } else if (strcmp(buffer, "QUIT") == 0) {
            app->running = false;
        } else if (strncmp(buffer, "LOAD ", 5) == 0) {
            const char *filepath = buffer + 5;
            // Add song and play it
            // Need to parse metadata or just add it
            extern bool get_metadata(const char *path, Track *out_track, char *out_artist, char *out_album);
            Track t = {0}; char artist[256], album[256];
            if (get_metadata(filepath, &t, artist, album)) {
                player_add_song(app->player, t.title, artist, album, filepath, t.art_filename);
                char play_cmd[64];
                snprintf(play_cmd, sizeof(play_cmd), "play_song_%zu", app->player->count - 1);
                dispatch_command(app, play_cmd, 0);
            }
        } else if (strncmp(buffer, "SEEK ", 5) == 0) {
            float seconds = atof(buffer + 5);
            /* Pass seconds * 100 as int to preserve precision */
            dispatch_command(app, "seek_to", (int)(seconds * 100));
        } else if (strncmp(buffer, "VOLUME ", 7) == 0) {
            int vol = atoi(buffer + 7);
            dispatch_command(app, "set_volume", vol);
        }
    }
}
