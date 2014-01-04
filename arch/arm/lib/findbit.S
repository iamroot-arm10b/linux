/*
 *  linux/arch/arm/lib/findbit.S
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 16th March 2001 - John Ripley <jripley@sonicblue.com>
 *   Fixed so that "size" is an exclusive not an inclusive quantity.
 *   All users of these functions expect exclusive sizes, and may
 *   also call with zero size.
 * Reworked by rmk.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
                .text

/*
 * Purpose  : Find a 'zero' bit
 * Prototype: int find_first_zero_bit(void *addr, unsigned int maxbit);
 */
ENTRY(_find_first_zero_bit_le)
		teq	r1, #0	
		beq	3f
		mov	r2, #0
1:
 ARM(		ldrb	r3, [r0, r2, lsr #3]	)
 THUMB(		lsr	r3, r2, #3		)
 THUMB(		ldrb	r3, [r0, r3]		)
		eors	r3, r3, #0xff		@ invert bits
		bne	.L_found		@ any now set - found zero bit
		add	r2, r2, #8		@ next bit pointer
2:		cmp	r2, r1			@ any more?
		blo	1b
3:		mov	r0, r1			@ no free bits
		mov	pc, lr
ENDPROC(_find_first_zero_bit_le)

/*
 * Purpose  : Find next 'zero' bit
 * Prototype: int find_next_zero_bit(void *addr, unsigned int maxbit, int offset)
 */
/*! 20131116 find_next_zero_bit(bdata->node_bootmem_map, midx, sidx); */
ENTRY(_find_next_zero_bit_le)
		teq	r1, #0
		beq	3b
		/*! 20131116 r1(bitmap) 이 0이면 return */
		ands	ip, r2, #7
		/*! 20131116 0, 8, 16 인 경우에는 위쪽으로 jump */
		beq	1b			@ If new byte, goto old routine
 ARM(		ldrb	r3, [r0, r2, lsr #3]	)
 THUMB(		lsr	r3, r2, #3		)
 THUMB(		ldrb	r3, [r0, r3]		)
		eor	r3, r3, #0xff		@ now looking for a 1 bit
		movs	r3, r3, lsr ip		@ shift off unused bits
		bne	.L_found
		/*! 20131116 0이 나타나는 첫번째 위치offset 리턴 */
		orr	r2, r2, #7		@ if zero, then no bits here
		add	r2, r2, #1		@ align bit pointer
		b	2b			@ loop for next bit
ENDPROC(_find_next_zero_bit_le)

/*
 * Purpose  : Find a 'one' bit
 * Prototype: int find_first_bit(const unsigned long *addr, unsigned int maxbit);
 */
ENTRY(_find_first_bit_le)
		teq	r1, #0	
		beq	3f
		mov	r2, #0
1:
 ARM(		ldrb	r3, [r0, r2, lsr #3]	)
 THUMB(		lsr	r3, r2, #3		)
 THUMB(		ldrb	r3, [r0, r3]		)
		movs	r3, r3
		bne	.L_found		@ any now set - found zero bit
		add	r2, r2, #8		@ next bit pointer
2:		cmp	r2, r1			@ any more?
		blo	1b
3:		mov	r0, r1			@ no free bits
		mov	pc, lr
ENDPROC(_find_first_bit_le)

/*
 * Purpose  : Find next 'one' bit
 * Prototype: int find_next_zero_bit(void *addr, unsigned int maxbit, int offset)
 */
/*! 20140104
 * r0 = addr = bitmap 배열의 시작주소
 * r1 = maxbit = bitmap의 전체 크기
 * r2 = offset = 찾을 시작 bit
 * r2(offset) 이후에 1이되어있는 offset 값을 반환한다.
 */
ENTRY(_find_next_bit_le)
		/*! 20140104 r1 즉 bitmap의 크기가 0이면 return */
		teq	r1, #0
		beq	3b
		/*! 20140104 r1(bitmap) 이 0이면 return */
		ands	ip, r2, #7
		/*! 20140104 0, 8, 16 인 경우에는 위쪽으로 jump */
		beq	1b			@ If new byte, goto old routine
		/*! 20140104
		 * ex) 18 = 0b10010 -> 하위 3bit 010 / 나머지 10
		 * byte offset = 0b10 = 2, bit offset = 0b010 = 2
		 * 2번째 byte의 2번째 bit 즉 18번째 bit를 찾는다.
		 * 18번째 bit부터 1이 설정되어 있는 bit를 찾는다.
		 */

		/*! 20140104 r0 에서 byte offset 만큼 떨어진 위치의 1byte를 읽어옴 */
 ARM(		ldrb	r3, [r0, r2, lsr #3]	)
 THUMB(		lsr	r3, r2, #3		)
 THUMB(		ldrb	r3, [r0, r3]		)
		/*! 20140104 r3 = r3 >> ip(=bit offset), 1을 찾을때까지 루프 */
		movs	r3, r3, lsr ip		@ shift off unused bits
		bne	.L_found
		/*! 20140104
		 * 7을 or 연산함으로써 다음 Byte를 가리키게 함
		 * 0b10010 or 0b111 = 0b10111 = 다음 byte
		 */
		orr	r2, r2, #7		@ if zero, then no bits here
		add	r2, r2, #1		@ align bit pointer
		b	2b			@ loop for next bit
ENDPROC(_find_next_bit_le)

#ifdef __ARMEB__

ENTRY(_find_first_zero_bit_be)
		teq	r1, #0
		beq	3f
		mov	r2, #0
1:		eor	r3, r2, #0x18		@ big endian byte ordering
 ARM(		ldrb	r3, [r0, r3, lsr #3]	)
 THUMB(		lsr	r3, #3			)
 THUMB(		ldrb	r3, [r0, r3]		)
		eors	r3, r3, #0xff		@ invert bits
		bne	.L_found		@ any now set - found zero bit
		add	r2, r2, #8		@ next bit pointer
2:		cmp	r2, r1			@ any more?
		blo	1b
3:		mov	r0, r1			@ no free bits
		mov	pc, lr
ENDPROC(_find_first_zero_bit_be)

ENTRY(_find_next_zero_bit_be)
		teq	r1, #0
		beq	3b
		ands	ip, r2, #7
		beq	1b			@ If new byte, goto old routine
		eor	r3, r2, #0x18		@ big endian byte ordering
 ARM(		ldrb	r3, [r0, r3, lsr #3]	)
 THUMB(		lsr	r3, #3			)
 THUMB(		ldrb	r3, [r0, r3]		)
		eor	r3, r3, #0xff		@ now looking for a 1 bit
		movs	r3, r3, lsr ip		@ shift off unused bits
		bne	.L_found
		orr	r2, r2, #7		@ if zero, then no bits here
		add	r2, r2, #1		@ align bit pointer
		b	2b			@ loop for next bit
ENDPROC(_find_next_zero_bit_be)

ENTRY(_find_first_bit_be)
		teq	r1, #0
		beq	3f
		mov	r2, #0
1:		eor	r3, r2, #0x18		@ big endian byte ordering
 ARM(		ldrb	r3, [r0, r3, lsr #3]	)
 THUMB(		lsr	r3, #3			)
 THUMB(		ldrb	r3, [r0, r3]		)
		movs	r3, r3
		bne	.L_found		@ any now set - found zero bit
		add	r2, r2, #8		@ next bit pointer
2:		cmp	r2, r1			@ any more?
		blo	1b
3:		mov	r0, r1			@ no free bits
		mov	pc, lr
ENDPROC(_find_first_bit_be)

ENTRY(_find_next_bit_be)
		teq	r1, #0
		beq	3b
		ands	ip, r2, #7
		beq	1b			@ If new byte, goto old routine
		eor	r3, r2, #0x18		@ big endian byte ordering
 ARM(		ldrb	r3, [r0, r3, lsr #3]	)
 THUMB(		lsr	r3, #3			)
 THUMB(		ldrb	r3, [r0, r3]		)
		movs	r3, r3, lsr ip		@ shift off unused bits
		bne	.L_found
		orr	r2, r2, #7		@ if zero, then no bits here
		add	r2, r2, #1		@ align bit pointer
		b	2b			@ loop for next bit
ENDPROC(_find_next_bit_be)

#endif

/*
 * One or more bits in the LSB of r3 are assumed to be set.
 */
.L_found:
#if __LINUX_ARM_ARCH__ >= 5
		rsb	r0, r3, #0
		/*! 20131116 r0 = 0 - r3 */
		and	r3, r3, r0
		/*! 20131116 r3: r3의 bit 중에서 가장 오른쪽에 있는 bit위치의 값이 1인 값 */
		clz	r3, r3
		/*! 20131116 r3: 31bit에서 시작하여 첫번째로 1을 만나는 위치값 */
		rsb	r3, r3, #31
		/*! 20131116 r3 = 31 - r3 */
		add	r0, r2, r3
#else
		tst	r3, #0x0f
		addeq	r2, r2, #4
		movne	r3, r3, lsl #4
		tst	r3, #0x30
		addeq	r2, r2, #2
		movne	r3, r3, lsl #2
		tst	r3, #0x40
		addeq	r2, r2, #1
		mov	r0, r2
#endif
		cmp	r1, r0			@ Clamp to maxbit
		movlo	r0, r1
		mov	pc, lr

