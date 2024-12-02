#!/bin/bash
gcc -o minishell minishell.c -L./parser -lparser -I./parser -no-pie
