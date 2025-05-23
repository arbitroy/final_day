#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>

#include "network.h"
#include "io_helpers.h"
#include "commands.h"
#include "variables.h"

#define MAX_CONNECTIONS 10
#define MAX_CLIENTS 100
#define BUFFER_SIZE 4096
#define MAX_MSG_SIZE (BUFFER_SIZE - 20) /* Leave room for client prefix */

// Global server information
static server_info_t server_info = {-1, -1, 0, -1};

// Array to track connected clients
static int client_count = 0;
static int connected_clients = 0;
static int client_sockets[MAX_CLIENTS];
static int client_ids[MAX_CLIENTS]; // Track unique client IDs

// Initialize all client sockets to -1 (invalid)
void init_client_sockets()
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        client_sockets[i] = -1;
        client_ids[i] = -1;
    }
    // Reset connected clients count
    connected_clients = 0;
    // Don't reset client_count to preserve unique IDs
}

// Initialize server information
void init_server_info(void)
{
    server_info.sockfd = -1;
    server_info.port = -1;
    server_info.running = 0;
    server_info.server_pid = -1;
    init_client_sockets();
    client_count = 0; // Reset client counter on initialization
}

// Clean up server resources
void cleanup_server(void)
{
    // First close all client connections
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_sockets[i] >= 0)
        {
            close(client_sockets[i]);
            client_sockets[i] = -1;
        }
    }

    // Then close the server socket
    if (server_info.sockfd >= 0)
    {
        close(server_info.sockfd);
        server_info.sockfd = -1;
    }

    // Reset server state - Make sure ALL server state is reset
    server_info.port = -1;
    server_info.running = 0;
    server_info.server_pid = -1;

    // Reset client counts
    connected_clients = 0;
}

// Function to broadcast message to all connected clients
void broadcast_message(const char *message, int exclude_fd)
{
    if (message == NULL)
        return;

    // Debug print to verify what's being broadcast and to whom
    fprintf(stderr, "DEBUG: Broadcasting message: %s\n", message);
    fprintf(stderr, "DEBUG: exclude_fd: %d\n", exclude_fd);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        // Only send to valid sockets and respect exclude_fd parameter
        if (client_sockets[i] >= 0)
        {
            // Check if this socket should be excluded from broadcast
            if (exclude_fd != -1 && client_sockets[i] == exclude_fd)
            {
                fprintf(stderr, "DEBUG: Skipping fd %d\n", client_sockets[i]);
                continue; // Skip this client
            }

            // Try to send the message
            ssize_t bytes_sent = send(client_sockets[i], message, strlen(message), 0);
            fprintf(stderr, "DEBUG: Sent to fd %d, result: %zd\n", client_sockets[i], bytes_sent);

            if (bytes_sent < 0)
            {
                // If sending fails, close this client connection properly
                close(client_sockets[i]);
                client_sockets[i] = -1;
                connected_clients--;
            }
        }
    }
}
// Handle a new client connection
void handle_new_connection(int server_fd)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Verify server socket is valid
    if (fcntl(server_fd, F_GETFD) < 0)
    {
        return;
    }

    // Check server socket error state
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(server_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0)
    {
        return;
    }

    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0)
    {
        return;
    }

    // Set non-blocking
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    // Set keepalive for client socket
    int keep_alive = 1;
    setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive));

    // Set client socket linger option
    struct linger lin;
    lin.l_onoff = 0;
    lin.l_linger = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_LINGER, (const char *)&lin, sizeof(lin));

    // Find an available slot in the client array
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_sockets[i] < 0)
        {
            slot = i;
            break;
        }
    }

    if (slot < 0)
    {
        close(client_fd);
        return;
    }

    // Increment client count before assigning IDs
    client_count++;

    // Store the client socket and assign unique ID
    client_sockets[slot] = client_fd;
    client_ids[slot] = client_count;
    connected_clients++;

    // Send a welcome message to the new client - format without #
    char welcome[BUFFER_SIZE];
    snprintf(welcome, BUFFER_SIZE, "Welcome! You are client%d\n", client_count);
    send(client_fd, welcome, strlen(welcome), 0);

    // Display a message on the server
    char info[BUFFER_SIZE];
    snprintf(info, BUFFER_SIZE, "New connection: client%d\n", client_count);
    display_message(info);
}

