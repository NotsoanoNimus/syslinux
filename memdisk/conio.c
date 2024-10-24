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
 * conio.c
 *
 * Output to the screen
 */

#include <stdint.h>
#include "memdisk.h"
#include "conio.h"



/* Get a string from the user, placed into the 'to' buffer. */
int gets(char *to, int max_len, const char *prompt, int is_hidden)
{
	volatile char *scroll;
	char read;
    com32sys_t regs;
    com32sys_t out_regs;

	scroll = to;

	do {
		if (NULL != prompt) {
			putchar('\r');
			puts(prompt);

			if (is_hidden) {
				for (int i = 0; i < (scroll - to); ++i) putchar('*');
				for (int i = (scroll - to); i < max_len; ++i) putchar(' ');
			} else {
				puts(to);
			}
		}

		/* INT 16h, option 0x10 - Block and accept a character input. */
		/* Register byte AL holds the read character. We don't care about the Scan Code (AH). */
    	memset(&regs, 0, sizeof(regs));
    	memset(&out_regs, 0, sizeof(regs));

		regs.eax.w[0] = 0x0000;
		intcall(0x16, &regs, &out_regs);

		read = (out_regs.eax.w[0] & 0x00FF);

		if ('\n' == read || '\r' == read) {
			puts("\r\n");
			break;
		}

		/* Backspace. */
		if (0x08 == read) {
			*scroll = '\0';

			if (scroll > to) --scroll;

			continue;
		}

		/* Skip invalid (non-printable) characters. */
		if (read <= 0x1F || read >= 0x7F) continue;

		/* Any other valid printable character. Just make sure it doesn't overflow. */
		if (0x00 == read || (scroll - to) >= max_len) continue;

		*scroll = read;
		++scroll;
	} while (true);

	/* Enforce null termination of the string. */
	*(scroll + 1) = '\0';

	/* Return read string's length. */
	return (scroll - to);
}


int putchar(int ch)
{
    com32sys_t regs;
    memset(&regs, 0, sizeof regs);

    if (ch == '\n') {
	/* \n -> \r\n */
	putchar('\r');
    }

    regs.eax.w[0] = 0x0e00 | (ch & 0xff);
    intcall(0x10, &regs, NULL);

    return ch;
}

int puts(const char *s)
{
    int count = 0;

    while (*s) {
	putchar(*s);
	count++;
	s++;
    }

    return count;
}

void pause(const char *message)
{
    com32sys_t regs;

    puts(message);

    memset(&regs, 0, sizeof regs);
    regs.eax.w[0] = 0;
    intcall(0x16, &regs, NULL);
}

/*
 * Oh, it's a waste of space, but oh-so-yummy for debugging.  It's just
 * initialization code anyway, so it doesn't take up space when we're
 * actually running.  This version of printf() does not include 64-bit
 * support.  "Live with it."
 *
 * Most of this code was shamelessly snarfed from the Linux kernel, then
 * modified.
 */

static inline int isdigit(int ch)
{
    return (ch >= '0') && (ch <= '9');
}

static int skip_atoi(const char **s)
{
    int i = 0;

    while (isdigit(**s))
	i = i * 10 + *((*s)++) - '0';
    return i;
}

unsigned int atou(const char *s)
{
    unsigned int i = 0;
    while (isdigit(*s))
	i = i * 10 + (*s++ - '0');
    return i;
}

static int strnlen(const char *s, int maxlen)
{
    const char *es = s;
    while (*es && maxlen) {
	es++;
	maxlen--;
    }

    return (es - s);
}

#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SPECIAL	32		/* 0x */
#define LARGE	64		/* use 'ABCDEF' instead of 'abcdef' */

#define do_div(n,base) ({ \
int __res; \
__res = ((unsigned long) n) % (unsigned) base; \
n = ((unsigned long) n) / (unsigned) base; \
__res; })

static char *number(char *str, long num, int base, int size, int precision,
		    int type)
{
    char c, sign, tmp[66];
    const char *digits = "0123456789abcdef";
    int i;

    if (type & LARGE)
	digits = "0123456789ABCDEF";
    if (type & LEFT)
	type &= ~ZEROPAD;
    if (base < 2 || base > 36)
	return 0;
    c = (type & ZEROPAD) ? '0' : ' ';
    sign = 0;
    if (type & SIGN) {
	if (num < 0) {
	    sign = '-';
	    num = -num;
	    size--;
	} else if (type & PLUS) {
	    sign = '+';
	    size--;
	} else if (type & SPACE) {
	    sign = ' ';
	    size--;
	}
    }
    if (type & SPECIAL) {
	if (base == 16)
	    size -= 2;
	else if (base == 8)
	    size--;
    }
    i = 0;
    if (num == 0)
	tmp[i++] = '0';
    else
	while (num != 0)
	    tmp[i++] = digits[do_div(num, base)];
    if (i > precision)
	precision = i;
    size -= precision;
    if (!(type & (ZEROPAD + LEFT)))
	while (size-- > 0)
	    *str++ = ' ';
    if (sign)
	*str++ = sign;
    if (type & SPECIAL) {
	if (base == 8)
	    *str++ = '0';
	else if (base == 16) {
	    *str++ = '0';
	    *str++ = digits[33];
	}
    }
    if (!(type & LEFT))
	while (size-- > 0)
	    *str++ = c;
    while (i < precision--)
	*str++ = '0';
    while (i-- > 0)
	*str++ = tmp[i];
    while (size-- > 0)
	*str++ = ' ';
    return str;
}

