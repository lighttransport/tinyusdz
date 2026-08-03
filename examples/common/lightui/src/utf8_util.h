/*
 * src/utf8_util.h — Shared UTF-8 inline helpers
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_UTF8_UTIL_H
#define LIGHTUI_UTF8_UTIL_H

/*
 * Return byte length of the UTF-8 codepoint starting at buf[pos].
 * Returns 0 if pos >= len.  Returns 1 for invalid lead bytes.
 */
static inline int lui__utf8_cp_len(const char *buf, int pos, int len)
{
    if (pos >= len) return 0;
    unsigned char c = (unsigned char)buf[pos];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return (pos + 2 <= len) ? 2 : 1;
    if ((c & 0xF0) == 0xE0) return (pos + 3 <= len) ? 3 : 1;
    if ((c & 0xF8) == 0xF0) return (pos + 4 <= len) ? 4 : 1;
    return 1;
}

/*
 * Return byte offset of the previous codepoint start before pos.
 * Walks backwards over continuation bytes (10xxxxxx).
 */
static inline int lui__utf8_prev(const char *buf, int pos)
{
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)buf[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

#endif /* LIGHTUI_UTF8_UTIL_H */