// Handle received data from a client
void handle_client_data(int client_fd, int client_index)
{
    // Verify the socket is valid
    if (fcntl(client_fd, F_GETFD) < 0)
    {
        client_sockets[client_index] = -1;
        connected_clients--;
        return;
    }

    // Check socket error state
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0)
    {
        close(client_fd);
        client_sockets[client_index] = -1;
        connected_clients--;
        return;
    }

    char buffer[MAX_MSG_SIZE];
    ssize_t bytes_read = recv(client_fd, buffer, MAX_MSG_SIZE - 1, 0);

    if (bytes_read <= 0)
    {
        // Properly close the socket
        close(client_fd);

        // Mark slot as available
        client_sockets[client_index] = -1;
        connected_clients--;
        return;
    }

    // Null-terminate the received data
    buffer[bytes_read] = '\0';

    // Remove newline character if present
    if (bytes_read > 0 && buffer[bytes_read - 1] == '\n')
    {
        buffer[bytes_read - 1] = '\0';
    }

    // Check for special command
    if (strcmp(buffer, "\\connected") == 0)
    {
        char response[BUFFER_SIZE];
        // Use client ID without # character
        snprintf(response, BUFFER_SIZE, "client%d: There are %d clients connected\n",
                 client_ids[client_index], connected_clients);

        // Send to all clients (including the sender)
        broadcast_message(response, -1);

        // Display on server
        display_message(response);
        return;
    }

    // Format message with client ID (no # character)
    char formatted_msg[BUFFER_SIZE];
    snprintf(formatted_msg, BUFFER_SIZE, "client%d: %s\n", client_ids[client_index], buffer);

    // Broadcast to all clients (including sender)
    broadcast_message(formatted_msg, -1);

    // Display on server
    display_message(formatted_msg);
}

// Server main function
void run_server(int port)
{
    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        exit(EXIT_FAILURE);
    }

    // Set socket options to allow reuse of address/port
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Add this new option to prevent "Address already in use" errors
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    // Add linger option to handle socket closure better
    struct linger lin;
    lin.l_onoff = 0;
    lin.l_linger = 0;
    setsockopt(server_fd, SOL_SOCKET, SO_LINGER, (const char *)&lin, sizeof(lin));

    // Add TCP keepalive to detect broken connections
    int keep_alive = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive));

    // Set non-blocking
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    // Bind to port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_fd, MAX_CONNECTIONS) < 0)
    {
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Initialize client sockets array
    init_client_sockets();

    // Update server info
    server_info.sockfd = server_fd;
    server_info.running = 1;
    server_info.port = port;

    // Main server loop
    fd_set read_fds;
    struct timeval tv;
    int loop_count = 0;
    int heartbeat_interval = 0;

    while (server_info.running)
    {
        loop_count++;

        // Check state of all client sockets periodically
        if (loop_count % 100 == 0)
        {
            int error = 0;
            socklen_t len = sizeof(error);

            // Check status of the server socket
            int result = getsockopt(server_fd, SOL_SOCKET, SO_ERROR, &error, &len);

            // Check state of all client sockets
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (client_sockets[i] >= 0)
                {
                    result = getsockopt(client_sockets[i], SOL_SOCKET, SO_ERROR, &error, &len);
                    if (result < 0 || error != 0)
                    {
                        close(client_sockets[i]);
                        client_sockets[i] = -1;
                        connected_clients--;
                    }
                }
            }

            heartbeat_interval = 0;
        }
        heartbeat_interval++;

        // Set up the fd_set
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        int max_fd = server_fd;

        // Add client sockets to fd_set
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            int fd = client_sockets[i];
            if (fd >= 0)
            {
                FD_SET(fd, &read_fds);
                if (fd > max_fd)
                {
                    max_fd = fd;
                }
            }
        }

        // Set timeout (100ms)
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        // Wait for activity
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

        if (activity < 0)
        {
            if (errno != EINTR)
            {
                // Don't break, try to continue
            }
            continue; // Skip to next iteration instead of breaking
        }

        // Check for new connections
        if (FD_ISSET(server_fd, &read_fds))
        {
            handle_new_connection(server_fd);
        }

        // Check for data from clients
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            int fd = client_sockets[i];
            if (fd >= 0 && FD_ISSET(fd, &read_fds))
            {
                handle_client_data(fd, i);
            }
        }
    }

    // Clean up
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_sockets[i] >= 0)
        {
            close(client_sockets[i]);
            client_sockets[i] = -1;
        }
    }

    close(server_fd);
    exit(EXIT_SUCCESS);
}

