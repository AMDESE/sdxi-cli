# Coding Style Guide

This document defines the coding standards for the SDXI-CLI project.

## General Principles

You are an expert C programmer. Always adhere to the following coding style:

## Brace Style

- Use K&R (Kernighan & Ritchie) brace style
- Opening braces go on the same line as the statement
- Closing braces go on their own line
- Function's opening braces should start with a new line

Example:
```c
if (condition) {
    do_something();
} else {
    do_something_else();
}

int function_name(int param)
{
    // function body
}
```

## Indentation

- Indent with tabs
- Continuation lines should be indented appropriately

## Naming Conventions

- Use **foo_bar** for all variable and function names
- Variables: `my_var`, `count`, `buffer_size`
- Make variable names concise using abbreviation and recognizable
- Functions: `get_data()`, `process_input()`, `calculate_sum()`
- Constants: May use UPPER_FOO_BAR for #define macros

Example:
```c
int my_var = 0;
char *buffer_name = NULL;

int calculate_total(int first_val, int second_val) {
    return first_val + second_val;
}
```

## Comments

- Add comments for all functions explaining:
  - Their purpose
  - Parameters (what they represent and expected values)
  - Return values (what is returned and meaning)
- Use C-style comments (`/* */`) for function documentation
- Single-line comments (`//`) are acceptable for inline comments

Example:
```c
/*
 * calculate_sum - Adds two integers together
 * @a: First integer value
 * @b: Second integer value
 *
 * Returns: The sum of a and b
 */
int calculate_sum(int a, int b) {
    return a + b;  // Simple addition
}
```

## Line Length

- If possible, ensure lines do not exceed **80 characters**
- Break long lines appropriately
- Break function calls and declarations at logical points

Example:
```c
int result = some_long_function_name(
    first_parameter,
    second_parameter,
    third_parameter
);
```

## Additional Guidelines

- Use meaningful variable and function names
- Avoid magic numbers; use named constants or enums
- Keep functions focused on a single task
- Prefer clarity over cleverness
