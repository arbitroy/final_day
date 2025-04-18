#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "variables.h"
#include "io_helpers.h"

// Define a structure to hold variable key-value pairs
typedef struct variable {
    char *key;
    char *value;
    struct variable *next;
} variable_t;

// Head of the linked list to store variables
static variable_t *var_list = NULL;

/*
 * Set or overwrite a variable KEY to VALUE.
 * Return 0 on success, or -1 on error (e.g. out of memory).
 */
int set_variable(const char *key, const char *value) {
    // Check for NULL inputs
    if (key == NULL || value == NULL) {
        return -1;
    }
    
    // Check for spaces in key 
    for (int i = 0; key[i] != '\0'; i++) {
        if (key[i] == ' ' || key[i] == '\t') {
            display_error("ERROR: Variable name cannot contain spaces", "");
            return -1;
        }
    }

    // Look for existing variable to update
    variable_t *current = var_list;
    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            // Found existing key, update value
            char *new_value = strdup(value);
            if (new_value == NULL) {
                return -1;  // Memory allocation error
            }
            
            // Free old value and update
            free(current->value);
            current->value = new_value;
            return 0;
        }
        current = current->next;
    }

    // No existing key found, create new variable
    variable_t *new_var = malloc(sizeof(variable_t));
    if (new_var == NULL) {
        return -1;  // Memory allocation error
    }

    // Allocate and copy key and value
    new_var->key = strdup(key);
    if (new_var->key == NULL) {
        free(new_var);
        return -1;
    }

    new_var->value = strdup(value);
    if (new_var->value == NULL) {
        free(new_var->key);
        free(new_var);
        return -1;
    }

    // Add to beginning of linked list
    new_var->next = var_list;
    var_list = new_var;

    return 0;
}

/*
 * Return a pointer to the value for KEY, or NULL if undefined.
 * The caller must not modify or free this pointer.
 */
const char* get_variable(const char *key) {
    if (key == NULL) {
        return NULL;
    }

    // Search for the key in our variable list
    variable_t *current = var_list;
    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            return current->value;
        }
        current = current->next;
    }

    // Key not found
    return NULL;
}

/*
 * Expand variables in a string.
 * Returns a newly allocated string with variables expanded.
 * The caller must free the returned string when done.
 */
// In variables.c, update expand_variables function:
char* expand_variables(const char *str) {
    if (str == NULL) {
        return NULL;
    }
    
    // If no variable references, just return a copy
    if (strchr(str, '$') == NULL) {
        return strdup(str);
    }
    
    // Buffer for the result
    char *result = malloc(MAX_STR_LEN + 1);
    if (result == NULL) {
        return NULL;
    }
    result[0] = '\0';
    
    size_t result_len = 0;
    size_t i = 0;
    
    while (str[i] != '\0' && result_len < MAX_STR_LEN) {
        if (str[i] == '$') {
            // Found a variable reference
            i++;  // Skip the '$'
            
            // Extract variable name
            char var_name[MAX_STR_LEN] = {0};
            size_t var_name_len = 0;
            
            while (str[i] != '\0' && str[i] != ' ' && str[i] != '\t' && 
                   str[i] != '\n' && str[i] != '$' && var_name_len < MAX_STR_LEN - 1) {
                var_name[var_name_len++] = str[i++];
            }
            var_name[var_name_len] = '\0';
            
            if (var_name_len > 0) {
                // Look up variable value
                const char *value = get_variable(var_name);
                
                // Append value (or empty string if not found)
                if (value != NULL) {
                    size_t value_len = strlen(value);
                    
                    // Make sure we don't exceed MAX_STR_LEN
                    if (result_len + value_len > MAX_STR_LEN) {
                        value_len = MAX_STR_LEN - result_len;
                    }
                    
                    strncpy(result + result_len, value, value_len);
                    result_len += value_len;
                    result[result_len] = '\0';
                }
                // Empty string for undefined variables - do nothing here
            } else {
                // Just a lone $
                if (result_len < MAX_STR_LEN) {
                    result[result_len++] = '$';
                    result[result_len] = '\0';
                }
            }
        } else {
            // Regular character
            if (result_len < MAX_STR_LEN) {
                result[result_len++] = str[i++];
                result[result_len] = '\0';
            } else {
                break;
            }
        }
    }
    
    return result;
}

/*
 * Create a copy of the variable environment for a child process.
 * This is useful when forking for pipes, where variables in one process
 * shouldn't affect the other.
 * Returns a new variable list or NULL on error.
 */
variable_t* duplicate_variables(void) {
    variable_t *new_list = NULL;
    variable_t *current = var_list;
    
    while (current != NULL) {
        // Create a new variable
        variable_t *new_var = malloc(sizeof(variable_t));
        if (new_var == NULL) {
            // Clean up on error
            variable_t *temp;
            while (new_list != NULL) {
                temp = new_list;
                new_list = new_list->next;
                free(temp->key);
                free(temp->value);
                free(temp);
            }
            return NULL;
        }
        
        // Copy key and value
        new_var->key = strdup(current->key);
        if (new_var->key == NULL) {
            free(new_var);
            // Clean up on error
            variable_t *temp;
            while (new_list != NULL) {
                temp = new_list;
                new_list = new_list->next;
                free(temp->key);
                free(temp->value);
                free(temp);
            }
            return NULL;
        }
        
        new_var->value = strdup(current->value);
        if (new_var->value == NULL) {
            free(new_var->key);
            free(new_var);
            // Clean up on error
            variable_t *temp;
            while (new_list != NULL) {
                temp = new_list;
                new_list = new_list->next;
                free(temp->key);
                free(temp->value);
                free(temp);
            }
            return NULL;
        }
        
        // Add to new list
        new_var->next = new_list;
        new_list = new_var;
        
        current = current->next;
    }
    
    return new_list;
}

/*
 * Set the variable list to a new list (for use in child processes)
 */
void set_variable_list(variable_t *new_list) {
    // Free the old list
    free_variables();
    
    // Set the new list
    var_list = new_list;
}

/*
 * Free all memory used by the variables system.
 */
void free_variables(void) {
    variable_t *current = var_list;
    variable_t *next;
    
    while (current != NULL) {
        next = current->next;
        free(current->key);
        free(current->value);
        free(current);
        current = next;
    }
    var_list = NULL;
}

