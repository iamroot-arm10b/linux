#ifndef _LINUX_PFN_H_
#define _LINUX_PFN_H_

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif

#define PFN_ALIGN(x)	(((unsigned long)(x) + (PAGE_SIZE - 1)) & PAGE_MASK)
#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
/*! PFN_UP : 20131109 페이지 단위로 올림 */
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
/*! PFN_DOWN : 20131109 페이지 단위로 버림 */
#define PFN_PHYS(x)	((phys_addr_t)(x) << PAGE_SHIFT)

#endif
