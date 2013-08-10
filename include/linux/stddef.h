#ifndef _LINUX_STDDEF_H
#define _LINUX_STDDEF_H

#include <uapi/linux/stddef.h>


#undef NULL
#define NULL ((void *)0)

enum {
	false	= 0,
	true	= 1
};

#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
/*! 20130810
 * 컴파일러에서 offsetof 기능을 지원하면 내장 기능을 사용
 */
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
/*! 20130810
 * 구조체 시작에서 member의 offset를 구한다.
 */
#endif
#endif