// Start a server on the specified port
ssize_t cmd_start_server(char **tokens)
{
    // Check if a port is provided
    if (tokens[1] == NULL)
    {
        display_error("ERROR: No port provided", "");
        return -1;
    }

    // Check if server is already running
    if (server_info.running)
    {
        display_error("ERROR: Server is already running on port ", tokens[1]);
        return -1;
    }

    // Parse port number
    int port = atoi(tokens[1]);
    if (port <= 0 || port > 65535)
    {
        display_error("ERROR: Invalid port number", "");
        return -1;
    }

    // Check if port is already in use by some other process
    int test_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (test_socket < 0)
    {
        display_error("ERROR: Failed to create socket", "");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(test_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(test_socket);
        display_error("ERROR: Port is already in use", "");
        return -1;
    }

    close(test_socket);

    // Initialize client sockets array
    init_client_sockets();
    client_count = 0; // Reset client counter when starting server
    connected_clients = 0;

    // Avoid zombie processes - ignore SIGCHLD
    signal(SIGCHLD, SIG_IGN);

    // Fork a child process to run the server
    pid_t pid = fork();

    if (pid < 0)
    {
        display_error("ERROR: Failed to start server", "");
        return -1;
    }
    else if (pid == 0)
    {
        // Child process - detach from parent
        setsid();

        // Run the server
        run_server(port);

        // Should never reach here
        exit(EXIT_SUCCESS);
    }
    else
    {
        // Parent process - save server info
        server_info.port = port;
        server_info.running = 1;
        server_info.server_pid = pid;

        char message[MAX_STR_LEN];
        snprintf(message, MAX_STR_LEN, "Server started on port %d\n", port);
        display_message(message);

        return 0;
    }
}

// Close the currently running server
ssize_t cmd_close_server(char **tokens __attribute__((unused)))
{
    if (!server_info.running)
    {
        display_error("ERROR: No server is currently running", "");
        return -1;
    }

    if (server_info.server_pid > 0)
    {
        if (kill(server_info.server_pid, 0) < 0 && errno == ESRCH)
        {
            // Process already gone, just clean up state
            cleanup_server();
            display_message("Server has been closed\n");
            return 0;
        }
    }

    // Send a kill signal to the server process
    if (kill(server_info.server_pid, SIGTERM) < 0)
    {
        // Check if process exists
        if (errno == ESRCH)
        {
            // Process already gone, just clean up
            cleanup_server();
            display_message("Server has been closed\n");
            return 0;
        }

        display_error("ERROR: Failed to terminate server", "");
        return -1;
    }

    // Wait for server process to terminate with timeout
    int status;
    pid_t result;
    int retries = 10;

    while (retries--)
    {
        result = waitpid(server_info.server_pid, &status, WNOHANG);
        if (result == server_info.server_pid)
        {
            break; // Process terminated
        }
        else if (result == 0)
        {
            // Still running, wait a bit longer
            usleep(200000); // 200ms
        }
        else
        {
            // Error or process doesn't exist
            break;
        }
    }

    // If still running, try SIGKILL
    if (retries <= 0)
    {
        kill(server_info.server_pid, SIGKILL);
        waitpid(server_info.server_pid, NULL, 0);
    }

    // Clean up server resources
    cleanup_server();
    server_info.server_pid = -1; // Reset server_pid
    if (server_info.server_pid > 0)
    {
        // Check if process still exists
        if (kill(server_info.server_pid, 0) == -1 && errno == ESRCH)
        {
            // Process is gone, make sure state reflects that
            server_info.running = 0;
            server_info.port = -1;
            server_info.server_pid = -1;
        }
    }

    display_message("Server has been closed\n");
    return 0;
}

// Check if a port is available
int is_port_available(int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        return 0;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    int available = (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);

    close(sock);
    return available;
}

// Send a message to a specified host/port
int send_message(int port, const char *hostname, const char *message)
{
    // Check if server is running on this port locally
    if (port == server_info.port && server_info.running)
    {
        // Verify server process is actually running
        if (kill(server_info.server_pid, 0) < 0)
        {
            if (errno == ESRCH)
            {
                server_info.running = 0;
                server_info.port = -1;
            }
        }
    }

    // Check arguments
    if (port <= 0 || port > 65535 || hostname == NULL || message == NULL)
    {
        return -1;
    }

    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        return -1;
    }

    // Get host information
    struct hostent *host = gethostbyname(hostname);
    if (host == NULL)
    {
        close(sockfd);
        return -1;
    }

    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    // Connect to server with a timeout
    struct timeval tv;
    tv.tv_sec = 2; // 2 second timeout
    tv.tv_usec = 0;

    // Set socket option for timeout
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv);

    // Try to establish a connection several times
    int retries = 5; // Increase retries
    int connected = 0;

    while (retries-- > 0 && !connected)
    {
        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0)
        {
            connected = 1;
            break;
        }

        // Small delay before retry
        usleep(200000); // 200ms - longer delay
    }

    if (!connected)
    {
        close(sockfd);

        fprintf(stderr, "DEBUG: Failed to connect after %d attempts\n", 5 - retries);

        // If our server is supposed to be running, check if it's actually running
        if (port == server_info.port && server_info.running)
        {
            // Check if process exists
            if (kill(server_info.server_pid, 0) < 0 && errno == ESRCH)
            {
                fprintf(stderr, "DEBUG: Server process does not exist, cleaning up state\n");
                cleanup_server();
            }
        }

        return -1;
    }

    // Normalize the message by removing extra spaces
    char normalized_message[BUFFER_SIZE];
    int j = 0;
    int in_space = 0;

    for (int i = 0; message[i] != '\0' && j < BUFFER_SIZE - 2; i++)
    {
        if (message[i] == ' ' || message[i] == '\t')
        {
            if (!in_space)
            {
                normalized_message[j++] = ' ';
                in_space = 1;
            }
        }
        else
        {
            normalized_message[j++] = message[i];
            in_space = 0;
        }
    }
    normalized_message[j] = '\0';

    // Send message
    ssize_t sent = send(sockfd, normalized_message, strlen(normalized_message), 0);
    if (sent < 0)
    {
        close(sockfd);
        return -1;
    }

    // Add newline if not present
    if (sent > 0 && normalized_message[strlen(normalized_message) - 1] != '\n')
    {
        send(sockfd, "\n", 1, 0);
    }

    // Give some time for the message to be processed
    usleep(100000); // 100ms

    // Close socket
    close(sockfd);
    return 0;
}

