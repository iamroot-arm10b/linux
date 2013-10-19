/*
 * arch/arm/mm/proc-v7-2level.S
 *
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define TTB_S		(1 << 1)
#define TTB_RGN_NC	(0 << 3)
#define TTB_RGN_OC_WBWA	(1 << 3)
#define TTB_RGN_OC_WT	(2 << 3)
#define TTB_RGN_OC_WB	(3 << 3)
#define TTB_NOS		(1 << 5)
#define TTB_IRGN_NC	((0 << 0) | (0 << 6))
#define TTB_IRGN_WBWA	((0 << 0) | (1 << 6))
#define TTB_IRGN_WT	((1 << 0) | (0 << 6))
#define TTB_IRGN_WB	((1 << 0) | (1 << 6))

/* PTWs cacheable, inner WB not shareable, outer WB not shareable */
#define TTB_FLAGS_UP	TTB_IRGN_WB|TTB_RGN_OC_WB
#define PMD_FLAGS_UP	PMD_SECT_WB

/* PTWs cacheable, inner WBWA shareable, outer WBWA not shareable */
#define TTB_FLAGS_SMP	TTB_IRGN_WBWA|TTB_S|TTB_NOS|TTB_RGN_OC_WBWA
#define PMD_FLAGS_SMP	PMD_SECT_WBWA|PMD_SECT_S
/*!
 * WBWA(write back write allocate), section sharable 
 * Write-allocate cache
 * : A cache in which a cache miss on storing data causes a cache line to be allocated into the cache.
 * : 캐쉬가 쓰기 miss 발생할때 그 내용을 캐쉬에 올릴지 안올릴지를 결정하는 정책 차이
 *   기존에는 쓰기할때 캐쉬에 없으면 메모리에 쓰는데,
 *   이 옵션은 캐쉬에 없는 내용을 다시 캐쉬에 올려서 캐쉬에만 쓰고 넘어간다는 것.
 * SMP 일 경우에는 두 상수 모두 켜져있다.
 */

/*
 *	cpu_v7_switch_mm(pgd_phys, tsk)
 *
 *	Set the translation table base pointer to be pgd_phys
 *
 *	- pgd_phys - physical address of new TTB
 *
 *	It is assumed that:
 *	- we are not using split page tables
 */
