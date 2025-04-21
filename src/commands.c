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
int add_bg_process(pid_t pid, const char *command)
{
    debug_log("Adding background process: pid=%d, command=%s", pid, command);

    bg_process_t *new_process = malloc(sizeof(bg_process_t));
    if (new_process == NULL)
    {
        debug_log("Failed to allocate memory for background process");
        return -1;
    }

    new_process->pid = pid;
    new_process->job_id = next_job_id++;
    new_process->command = strdup(command);
    if (new_process->command == NULL)
    {
        debug_log("Failed to duplicate command string");
        free(new_process);
        return -1;
    }

    new_process->next = bg_process_list;
    bg_process_list = new_process;

    // Display job information with exact format required by tests
    char buffer[MAX_STR_LEN];
    snprintf(buffer, MAX_STR_LEN, "[%d] %d", new_process->job_id, pid);
    display_message(buffer);
    display_message("\n");

    debug_log("Added background process: job_id=%d, pid=%d", new_process->job_id, pid);
    return new_process->job_id;
}

// Remove a background process
void remove_bg_process(pid_t pid)
{
    bg_process_t *current = bg_process_list;
    bg_process_t *prev = NULL;

    while (current != NULL)
    {
        if (current->pid == pid)
        {
            if (prev == NULL)
            {
                bg_process_list = current->next;
            }
            else
            {
                prev->next = current->next;
            }

            free(current->command);
            free(current);

            // FIX: Reset job ID counter when all processes are done
            if (bg_process_list == NULL)
            {
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
    }
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
int execute_command(char **tokens, int input_fd, int output_fd, int in_background)
{
    if (tokens == NULL || tokens[0] == NULL)
    {
        return -1;
    }

    // Check if this is a builtin command
    bn_ptr builtin_fn = check_builtin(tokens[0]);
    if (builtin_fn != NULL)
    {
        // Handle redirecting stdin/stdout for builtins
        int old_stdin = -1, old_stdout = -1;

        // Redirect input if needed
        if (input_fd != STDIN_FILENO)
        {
            old_stdin = dup(STDIN_FILENO);
            if (old_stdin == -1)
            {
                perror("dup stdin");
                safe_close(input_fd);
                if (output_fd != STDOUT_FILENO)
                {
                    safe_close(output_fd);
                }
                return -1;
            }

            if (dup2(input_fd, STDIN_FILENO) == -1)
            {
                perror("dup2 stdin");
                safe_close(old_stdin);
                safe_close(input_fd);
                if (output_fd != STDOUT_FILENO)
                {
                    safe_close(output_fd);
                }
                return -1;
            }

            safe_close(input_fd); // Close the duplicated fd
        }

        // Redirect output if needed
        if (output_fd != STDOUT_FILENO)
        {
            old_stdout = dup(STDOUT_FILENO);
            if (old_stdout == -1)
            {
                perror("dup stdout");
                if (old_stdin != -1)
                {
                    dup2(old_stdin, STDIN_FILENO);
                    safe_close(old_stdin);
                }
                safe_close(output_fd);
                return -1;
            }

            if (dup2(output_fd, STDOUT_FILENO) == -1)
            {
                perror("dup2 stdout");
                if (old_stdin != -1)
                {
                    dup2(old_stdin, STDIN_FILENO);
                    safe_close(old_stdin);
                }
                // Process any immediate completion messages
                char *immediate_msg;
                while ((immediate_msg = get_next_bg_message()) != NULL) {
                    display_message(immediate_msg);
                    display_message("\n");
                    free(immediate_msg);
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
        if (old_stdin != -1)
        {
            dup2(old_stdin, STDIN_FILENO);
            safe_close(old_stdin);
        }

        if (old_stdout != -1)
        {
            dup2(old_stdout, STDOUT_FILENO);
            safe_close(old_stdout);
        }

        return result;
    }
    else
    {
        // Not a builtin, execute as system command
        pid_t pid = fork();

        if (pid == -1)
        {
            display_error("ERROR: Failed to fork", "");
            if (input_fd != STDIN_FILENO)
            {
                safe_close(input_fd);
            }
            if (output_fd != STDOUT_FILENO)
            {
                safe_close(output_fd);
            }
            return -1;
        }
        else if (pid == 0)
        {
            // Child process

            // Redirect input if needed
            if (input_fd != STDIN_FILENO)
            {
                if (dup2(input_fd, STDIN_FILENO) == -1)
                {
                    perror("dup2 stdin");
                    exit(EXIT_FAILURE);
                }
                safe_close(input_fd);
            }

            // Redirect output if needed
            if (output_fd != STDOUT_FILENO)
            {
                if (dup2(output_fd, STDOUT_FILENO) == -1)
                {
                    perror("dup2 stdout");
                    exit(EXIT_FAILURE);
                }
                safe_close(output_fd);
            }

            // Execute the command
            if (command_exists(tokens[0]))
            {
                execvp(tokens[0], tokens);
                // If execvp returns, there was an error
                perror("execvp");
                exit(EXIT_FAILURE); // Make sure we exit immediately on error
            }
            else
            {
                // IMPORTANT CHANGE: Ensure error message goes to stderr
                int stderr_copy = dup(STDERR_FILENO);
                if (stderr_copy != -1)
                {
                    display_error("ERROR: Unknown command: ", tokens[0]);
                    dup2(stderr_copy, STDERR_FILENO);
                    close(stderr_copy);
                }
                exit(EXIT_FAILURE);
            }

            // If we get here, exec failed
            exit(EXIT_FAILURE);
        }
        else
        {
            // Parent process
            // Close pipe ends in parent if we're using pipes
            if (input_fd != STDIN_FILENO)
            {
                safe_close(input_fd);
            }
            if (output_fd != STDOUT_FILENO)
            {
                safe_close(output_fd);
            }

            if (in_background)
            {
                // Background process, don't wait
                char command_str[MAX_STR_LEN] = "";

                // Reconstruct the command
                for (int i = 0; tokens[i] != NULL; i++)
                {
                    if (i > 0)
                    {
                        strcat(command_str, " ");
                    }
                    strcat(command_str, tokens[i]);
                }

                // Add to background process list
                add_bg_process(pid, command_str);

                return 0;
            }
            else
            {
                // Foreground process, wait for completion
                int status;
                waitpid(pid, &status, 0);

                // KEY CHANGE: Always return 0 in a pipe chain, even if a command fails
                // This ensures the pipeline doesn't block waiting for the next command
                if (input_fd != STDIN_FILENO || output_fd != STDOUT_FILENO)
                {
                    return 0; // Always continue pipe chains
                }
                return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
        }
    }
}

// Handle a pipeline of commands
int handle_pipeline(char **tokens)
{
    // Count commands in pipeline
    int cmd_count = 1;
    for (int i = 0; tokens[i] != NULL; i++)
    {
        if (strcmp(tokens[i], "|") == 0)
        {
            cmd_count++;
        }
    }

    debug_log("Pipeline has %d commands", cmd_count);

    // Single command case
    if (cmd_count == 1)
    {
        int in_background = 0;
        int last_token = 0;

        while (tokens[last_token + 1] != NULL)
        {
            last_token++;
        }

        if (strcmp(tokens[last_token], "&") == 0)
        {
            in_background = 1;
            tokens[last_token] = NULL;
        }

        debug_log("Executing single command: %s (background: %d)", tokens[0], in_background);
        return execute_command(tokens, STDIN_FILENO, STDOUT_FILENO, in_background);
    }

    // Split into individual commands
    char **cmds[cmd_count];
    int cmd_index = 0;
    cmds[0] = tokens;

    debug_log("Splitting pipeline into commands");
    for (int i = 0; tokens[i] != NULL; i++)
    {
        if (strcmp(tokens[i], "|") == 0)
        {
            tokens[i] = NULL; // Replace pipe with NULL to terminate the command
            cmds[++cmd_index] = &tokens[i + 1];
            debug_log("Command %d starts at token index %d", cmd_index, i + 1);
        }
    }

    // Verify all commands
    for (int i = 0; i < cmd_count; i++)
    {
        if (cmds[i][0] == NULL)
        {
            display_error("ERROR: Empty command in pipeline", "");
            return -1;
        }

        debug_log("Verifying command %d: %s", i, cmds[i][0]);

        // Check command existence first
        if (check_builtin(cmds[i][0]) == NULL && !command_exists(cmds[i][0]))
        {
            display_error("ERROR: Unknown command: ", cmds[i][0]);
            return -1;
        }
    }

    // Check for background
    int in_background = 0;
    int last_cmd = cmd_count - 1;
    int last_token = 0;

    while (cmds[last_cmd][last_token + 1] != NULL)
    {
        last_token++;
    }

    if (cmds[last_cmd][last_token] != NULL && strcmp(cmds[last_cmd][last_token], "&") == 0)
    {
        in_background = 1;
        cmds[last_cmd][last_token] = NULL;
        debug_log("Pipeline will run in background");
    }

    // Create pipes
    int pipes[cmd_count - 1][2];
    debug_log("Creating %d pipes for command pipeline", cmd_count - 1);

    for (int i = 0; i < cmd_count - 1; i++)
    {
        if (pipe(pipes[i]) == -1)
        {
            display_error("ERROR: Failed to create pipe", "");
            for (int j = 0; j < i; j++)
            {
                safe_close(pipes[j][0]);
                safe_close(pipes[j][1]);
            }
            return -1;
        }
        debug_log("Created pipe %d: read_fd=%d, write_fd=%d", i, pipes[i][0], pipes[i][1]);
    }

    // Duplicate parent's variable environment for child processes
    // FIX: Make sure each process gets variables from parent
    variable_t *parent_vars = duplicate_variables();

    // Execute commands
    pid_t pids[cmd_count];
    int status = 0;

    for (int i = 0; i < cmd_count; i++)
    {
        debug_log("Forking for command %d: %s", i, cmds[i][0]);
        pids[i] = fork();

        if (pids[i] == -1)
        {
            display_error("ERROR: Failed to fork", "");

            // Clean up pipes and processes
            for (int j = 0; j < cmd_count - 1; j++)
            {
                safe_close(pipes[j][0]);
                safe_close(pipes[j][1]);
            }
            for (int j = 0; j < i; j++)
            {
                kill(pids[j], SIGTERM);
            }
            return -1;
        }
        else if (pids[i] == 0)
        {
            // Child process
            debug_log("[Child %d] Setting up redirections for command: %s", getpid(), cmds[i][0]);

            // FIX: Set up variables for the child process
            if (parent_vars != NULL)
            {
                // Give each child a copy of the parent's variables
                variable_t *child_vars = duplicate_variables();
                if (child_vars != NULL)
                {
                    set_variable_list(child_vars);
                }
            }

            // Set up redirections
            if (i > 0)
            {
                // Not the first command, redirect stdin from previous pipe
                debug_log("[Child %d] Redirecting stdin from pipe %d (fd %d)",
                          getpid(), i - 1, pipes[i - 1][0]);
                if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1)
                {
                    perror("dup2 stdin");
                    exit(EXIT_FAILURE);
                }
            }

            if (i < cmd_count - 1)
            {
                // Not the last command, redirect stdout to next pipe
                debug_log("[Child %d] Redirecting stdout to pipe %d (fd %d)",
                          getpid(), i, pipes[i][1]);
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1)
                {
                    perror("dup2 stdout");
                    exit(EXIT_FAILURE);
                }
            }

            // Close all pipe fds
            debug_log("[Child %d] Closing all pipe fds", getpid());
            for (int j = 0; j < cmd_count - 1; j++)
            {
                safe_close(pipes[j][0]);
                safe_close(pipes[j][1]);
            }

            // Execute command
            bn_ptr builtin_fn = check_builtin(cmds[i][0]);
            if (builtin_fn != NULL)
            {
                debug_log("[Child %d] Executing builtin: %s", getpid(), cmds[i][0]);
                exit(builtin_fn(cmds[i]) == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
            }
            else
            {
                debug_log("[Child %d] Executing external command: %s", getpid(), cmds[i][0]);
                execvp(cmds[i][0], cmds[i]);
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        }
        // Parent continues...
    }

    // Parent process
    debug_log("[Parent] Closing all pipe fds");
    for (int i = 0; i < cmd_count - 1; i++)
    {
        safe_close(pipes[i][0]);
        safe_close(pipes[i][1]);
    }

    // Wait for completion unless background
    if (!in_background)
    {
        debug_log("[Parent] Waiting for all child processes to complete");
        for (int i = 0; i < cmd_count; i++)
        {
            int cmd_status;
            debug_log("[Parent] Waiting for command %d (pid %d)", i, pids[i]);
            
            // Add a timeout for the waitpid call
            int wait_status = waitpid(pids[i], &cmd_status, WNOHANG);
            if (wait_status == 0) {
                // Process hasn't finished yet, wait with a timeout
                int waited_ms = 0;
                const int max_wait_ms = 500; // 500ms max wait
                
                while (waitpid(pids[i], &cmd_status, WNOHANG) == 0 && waited_ms < max_wait_ms) {
                    usleep(10000); // 10ms wait
                    waited_ms += 10;
                }
                
                if (waited_ms >= max_wait_ms) {
                    // Process is taking too long, kill it
                    kill(pids[i], SIGTERM);
                    waitpid(pids[i], &cmd_status, 0);
                }
            }
            
            // Process the exit status
            if (WIFEXITED(cmd_status)) {
                status = WEXITSTATUS(cmd_status);
            }
        }
    }
    else
    {
        // Background process handling
        debug_log("[Parent] Setting up background process for pipeline");
        char command_str[MAX_STR_LEN] = "";
        for (int i = 0; i < cmd_count; i++)
        {
            for (int j = 0; cmds[i][j] != NULL; j++)
            {
                if (command_str[0] != '\0')
                    strcat(command_str, " ");
                strcat(command_str, cmds[i][j]);
            }

            if (i < cmd_count - 1)
            {
                strcat(command_str, " |");
            }
        }

        if (in_background)
        {
            strcat(command_str, " &");
        }

        add_bg_process(pids[cmd_count - 1], command_str);
        debug_log("[Parent] Background process added: %s", command_str);
    }

    debug_log("[Parent] Pipeline execution complete with status %d", status);
    return status;
}