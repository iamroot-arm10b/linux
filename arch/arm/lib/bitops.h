#include <asm/unwind.h>

#if __LINUX_ARM_ARCH__ >= 6
/*! 20131102 instr를 atomic 하게 동작하기 위한 코드 */
	.macro	bitop, name, instr
ENTRY(	\name		)
UNWIND(	.fnstart	)
	ands	ip, r1, #3
	/*!
	 * r0: nr 의 현재 동작중인 CPU ID
	 * r1: p, long배열 Index
	 */
	/*! 20131109 ip: r1 & 3 (4byte align한 것) */
	strneb	r1, [ip]		@ assert word-aligned
	/*!
	 * ip값이 1,2,3 인 경우(4의 배수가 아닌 경우)에 종료를 할 것이라고 추측?
	 */
	mov	r2, #1
	and	r3, r0, #31		@ Get bit offset
	mov	r0, r0, lsr #5
	add	r1, r1, r0, lsl #2	@ Get word offset
	/*!
	 * cpu id 의 하위 5bit를 받아서 long단위로 쪼갠다.
	 * bitmap % 32 = r3
	 * bitmap / 32 = r1
	 * r0가 100 이라면, r1은 3, r3는 4
	 */
	mov	r3, r2, lsl r3
	/*! bitmap string에서 원하는 코드를 atomic하게 구하기 위해서. */
1:	ldrex	r2, [r1]
	/*! 20131109 r2: 계산할 노드 위치, r1: node 주소 */
	\instr	r2, r2, r3
	strex	r0, r2, [r1]
	cmp	r0, #0
	bne	1b
	/*! 20131109
	 * r1 메모리번지를 모니터링하여 load하여 값을 셋팅하고 store하기 전에 다른 값으로 변경되면
	 * 다시 load하여 재기록 하여 atomic을 보장해 준다.
	 */
	bx	lr
	/*! lr의 0번bit가 0이면 ARM상태로, 1이면 Thumb상태로 리턴한다. */
UNWIND(	.fnend		)
ENDPROC(\name		)
	.endm

	.macro	testop, name, instr, store
ENTRY(	\name		)
UNWIND(	.fnstart	)
	/*! 20131109 #define test_and_clear_bit(nr,p) */
	ands	ip, r1, #3
	strneb	r1, [ip]		@ assert word-aligned
	/*! 20131109
	/* for checking 4bytes align of p
	 * 4byte align을 어겼을 경우, va 기준으로 첫번째 page의 속성에
	 * access 권한을 없애 page fault가 발생하여 종료할 것이라고 추측?
	 */
	mov	r2, #1
	and	r3, r0, #31		@ Get bit offset
	mov	r0, r0, lsr #5
	add	r1, r1, r0, lsl #2	@ Get word offset
	mov	r3, r2, lsl r3		@ create mask
	/*!
	 * p의 하위 5bit를 받아서 long단위로 쪼갠다.(
	 * bitmap % 32 = r3
	 * bitmap / 32 = r1
	 * r1의  100번째 비트라면, r1=r1+3, r3=0x10
	 */
	smp_dmb
1:	ldrex	r2, [r1]
	ands	r0, r2, r3		@ save old value of bit
	\instr	r2, r2, r3		@ toggle bit
	strex	ip, r2, [r1]
	/*! 20131109
	 * 워드 오프셋 위치에서 4bytes읽어와서, 해당비트 체크.
	 * 해당비트가 1인경우에 clear한후, 워드 오프셋에 다시 저장.
	 */
	cmp	ip, #0
	bne	1b
	smp_dmb
	/*! 20131109  워드오프셋위치의 데이터가 변경된 경우, 다시실행(1번 레이블로 점프) */
	cmp	r0, #0
	movne	r0, #1
2:	bx	lr
	/*! 20131109 해당비트의 원래 값을 리턴 */
UNWIND(	.fnend		)
ENDPROC(\name		)
	.endm
#else
	.macro	bitop, name, instr
ENTRY(	\name		)
UNWIND(	.fnstart	)
	ands	ip, r1, #3
	strneb	r1, [ip]		@ assert word-aligned
	and	r2, r0, #31
	mov	r0, r0, lsr #5
	mov	r3, #1
	mov	r3, r3, lsl r2
	save_and_disable_irqs ip
	ldr	r2, [r1, r0, lsl #2]
	\instr	r2, r2, r3
	str	r2, [r1, r0, lsl #2]
	restore_irqs ip
	mov	pc, lr
UNWIND(	.fnend		)
ENDPROC(\name		)
	.endm

/**
 * testop - implement a test_and_xxx_bit operation.
 * @instr: operational instruction
 * @store: store instruction
 *
 * Note: we can trivially conditionalise the store instruction
 * to avoid dirtying the data cache.
 */
	.macro	testop, name, instr, store
ENTRY(	\name		)
UNWIND(	.fnstart	)
	ands	ip, r1, #3
	strneb	r1, [ip]		@ assert word-aligned
	and	r3, r0, #31
	mov	r0, r0, lsr #5
	save_and_disable_irqs ip
	ldr	r2, [r1, r0, lsl #2]!
	mov	r0, #1
	tst	r2, r0, lsl r3
	\instr	r2, r2, r0, lsl r3
	\store	r2, [r1]
	moveq	r0, #0
	restore_irqs ip
	mov	pc, lr
UNWIND(	.fnend		)
ENDPROC(\name		)
	.endm
#endif
