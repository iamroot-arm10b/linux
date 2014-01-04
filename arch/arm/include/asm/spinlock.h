#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#if __LINUX_ARM_ARCH__ < 6
#error SMP not supported on pre-ARMv6 CPUs
#endif

#include <asm/processor.h>

/*
 * sev and wfe are ARMv6K extensions.  Uniprocessor ARMv6 may not have the K
 * extensions, so when running on UP, we have to patch these instructions away.
 */
#define ALT_SMP(smp, up)					\
	"9998:	" smp "\n"					\
	"	.pushsection \".alt.smp.init\", \"a\"\n"	\
	"	.long	9998b\n"				\
	"	" up "\n"					\
	"	.popsection\n"

#ifdef CONFIG_THUMB2_KERNEL
#define SEV		ALT_SMP("sev.w", "nop.w")
/*
 * For Thumb-2, special care is needed to ensure that the conditional WFE
 * instruction really does assemble to exactly 4 bytes (as required by
 * the SMP_ON_UP fixup code).   By itself "wfene" might cause the
 * assembler to insert a extra (16-bit) IT instruction, depending on the
 * presence or absence of neighbouring conditional instructions.
 *
 * To avoid this unpredictableness, an approprite IT is inserted explicitly:
 * the assembler won't change IT instructions which are explicitly present
 * in the input.
 */
/*! 20131221 WFE는 인터럽트 등의 이벤트 또는 sev 명령어가 실행될 때 까지 정지한다. */
#define WFE(cond)	ALT_SMP(		\
	"it " cond "\n\t"			\
	"wfe" cond ".n",			\
						\
	"nop.w"					\
)
#else
#define SEV		ALT_SMP("sev", "nop")
#define WFE(cond)	ALT_SMP("wfe" cond, "nop")
#endif

/*! 20140104
 * dsb = cache의 write buffer에 들어있는 값을 물리적 memory에 입력
 * sev 함수로 인해 arch_spin_lock의 while문의 wfe() 가 깨어나며 다음라인으로 진행됨.
 */
static inline void dsb_sev(void)
{
#if __LINUX_ARM_ARCH__ >= 7
	__asm__ __volatile__ (
		"dsb\n"
		SEV
	);
#else
	__asm__ __volatile__ (
		"mcr p15, 0, %0, c7, c10, 4\n"
		SEV
		: : "r" (0)
	);
#endif
}

/*
 * ARMv6 ticket-based spin-locking.
 *
 * A memory barrier is required after we get a lock, and before we
 * release it, because V6 CPUs are assumed to have weakly ordered
 * memory.
 */

#define arch_spin_unlock_wait(lock) \
	do { while (arch_spin_is_locked(lock)) cpu_relax(); } while (0)

/*! 20140104 여기가 실행됨 */
#define arch_spin_lock_flags(lock, flags) arch_spin_lock(lock)

static inline void arch_spin_lock(arch_spinlock_t *lock)
{
	unsigned long tmp;
	u32 newval;
	arch_spinlock_t lockval;

	/*! 20140104
	 * 고유한 tick(slock) 을 할당받는다.
	 *
	 * lockval = lock->slock;
	 * newval = lockval + (1 << 16(=TICKET_SHIFT)); //tickets.next를 1 증가
	 * lock->slock = newval; / strex의 성공여부가 tmp에 들어감
	 * strex 성공하면 통과 실패하면(tmp가 0인 경우) "label 1" 다시수행
	 */
	__asm__ __volatile__(
"1:	ldrex	%0, [%3]\n"
"	add	%1, %0, %4\n"
"	strex	%2, %1, [%3]\n"
"	teq	%2, #0\n"
"	bne	1b"
	: "=&r" (lockval), "=&r" (newval), "=&r" (tmp)
	: "r" (&lock->slock), "I" (1 << TICKET_SHIFT)
	: "cc");

	/*! 20140104
	 * ticket union -> [next,owner]
	 * 상위16bit next  - lock을 잡을때마다 1씩 증가
	 * 하위16bit owner - lock을 릴리즈할때마다 1씩 증가
	 * lockval은 local 변수 / lock은 인자로 넘어온 변수(코어마다 가지는 전역변수)
	 * 1. 이 시점에서 최초수행시		lockval=[0,0] / lock=[1,0] 
	 * 2. 두번째 수행되면			lockval=[1,0] / lock=[2,0]
	 * 3. 1번에서 잡은 lock이 릴리즈되면	lockval=[1,1] / lock=[2,1]
	 * 4. 따라서, 1번에서 잡은 lock이 릴리즈되면 while문을 벗어나게 된다. 
	 */
	while (lockval.tickets.next != lockval.tickets.owner) {
		/*! 20140104 arm 코어가 event를 받을 때까지 sleep됨 */
		wfe();
		/*! 20140104 lockval 값을 lock 값으로 업데이트 */
		lockval.tickets.owner = ACCESS_ONCE(lock->tickets.owner);
	}

	smp_mb();
}

static inline int arch_spin_trylock(arch_spinlock_t *lock)
{
	unsigned long contended, res;
	u32 slock;

	do {
		__asm__ __volatile__(
		"	ldrex	%0, [%3]\n"
		"	mov	%2, #0\n"
		"	subs	%1, %0, %0, ror #16\n"
		"	addeq	%0, %0, %4\n"
		"	strexeq	%2, %0, [%3]"
		: "=&r" (slock), "=&r" (contended), "=&r" (res)
		: "r" (&lock->slock), "I" (1 << TICKET_SHIFT)
		: "cc");
	} while (res);

	if (!contended) {
		smp_mb();
		return 1;
	} else {
		return 0;
	}
}

