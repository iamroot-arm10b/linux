/*
 *  arch/arm/include/asm/assembler.h
 *
 *  Copyright (C) 1996-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains arm architecture specific defines
 *  for the different processors.
 *
 *  Do not include any C declarations in this file - it is included by
 *  assembler source.
 */
#ifndef __ASM_ASSEMBLER_H__
#define __ASM_ASSEMBLER_H__

#ifndef __ASSEMBLY__
#error "Only include this from assembly code"
#endif

#include <asm/ptrace.h>
#include <asm/domain.h>
#include <asm/opcodes-virt.h>

#define IOMEM(x)	(x)

/*
 * Endian independent macros for shifting bytes within registers.
 */
#ifndef __ARMEB__
#define pull            lsr
#define push            lsl
#define get_byte_0      lsl #0
#define get_byte_1	lsr #8
#define get_byte_2	lsr #16
#define get_byte_3	lsr #24
#define put_byte_0      lsl #0
#define put_byte_1	lsl #8
#define put_byte_2	lsl #16
#define put_byte_3	lsl #24
#else
#define pull            lsl
#define push            lsr
#define get_byte_0	lsr #24
#define get_byte_1	lsr #16
#define get_byte_2	lsr #8
#define get_byte_3      lsl #0
#define put_byte_0	lsl #24
#define put_byte_1	lsl #16
#define put_byte_2	lsl #8
#define put_byte_3      lsl #0
#endif

/*
 * Data preload for architectures that support it
 */
#if __LINUX_ARM_ARCH__ >= 5
#define PLD(code...)	code
#else
#define PLD(code...)
#endif

/*
 * This can be used to enable code to cacheline align the destination
 * pointer when bulk writing to memory.  Experiments on StrongARM and
 * XScale didn't show this a worthwhile thing to do when the cache is not
 * set to write-allocate (this would need further testing on XScale when WA
 * is used).
 *
 * On Feroceon there is much to gain however, regardless of cache mode.
 */
#ifdef CONFIG_CPU_FEROCEON
#define CALGN(code...) code
#else
#define CALGN(code...)
#endif

/*
 * Enable and disable interrupts
 */
#if __LINUX_ARM_ARCH__ >= 6
	.macro	disable_irq_notrace
	cpsid	i
	/*!
	 * cpsid = change processor state interrupt disable 
	 */
	.endm

	.macro	enable_irq_notrace
	cpsie	i
	.endm
#else
	.macro	disable_irq_notrace
	msr	cpsr_c, #PSR_I_BIT | SVC_MODE
	.endm

	.macro	enable_irq_notrace
	msr	cpsr_c, #SVC_MODE
	.endm
#endif

	.macro asm_trace_hardirqs_off
#if defined(CONFIG_TRACE_IRQFLAGS)
	stmdb   sp!, {r0-r3, ip, lr}
	bl	trace_hardirqs_off
	ldmia	sp!, {r0-r3, ip, lr}
#endif
	.endm

	.macro asm_trace_hardirqs_on_cond, cond
#if defined(CONFIG_TRACE_IRQFLAGS)
	/*
	 * actually the registers should be pushed and pop'd conditionally, but
	 * after bl the flags are certainly clobbered
	 */
	stmdb   sp!, {r0-r3, ip, lr}
	bl\cond	trace_hardirqs_on
	ldmia	sp!, {r0-r3, ip, lr}
#endif
	.endm

	.macro asm_trace_hardirqs_on
	asm_trace_hardirqs_on_cond al
	.endm

	.macro disable_irq
	disable_irq_notrace
	asm_trace_hardirqs_off
	.endm

	.macro enable_irq
	asm_trace_hardirqs_on
	enable_irq_notrace
	.endm
/*
 * Save the current IRQ state and disable IRQs.  Note that this macro
 * assumes FIQs are enabled, and that the processor is in SVC mode.
 */
	.macro	save_and_disable_irqs, oldcpsr
#ifdef CONFIG_CPU_V7M
	mrs	\oldcpsr, primask
#else
	mrs	\oldcpsr, cpsr