int vsprintf(char *buf, const char *fmt, va_list args)
{
    int len;
    unsigned long num;
    int i, base;
    char *str;
    const char *s;

    int flags;			/* flags to number() */

    int field_width;		/* width of output field */
    int precision;		/* min. # of digits for integers; max
				   number of chars for from string */
    int qualifier;		/* 'h', 'l', or 'L' for integer fields */

    for (str = buf; *fmt; ++fmt) {
	if (*fmt != '%') {
	    *str++ = *fmt;
	    continue;
	}

	/* process flags */
	flags = 0;
repeat:
	++fmt;			/* this also skips first '%' */
	switch (*fmt) {
	case '-':
	    flags |= LEFT;
	    goto repeat;
	case '+':
	    flags |= PLUS;
	    goto repeat;
	case ' ':
	    flags |= SPACE;
	    goto repeat;
	case '#':
	    flags |= SPECIAL;
	    goto repeat;
	case '0':
	    flags |= ZEROPAD;
	    goto repeat;
	}

	/* get field width */
	field_width = -1;
	if (isdigit(*fmt))
	    field_width = skip_atoi(&fmt);
	else if (*fmt == '*') {
	    ++fmt;
	    /* it's the next argument */
	    field_width = va_arg(args, int);
	    if (field_width < 0) {
		field_width = -field_width;
		flags |= LEFT;
	    }
	}

	/* get the precision */
	precision = -1;
	if (*fmt == '.') {
	    ++fmt;
	    if (isdigit(*fmt))
		precision = skip_atoi(&fmt);
	    else if (*fmt == '*') {
		++fmt;
		/* it's the next argument */
		precision = va_arg(args, int);
	    }
	    if (precision < 0)
		precision = 0;
	}

	/* get the conversion qualifier */
	qualifier = -1;
	if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') {
	    qualifier = *fmt;
	    ++fmt;
	}

	/* default base */
	base = 10;

	switch (*fmt) {
	case 'c':
	    if (!(flags & LEFT))
		while (--field_width > 0)
		    *str++ = ' ';
	    *str++ = (unsigned char)va_arg(args, int);
	    while (--field_width > 0)
		*str++ = ' ';
	    continue;

	case 's':
	    s = va_arg(args, char *);
	    len = strnlen(s, precision);

	    if (!(flags & LEFT))
		while (len < field_width--)
		    *str++ = ' ';
	    for (i = 0; i < len; ++i)
		*str++ = *s++;
	    while (len < field_width--)
		*str++ = ' ';
	    continue;

	case 'p':
	    if (field_width == -1) {
		field_width = 2 * sizeof(void *);
		flags |= ZEROPAD;
	    }
	    str = number(str,
			 (unsigned long)va_arg(args, void *), 16,
			 field_width, precision, flags);
	    continue;

	case 'n':
	    if (qualifier == 'l') {
		long *ip = va_arg(args, long *);
		*ip = (str - buf);
	    } else {
		int *ip = va_arg(args, int *);
		*ip = (str - buf);
	    }
	    continue;

	case '%':
	    *str++ = '%';
	    continue;

	    /* integer number formats - set up the flags and "break" */
	case 'o':
	    base = 8;
	    break;

	case 'X':
	    flags |= LARGE;
	case 'x':
	    base = 16;
	    break;

	case 'd':
	case 'i':
	    flags |= SIGN;
	case 'u':
	    break;

	default:
	    *str++ = '%';
	    if (*fmt)
		*str++ = *fmt;
	    else
		--fmt;
	    continue;
	}
	if (qualifier == 'l')
	    num = va_arg(args, unsigned long);
	else if (qualifier == 'h') {
	    num = (unsigned short)va_arg(args, int);
	    if (flags & SIGN)
		num = (short)num;
	} else if (flags & SIGN)
	    num = va_arg(args, int);
	else
	    num = va_arg(args, unsigned int);
	str = number(str, num, base, field_width, precision, flags);
    }
    *str = '\0';
    return str - buf;
}

#if 0
int sprintf(char *buf, const char *fmt, ...)
{
    va_list args;
    int i;

    va_start(args, fmt);
    i = vsprintf(buf, fmt, args);
    va_end(args);
    return i;
}
#endif

int vprintf(const char *fmt, va_list args)
{
    char printf_buf[2048];
    int printed;

    printed = vsprintf(printf_buf, fmt, args);
    puts(printf_buf);
    return printed;
}

int printf(const char *fmt, ...)
{
    va_list args;
    int printed;

    va_start(args, fmt);
    printed = vprintf(fmt, args);
    va_end(args);
    return printed;
}

/*
 * Jump here if all hope is gone...
 */
void __attribute__ ((noreturn)) die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    sti();
    for (;;)
	asm volatile("hlt");
}
