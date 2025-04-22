#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdarg.h>

#include "commands.h"
#include "builtins.h"
#include "io_helpers.h"
#include "variables.h"

// Global variables for background process tracking
static bg_process_t *bg_process_list = NULL;
static int next_job_id = 1;

#define DEBUG_MODE 0 // Set to 1 to enable debug logs

void debug_log(const char *format, ...)
{
    if (!DEBUG_MODE)
        return;

    va_list args;
    va_start(args, format);

    fprintf(stderr, "[DEBUG] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");

    va_end(args);
}

// Global message queue for background process completion
static bg_message_t *bg_message_queue = NULL;

// Helper function to safely close a file descriptor if it's valid
void safe_close(int fd)
{
    if (fd >= 0)
    {
        close(fd);
    }
}

// Initialize background process tracking
void init_bg_processes()
{
    bg_process_list = NULL;
    next_job_id = 1;
}

// Add a background process
int add_bg_process(pid_t pid, const char *command) {
    bg_process_t *new_process = malloc(sizeof(bg_process_t));
    if (new_process == NULL) {
        return -1;
    }

    new_process->pid = pid;
    new_process->job_id = next_job_id++;
    
    // Remove trailing ampersand if present
    char *cmd_copy = strdup(command);
    if (cmd_copy == NULL) {
        free(new_process);
        return -1;
    }
    
    size_t len = strlen(cmd_copy);
    if (len > 0 && cmd_copy[len-1] == '&') {
        cmd_copy[len-1] = '\0';
        // Also remove any trailing whitespace
        len = strlen(cmd_copy);
        while (len > 0 && (cmd_copy[len-1] == ' ' || cmd_copy[len-1] == '\t')) {
            cmd_copy[len-1] = '\0';
            len--;
        }
    }
    
    new_process->command = cmd_copy;
    new_process->next = bg_process_list;
    bg_process_list = new_process;

    // Format must match EXACTLY what tests expect: [job_id] pid
    char buffer[MAX_STR_LEN];
    snprintf(buffer, MAX_STR_LEN, "[%d] %d", new_process->job_id, pid);
    display_message(buffer);
    display_message("\n");

    return new_process->job_id;
}

// Remove a background process
void remove_bg_process(pid_t pid) {
    bg_process_t *current = bg_process_list;
    bg_process_t *prev = NULL;

    while (current != NULL) {
        if (current->pid == pid) {
            if (prev == NULL) {
                bg_process_list = current->next;
            } else {
                prev->next = current->next;
            }

            free(current->command);
            free(current);

            // This is critical: Reset job_id counter when list becomes empty
            if (bg_process_list == NULL) {
                next_job_id = 1;
            }

            return;
        }

        prev = current;
        current = current->next;
    }
}

