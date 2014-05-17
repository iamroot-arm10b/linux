#ifndef __LINUX_BIT_SPINLOCK_H
#define __LINUX_BIT_SPINLOCK_H

#include <linux/kernel.h>
#include <linux/preempt.h>
#include <linux/atomic.h>
#include <linux/bug.h>

/*
 *  bit-based spin_lock()
 *
 * Don't use this unless you really need to: spin_lock() and spin_unlock()
 * are significantly faster.
 */
static inline void bit_spin_lock(int bitnum, unsigned long *addr)
{
	/*
	 * Assuming the lock is uncontended, this never enters
	 * the body of the outer loop. If it is contended, then
	 * within the inner loop a non-atomic test is used to
	 * busywait with less bus contention for a good time to
	 * attempt to acquire the lock bit.
	 */
	preempt_disable();
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	while (unlikely(test_and_set_bit_lock(bitnum, addr))) {
		/*! 20140517 addr가르키는 공간의 bitnum위치의 bit을 set하고, 이전 bit값을 리턴한다
		 * 1이 리턴되는 경우는 이전 bit값이 설정되어 있는 경우, 
		 * => 즉 다른 user가 현재 bit lock을 잡고 있는 경우 
		 */
		preempt_enable();
		/*! 20140517 선점을 enable한다 */
		do {
			cpu_relax();
			/*! 20140517 cpu_relax() -> barrier() */
		} while (test_bit(bitnum, addr));
		/*! 20140517 addr가 가르키는 공간의 bitnum번째 비트가 0되면, while문을 빠져나온다
		 *  => 즉, 다른 user가 bitnum의 bitlock을 해제한 경우다
		  */
		preempt_disable();
	}
#endif
	/*! 20140517 여기까지 오면, 내가 bitnum위치의 bit lock을 획득했다는 의미
	 *  => 즉, test_and_set_bit_lock()에서 0을 리턴한 경우 
	 */

	__acquire(bitlock);
	/*! 20140517 sparse 일 경우만 실행, 현재 실행안됨 */
}

/*
 * Return true if it was acquired
 */
static inline int bit_spin_trylock(int bitnum, unsigned long *addr)
{
	preempt_disable();
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	if (unlikely(test_and_set_bit_lock(bitnum, addr))) {
		preempt_enable();
		return 0;
	}
#endif
	__acquire(bitlock);
	return 1;
}

/*
 *  bit-based spin_unlock()
 */
static inline void bit_spin_unlock(int bitnum, unsigned long *addr)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(!test_bit(bitnum, addr));
#endif
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	clear_bit_unlock(bitnum, addr);
#endif
	preempt_enable();
	__release(bitlock);
}

/*
 *  bit-based spin_unlock()
 *  non-atomic version, which can be used eg. if the bit lock itself is
 *  protecting the rest of the flags in the word.
 */
static inline void __bit_spin_unlock(int bitnum, unsigned long *addr)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(!test_bit(bitnum, addr));
#endif
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	__clear_bit_unlock(bitnum, addr);
#endif
	preempt_enable();
	__release(bitlock);
}

/*
 * Return true if the lock is held.
 */
static inline int bit_spin_is_locked(int bitnum, unsigned long *addr)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	return test_bit(bitnum, addr);
#elif defined CONFIG_PREEMPT_COUNT
	return preempt_count();
#else
	return 1;
#endif
}

#endif /* __LINUX_BIT_SPINLOCK_H */

