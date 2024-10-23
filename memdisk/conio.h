/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2001-2008 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * conio.h
 *
 * Limited console I/O
 */

#ifndef CONIO_H
#define CONIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define MEMDUMP(ptr, len) \
    puts("MEMORY DUMP:\n"); \
    for (int i = 0; i < (len); ++i) { \
        printf("%02x%c", *((uint8_t *)(ptr)+i), !((i+1) % 16) ? '\n' : ' '); \
    } \
    if ((len) % 16) { putchar('\n'); }



int gets(char *, int max_len, const char *prompt, int is_hidden);
int putchar(int);
int puts(const char *);
void pause(const char *message);
int vprintf(const char *, va_list ap);
int printf(const char *, ...);
void __attribute__((noreturn)) die(const char *, ...);
unsigned int atou(const char *);



#endif
