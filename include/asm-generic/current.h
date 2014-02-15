#ifndef __ASM_GENERIC_CURRENT_H
#define __ASM_GENERIC_CURRENT_H

#include <linux/thread_info.h>

#define get_current() (current_thread_info()->task)
/*! 20140215 여기 실행됨 */
#define current get_current()

#endif /* __ASM_GENERIC_CURRENT_H */
