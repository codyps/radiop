#include <stdio.h>
#include <ctype.h>

#include "print.h"

static void print_hex_byte(char byte, FILE *f)
{
	static const char hex_lookup[] = "0123456789abcdef";
	putc(hex_lookup[(byte >> 4) & 0x0f], f);
	putc(hex_lookup[byte & 0x0f], f);
}

static void print_cstring_char(int c, FILE *f)
{
	if (iscntrl(c) || !isprint(c)) {
		switch (c) {
		case '\0':
			putc('\\', f);
			putc('0', f);
			break;
		case '\n':
			putc('\\', f);
			putc('n', f);
			break;
		case '\r':
			putc('\\', f);
			putc('r', f);
			break;
		default:
			putc('\\', f);
			putc('x', f);
			print_hex_byte(c, f);
		}
	} else  {
		switch (c) {
		case '"':
		case '\\':
			putc('\\', f);
		default:
			putc(c, f);
		}
	}
}

void print_string_as_cstring_(const void *data, size_t data_len, FILE *f)
{
	const char *p = data;
	size_t i;
	for (i = 0; i < data_len; i++) {
		char c = p[i];
		if (c == '\0')
			break;
		print_cstring_char(c, f);
	}
}

static void print_bytes_as_cstring_(const void *data, size_t data_len, FILE *f)
{
	const char *p = data;
	size_t i;
	for (i = 0; i < data_len; i++) {
		char c = p[i];
		print_cstring_char(c, f);
	}
}

void print_bytes_as_cstring(const void *data, size_t data_len, FILE *f)
{
	putc('"', f);
	print_bytes_as_cstring_(data, data_len, f);
	putc('"', f);
}