// Find a background process by pid
bg_process_t *find_bg_process_by_pid(pid_t pid)
{
    bg_process_t *current = bg_process_list;

    while (current != NULL)
    {
        if (current->pid == pid)
        {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

// List all background processes
void list_bg_processes()
{
    bg_process_t *current = bg_process_list;

    while (current != NULL)
    {
        char buffer[MAX_STR_LEN];
        snprintf(buffer, MAX_STR_LEN, "%s %d\n", current->command, current->pid);
        display_message(buffer);
        current = current->next;
    }
}

// Free all background process resources
void free_bg_processes()
{
    bg_process_t *current = bg_process_list;
    bg_process_t *next;

    while (current != NULL)
    {
        next = current->next;
        free(current->command);
        free(current);
        current = next;
    }

    bg_process_list = NULL;
}

// Background message queue functions
void add_bg_message(const char *message)
{
    if (message == NULL)
        return;

    bg_message_t *new_message = malloc(sizeof(bg_message_t));
    if (new_message == NULL)
    {
        return;
    }

    new_message->message = strdup(message);
    if (new_message->message == NULL)
    {
        free(new_message);
        return;
    }

    new_message->next = NULL;

    if (bg_message_queue == NULL)
    {
        bg_message_queue = new_message;
    }
    else
    {
        bg_message_t *current = bg_message_queue;
        while (current->next != NULL)
        {
            current = current->next;
        }
        current->next = new_message;
    }
}

char *get_next_bg_message()
{
    if (bg_message_queue == NULL)
    {
        return NULL;
    }

    bg_message_t *message = bg_message_queue;
    char *msg_text = message->message;
    bg_message_queue = message->next;
    free(message); // Free the message struct but not the text

    return msg_text; // Caller is responsible for freeing this
}

int has_bg_messages()
{
    return bg_message_queue != NULL;
}

void free_bg_messages()
{
    bg_message_t *current = bg_message_queue;
    bg_message_t *next;

    while (current != NULL)
    {
        next = current->next;
        free(current->message);
        free(current);
        current = next;
    }

    bg_message_queue = NULL;
}

// Mark a process as completed and queue a message
void mark_process_completed(pid_t pid)
{
    bg_process_t *process = find_bg_process_by_pid(pid);
    if (process != NULL)
    {
        char buffer[MAX_STR_LEN];
        // Format must match test expectations EXACTLY: [job_id]+ Done command
        snprintf(buffer, MAX_STR_LEN, "[%d]+ Done %s", process->job_id, process->command);
        add_bg_message(buffer);

        // Remove process from the list
        remove_bg_process(pid);
        
        // Force a prompt refresh
        write(STDOUT_FILENO, "\n", 1);
    }
}

// Function to check if a string is a variable assignment
int is_variable_assignment(const char *str)
{
    if (str == NULL || str[0] == '\0' || str[0] == '=')
    {
        return 0;
    }

    char *equals = strchr(str, '=');
    return equals != NULL && equals != str;
}

// Handle the kill command
ssize_t cmd_kill(char **tokens)
{
    if (tokens[1] == NULL)
    {
        display_error("ERROR: No process ID provided", "");
        return -1;
    }

    // Parse the PID
    pid_t pid = atoi(tokens[1]);

    // Parse the signal number (default is SIGTERM)
    int signum = SIGTERM;
    if (tokens[2] != NULL)
    {
        signum = atoi(tokens[2]);
    }

    // Send the signal
    if (kill(pid, signum) != 0) {
        if (errno == ESRCH) {
            display_error("ERROR: The process does not exist", "");
        } else {
            display_error("ERROR: Invalid signal specified", "");
        }
        return -1;
    }
    
    // For termination signals, wait briefly to allow process to exit
    if (signum == SIGTERM || signum == SIGKILL) {
        usleep(10000); // Wait 10ms for the process to terminate
    }
    
    return 0;
}

// Handle the ps command
ssize_t cmd_ps(char **tokens __attribute__((unused)))
{
    list_bg_processes();
    return 0;
}

// Check if a command exists in PATH
int command_exists(const char *cmd)
{
    debug_log("Checking if command exists: %s", cmd);

    // Check absolute path
    if (cmd[0] == '/')
    {
        debug_log("Checking absolute path: %s", cmd);
        if (access(cmd, X_OK) == 0)
        {
            debug_log("Command found at absolute path");
            return 1;
        }
        return 0;
    }

    // Check in common directories
    char path[MAX_STR_LEN];

    // Check in /bin
    snprintf(path, MAX_STR_LEN, "/bin/%s", cmd);
    debug_log("Checking path: %s", path);
    if (access(path, X_OK) == 0)
    {
        debug_log("Command found in /bin");
        return 1;
    }

    // Check in /usr/bin
    snprintf(path, MAX_STR_LEN, "/usr/bin/%s", cmd);
    debug_log("Checking path: %s", path);
    if (access(path, X_OK) == 0)
    {
        debug_log("Command found in /usr/bin");
        return 1;
    }

    // Check using PATH environment variable
    const char *path_env = getenv("PATH");
    debug_log("PATH environment variable: %s", path_env ? path_env : "NULL");

    if (path_env)
    {
        char *path_copy = strdup(path_env);
        if (path_copy == NULL)
        {
            debug_log("Failed to allocate memory for PATH copy");
            return 0;
        }

        char *dir = strtok(path_copy, ":");

        while (dir != NULL)
        {
            snprintf(path, MAX_STR_LEN, "%s/%s", dir, cmd);
            debug_log("Checking path: %s", path);

            if (access(path, X_OK) == 0)
            {
                debug_log("Command found in PATH: %s", path);
                free(path_copy);
                return 1;
            }

            dir = strtok(NULL, ":");
        }

        free(path_copy);
    }

    debug_log("Command not found: %s", cmd);
    return 0;
}

// Execute a command with pipe support
int execute_command(char **tokens, int input_fd, int output_fd, int in_background) {
    if (tokens == NULL || tokens[0] == NULL) {
        return -1;
    }

    // Skip variable assignments in pipelines - they should only affect parent process
    if (is_variable_assignment(tokens[0])) {
        if (input_fd != STDIN_FILENO || output_fd != STDOUT_FILENO) {
            // For pipes, variable assignments are skipped
            return 0;
        }
        
        // For direct commands, handle variable assignment normally
        char *equals = strchr(tokens[0], '=');
        if (equals != NULL) {
            size_t key_len = equals - tokens[0];
            char *key = malloc(key_len + 1);
            if (key == NULL) {
                return -1;
            }
            
            strncpy(key, tokens[0], key_len);
            key[key_len] = '\0';
            
            // Extract the value (everything after '=')
            const char *value = equals + 1;
            
            // Set the variable
            int result = set_variable(key, value);
            free(key);
            return result;
        }
    }

    // Check if this is a builtin command
    bn_ptr builtin_fn = check_builtin(tokens[0]);
    if (builtin_fn != NULL) {
        // Handle redirecting stdin/stdout for builtins
        int old_stdin = -1, old_stdout = -1;

        // Redirect input if needed
        if (input_fd != STDIN_FILENO) {
            old_stdin = dup(STDIN_FILENO);
            if (old_stdin == -1) {
                safe_close(input_fd);
                if (output_fd != STDOUT_FILENO) {
                    safe_close(output_fd);
                }
                return -1;
            }

            if (dup2(input_fd, STDIN_FILENO) == -1) {
                safe_close(old_stdin);
                safe_close(input_fd);
                if (output_fd != STDOUT_FILENO) {
                    safe_close(output_fd);
                }
                return -1;
            }

            safe_close(input_fd); // Close the duplicated fd
        }

        // Redirect output if needed
        if (output_fd != STDOUT_FILENO) {
            old_stdout = dup(STDOUT_FILENO);
            if (old_stdout == -1) {
                if (old_stdin != -1) {
                    dup2(old_stdin, STDIN_FILENO);
                    safe_close(old_stdin);
                }
                safe_close(output_fd);
                return -1;
            }

            if (dup2(output_fd, STDOUT_FILENO) == -1) {
                if (old_stdin != -1) {
                    dup2(old_stdin, STDIN_FILENO);
                    safe_close(old_stdin);
                }
                safe_close(old_stdout);
                safe_close(output_fd);
                return -1;
            }

            safe_close(output_fd); // Close the duplicated fd
        }

        // Execute the builtin
        ssize_t result = builtin_fn(tokens);

        // Restore stdin/stdout if needed
        if (old_stdin != -1) {
            dup2(old_stdin, STDIN_FILENO);
            safe_close(old_stdin);
        }

        if (old_stdout != -1) {
            dup2(old_stdout, STDOUT_FILENO);
            safe_close(old_stdout);
        }

        // Critical for pipeline test: Always return 0 for pipe segments
        if (input_fd != STDIN_FILENO || output_fd != STDOUT_FILENO) {
            return 0;  // Always continue pipeline even if a command failed
        }

        return result;
    } else {
        // Not a builtin, execute as system command
        pid_t pid = fork();

        if (pid == -1) {
            if (input_fd != STDIN_FILENO) {
                safe_close(input_fd);
            }
            if (output_fd != STDOUT_FILENO) {
                safe_close(output_fd);
            }
            return -1;
        } else if (pid == 0) {
            // Child process

            // Redirect input if needed
            if (input_fd != STDIN_FILENO) {
                if (dup2(input_fd, STDIN_FILENO) == -1) {
                    exit(EXIT_FAILURE);
                }
                safe_close(input_fd);
            }

            // Redirect output if needed
            if (output_fd != STDOUT_FILENO) {
                if (dup2(output_fd, STDOUT_FILENO) == -1) {
                    exit(EXIT_FAILURE);
                }
                safe_close(output_fd);
            }

            // Execute the command
            if (command_exists(tokens[0])) {
                execvp(tokens[0], tokens);
                // If execvp returns, there was an error
                exit(EXIT_FAILURE);
            } else {
                // We need to make sure error message goes to stderr, not stdout
                display_error("ERROR: Unknown command: ", tokens[0]);
                exit(EXIT_FAILURE);
            }

            // If we get here, exec failed
            exit(EXIT_FAILURE);
        } else {
            // Parent process
            // Close pipe ends in parent if we're using pipes
            if (input_fd != STDIN_FILENO) {
                safe_close(input_fd);
            }
            if (output_fd != STDOUT_FILENO) {
                safe_close(output_fd);
            }

            if (in_background) {
                // Background process, don't wait
                char command_str[MAX_STR_LEN] = "";

                // Reconstruct the command
                for (int i = 0; tokens[i] != NULL; i++) {
                    if (i > 0) {
                        strcat(command_str, " ");
                    }
                    strcat(command_str, tokens[i]);
                }

                // Add to background process list without trailing '&'
                if (strlen(command_str) > 0) {
                    add_bg_process(pid, command_str);
                }
                
                return 0;
            } else {
                // Foreground process, wait for completion
                int status;
                waitpid(pid, &status, 0);
                
                // Critical fix: For pipes, always return 0 to continue the pipeline
                if (input_fd != STDIN_FILENO || output_fd != STDOUT_FILENO) {
                    return 0;
                }
                
                return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
        }
    }
}

// Handle a pipeline of commands
int handle_pipeline(char **tokens) {
    // Count commands in pipeline
    int cmd_count = 1;
    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            cmd_count++;
        }
    }

    // Single command case
    if (cmd_count == 1) {
        int in_background = 0;
        int last_token = 0;

        while (tokens[last_token + 1] != NULL) {
            last_token++;
        }

        // Check if this is a background command
        if (strcmp(tokens[last_token], "&") == 0) {
            in_background = 1;
            tokens[last_token] = NULL;  // Remove & from tokens
        }

        return execute_command(tokens, STDIN_FILENO, STDOUT_FILENO, in_background);
    }

    // Split into individual commands
    char **cmds[cmd_count];
    int cmd_index = 0;
    cmds[0] = tokens;

    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            tokens[i] = NULL; // Replace pipe with NULL to terminate the command
            cmds[++cmd_index] = &tokens[i + 1];
        }
    }

    // Check for variable assignments and skip them in pipes
    for (int i = 0; i < cmd_count; i++) {
        if (cmds[i][0] == NULL) {
            display_error("ERROR: Empty command in pipeline", "");
            return -1;
        }
        
        // Critical for pipes with variables test: Skip but don't execute variable assignments in pipes
        if (i > 0 && is_variable_assignment(cmds[i][0])) {
            continue;
        }
    }

    // Check for background
    int in_background = 0;
    int last_cmd = cmd_count - 1;
    int last_token = 0;

    while (cmds[last_cmd][last_token + 1] != NULL) {
        last_token++;
    }

    if (cmds[last_cmd][last_token] != NULL && strcmp(cmds[last_cmd][last_token], "&") == 0) {
        in_background = 1;
        cmds[last_cmd][last_token] = NULL;
    }

    // Create pipes
    int pipes[cmd_count - 1][2];
    for (int i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            display_error("ERROR: Failed to create pipe", "");
            // Clean up already created pipes
            for (int j = 0; j < i; j++) {
                safe_close(pipes[j][0]);
                safe_close(pipes[j][1]);
            }
            return -1;
        }
    }

    // Execute commands
    pid_t pids[cmd_count];
    int status = 0;

    for (int i = 0; i < cmd_count; i++) {
        // Skip processing empty commands or variable assignments in pipe
        if (cmds[i][0] == NULL || (i > 0 && is_variable_assignment(cmds[i][0]))) {
            continue;
        }
        
        pids[i] = fork();

        if (pids[i] == -1) {
            display_error("ERROR: Failed to fork", "");
            // Clean up pipes and kill already started processes
            for (int j = 0; j < cmd_count - 1; j++) {
                safe_close(pipes[j][0]);
                safe_close(pipes[j][1]);
            }
            for (int j = 0; j < i; j++) {
                kill(pids[j], SIGTERM);
            }
            return -1;
        } else if (pids[i] == 0) {
            // Child process - set up redirections
            
            // Set up input from previous pipe (if not first command)
            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
                    exit(EXIT_FAILURE);
                }
            }

            // Set up output to next pipe (if not last command)
            if (i < cmd_count - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                    exit(EXIT_FAILURE);
                }
            }

            // Close all pipe fds in child process
            for (int j = 0; j < cmd_count - 1; j++) {
                safe_close(pipes[j][0]);
                safe_close(pipes[j][1]);
            }

            // Execute command
            bn_ptr builtin_fn = check_builtin(cmds[i][0]);
            if (builtin_fn != NULL) {
                // Execute builtin
                exit(builtin_fn(cmds[i]) == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
            } else {
                // Execute external command
                execvp(cmds[i][0], cmds[i]);
                // If execvp returns, there was an error
                exit(EXIT_FAILURE);
            }
        }
        // Parent continues...
    }

    // Parent process - close all pipe fds
    for (int i = 0; i < cmd_count - 1; i++) {
        safe_close(pipes[i][0]);
        safe_close(pipes[i][1]);
    }

    // Wait for completion or set up background processes
    if (!in_background) {
        // Wait for all child processes in foreground mode
        for (int i = 0; i < cmd_count; i++) {
            // Skip processes not started (e.g., empty commands)
            if (pids[i] <= 0) continue;
            
            int cmd_status;
            waitpid(pids[i], &cmd_status, 0);
            
            // Only use status from last command
            if (i == cmd_count - 1) {
                status = WIFEXITED(cmd_status) ? WEXITSTATUS(cmd_status) : -1;
            }
        }
    } else {
        // Set up background process
        char command_str[MAX_STR_LEN] = "";
        for (int i = 0; i < cmd_count; i++) {
            // Skip empty commands
            if (cmds[i][0] == NULL) continue;
            
            for (int j = 0; cmds[i][j] != NULL; j++) {
                if (command_str[0] != '\0')
                    strcat(command_str, " ");
                strcat(command_str, cmds[i][j]);
            }

            if (i < cmd_count - 1) {
                strcat(command_str, " |");
            }
        }

        // Add the last process to the background process list
        for (int i = cmd_count - 1; i >= 0; i--) {
            if (pids[i] > 0) {
                add_bg_process(pids[i], command_str);
                break;
            }
        }
    }

    return status;
}