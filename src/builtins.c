#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>

#include "builtins.h"
#include "io_helpers.h"
#include "variables.h"

// ====== Command execution =====

/* Return: index of builtin or -1 if cmd doesn't match a builtin
 */
bn_ptr check_builtin(const char *cmd) {
    ssize_t cmd_num = 0;
    while (cmd_num < BUILTINS_COUNT &&
           strncmp(BUILTINS[cmd_num], cmd, MAX_STR_LEN) != 0) {
        cmd_num += 1;
    }
    if (cmd_num >= BUILTINS_COUNT) {
        return NULL;
    }
    return BUILTINS_FN[cmd_num];
}


// ===== Builtins =====

/* Prereq: tokens is a NULL terminated sequence of strings.
 * Return 0 on success and -1 on error ... but there are no errors on echo. 
 */

 ssize_t bn_echo(char **tokens) {
    // Normal echo behavior
    ssize_t index = 1;
    int first = 1;
    
    // If no arguments, just print a newline
    if (tokens[1] == NULL) {
        write(STDOUT_FILENO, "\n", 1);
        return 0;
    }
    
    while (tokens[index] != NULL) {
        if (!first) {
            write(STDOUT_FILENO, " ", 1);
        }
        write(STDOUT_FILENO, tokens[index], strlen(tokens[index]));
        first = 0;
        index += 1;
    }
    write(STDOUT_FILENO, "\n", 1);
    
    return 0;
}

/* Helper function to recursively list directory contents
 * Return 0 on success and -1 on error.
 */
ssize_t list_directory_recursive(const char *path, int depth, const char *substring) {
    DIR *dir = opendir(path);
    if (!dir) {
        display_error("ERROR: Invalid path", "");
        return -1;
    }
    
    struct dirent *entry;
    char entries[MAX_STR_LEN][MAX_STR_LEN]; // To store entries for sorting
    int entry_count = 0;
    
    // First, collect all entries
    while ((entry = readdir(dir)) != NULL) {
        // If substring filter is active, check if name contains it
        if (substring != NULL && strstr(entry->d_name, substring) == NULL) {
            continue;
        }
        
        // Add to our collection
        if (entry_count < MAX_STR_LEN) {
            strncpy(entries[entry_count], entry->d_name, MAX_STR_LEN - 1);
            entries[entry_count][MAX_STR_LEN - 1] = '\0';
            entry_count++;
        }
    }
    
    // Display the entries
    for (int i = 0; i < entry_count; i++) {
        display_message(entries[i]);
        display_message("\n");
    }
    
    // If we should recurse into subdirectories and depth allows
    if (depth > 1 || depth == -1) {
        rewinddir(dir);
        
        while ((entry = readdir(dir)) != NULL) {
            // Skip . and ..
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            // Create full path for the entry
            char full_path[PATH_MAX];
            snprintf(full_path, PATH_MAX, "%s/%s", path, entry->d_name);
            
            // Check if it's a directory
            struct stat st;
            if (stat(full_path, &st) != 0) {
                // Skip this entry if we can't stat it
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                // Only recurse if we have depth remaining
                int new_depth = (depth == -1) ? -1 : depth - 1;
                
                if (new_depth != 0) {
                    list_directory_recursive(full_path, new_depth, substring);
                }
            }
        }
    }
    
    closedir(dir);
    return 0;
}

/* Prereq: tokens is a NULL terminated sequence of strings.
 * Return 0 on success and -1 on error.
 */