// Send a message command
ssize_t cmd_send(char **tokens)
{
    // Check if a port is provided
    if (tokens[1] == NULL)
    {
        display_error("ERROR: No port provided", "");
        return -1;
    }

    // Check if hostname is provided
    if (tokens[2] == NULL)
    {
        display_error("ERROR: No hostname provided", "");
        return -1;
    }

    // Check if message is provided
    if (tokens[3] == NULL)
    {
        display_error("ERROR: No message provided", "");
        return -1;
    }

    // Expand variables in port and hostname if needed
    char *expanded_port = NULL;
    if (strchr(tokens[1], '$') != NULL)
    {
        expanded_port = expand_variables(tokens[1]);
    }

    char *expanded_hostname = NULL;
    if (strchr(tokens[2], '$') != NULL)
    {
        expanded_hostname = expand_variables(tokens[2]);
    }

    // Parse port number (use expanded port if available)
    int port;
    if (expanded_port != NULL)
    {
        port = atoi(expanded_port);
        free(expanded_port);
    }
    else
    {
        port = atoi(tokens[1]);
    }

    if (port <= 0 || port > 65535)
    {
        if (expanded_hostname != NULL)
            free(expanded_hostname);
        display_error("ERROR: Invalid port number", "");
        return -1;
    }

    // Get hostname (use expanded hostname if available)
    char *hostname = expanded_hostname != NULL ? expanded_hostname : tokens[2];

    // Combine all remaining tokens into a message
    char message[BUFFER_SIZE] = "";
    for (int i = 3; tokens[i] != NULL; i++)
    {
        if (i > 3)
        {
            strcat(message, " "); // Add space between tokens
        }

        // Check if token has quotes at beginning and end
        size_t len = strlen(tokens[i]);
        if (len >= 2 && tokens[i][0] == '"' && tokens[i][len - 1] == '"')
        {
            // Copy without the quotes
            strncat(message, tokens[i] + 1, len - 2);
        }
        else
        {
            strcat(message, tokens[i]);
        }
    }

    // Expand variables in the message
    char *expanded_message = expand_variables(message);

    // Send the message (use expanded message if available)
    int result;
    // Add debug information
    fprintf(stderr, "DEBUG: Port: %d\n", port);
    fprintf(stderr, "DEBUG: Hostname: %s\n", hostname);

    if (expanded_message != NULL)
    {
        fprintf(stderr, "DEBUG: Expanded message: %s\n", expanded_message);
        result = send_message(port, hostname, expanded_message);
        free(expanded_message);
    }
    else
    {
        fprintf(stderr, "DEBUG: Original message: %s\n", message);
        result = send_message(port, hostname, message);
    }

    // Free expanded hostname if it was allocated
    if (expanded_hostname != NULL)
    {
        free(expanded_hostname);
    }

    if (result < 0)
    {
        display_error("ERROR: Failed to send message", "");
        return -1;
    }

    return 0;
}

