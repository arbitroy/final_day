#ifndef __VARIABLES_H__
#define __VARIABLES_H__

// Forward declaration for the variable structure
typedef struct variable variable_t;

/*
 * Set or overwrite a variable KEY to VALUE.
 * Return 0 on success, or -1 on error (e.g. out of memory).
 */
int set_variable(const char *key, const char *value);

/*
 * Return a pointer to the value for KEY, or NULL if undefined.
 * The caller must not modify or free this pointer.
 */
const char* get_variable(const char *key);

/*
 * Create a copy of the variable environment for a child process.
 * Returns a new variable list or NULL on error.
 */
variable_t* duplicate_variables(void);

/*
 * Set the variable list to a new list (for use in child processes)
 */
void set_variable_list(variable_t *new_list);

/*
 * Free all memory used by the variables system.
 * This is not required by the project but is good practice to avoid memory leaks.
 */
void free_variables(void);
char* expand_variables(const char *str);

#endif