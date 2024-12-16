#!/bin/bash
gcc -Wall -Wextra -o minishell minishell.c -L./parser -lparser -I./parser -no-pie