#endif
	disable_irq
	.endm

	.macro	save_and_disable_irqs_notrace, oldcpsr
	/*!
	 * 여기에 들어옴
	 */
	mrs	\oldcpsr, cpsr
	disable_irq_notrace
	/*!
	 * 인터럽트 disable
	 */
	.endm

/*
 * Restore interrupt state previously stored in a register.  We don't
 * guarantee that this will preserve the flags.
 */
	.macro	restore_irqs_notrace, oldcpsr
#ifdef CONFIG_CPU_V7M
	msr	primask, \oldcpsr
#else
	msr	cpsr_c, \oldcpsr
#endif
	.endm

	.macro restore_irqs, oldcpsr
	tst	\oldcpsr, #PSR_I_BIT
	asm_trace_hardirqs_on_cond eq
	restore_irqs_notrace \oldcpsr
	.endm

    /*! 20140309
     * 사용법 .pushsection name [, subsection] [, "flags"[, @type[,arguments]]]
     * https://sourceware.org/binutils/docs/as/Section.html 참고
     * flags = "a" 는 section is allocatable 을 나타냄
     */
/*! 20140309 arch/arm/mm/cache-v7.S 9001: label 및 2: label 참고할것*/
#define USER(x...)				\
9999:	x;					\
	.pushsection __ex_table,"a";		\
	.align	3;				\
	.long	9999b,9001f;			\
	.popsection

#ifdef CONFIG_SMP
#define ALT_SMP(instr...)					\
9998:	instr
/*
 * Note: if you get assembler errors from ALT_UP() when building with
 * CONFIG_THUMB2_KERNEL, you almost certainly need to use
 * ALT_SMP( W(instr) ... )
 */
	/*!
	 * SMP라면 그냥 그대로 수행이 되고
	 * 9998 은 SMP의 명령어로 되어있는 것인데.'
	 * RUN Time 에서 SMP를 지원안하는 프로세서는 명령어를 대체한다.
	 *
	 * 실제로 어떤상황에서 일어날수 있는가?
	 * 컴파일 타임과 런타임에서 달라질 경우에 발생될 수 있다.
	 * ALT_SMP, ALT_UP를 다써주는데 
	 * UP이면 아래쪽을 타고 돌라고 해준다.
	 * 기본적으로 ALT_SMP로 컴파일 되며
	 * ALT_UP일때 런타임시 한번에 ALT_UP 명령어로 덮어씌운다.
	 */

	/*!
	 * 디렉티브이고 . 아래 2가지가 실제로 가져올 데이터가 된다
	 * ldr r4 , {r0, r6}
	 * r0 = .long 9998b
	 * r6 = instr

	 * alt.smp.init 대체 명령어들의 집합. 
	 */
#define ALT_UP(instr...)					\
	.pushsection ".alt.smp.init", "a"			;\
	.long	9998b						;\
9997:	instr							;\
	.if . - 9997b != 4					;\
		.error "ALT_UP() content must assemble to exactly 4 bytes";\
	.endif							;\
	.popsection
#define ALT_UP_B(label)					\
	.equ	up_b_offset, label - 9998b			;\
	.pushsection ".alt.smp.init", "a"			;\
	.long	9998b						;\
	W(b)	. + up_b_offset					;\
	.popsection
        /*!
         * 디렉티브이고 . 아래 2가지가 실제로 가져올 데이터가 된다
         ldr r4 , { r0, r6}
         r0 = .long 9998b
         r 6 = instr

         alt.smp.init 대체 명령어들의 집합. 
         */
#else
#define ALT_SMP(instr...)
#define ALT_UP(instr...) instr
#define ALT_UP_B(label) b label
#endif

/*
 * Instruction barrier
 */
	.macro	instr_sync
#if __LINUX_ARM_ARCH__ >= 7
	isb
#elif __LINUX_ARM_ARCH__ == 6
	mcr	p15, 0, r0, c7, c5, 4
#endif
	.endm

/*
 * SMP data memory barrier
 */
	.macro	smp_dmb mode
