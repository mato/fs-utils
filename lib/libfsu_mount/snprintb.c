/*	$NetBSD: snprintb.c,v 1.7 2012/01/23 03:22:41 christos Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * snprintb: print an interpreted bitmask to a buffer
 *
 * => returns the length of the buffer that would be required to print the
 *    string minus the terminating NUL.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int
snprintb_m(char *buf, size_t buflen, const char *bitfmt, uint64_t val,
	   size_t l_max)
{
	char *bp = buf, *s_bp = NULL;
	const char *c_fmt, *s_fmt = NULL, *cur_fmt;
	const char *sbase;
	int bit, ch, t_len, s_len = 0, l_len, f_len, v_len, sep;
	int restart = 0;
	uint64_t field;

	ch = *bitfmt++;
	switch (ch != '\177' ? ch : *bitfmt++) {
	case 8:
		sbase = "0%" PRIo64;
		break;
	case 10:
		sbase = "%" PRId64;
		break;
	case 16:
		sbase = "0x%" PRIx64;
		break;
	default:
		goto internal;
	}

	/* Reserve space for trailing blank line if needed */
	if (l_max > 0)
		buflen--;

	t_len = snprintf(bp, buflen, sbase, val);
	if (t_len < 0)
		goto internal;

	v_len = l_len = t_len;

	if ((size_t)t_len < buflen)
		bp += t_len;
	else
		bp += buflen - 1;

	/*
	 * If the value we printed was 0 and we're using the old-style format,
	 * we're done.
	 */
	if ((val == 0) && (ch != '\177'))
		goto terminate;

#define STORE(c) { l_len++;						\
		   if ((size_t)(++t_len) < buflen)			\
		   	*bp++ = (c);					\
		 } while ( /* CONSTCOND */ 0)

#define	BACKUP	{ if (s_bp != NULL) {					\
			bp = s_bp; s_bp = NULL;				\
			t_len -= l_len - s_len;				\
			restart = 1;					\
			bitfmt = s_fmt;					\
		  }							\
		  STORE('>'); STORE('\0');				\
		  if ((size_t)t_len < buflen)				\
			snprintf(bp, buflen - t_len, sbase, val);	\
		  t_len += v_len; l_len = v_len; bp += v_len;		\
		} while ( /* CONSTCOND */ 0)

#define	PUTSEP								\
		if (l_max > 0 && (size_t)l_len >= l_max) {		\
			BACKUP;						\
			STORE('<');					\
		} else {						\
			/* Remember separator location */		\
			if (l_max > 0 && sep != '<') {			\
				s_len = l_len;				\
				s_bp  = bp;				\
				s_fmt = cur_fmt;			\
			}						\
			STORE(sep);					\
			restart = 0;					\
		}							\

#define	PUTCHR(c)							\
		if (l_max > 0 && (size_t)l_len >= (l_max - 1)) {	\
			BACKUP;						\
			if (restart == 0) {				\
				STORE(c);				\
			} else						\
				sep = '<';				\
		} else {						\
			STORE(c);					\
			restart = 0;					\
		}							\

#define PUTS(s) while ((ch = *(s)++) != 0) {				\
			PUTCHR(ch);					\
			if (restart)					\
				break;					\
		}

	/*
	 * Chris Torek's new bitmask format is identified by a leading \177
	 */
	sep = '<';
	if (ch != '\177') {
		/* old (standard) format. */
		for (;(bit = *bitfmt) != 0;) {
			cur_fmt = bitfmt++;
			if (val & (1 << (bit - 1))) {
				PUTSEP;
				if (restart)
					continue;
				sep = ',';
				for (; (ch = *bitfmt) > ' '; ++bitfmt) {
					PUTCHR(ch); 
					if (restart)
						break;
				}
			} else
				for (; *bitfmt > ' '; ++bitfmt)
					continue;
		}
	} else {
		/* new quad-capable format; also does fields. */
		field = val;
		while (c_fmt = bitfmt, (ch = *bitfmt++) != '\0') {
			bit = *bitfmt++;	/* now 0-origin */
			switch (ch) {
			case 'b':
				if (((unsigned int)(val >> bit) & 1) == 0)
					goto skip;
				cur_fmt = c_fmt;
				PUTSEP;
				if (restart)
					break;
				PUTS(bitfmt);
				if (restart == 0)
					sep = ',';
				break;
			case 'f':
			case 'F':
				cur_fmt = c_fmt;
				f_len = *bitfmt++;	/* field length */
				field = (val >> bit) &
					    (((uint64_t)1 << f_len) - 1);
				PUTSEP;
				if (restart == 0)
					sep = ',';
				if (ch == 'F')	/* just extract */
					break;
				if (restart == 0) {
					PUTS(bitfmt);
					PUTCHR('=');
				}
				if (restart == 0) {
					f_len = snprintf(bp, buflen - t_len,
							 sbase, field);
					if (f_len < 0)
						goto internal;
					t_len += f_len;
					l_len += f_len;
					if ((size_t)t_len < buflen)
						bp += f_len;
					if (l_max > 0 &&
					    (size_t)l_len > l_max) {
						PUTCHR('#');
					}
				}
				break;
			case '=':
			case ':':
				/*
				 * Here "bit" is actually a value instead,
				 * to be compared against the last field.
				 * This only works for values in [0..255],
				 * of course.
				 */
				if ((int)field != bit)
					goto skip;
				if (ch == '=') {
					PUTCHR('=');
				}
				PUTS(bitfmt);
				break;
			default:
			skip:
				while (*bitfmt++ != '\0')
					continue;
				break;
			}
		}
	}
	l_len++;
	if ((size_t)(++t_len) < buflen)
		*bp++ = '>';
terminate:
	*bp++ = '\0';
	if (l_max != 0) {
		t_len++;
		*bp = '\0';
	}
	return t_len;
internal:
	errno = EINVAL;
	return -1;
}

int
snprintb(char *buf, size_t buflen, const char *bitfmt, uint64_t val)
{
	return snprintb_m(buf, buflen, bitfmt, val, 0);
}
