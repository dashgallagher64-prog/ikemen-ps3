#ifndef IK_TEXT_H
#define IK_TEXT_H
/* Small text-file helpers shared by the AIR / CMD / CNS parsers. Portable C, read-only. */
#include <stddef.h>
#include <stdint.h>

#define TEXT_LINE_MAX 4096

/* Reads a whole text file (read-only). Returns a NUL-terminated heap buffer (free() it), or NULL if the file
 * is missing, empty, or larger than max bytes. A UTF-8 byte-order mark is skipped. */
char *text_load(const char *path, size_t max, size_t *len);

/* Copies the next line of text[0..len) into out (at most cap-1 characters; the rest of an over-long line is
 * dropped). Handles \n, \r\n and lone \r. ';' starts a comment (unless inside double quotes) and the line is
 * trimmed. Returns 1 if a line was produced, 0 at the end of the text. *pos is the read cursor (start at 0). */
int text_next_line(const char *text, size_t len, size_t *pos, char *out, size_t cap);

void text_lower(char *s);

/* Parses a decimal integer: leading blanks, optional sign, digits. Saturates to the int32 range instead of
 * overflowing. Returns the number of characters consumed (0 if there were no digits; *out is then 0). */
int text_parse_int(const char *s, int32_t *out);

/* Parses a decimal number with an optional fraction ("-2.5", ".5", "3"). Returns chars consumed, 0 if none. */
int text_parse_float(const char *s, float *out);

/* Advances past blanks. */
const char *text_skip_blank(const char *s);

#endif