#ifdef CONFIG_SMP
#if __LINUX_ARM_ARCH__ >= 7
	.ifeqs "\mode","arm"
	ALT_SMP(dmb)
	.else
	ALT_SMP(W(dmb))
	.endif
#elif __LINUX_ARM_ARCH__ == 6
	ALT_SMP(mcr	p15, 0, r0, c7, c10, 5)	@ dmb
#else
#error Incompatible SMP platform
#endif
	.ifeqs "\mode","arm"
	ALT_UP(nop)
	.else
	ALT_UP(W(nop))
	.endif
#endif
	.endm

#if defined(CONFIG_CPU_V7M)
	/*
	 * setmode is used to assert to be in svc mode during boot. For v7-M
	 * this is done in __v7m_setup, so setmode can be empty here.
	 */
	.macro	setmode, mode, reg
	.endm
#elif defined(CONFIG_THUMB2_KERNEL)
	.macro	setmode, mode, reg
	mov	\reg, #\mode
	msr	cpsr_c, \reg
	.endm
#else
	.macro	setmode, mode, reg
	msr	cpsr_c, #\mode
	.endm
#endif

/*
 * Helper macro to enter SVC mode cleanly and mask interrupts. reg is
 * a scratch register for the macro to overwrite.
 *
 * This macro is intended for forcing the CPU into SVC mode at boot time.
 * you cannot return to the original mode.
 */
.macro safe_svcmode_maskall reg:req
/*! 
  req: 반드시 필요하다는 의미 
  reg = r0 (not_angel: 에서 call할때의 reg값)
 */
#if __LINUX_ARM_ARCH__ >= 6
/*! 
  __LINUX_ARM_ARCH__ = 7 로 셋팅되어 있다.
 */
	mrs	\reg , cpsr
	eor	\reg, \reg, #HYP_MODE
	/*!
	 * HYP_MODE = 0x0000001a
	 * eor : exclusive or
	 */
	tst	\reg, #MODE_MASK
	/*!
	 * MODE_MASK = 0x0000001f
	 * AND 연산하여 0이면 Z flag가 1, 아니면 0
	 */
	bic	\reg , \reg , #MODE_MASK
	/*!
	 * bic: bit clear
	 * MODE_MASK = 0x0000001f
	 * 아래 5bit(mode bit 부분)을 클리어한다. 
	 */
	orr	\reg , \reg , #PSR_I_BIT | PSR_F_BIT | SVC_MODE
	/*!
	 * orr: reg값과 주어진 값을 or연산하여 reg에 저장
	 * 모드를 SVC로 만들고 I/F bit를 set하여 인터럽트를 disable한다.
	 * PSR_I_BIT = 0x00000080
	 * PSR_F_BIT = 0x00000040
	 * SVC_MODE  = 0x00000013
	 */
