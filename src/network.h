#ifndef __NETWORK_H__
#define __NETWORK_H__

#include <unistd.h>

// Server data structure
typedef struct {
    int sockfd;
    int port;
    int running;
    pid_t server_pid;
} server_info_t;

// Client data structure
typedef struct {
    int sockfd;
    int port;
    char *hostname;
} client_info_t;

// Forward declaration for variable expansion function
char* expand_variables(const char *str);

// Network command functions
ssize_t cmd_start_server(char **tokens);
ssize_t cmd_close_server(char **tokens);
ssize_t cmd_send(char **tokens);
ssize_t cmd_start_client(char **tokens);

// Helper functions
void init_server_info(void);
void cleanup_server(void);
int send_message(int port, const char *hostname, const char *message);

#endif