ssize_t bn_ls(char **tokens) {
    char *path = ".";  // Default to current directory
    char *substring = NULL;
    int recursive = 0;
    int depth = -1;  // Default to unlimited depth
    int arg_index = 1;
    
    // Parse arguments
    while (tokens[arg_index] != NULL) {
        if (strcmp(tokens[arg_index], "--f") == 0) {
            if (tokens[arg_index + 1] == NULL) {
                display_error("ERROR: Builtin failed: ls", "");
                return -1;
            }
            substring = tokens[arg_index + 1];
            arg_index += 2;  // Skip the --f and its argument
        } else if (strcmp(tokens[arg_index], "--rec") == 0) {
            recursive = 1;
            arg_index++;
        } else if (strcmp(tokens[arg_index], "--d") == 0) {
            if (tokens[arg_index + 1] == NULL) {
                display_error("ERROR: Builtin failed: ls", "");
                return -1;
            }
            depth = atoi(tokens[arg_index + 1]);
            arg_index += 2;  // Skip the --d and its argument
        } else {
            // Assume this is the path
            path = tokens[arg_index];
            arg_index++;
            
            // Check if there are too many arguments
            if (tokens[arg_index] != NULL && 
                strcmp(tokens[arg_index], "--f") != 0 && 
                strcmp(tokens[arg_index], "--rec") != 0 && 
                strcmp(tokens[arg_index], "--d") != 0) {
                display_error("ERROR: Too many arguments: ls takes a single", " path");
                return -1;
            }
        }
    }
    
    // Check if --d is provided without --rec
    if (depth != -1 && !recursive) {
        display_error("ERROR: --d requires --rec", "");
        return -1;
    }
    
    // Open the directory
    DIR *dir = opendir(path);
    if (!dir) {
        display_error("ERROR: Invalid path", "");
        display_error("ERROR: Builtin failed: ls", "");
        return -1;
    }
    
    // For non-recursive listing
    if (!recursive) {
        struct dirent *entry;
        char entries[MAX_STR_LEN][MAX_STR_LEN]; // To store entries for sorting
        int entry_count = 0;
        
        // First, collect all entries
        while ((entry = readdir(dir)) != NULL) {
            // If substring filter is active, check if name contains it
            if (substring != NULL && strstr(entry->d_name, substring) == NULL) {
                continue;
            }
            
            // Add to our collection
            if (entry_count < MAX_STR_LEN) {
                strncpy(entries[entry_count], entry->d_name, MAX_STR_LEN - 1);
                entries[entry_count][MAX_STR_LEN - 1] = '\0';
                entry_count++;
            }
        }
        
        // Display the entries
        for (int i = 0; i < entry_count; i++) {
            display_message(entries[i]);
            display_message("\n");
        }
        
        closedir(dir);
        return 0;
    }
    
    // For recursive listing - close and reopen to reset directory stream
    closedir(dir);
    
    // If we're doing recursive listing, use the helper function
    return list_directory_recursive(path, depth, substring);
}

/* Prereq: tokens is a NULL terminated sequence of strings.
 * Return 0 on success and -1 on error.
 */
ssize_t bn_cd(char **tokens) {
    // Check if a path is provided
    if (tokens[1] == NULL) {
        display_error("ERROR: Builtin failed: cd", "");
        return -1;
    }

    // Check if too many arguments are provided
    if (tokens[2] != NULL) {
        display_error("ERROR: Too many arguments: cd takes a single", " path");
        return -1;
    }

    // Support for special paths
    char *path = tokens[1];
    
    // Handle special cases
    if (strcmp(path, "...") == 0) {
        path = "../..";
    } else if (strcmp(path, "....") == 0) {
        path = "../../..";
    }
    
    // Change directory
    if (chdir(path) != 0) {
        display_error("ERROR: Invalid path", "");
        display_error("ERROR: Builtin failed: cd", "");
        return -1;
    }
    
    return 0;
}


/* Prereq: tokens is a NULL terminated sequence of strings.
 * Return 0 on success and -1 on error.
 */
ssize_t bn_cat(char **tokens) {
    // Handle stdin case (from pipe or redirection)
    if (tokens[1] == NULL) {
        char buffer[MAX_STR_LEN];
        ssize_t bytes_read;
        
        while ((bytes_read = read(STDIN_FILENO, buffer, MAX_STR_LEN)) > 0) {
            write(STDOUT_FILENO, buffer, bytes_read);
        }
        
        if (bytes_read < 0) {
            display_error("ERROR: Failed to read from stdin", "");
            return -1;
        }
        return 0;
    }

    // Normal file handling
    FILE *file = fopen(tokens[1], "r");
    if (file == NULL) {
        display_error("ERROR: Cannot open file", "");
        return -1;
    }

    char buffer[MAX_STR_LEN];
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, MAX_STR_LEN, file)) > 0) {
        write(STDOUT_FILENO, buffer, bytes_read);
    }

    fclose(file);
    return 0;
}

/* Prereq: tokens is a NULL terminated sequence of strings.
 * Return 0 on success and -1 on error.
 */
ssize_t bn_wc(char **tokens) {
    int word_count = 0, char_count = 0, newline_count = 0, in_word = 0;
    FILE *input = NULL;
    
    if (tokens[1] == NULL) {
        input = stdin;
    } else {
        input = fopen(tokens[1], "r");
        if (input == NULL) {
            display_error("ERROR: Cannot open file", "");
            return -1;
        }
    }

    int c;
    while ((c = fgetc(input)) != EOF) {
        char_count++;
        
        if (c == '\n') newline_count++;
        
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            word_count++;
        }
    }

    if (input != stdin) fclose(input);

    char result[MAX_STR_LEN];
    snprintf(result, MAX_STR_LEN, "word count %d\n", word_count);
    write(STDOUT_FILENO, result, strlen(result));
    
    snprintf(result, MAX_STR_LEN, "character count %d\n", char_count);
    write(STDOUT_FILENO, result, strlen(result));
    
    snprintf(result, MAX_STR_LEN, "newline count %d\n", newline_count);
    write(STDOUT_FILENO, result, strlen(result));
    
    return 0;
}