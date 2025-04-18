#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdarg.h>

#include "builtins.h"
#include "io_helpers.h"
#include "variables.h"
#include "commands.h"
#include "network.h"

// Debug flag - Set to 0 to disable debug logs
#define DEBUG_MODE 0

// Debug logging function
void mysh_debug_log(const char *format, ...) {
    if (!DEBUG_MODE) return;
    
    va_list args;
    va_start(args, format);
    
    fprintf(stderr, "[MYSH_DEBUG] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    
    va_end(args);
}

// Track allocated memory for expanded variables
static char **expanded_memory = NULL;
static size_t expanded_count = 0;
static size_t expanded_capacity = 0;

// Signal handler for SIGCHLD (child process termination)
void sigchld_handler(int signum __attribute__((unused)))
{
    pid_t pid;
    int status;

    // Non-blocking wait to collect all terminated children
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        bg_process_t *process = find_bg_process_by_pid(pid);
        if (process != NULL)
        {
            // Queue the message for next prompt
            mark_process_completed(pid);
        }
    }
}

// Signal handler for SIGINT (Ctrl+C)
void sigint_handler(int signum __attribute__((unused)))
{
    // Just catch the signal to prevent shell from exiting
    // Display a new prompt
    display_message("\nmysh$ ");
}

// Free all memory tracked for expanded variables
void free_expanded_memory()
{
    if (expanded_memory != NULL)
    {
        for (size_t i = 0; i < expanded_count; i++)
        {
            if (expanded_memory[i] != NULL)
            {
                free(expanded_memory[i]);
            }
        }
        free(expanded_memory);
        expanded_memory = NULL;
        expanded_count = 0;
        expanded_capacity = 0;
    }
}

// Track memory allocated for variable expansion
void track_expanded_memory(char *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    // Initialize or expand the tracking array if needed
    if (expanded_memory == NULL)
    {
        expanded_capacity = 10;
        expanded_memory = malloc(expanded_capacity * sizeof(char *));
        if (expanded_memory == NULL)
        {
            // If we can't track it, at least free it to avoid leaks
            free(ptr);
            return;
        }
    }
    else if (expanded_count >= expanded_capacity)
    {
        expanded_capacity *= 2;
        char **new_memory = realloc(expanded_memory, expanded_capacity * sizeof(char *));
        if (new_memory == NULL)
        {
            // If we can't expand tracking, at least free the current pointer
            free(ptr);
            return;
        }
        expanded_memory = new_memory;
    }

    // Add the pointer to our tracking array
    expanded_memory[expanded_count++] = ptr;
}

// Function to check if a string is a variable assignment (contains '=' but not as first char)
int is_variable_assignment(const char *str)
{
    if (str == NULL || str[0] == '\0' || str[0] == '=')
    {
        return 0;
    }

    char *equals = strchr(str, '=');
    return equals != NULL && equals != str;
}

// Function to handle variable assignment
int handle_variable_assignment(const char *str)
{
    char *equals = strchr(str, '=');
    if (equals == NULL)
    {
        return -1;
    }

    // Extract key (everything before '=')
    size_t key_len = equals - str;
    char *key = malloc(key_len + 1);
    if (key == NULL)
    {
        return -1;
    }
    strncpy(key, str, key_len);
    key[key_len] = '\0';

    // Extract value (everything after '=')
    const char *value = equals + 1;

    // Handle variable expansion in value
    char *expanded_value = NULL;
    if (strchr(value, '$') != NULL)
    {
        expanded_value = expand_variables(value);
        if (expanded_value != NULL)
        {
            value = expanded_value;
        }
    }

    // Set the variable
    int result = set_variable(key, value);

    // Clean up
    free(key);
    if (expanded_value != NULL)
    {
        free(expanded_value);
    }

    return result;
}

void expand_variables_in_tokens(char **tokens)
{
    if (tokens == NULL)
    {
        return;
    }

    for (int i = 0; tokens[i] != NULL; i++)
    {
        // Skip expanding the first token if it's a variable assignment
        // A variable assignment must have = not as the first character
        if (i == 0 && is_variable_assignment(tokens[0]))
        {
            continue;
        }

        // Check if token contains a '$'
        if (strchr(tokens[i], '$') != NULL)
        {
            char *expanded = expand_variables(tokens[i]);
            if (expanded != NULL)
            {
                // Track the allocated memory for later cleanup
                track_expanded_memory(expanded);
                // Replace token with expanded version
                tokens[i] = expanded;
            }
        }
    }
}

// Check if a command exists in PATH
int check_command_exists(const char *cmd)
{
    // Check if the command exists in common directories
    char path[MAX_STR_LEN];

    // Check in /bin
    snprintf(path, MAX_STR_LEN, "/bin/%s", cmd);
    if (access(path, X_OK) == 0)
    {
        return 1;
    }

    // Check in /usr/bin
    snprintf(path, MAX_STR_LEN, "/usr/bin/%s", cmd);
    if (access(path, X_OK) == 0)
    {
        return 1;
    }

    return 0;
}