ENTRY(cpu_v7_switch_mm)
#ifdef CONFIG_MMU
	mov	r2, #0
	mmid	r1, r1				@ get mm->context.id
	ALT_SMP(orr	r0, r0, #TTB_FLAGS_SMP)
	ALT_UP(orr	r0, r0, #TTB_FLAGS_UP)
#ifdef CONFIG_ARM_ERRATA_430973
	mcr	p15, 0, r2, c7, c5, 6		@ flush BTAC/BTB
#endif
#ifdef CONFIG_PID_IN_CONTEXTIDR
	mrc	p15, 0, r2, c13, c0, 1		@ read current context ID
	lsr	r2, r2, #8			@ extract the PID
	bfi	r1, r2, #8, #24			@ insert into new context ID
#endif
#ifdef CONFIG_ARM_ERRATA_754322
	dsb
#endif
	mcr	p15, 0, r1, c13, c0, 1		@ set context ID
	isb
	mcr	p15, 0, r0, c2, c0, 0		@ set TTB 0
	isb
#endif
	mov	pc, lr
ENDPROC(cpu_v7_switch_mm)

/*
 *	cpu_v7_set_pte_ext(ptep, pte)
 *
 *	Set a level 2 translation table entry.
 *
 *	- ptep  - pointer to level 2 translation table entry
 *		  (hardware version is stored at +2048 bytes)
 *	- pte   - PTE value to store
 *	- ext	- value for extended PTE bits
 */
ENTRY(cpu_v7_set_pte_ext)
	/*! 20131019 아키텍처에 의존적이어서 Asm으로 구현 */
#ifdef CONFIG_MMU
	str	r1, [r0]			@ linux version

	/*! 20131019
	 * r1: H/W pte를 셋팅하기 위해 필요한 linux pte 정보
	 * r3: H/W pte 정보를 셋팅한다. 
	 * arm reference manual B3.5.1 small page 참조
	 */
	bic	r3, r1, #0x000003f0
	/*! 20131019 4~9번 bit clear */
	bic	r3, r3, #PTE_TYPE_MASK
	/*! 20131019 #PTE_TYPE_MASK: 0, 1번 bit clear */
	orr	r3, r3, r2
	orr	r3, r3, #PTE_EXT_AP0 | 2
	/*! 20131019
	 * #PTE_EXT_AP0: 4번 bit, Access flag 설정
	 * 2 : small page
	 */

	tst	r1, #1 << 4
	orrne	r3, r3, #PTE_EXT_TEX(1)
	/*! 20131019 r1의 4번 bit가 1이라면 TEX[0](6번 bit)을 1로 설정 */

	eor	r1, r1, #L_PTE_DIRTY
	/*! 20131019 L_PTE_DIRTY 를 토글 */
	tst	r1, #L_PTE_RDONLY | L_PTE_DIRTY
	orrne	r3, r3, #PTE_EXT_APX
	/*! 20131019 L_PTE_RDONLY: 1, L_PTE_DIRTY: 0 인 경우 readonly로 설정(9번bit:1) */

	tst	r1, #L_PTE_USER
	orrne	r3, r3, #PTE_EXT_AP1
	/*! 20131019 L_PTE_USER: 1 인 경우에는 모든 권한에서 접근 가능하도록 설정(5번bit:1) */
#ifdef CONFIG_CPU_USE_DOMAINS
	@ allow kernel read/write access to read-only user pages
	tstne	r3, #PTE_EXT_APX
	bicne	r3, r3, #PTE_EXT_APX | PTE_EXT_AP0
#endif

	tst	r1, #L_PTE_XN
	orrne	r3, r3, #PTE_EXT_XN
	/*! 20131019 L_PTE_XN: 1 인 경우에는 실행불가 (0번bit:1) */

	tst	r1, #L_PTE_YOUNG
	tstne	r1, #L_PTE_VALID
#ifndef CONFIG_CPU_USE_DOMAINS
	eorne	r1, r1, #L_PTE_NONE
	tstne	r1, #L_PTE_NONE
#endif
	moveq	r3, #0
	/*! 20131019
	 * L_PTE_YOUNG: 1, L_PTE_VALID: 1 이면 pass
	 * L_PTE_YOUNG:0 또는 L_PTE_VALID:0 인 경우에는 r3를 0으로 설정
	 */

	/*! 20131019 현재 address에 대한 H/W pte 값 설정 */
 ARM(	str	r3, [r0, #2048]! )
 THUMB(	add	r0, r0, #2048 )
 THUMB(	str	r3, [r0] )
	ALT_SMP(W(nop))
	ALT_UP (mcr	p15, 0, r0, c7, c10, 1)		@ flush_pte
#endif
	mov	pc, lr
ENDPROC(cpu_v7_set_pte_ext)

	/*
	 * Memory region attributes with SCTLR.TRE=1
	 *
	 *   n = TEX[0],C,B
	 *   TR = PRRR[2n+1:2n]		- memory type
	 *   IR = NMRR[2n+1:2n]		- inner cacheable property
	 *   OR = NMRR[2n+17:2n+16]	- outer cacheable property
	 *
	 *			n	TR	IR	OR
	 *   UNCACHED		000	00
	 *   BUFFERABLE		001	10	00	00
	 *   WRITETHROUGH	010	10	10	10
	 *   WRITEBACK		011	10	11	11
	 *   reserved		110
	 *   WRITEALLOC		111	10	01	01
	 *   DEV_SHARED		100	01
	 *   DEV_NONSHARED	100	01
	 *   DEV_WC		001	10
	 *   DEV_CACHED		011	10
	 *
	 * Other attributes:
	 *
	 *   DS0 = PRRR[16] = 0		- device shareable property
	 *   DS1 = PRRR[17] = 1		- device shareable property
	 *   NS0 = PRRR[18] = 0		- normal shareable property
	 *   NS1 = PRRR[19] = 1		- normal shareable property
	 *   NOS = PRRR[24+n] = 1	- not outer shareable
	 */
.equ	PRRR,	0xff0a81a8
.equ	NMRR,	0x40e040e0

	/*
	 * Macro for setting up the TTBRx and TTBCR registers.
	 * - \ttb0 and \ttb1 updated with the corresponding flags.
	 */
	.macro	v7_ttb_setup, zero, ttbr0, ttbr1, tmp
	mcr	p15, 0, \zero, c2, c0, 2	@ TTB control register
	ALT_SMP(orr	\ttbr0, \ttbr0, #TTB_FLAGS_SMP)
	ALT_UP(orr	\ttbr0, \ttbr0, #TTB_FLAGS_UP)
	ALT_SMP(orr	\ttbr1, \ttbr1, #TTB_FLAGS_SMP)
	ALT_UP(orr	\ttbr1, \ttbr1, #TTB_FLAGS_UP)
	mcr	p15, 0, \ttbr1, c2, c0, 1	@ load TTB1
	.endm

	/*   AT
	 *  TFR   EV X F   I D LR    S
	 * .EEE ..EE PUI. .T.T 4RVI ZWRS BLDP WCAM
	 * rxxx rrxx xxx0 0101 xxxx xxxx x111 xxxx < forced
	 *   01    0 110       0011 1100 .111 1101 < we want
	 */
	.align	2
	.type	v7_crval, #object
v7_crval:
	crval	clear=0x2120c302, mmuset=0x10c03c7d, ucset=0x00c01c7c
