#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#ifndef __ASSEMBLY__
#include <asm/outercache.h>

#define nop() __asm__ __volatile__("mov\tr0,r0\t@ nop\n\t");

#if __LINUX_ARM_ARCH__ >= 7 ||		\
	(__LINUX_ARM_ARCH__ == 6 && defined(CONFIG_CPU_32v6K))
/*! 20140104 sev가 발생하면 wfe로 기다리던 것이 깨어난다. */
#define sev()	__asm__ __volatile__ ("sev" : : : "memory")
/*! 20140104 arm 코어가 event를 받을 때까지 sleep됨 irq/frq 포함 sev명령어가 발생하면 깨어남 */
#define wfe()	__asm__ __volatile__ ("wfe" : : : "memory")
/*! 20140104 arm 코어가 interrupt를 받을 때까지 sleep됨 irq/frq 받는 경우 깨어남 */
#define wfi()	__asm__ __volatile__ ("wfi" : : : "memory")
#endif

#if __LINUX_ARM_ARCH__ >= 7
#define isb() __asm__ __volatile__ ("isb" : : : "memory")
#define dsb() __asm__ __volatile__ ("dsb" : : : "memory")
#define dmb() __asm__ __volatile__ ("dmb" : : : "memory")
/*! 20140208 dmb = cpu가 순서를 보장하게 만들고, memory 는 컴파일러가 순서를 보장하게 만듬. (최적화를 금지함)*/
#elif defined(CONFIG_CPU_XSC3) || __LINUX_ARM_ARCH__ == 6
#define isb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 4" \
				    : : "r" (0) : "memory")
#define dsb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#define dmb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 5" \
				    : : "r" (0) : "memory")
#elif defined(CONFIG_CPU_FA526)
#define isb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c5, 4" \
				    : : "r" (0) : "memory")
#define dsb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#define dmb() __asm__ __volatile__ ("" : : : "memory")
#else
#define isb() __asm__ __volatile__ ("" : : : "memory")
#define dsb() __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 4" \
				    : : "r" (0) : "memory")
#define dmb() __asm__ __volatile__ ("" : : : "memory")
#endif

#ifdef CONFIG_ARCH_HAS_BARRIERS
#include <mach/barriers.h>
#elif defined(CONFIG_ARM_DMA_MEM_BUFFERABLE) || defined(CONFIG_SMP)
#define mb()		do { dsb(); outer_sync(); } while (0)
#define rmb()		dsb()
#define wmb()		mb()
#else
#define mb()		barrier()
#define rmb()		barrier()
#define wmb()		barrier()
#endif

/*! 20131221 메모리 barrier를 친다. smp가 아니면 컴파일러 최적화만 막는다. */
#ifndef CONFIG_SMP
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#else
/*! 20140104 data memory barrier:쓰기 순서를 보장함 */
#define smp_mb()	dmb()
#define smp_rmb()	dmb()
#define smp_wmb()	dmb()
#endif

#define read_barrier_depends()		do { } while(0)
#define smp_read_barrier_depends()	do { } while(0)

#define set_mb(var, value)	do { var = value; smp_mb(); } while (0)

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_BARRIER_H */
