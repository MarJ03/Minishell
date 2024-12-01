#!/bin/bash
gcc -Wall -Wextra minishell.c ./parser/libparser.a -o test -static
