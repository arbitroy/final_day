#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>

#include "io_helpers.h"

// Debug flag
#define DEBUG_MODE 0

// Helper function for debugging
void io_debug_log(const char *format, ...)
{
    if (!DEBUG_MODE)
        return;

    va_list args;
    va_start(args, format);

    fprintf(stderr, "[IO_DEBUG] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");

    va_end(args);
}

// ===== Output helpers =====

/* Prereq: str is a NULL terminated string
 */
void display_message(const char *str)
{
    if (str == NULL)
        return;
    write(STDOUT_FILENO, str, strnlen(str, MAX_STR_LEN));
}

/* Prereq: pre_str, str are NULL terminated string
 */
void display_error(const char *pre_str, const char *str)
{
    if (pre_str == NULL)
        pre_str = "";
    if (str == NULL)
        str = "";

    write(STDERR_FILENO, pre_str, strnlen(pre_str, MAX_STR_LEN));
    write(STDERR_FILENO, str, strnlen(str, MAX_STR_LEN));
    write(STDERR_FILENO, "\n", 1);
}

// ===== Input tokenizing =====

/* Prereq: in_ptr points to a character buffer of size > MAX_STR_LEN
 * Return: number of bytes read
 */
ssize_t get_input(char *in_ptr)
{
    int retval = read(STDIN_FILENO, in_ptr, MAX_STR_LEN + 1); // Not a sanitizer issue since in_ptr is allocated as MAX_STR_LEN+1
    int read_len = retval;
    if (retval == -1)
    {
        read_len = 0;
    }
    if (read_len > MAX_STR_LEN)
    {
        read_len = 0;
        retval = -1;
        write(STDERR_FILENO, "ERROR: input line too long\n", strlen("ERROR: input line too long\n"));
        int junk = 0;
        while ((junk = getchar()) != EOF && junk != '\n')
            ;
    }
    in_ptr[read_len] = '\0';

    io_debug_log("Read input: '%s'", in_ptr);
    return retval;
}

/* Prereq: in_ptr is a string, tokens is of size >= len(in_ptr)
 * Warning: in_ptr is modified
 * Return: number of tokens.
 */

size_t tokenize_input(char *in_ptr, char **tokens) {
    // Check if this is an echo command
    int is_echo_command = 0;
    char *echo_cmd = strstr(in_ptr, "echo ");
    
    // Only consider it an echo command if it's at the beginning or after a pipe
    if (echo_cmd == in_ptr || (echo_cmd > in_ptr && *(echo_cmd-1) == ' ' && 
                              (echo_cmd > in_ptr+1 && *(echo_cmd-2) == '|'))) {
        is_echo_command = 1;
    }

    // First, explicitly handle special characters by ensuring they are surrounded by spaces
    char *temp_ptr = in_ptr;
    while (*temp_ptr != '\0') {
        // We're looking for pipe characters (|) and standalone background characters (&)
        if (*temp_ptr == '|' || (*temp_ptr == '&' && !is_echo_command)) {
            // Insert spaces around character if needed
            if (temp_ptr > in_ptr && *(temp_ptr - 1) != ' ') {
                // Shift everything right to make room for a space before character
                size_t rest_len = strlen(temp_ptr);
                memmove(temp_ptr + 1, temp_ptr, rest_len + 1); // +1 for null terminator
                *temp_ptr = ' ';
                temp_ptr += 2; // Skip the space and the special character
            } else {
                temp_ptr++; // Skip the special character
            }
            
            if (*temp_ptr != ' ' && *temp_ptr != '\0') {
                // Shift everything right to make room for a space after character
                size_t rest_len = strlen(temp_ptr);
                memmove(temp_ptr + 1, temp_ptr, rest_len + 1); // +1 for null terminator
                *temp_ptr = ' ';
                temp_ptr += 1; // Skip the space
            }
        } else {
            // Special handling for & character in echo arguments
            if (is_echo_command && *temp_ptr == '&') {
                // If we're in an echo command and found an & within the echo arguments,
                // we should check if it's meant to be a background process marker

                // It's a background process marker if it's at the end of the command
                // or followed by only whitespace
                char *check_ptr = temp_ptr + 1;
                int is_bg_marker = 1;

                // Skip whitespace
                while (*check_ptr == ' ' || *check_ptr == '\t') {
                    check_ptr++;
                }

                // If there's more content, it's not a background process marker
                if (*check_ptr != '\0') {
                    is_bg_marker = 0;
                }

                if (is_bg_marker) {
                    // This is a background marker, handle it like other special characters
                    if (temp_ptr > in_ptr && *(temp_ptr - 1) != ' ') {
                        // Shift everything right to make room for a space before character
                        size_t rest_len = strlen(temp_ptr);
                        memmove(temp_ptr + 1, temp_ptr, rest_len + 1); // +1 for null terminator
                        *temp_ptr = ' ';
                        temp_ptr += 2; // Skip the space and the special character
                    } else {
                        temp_ptr++; // Skip the special character
                    }
                    
                    if (*temp_ptr != ' ' && *temp_ptr != '\0') {
                        // Shift everything right to make room for a space after character
                        size_t rest_len = strlen(temp_ptr);
                        memmove(temp_ptr + 1, temp_ptr, rest_len + 1); // +1 for null terminator
                        *temp_ptr = ' ';
                        temp_ptr += 1; // Skip the space
                    }
                } else {
                    // This is a regular & in echo argument, leave it alone
                    temp_ptr++;
                }
            } else {
                temp_ptr++;
            }
        }
    }

    // Split by whitespace
    size_t token_count = 0;
    char *curr_ptr = strtok(in_ptr, DELIMITERS);
    
    while (curr_ptr != NULL) {
        tokens[token_count] = curr_ptr;
        token_count++;
        curr_ptr = strtok(NULL, DELIMITERS);
    }
    tokens[token_count] = NULL;
    
    return token_count;
}
/* Combines multiple tokens into a single string with spaces in between
 * Prereq: tokens is a NULL-terminated array of strings
 * Returns: A newly allocated string that must be freed by the caller
 */
char *combine_tokens(char **tokens, int start_idx)
{
    if (tokens == NULL || tokens[start_idx] == NULL)
    {
        return strdup("");
    }

    // First, calculate the total length needed
    size_t total_len = 0;
    for (int i = start_idx; tokens[i] != NULL; i++)
    {
        total_len += strlen(tokens[i]);
        if (tokens[i + 1] != NULL)
        {
            total_len++; // For space between tokens
        }
    }

    // Allocate the memory
    char *result = malloc(total_len + 1); // +1 for null terminator
    if (result == NULL)
    {
        return NULL;
    }

    // Combine the tokens
    result[0] = '\0'; // Start with empty string
    for (int i = start_idx; tokens[i] != NULL; i++)
    {
        strcat(result, tokens[i]);
        if (tokens[i + 1] != NULL)
        {
            strcat(result, " ");
        }
    }

    return result;
}

/* Checks if a string contains a pipe character
 * Returns: 1 if contains pipe, 0 otherwise
 */
int contains_pipe(const char *str)
{
    if (str == NULL)
        return 0;
    int has_pipe = strchr(str, '|') != NULL;
    io_debug_log("Checking for pipe in '%s': %s", str, has_pipe ? "Found" : "Not found");
    return has_pipe;
}