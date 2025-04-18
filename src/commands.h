#ifndef __COMMANDS_H__
#define __COMMANDS_H__

#include <unistd.h>
#include <signal.h>

// Process tracking structure
typedef struct bg_process {
    pid_t pid;
    int job_id;
    char *command;
    struct bg_process *next;
} bg_process_t;

// Message queue for background process completion notifications
typedef struct bg_message {
    char *message;
    struct bg_message *next;
} bg_message_t;

// Initialize background process tracking
void init_bg_processes();

// Add a background process
int add_bg_process(pid_t pid, const char *command);

// Remove a background process
void remove_bg_process(pid_t pid);

// List all background processes
void list_bg_processes();

// Find process by pid
bg_process_t *find_bg_process_by_pid(pid_t pid);

// Clean up background process tracking
void free_bg_processes();

// Mark a process as completed
void mark_process_completed(pid_t pid);

// Background message queue functions
void add_bg_message(const char *message);
char *get_next_bg_message();
int has_bg_messages();
void free_bg_messages();

// Execute a command with pipe support
int execute_command(char **tokens, int input_fd, int output_fd, int in_background);

// Execute a system command
int execute_system_command(char **tokens, int input_fd, int output_fd, int in_background);

// Handle a pipeline of commands
int handle_pipeline(char **tokens);

// Command functions
ssize_t cmd_kill(char **tokens);
ssize_t cmd_ps(char **tokens);

#endif