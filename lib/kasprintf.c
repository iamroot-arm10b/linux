/*
 *  linux/lib/kasprintf.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <stdarg.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/string.h>

/* Simplified asprintf. */
char *kvasprintf(gfp_t gfp, const char *fmt, va_list ap)
{
	unsigned int len;
	char *p;
	va_list aq;

	va_copy(aq, ap);
	len = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);

	p = kmalloc_track_caller(len+1, gfp);
	/*! 20140607 slab에서 len+1에 맞는 object를 하나 할당받는다.
	 * 단 len+1의 크기가 8192byte보다 크면 buddy에서 직접 할당받는다.
	 */
	if (!p)
		return NULL;

	vsnprintf(p, len+1, fmt, ap);
	/*! 20140607 할당해 놓은 곳에 우리가 사용할 string을 복사한다. */

	return p;
}
EXPORT_SYMBOL(kvasprintf);

char *kasprintf(gfp_t gfp, const char *fmt, ...)
{
	va_list ap;
	char *p;

	va_start(ap, fmt);
	/*! 20140607 여기 진입함 */
	p = kvasprintf(gfp, fmt, ap);
	/*! 20140607 slab 또는 buddy에서 공간을 할당받아 string를 복사한다. */
	va_end(ap);

	return p;
}
EXPORT_SYMBOL(kasprintf);
