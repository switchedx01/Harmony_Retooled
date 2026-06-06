#ifndef HUB_CLIENT_H
#define HUB_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the ZeroMQ hub connection.
 * @return 0 on success, -1 on failure.
 */
int init_hub_connection(void);

/**
 * @brief Sends a JSON message string to the central hub and waits for an ACK.
 *        This function is non-blocking to prevent main thread freezing.
 * @param json_message Null-terminated string containing the JSON data.
 */
void send_to_hub(const char* json_message);

/**
 * @brief Safely closes the socket and cleans up ZeroMQ resources.
 */
void close_hub_connection(void);

#ifdef __cplusplus
}
#endif

#endif // HUB_CLIENT_H
