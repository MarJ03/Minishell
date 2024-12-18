#!/bin/bash
gcc -Wall -Wextra -o myshell myshell.c -L./parser -lparser -I./parser -static
