#ifndef __NUM_H
#define __NUM_H
// We are unable to change Makefile to compile separate num.c
// so implementation is located at lib/string.c

// parse until first character that cannot be parsed or '\0'
int parse_number(const char *str, int base, const char **next_token);

#endif