// Client receive function (runs in child process)
void client_receive(int sockfd)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // Set socket to non-blocking mode
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    fd_set read_fds;
    struct timeval tv;

    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);

        // Set timeout (100ms)
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int activity = select(sockfd + 1, &read_fds, NULL, NULL, &tv);

        if (activity < 0 && errno != EINTR)
        {
            // Error in select
            break;
        }
        else if (activity > 0 && FD_ISSET(sockfd, &read_fds))
        {
            bytes_read = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);

            if (bytes_read <= 0)
            {
                // Connection closed or error
                break;
            }

            // Null-terminate and display
            buffer[bytes_read] = '\0';
            display_message(buffer);
        }
    }

    exit(EXIT_SUCCESS);
}

// Start a client command
ssize_t cmd_start_client(char **tokens)
{
    // Check if a port is provided
    if (tokens[1] == NULL)
    {
        display_error("ERROR: No port provided", "");
        return -1;
    }

    // Check if hostname is provided
    if (tokens[2] == NULL)
    {
        display_error("ERROR: No hostname provided", "");
        return -1;
    }

    // Expand variables in port and hostname if needed
    char *expanded_port = NULL;
    if (strchr(tokens[1], '$') != NULL)
    {
        expanded_port = expand_variables(tokens[1]);
    }

    char *expanded_hostname = NULL;
    if (strchr(tokens[2], '$') != NULL)
    {
        expanded_hostname = expand_variables(tokens[2]);
    }

    // Parse port number (use expanded port if available)
    int port;
    if (expanded_port != NULL)
    {
        port = atoi(expanded_port);
        free(expanded_port);
    }
    else
    {
        port = atoi(tokens[1]);
    }

    if (port <= 0 || port > 65535)
    {
        if (expanded_hostname != NULL)
            free(expanded_hostname);
        display_error("ERROR: Invalid port number", "");
        return -1;
    }

    // Get hostname (use expanded hostname if available)
    char *hostname = expanded_hostname != NULL ? expanded_hostname : tokens[2];

    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        if (expanded_hostname != NULL)
            free(expanded_hostname);
        display_error("ERROR: Failed to create socket", "");
        return -1;
    }

    // Get host information
    struct hostent *host = gethostbyname(hostname);
    if (host == NULL)
    {
        if (expanded_hostname != NULL)
            free(expanded_hostname);
        display_error("ERROR: Failed to resolve hostname", "");
        close(sockfd);
        return -1;
    }

    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        if (expanded_hostname != NULL)
            free(expanded_hostname);
        display_error("ERROR: Failed to connect to server", "");
        close(sockfd);
        return -1;
    }

    // Free expanded hostname if it was allocated
    if (expanded_hostname != NULL)
    {
        free(expanded_hostname);
    }

    display_message("Connected to server. Type messages to send. Use CTRL+D to exit.\n");

    // Fork a child process to handle receiving messages
    pid_t pid = fork();

    if (pid < 0)
    {
        display_error("ERROR: Failed to create receiver process", "");
        close(sockfd);
        return -1;
    }
    else if (pid == 0)
    {
        // Child process - handle receiving
        client_receive(sockfd);
        exit(EXIT_SUCCESS); // Should never reach here
    }
    else
    {
        // Parent process - handle sending
        char input_buf[BUFFER_SIZE];
        ssize_t bytes_read;

        // Set stdin to non-blocking
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

        // Use select to wait for input without blocking indefinitely
        fd_set read_fds;
        struct timeval tv;

        while (1)
        {
            FD_ZERO(&read_fds);
            FD_SET(STDIN_FILENO, &read_fds);

            // Set timeout (100ms)
            tv.tv_sec = 0;
            tv.tv_usec = 100000;

            int activity = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &tv);

            if (activity < 0 && errno != EINTR)
            {
                break;
            }
            else if (activity > 0 && FD_ISSET(STDIN_FILENO, &read_fds))
            {
                bytes_read = read(STDIN_FILENO, input_buf, BUFFER_SIZE - 1);

                if (bytes_read <= 0)
                {
                    // EOF or error
                    break;
                }

                // Null-terminate the input
                input_buf[bytes_read] = '\0';

                // Remove trailing newline if present
                if (input_buf[bytes_read - 1] == '\n')
                {
                    input_buf[bytes_read - 1] = '\0';
                    bytes_read--;
                }

                // Check for EOF (empty message)
                if (bytes_read == 0)
                {
                    break;
                }

                // Handle special commands
                if (strncmp(input_buf, "\\connected", 10) == 0)
                {
                    // Special command to show connected clients
                    send(sockfd, "\\connected\n", 11, 0);
                }
                else
                {
                    // Normalize the message (remove extra spaces)
                    char normalized_message[BUFFER_SIZE];
                    int j = 0;
                    int in_space = 0;

                    for (int i = 0; input_buf[i] != '\0'; i++)
                    {
                        if (input_buf[i] == ' ' || input_buf[i] == '\t')
                        {
                            if (!in_space)
                            {
                                normalized_message[j++] = ' ';
                                in_space = 1;
                            }
                        }
                        else
                        {
                            normalized_message[j++] = input_buf[i];
                            in_space = 0;
                        }
                    }
                    normalized_message[j] = '\0';

                    // Add newline if not present
                    if (j > 0 && normalized_message[j - 1] != '\n')
                    {
                        normalized_message[j] = '\n';
                        normalized_message[j + 1] = '\0';
                    }

                    // Send the message
                    if (send(sockfd, normalized_message, strlen(normalized_message), 0) < 0)
                    {
                        display_error("ERROR: Failed to send message", "");
                        break;
                    }
                }
            }
        }

        // Clean up
        kill(pid, SIGTERM); // Terminate receiver process
        waitpid(pid, NULL, 0);
        close(sockfd);

        // Reset stdin to blocking mode
        fcntl(STDIN_FILENO, F_SETFL, flags);

        display_message("Client disconnected\n");
    }

    return 0;
}