int main(__attribute__((unused)) int argc,
         __attribute__((unused)) char *argv[])
{
    mysh_debug_log("mysh starting up");
    
    char *prompt = "mysh$ ";

    // Set up signal handlers
    struct sigaction sa_chld, sa_int;

    // Set up SIGCHLD handler for background processes
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);

    // Set up SIGINT handler for Ctrl+C
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);

    // Initialize background process tracking
    init_bg_processes();

    // Initialize server info
    init_server_info();

    char input_buf[MAX_STR_LEN + 1];
    input_buf[MAX_STR_LEN] = '\0';
    char *token_arr[MAX_STR_LEN] = {NULL};

    while (1)
    {
        char *message;
        while ((message = get_next_bg_message()) != NULL)
        {
            display_message(message);
            display_message("\n");
            free(message);
        }

        // Display the prompt
        display_message(prompt);

        // Get and tokenize input
        int ret = get_input(input_buf);

        // Clean up any previous expanded variables
        free_expanded_memory();

        // Check for EOF (Ctrl+D) or error
        if (ret == 0 || ret == -1)
        {
            mysh_debug_log("EOF or error detected, exiting loop");
            break;
        }

        mysh_debug_log("Input received: '%s'", input_buf);
        
        // Check if input contains a pipe before tokenizing
        int raw_has_pipe = strchr(input_buf, '|') != NULL;
        mysh_debug_log("Raw input contains pipe: %s", raw_has_pipe ? "YES" : "NO");
        
        size_t token_count = tokenize_input(input_buf, token_arr);
        mysh_debug_log("Tokenized into %zu tokens", token_count);

        // Check for empty input or exit command
        if (token_count == 0)
        {
            mysh_debug_log("Empty input, continuing");
            continue; // Empty input, just show prompt again
        }

        if (strcmp("exit", token_arr[0]) == 0)
        {
            mysh_debug_log("Exit command detected, breaking loop");
            break; // Exit command, break the loop
        }

        // Check tokens for pipe character
        int has_pipe = 0;
        for (size_t i = 0; i < token_count; i++) {
            if (strcmp(token_arr[i], "|") == 0) {
                has_pipe = 1;
                mysh_debug_log("Pipe token detected at position %zu", i);
                break;
            }
        }
        
        // Expand variables in the tokens
        expand_variables_in_tokens(token_arr);

        // Check for variable assignment
        if (is_variable_assignment(token_arr[0]))
        {
            mysh_debug_log("Variable assignment detected: %s", token_arr[0]);
            if (handle_variable_assignment(token_arr[0]) == -1)
            {
                display_error("ERROR: Failed to set variable: ", token_arr[0]);
            }
            continue; // Make sure we're properly continuing
        }
        
        // If we have a pipe, use the pipeline handler
        if (has_pipe) {
            mysh_debug_log("Detected pipeline, calling handle_pipeline");
            int err = handle_pipeline(token_arr);
            if (err == -1) {
                mysh_debug_log("Pipeline handler returned error");
                if (token_arr[0] != NULL) {
                    display_error("ERROR: Command failed: ", token_arr[0]);
                }
            }
            continue;
        }
        
        // Handle network commands
        if (strcmp(token_arr[0], "start-server") == 0)
        {
            mysh_debug_log("Handling start-server command");
            ssize_t err = cmd_start_server(token_arr);
            if (err == -1)
            {
                display_error("ERROR: Builtin failed: ", token_arr[0]);
            }
            continue;
        }
        else if (strcmp(token_arr[0], "close-server") == 0)
        {
            mysh_debug_log("Handling close-server command");
            ssize_t err = cmd_close_server(token_arr);
            if (err == -1)
            {
                display_error("ERROR: Builtin failed: ", token_arr[0]);
            }
            continue;
        }
        else if (strcmp(token_arr[0], "send") == 0)
        {
            mysh_debug_log("Handling send command");
            ssize_t err = cmd_send(token_arr);
            if (err == -1)
            {
                display_error("ERROR: Builtin failed: ", token_arr[0]);
            }
            continue;
        }
        else if (strcmp(token_arr[0], "start-client") == 0)
        {
            mysh_debug_log("Handling start-client command");
            ssize_t err = cmd_start_client(token_arr);
            if (err == -1)
            {
                display_error("ERROR: Builtin failed: ", token_arr[0]);
            }
            continue;
        }

        // Check for kill command
        if (strcmp(token_arr[0], "kill") == 0)
        {
            mysh_debug_log("Handling kill command");
            ssize_t err = cmd_kill(token_arr);
            if (err == -1)
            {
                display_error("ERROR: Builtin failed: ", token_arr[0]);
            }
            continue;
        }

        // Check for ps command
        if (strcmp(token_arr[0], "ps") == 0)
        {
            mysh_debug_log("Handling ps command");
            ssize_t err = cmd_ps(token_arr);
            if (err == -1)
            {
                display_error("ERROR: Builtin failed: ", token_arr[0]);
            }
            continue;
        }

        // Check if it's a builtin command
        bn_ptr builtin_fn = check_builtin(token_arr[0]);
        if (builtin_fn != NULL)
        {
            mysh_debug_log("Executing builtin command: %s", token_arr[0]);
            // Execute builtin directly
            ssize_t err = builtin_fn(token_arr);
            if (err == -1)
            {
                display_error("ERROR: Builtin failed: ", token_arr[0]);
            }
            continue;
        }

        // Check if command exists before attempting to run it
        if (!check_command_exists(token_arr[0]))
        {
            mysh_debug_log("Unknown command: %s", token_arr[0]);
            display_error("ERROR: Unknown command: ", token_arr[0]);
            continue;
        }

        // Handle pipeline or command execution
        mysh_debug_log("Executing system command: %s", token_arr[0]);
        int err = handle_pipeline(token_arr);
        if (err == -1)
        {
            // Only show error if not already handled by the called functions
            // This is to prevent duplicate errors
            if (token_arr[0] != NULL)
            {
                display_error("ERROR: Command failed: ", token_arr[0]);
            }
        }
    }

    // Clean up before exiting
    mysh_debug_log("Cleaning up and exiting");
    free_expanded_memory();
    free_variables();    // Clean up all variables
    free_bg_processes(); // Clean up background process tracking
    free_bg_messages();  // Clean up any pending messages
    cleanup_server();    // Clean up server resources

    return 0;
}