static inline void arch_spin_unlock(arch_spinlock_t *lock)
{
	smp_mb();
	/*! 20140104
	 * lock->tickets의 하위16bit(=owner)를 1증가시켜
	 * 다음 lock이 arch_spin_lock 함수의 while문을 벗어날 수 있게 해준다.
	 */
	lock->tickets.owner++;
	/*! 20140104
	 * dsb = cache의 write buffer에 들어있는 값을 물리적 memory에 입력
	 * sev 함수로 인해 arch_spin_lock의 while문의 wfe() 가 깨어나며 다음라인으로 진행됨.
	 */
	dsb_sev();
}

static inline int arch_spin_is_locked(arch_spinlock_t *lock)
{
	struct __raw_tickets tickets = ACCESS_ONCE(lock->tickets);
	return tickets.owner != tickets.next;
}

static inline int arch_spin_is_contended(arch_spinlock_t *lock)
{
	struct __raw_tickets tickets = ACCESS_ONCE(lock->tickets);
	return (tickets.next - tickets.owner) > 1;
}
#define arch_spin_is_contended	arch_spin_is_contended

/*
 * RWLOCKS
 *
 *
 * Write locks are easy - we just set bit 31.  When unlocking, we can
 * just write zero since the lock is exclusively held.
 */
/*! 20131221 쓰기 락을 얻을 때까지 대기한다.
 * 쓰기 lock을 얻으면 0x80000000 으로 lock을 획득하였다고 표시한다.
 */
static inline void arch_write_lock(arch_rwlock_t *rw)
{
	unsigned long tmp;
	/* 처음 lock을 실행하는 경우 WFE는 실행되지 않고
	 * 누군가 lock을 잡고 있으면 WFE가 실행되고
	 * lock을 잡았던 프로세서가 sev를 날리면 실행이 재개되며
	 * lock 획득을 시도한다.
	 */
	__asm__ __volatile__(
"1:	ldrex	%0, [%1]\n"
"	teq	%0, #0\n"
	WFE("ne")
"	strexeq	%0, %2, [%1]\n"
"	teq	%0, #0\n"
"	bne	1b"
	: "=&r" (tmp)
	: "r" (&rw->lock), "r" (0x80000000)
	: "cc");

	/*! 20131221 메모리 읽기 쓰기 순서를 보장함  */
	smp_mb();
}

static inline int arch_write_trylock(arch_rwlock_t *rw)
{
	unsigned long contended, res;

	do {
		__asm__ __volatile__(
		"	ldrex	%0, [%2]\n"
		"	mov	%1, #0\n"
		"	teq	%0, #0\n"
		"	strexeq	%1, %3, [%2]"
		: "=&r" (contended), "=&r" (res)
		: "r" (&rw->lock), "r" (0x80000000)
		: "cc");
	} while (res);

	if (!contended) {
		smp_mb();
		return 1;
	} else {
		return 0;
	}
}

/*! 20131221 0을 대입해 unlock한다. */
static inline void arch_write_unlock(arch_rwlock_t *rw)
{
	smp_mb();

	__asm__ __volatile__(
	"str	%1, [%0]\n"
	:
	: "r" (&rw->lock), "r" (0)
	: "cc");

	/*! 20131221 WFE로 기다리던 프로세서들을 깨어주기 위해
	 * sev 명령어로 signal 날린다.
	 */
	dsb_sev();
}

/* write_can_lock - would write_trylock() succeed? */
#define arch_write_can_lock(x)		((x)->lock == 0)

/*
 * Read locks are a bit more hairy:
 *  - Exclusively load the lock value.
 *  - Increment it.
 *  - Store new lock value if positive, and we still own this location.
 *    If the value is negative, we've already failed.
 *  - If we failed to store the value, we want a negative result.
 *  - If we failed, try again.
 * Unlocking is similarly hairy.  We may have multiple read locks
 * currently active.  However, we know we won't have any write
 * locks.
 */
static inline void arch_read_lock(arch_rwlock_t *rw)
{
	unsigned long tmp, tmp2;

	__asm__ __volatile__(
"1:	ldrex	%0, [%2]\n"
"	adds	%0, %0, #1\n"
"	strexpl	%1, %0, [%2]\n"
	WFE("mi")
"	rsbpls	%0, %1, #0\n"
"	bmi	1b"
	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&rw->lock)
	: "cc");

	smp_mb();
}

static inline void arch_read_unlock(arch_rwlock_t *rw)
{
	unsigned long tmp, tmp2;

	smp_mb();

	__asm__ __volatile__(
"1:	ldrex	%0, [%2]\n"
"	sub	%0, %0, #1\n"
"	strex	%1, %0, [%2]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&rw->lock)
	: "cc");

	if (tmp == 0)
		dsb_sev();
}

static inline int arch_read_trylock(arch_rwlock_t *rw)
{
	unsigned long contended, res;

	do {
		__asm__ __volatile__(
		"	ldrex	%0, [%2]\n"
		"	mov	%1, #0\n"
		"	adds	%0, %0, #1\n"
		"	strexpl	%1, %0, [%2]"
		: "=&r" (contended), "=&r" (res)
		: "r" (&rw->lock)
		: "cc");
	} while (res);

	/* If the lock is negative, then it is already held for write. */
	if (contended < 0x80000000) {
		smp_mb();
		return 1;
	} else {
		return 0;
	}
}

/* read_can_lock - would read_trylock() succeed? */
#define arch_read_can_lock(x)		((x)->lock < 0x80000000)

#define arch_read_lock_flags(lock, flags) arch_read_lock(lock)
#define arch_write_lock_flags(lock, flags) arch_write_lock(lock)

#define arch_spin_relax(lock)	cpu_relax()
#define arch_read_relax(lock)	cpu_relax()
#define arch_write_relax(lock)	cpu_relax()

#endif /* __ASM_SPINLOCK_H */