THUMB(	orr	\reg , \reg , #PSR_T_BIT	)
	bne	1f
	/*!
	 * PSR_T_BIT	0x00000020
	 * 하이퍼바이저가 아니면 1: 레이블로 점프
	 */
	orr	\reg, \reg, #PSR_A_BIT
	/*!
	 * PSR_A_BIT = 0x00000100
	 * 모호한 Abort를 clear시킨다.
	 * 책 p.622
	 */
	adr	lr, BSYM(2f)
	/*! 
	 * ifdef CONFIG_THUMB2_KERNEL
	 * thumb2면 주소에 1을 더한다. (책 pp.102~103)
	 * define BSYM(sym) sym + 1
	 * 아니면,
	 * define BSYM(sym)	sym
	 */
	msr	spsr_cxsf, \reg
	/*!
	 * 책 p.84부터 나온다.
	 * spsr은 4개의 필드로 구성되어 있다. control, extention, status, flag 필드
	 * 모든 bit를 reg값으로 업데이트 하라는 의미
	 * MSR의 경우엔 Move PSR,Reg 정도로 생각할 수 있다.
	 * 즉, PSR(Processor Status Register)에 레지스터값을 넣는 명령.
	 */
	__MSR_ELR_HYP(14)
	__ERET
	/*!
	 * arch/arm/include/asm/opcodes-virt.h 에서 정의. 
	 * 하이퍼바이저 기계어 코드다.
	 * #define __MSR_ELR_HYP(regnum)	__inst_arm_thumb32(		\
	 *   	0xE12EF300 | regnum,						\
	 *   	0xF3808E30 | (regnum << 16)					\
	 * )
	 * #define __ERET	__inst_arm_thumb32(				\
	 *   	0xE160006E,							\
	 *   	0xF3DE8F00							\
	 * )
	 */
1:	msr	cpsr_c, \reg
	/*!
	 * CPSR의 control 필드에 레지스터값을 넣는다.
	 */
2:
#else
	/*!
	 * else of #if __LINUX_ARM_ARCH__ >= 6
	 */
/*
 * workaround for possibly broken pre-v6 hardware
 * (akita, Sharp Zaurus C-1000, PXA270-based)
 */
	setmode	PSR_F_BIT | PSR_I_BIT | SVC_MODE, \reg
	/*!
	 * 예전(6미만)버전의 setmode 매크로
	 */
#endif
.endm
/*! 
 * safe_svcmode_maskall reg:req 매크로의 끝
 */

/*
 * STRT/LDRT access macros with ARM and Thumb-2 variants
 */
#ifdef CONFIG_THUMB2_KERNEL

	.macro	usraccoff, instr, reg, ptr, inc, off, cond, abort, t=TUSER()
9999:
	.if	\inc == 1
	\instr\cond\()b\()\t\().w \reg, [\ptr, #\off]
	.elseif	\inc == 4
	\instr\cond\()\t\().w \reg, [\ptr, #\off]
	.else
	.error	"Unsupported inc macro argument"
	.endif

	.pushsection __ex_table,"a"
	.align	3
	.long	9999b, \abort
	.popsection
	.endm

	.macro	usracc, instr, reg, ptr, inc, cond, rept, abort
	@ explicit IT instruction needed because of the label
	@ introduced by the USER macro
	.ifnc	\cond,al
	.if	\rept == 1
	itt	\cond
	.elseif	\rept == 2
	ittt	\cond
	.else
	.error	"Unsupported rept macro argument"
	.endif
	.endif

	@ Slightly optimised to avoid incrementing the pointer twice
	usraccoff \instr, \reg, \ptr, \inc, 0, \cond, \abort
	.if	\rept == 2
	usraccoff \instr, \reg, \ptr, \inc, \inc, \cond, \abort
	.endif

	add\cond \ptr, #\rept * \inc
	.endm

#else	/* !CONFIG_THUMB2_KERNEL */

	.macro	usracc, instr, reg, ptr, inc, cond, rept, abort, t=TUSER()
	.rept	\rept
9999:
	.if	\inc == 1
	\instr\cond\()b\()\t \reg, [\ptr], #\inc
	.elseif	\inc == 4
	\instr\cond\()\t \reg, [\ptr], #\inc
	.else
	.error	"Unsupported inc macro argument"
	.endif

	.pushsection __ex_table,"a"
	.align	3
	.long	9999b, \abort
	.popsection
	.endr
	.endm

#endif	/* CONFIG_THUMB2_KERNEL */

	.macro	strusr, reg, ptr, inc, cond=al, rept=1, abort=9001f
	usracc	str, \reg, \ptr, \inc, \cond, \rept, \abort
	.endm

	.macro	ldrusr, reg, ptr, inc, cond=al, rept=1, abort=9001f
	usracc	ldr, \reg, \ptr, \inc, \cond, \rept, \abort
	.endm

/* Utility macro for declaring string literals */
	.macro	string name:req, string
	.type \name , #object
\name:
	.asciz "\string"
	.size \name , . - \name
	.endm

	.macro check_uaccess, addr:req, size:req, limit:req, tmp:req, bad:req
#ifndef CONFIG_CPU_USE_DOMAINS
	adds	\tmp, \addr, #\size - 1
	sbcccs	\tmp, \tmp, \limit
	bcs	\bad
#endif
	.endm

#endif /* __ASM_ASSEMBLER_H__ */
