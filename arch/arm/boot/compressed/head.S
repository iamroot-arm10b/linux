/*
 *  linux/arch/arm/boot/compressed/head.S
 *
 *  Copyright (C) 1996-2002 Russell King
 *  Copyright (C) 2004 Hyok S. Choi (MPU support)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

	.arch	armv7-a
        /*! 타겟 프로세스를 지정해준다 */
	/*! .arch: select the target architecture */
        /*! 어셈블러 컴파일 할때 타깃을 지정해야 알맞게 컴파일 할 수 있다.  */
/*
 * Debugging stuff
 *
 * Note that these macros must not contain any code which is not
 * 100% relocatable.  Any attempt to do so will result in a crash.
 * Please select one of the following when turning on debugging.
 */
#ifdef DEBUG 

#if defined(CONFIG_DEBUG_ICEDCC)
        /*! 컴파일때 옵션으로 주는 것 */
        /*! DCC 메시지 출력 매크로정의. ARM core의 cp14번을 통해서 uart와 유사하게 메시지 출력하는 기능 */
        /*! mcr 부분 */

#if defined(CONFIG_CPU_V5) || defined(CONFIG_CPU_V6K) || defined(CONFIG_CPU_V7)
		.macro	loadsp, rb, tmp
		.endm
                /*! loadsp 라는 어셈블리 매크로를 정의한다. .endm 까지 */
                /*! rb, tmp 는argument */
                /*! ARM 교재 p.711 */
		.macro	writeb, ch, rb
		mcr	p14, 0, \ch, c0, c5, 0 
		.endm
#elif defined(CONFIG_CPU_XSCALE)
		.macro	loadsp, rb, tmp
		.endm
		.macro	writeb, ch, rb
		mcr	p14, 0, \ch, c8, c0, 0
		.endm
#else
		.macro	loadsp, rb, tmp
		.endm
		.macro	writeb, ch, rb
		mcr	p14, 0, \ch, c1, c0, 0
		.endm
#endif
/*! loadsp와 writeb 매크로를 config에 정해진 CPU에 따라 정의한 부분 */
/* 엑시노스는config에서 CONFIG_CPU_V7로 정의되어 있다. */

#else
/* CONFIG_DEBUG_ICEDCC가 정의되어 있지 않는 경우 */

#include CONFIG_DEBUG_LL_INCLUDE

		.macro	writeb,	ch, rb
		senduart \ch, \rb
                /*! senduart 는 
                   /home/arm/linux/arch/arm/include/debug/samsung.S 의 47라인에
                   .macro  senduart,rd,rx
                   strb    \rd, [\rx, # S3C2410_UTXH]
                   .endm
                   로 정의되어 있다.
                   writeb를 사용할 때 출력이 어느쪽으로 가는지를 정의하기 위한 것
                */
		.endm

#if defined(CONFIG_ARCH_SA1100)
		.macro	loadsp, rb, tmp
		mov	\rb, #0x80000000	@ physical base address
#ifdef CONFIG_DEBUG_LL_SER3
		add	\rb, \rb, #0x00050000	@ Ser3
#else
		add	\rb, \rb, #0x00010000	@ Ser1
#endif
		.endm
#elif defined(CONFIG_ARCH_S3C24XX)
		.macro loadsp, rb, tmp
		mov	\rb, #0x50000000
		add	\rb, \rb, #0x4000 * CONFIG_S3C_LOWLEVEL_UART_PORT
		.endm

#else           /*! CONFIG_CPU_V7 인 경우에 이 부분 실행된다. */
		.macro	loadsp,	rb, tmp
		addruart \rb, \tmp
                /*! addruart는 ~/linux/arch/arm/include/debug/exynos.S 
                    .macro addruart, rp, rv, tmp 에서 정의됨 */
		.endm
#endif
#endif
#endif          /*! end of  #ifdef DEBUG */ 

		.macro	kputc,val
		mov	r0, \val
		bl	putc
		.endm

		.macro	kphex,val,len
		mov	r0, \val
		mov	r1, #\len
		bl	phex
		.endm

		.macro	debug_reloc_start
#ifdef DEBUG
		kputc	#'\n'
		kphex	r6, 8		/* processor id */
		kputc	#':'
		kphex	r7, 8		/* architecture id */
#ifdef CONFIG_CPU_CP15
		kputc	#':'
		mrc	p15, 0, r0, c1, c0
		kphex	r0, 8		/* control reg */
#endif
		kputc	#'\n'
		kphex	r5, 8		/* decompressed kernel start */
		kputc	#'-'
		kphex	r9, 8		/* decompressed kernel end  */
		kputc	#'>'
		kphex	r4, 8		/* kernel execution address */
		kputc	#'\n'
#endif
		.endm

		.macro	debug_reloc_end
#ifdef DEBUG
		kphex	r5, 8		/* end of kernel */
		kputc	#'\n'
		mov	r0, r4
		bl	memdump		/* dump 256 bytes at start of kernel */
#endif
		.endm

		.section ".start", #alloc, #execinstr
/*
 * sort out different calling conventions
 */
		.align
                /*! ARM은 기본 4byte align 하는데 코드 들어가는 위치를 4의 배수로 만들어준다.
                  4의 배수가 아니면 pedding을 해 준다. 
                  숫자가 없으면 default 4
                 */
		.arm				@ Always enter in ARM state
start:
		.type	start,#function
                /*! 
                 * .type: start는 start 레이블 이름, #function 
                 * start라는 symbol이 #function이라는 type이다.  
                 * ELF의 symbol 테이블의 이름과 주소를 설정하는 부분 같은 것?
                 */
		.rept	7
		mov	r0, r0
		.endr
                /*! mov r0, r0를 7번 반복한다.
                 bootloader에서 기본주소를 32byte 더해줘서 실행해야하는 부분
                 */
   ARM(		mov	r0, r0		)
   ARM(		b	1f		)
 THUMB(		adr	r12, BSYM(1f)	)
 THUMB(		bx	r12		)
        /*! CONFIG_THUMB2_KERNEL 이 정의되어 있으면 THUMB 를 사용하고 아니면 ARM 이 실행된다. 
              include/asm/unified.h 에 있다.
            우리가 타겟으로 하는 아키텍처인 경우,
            ARM(		mov	r0, r0		)
            ARM(		b	1f		)
            이 실행된다. 
            뒷쪽(f:forward) 1번 레이블로 점프한다. 
         */
		.word	0x016f2818		@ Magic numbers to help the loader
        /*! 
            이 값을 ARM용 매직넘버로 사용한다.
            아키텍처에 따라 매직넘버를 안쓸수도 있다. 
            부트로더가 이 값을 검색해야 하므로 
            부트로더에서 ARM용인지를 체크할 수도 있다.
            이 매직넘버 다음에 시작레이블을 찾을 수 있다. 
         */
		.word	start			@ absolute load/run zImage address
		.word	_edata			@ zImage end address
        /*!
          _edata = .; 현재라인의 주소를 _edata에 넣는 것
          arm/kernel/vmlinux.lds.S 에 정의되어 있다. 
         */
 THUMB(		.thumb			) /*! thumb모드로 컴파일하라는 명령 */
1:
		mrs	r9, cpsr
        /*! cpsr을 r9로 복사한다. 
          http://infocenter.arm.com/help/topic/com.arm.doc.qrc0001l/QRC0001_UAL.pdf
         */
#ifdef CONFIG_ARM_VIRT_EXT
                /*! the ARM Virtualization 이 enable인경우 실행된다.
                   가상화 지원되는 제품인 경우에 사용한다.
                   default config 에는 없지만 default config 로 타겟을 
                   엑시노스4로 생성하면 enable 된다.
                 */
		bl	__hyp_stub_install	@ get into SVC mode, reversibly
                /*! 
                 * Hypervisor stub installation functions.
                      우리는 하이퍼바이저를 보지 않는다. 
                 */
#endif
		mov	r7, r1			@ save architecture ID
		mov	r8, r2			@ save atags pointer
                /*!
                  r1 = machine type number discovered in (3) above.
                  r2 = physical address of tagged list in system RAM, or
                       physical address of device tree block (dtb) in system RAM
                  부트로드에서 셋팅하는 부분
                  Documentation/arm/Booting 에 있다.
                 */

#ifndef __ARM_ARCH_2__
		/*
		 * Booting from Angel - need to enter SVC mode and disable
		 * FIQs/IRQs (numeric definitions from angel arm.h source).
		 * We only do this if we were in user mode on entry.
		 */
		mrs	r2, cpsr		@ get current mode
		tst	r2, #3			@ not user?
                /*! 
                  아래 두bit가 하나라도 1이면 EQ가 set되어 not_angel로 점프.
                  유저모드인지 아닌지를 체크한다. user mode는 끝 두bit가 00
                  유저모드이면 cpsr을 변경시키지 못하므로 mov r0, #0x17  로 간다.
                 */
		bne	not_angel
		mov	r0, #0x17		@ angel_SWIreason_EnterSVC
                /*! 
                  ?? angel에서 SVC모드로 들어가게 하는 인자값 일거라고 예상
                 */
 ARM(		swi	0x123456	)	@ angel_SWI_ARM
 THUMB(		svc	0xab		)	@ angel_SWI_THUMB
                /*! 
                  angel일때 SVC 로 바꾸는 루틴
                 */
not_angel:
		safe_svcmode_maskall r0
                /*!
                 * arch/arm/include/asm/assembler.h 의 .macro safe_svcmode_maskall reg:req 에서 정의됨.
                 * 인터럽트를 금지시키고 SVC 모드로 들어간다.
                 */
		msr	spsr_cxsf, r9		@ Save the CPU boot mode in
						@ SPSR
                /*!
                 * mrs	r9, cpsr 을 해 둔것에서 가져옴
                 */
#else
		teqp	pc, #0x0c000003		@ turn off interrupts
                /*!
                 * ARM_ARCH_2__에서 인터럽트 끄는 것
                 */
#endif

		/*
		 * Note that some cache flushing and other stuff may
		 * be needed here - is there an Angel SWI call for this?
		 */

		/*
		 * some architecture specific code can be inserted
		 * by the linker here, but it should preserve r7, r8, and r9.
		 */

		.text
                /*!
                 * text섹션(코드영역)의 시작
                 */

#ifdef CONFIG_AUTO_ZRELADDR
                /*!
                 * 설정되어 있지 않음
                 */
		@ determine final kernel image address
		mov	r4, pc
		and	r4, r4, #0xf8000000
		add	r4, r4, #TEXT_OFFSET
#else
		ldr	r4, =zreladdr
                /*!
                 * 고정된 주소값을 r4 영역으로 가져온다.
                 압축을 풀어서 vmlinux 를 저장시킬 주소
                 zreladdr = 0x40008000 
                 compressed 안에 있는 Makefile이 실행되면서 만들어준다.
                    # Supply ZRELADDR to the decompressor via a linker symbol.
                    ifneq ($(CONFIG_AUTO_ZRELADDR),y)
                    LDFLAGS_vmlinux += --defsym zreladdr=$(ZRELADDR)
                    endif
                    ifeq ($(CONFIG_CPU_ENDIAN_BE8),y)
                    LDFLAGS_vmlinux += --be8
                    endif
                 */
#endif

		bl	cache_on
                /*!
                 * cache_on 로 점프
                 */

restart:	adr	r0, LC0
/*!
 * LC0 의 주소값을 r0로 가져온다.
 */
		ldmia	r0, {r1, r2, r3, r6, r10, r11, r12}
                /*!
                  LC0에서 정보를 가져온다.
                 */
		ldr	sp, [r0, #28]

                /*!
                 * sp 설정 
                 */
		/*
		 * We might be running at a different address.  We need
		 * to fix up various pointers.
		 */
		sub	r0, r0, r1		@ calculate the delta offset
                /*!
                 * 보정하는 값을 구한다.
                 */
		add	r6, r6, r0		@ _edata
                /*!
                 * r6 의 현재 실행중인 위치 
                 */
		add	r10, r10, r0		@ inflated kernel size location
                /*!
                 * 압축한 파일의 사이즈 .
                 커널 컴파일한 바이너리를 include 하여서 바이너리의 끝을 가르키고 있다.
                 -4가지고 있음.
                 */

		/*
		 * The kernel build system appends the size of the
		 * decompressed kernel at the end of the compressed data
		 * in little-endian form.
		 */
		ldrb	r9, [r10, #0]
		ldrb	lr, [r10, #1]
		orr	r9, r9, lr, lsl #8
		ldrb	lr, [r10, #2]
		ldrb	r10, [r10, #3]
		orr	r9, r9, lr, lsl #16
		orr	r9, r9, r10, lsl #24
                /*!
                 * 빅 인이안 이더라도 제대로 읽을수 있도록 하여준다.
                 */

#ifndef CONFIG_ZBOOT_ROM
		/* malloc space is above the relocated stack (64k max) */
		add	sp, sp, r0
		add	r10, sp, #0x10000
#else
		/*
		 * With ZBOOT_ROM the bss/stack is non relocatable,
		 * but someone could still run this code from RAM,
		 * in which case our reference is _edata.
		 */
		mov	r10, r6
                /*!
                 * r6 = edata end_of data data의 끝 
                 */
#endif

		mov	r5, #0			@ init dtb size to 0
#ifdef CONFIG_ARM_APPENDED_DTB
/*
 *   r0  = delta
 *   r2  = BSS start
 *   r3  = BSS end
 *   r4  = final kernel address
 *   r5  = appended dtb size (still unknown)
 *   r6  = _edata
 *   r7  = architecture ID
 *   r8  = atags/device tree pointer
 *   r9  = size of decompressed image
 *   r10 = end of this image, including  bss/stack/malloc space if non XIP
 *   r11 = GOT start
 *   r12 = GOT end
 *   sp  = stack pointer
 *
 * if there are device trees (dtb) appended to zImage, advance r10 so that the
 * dtb data will get relocated along with the kernel if necessary.
 */

		ldr	lr, [r6, #0]
#ifndef __ARMEB__
                /*!
                 * ARM Endian Big??
                 */
		ldr	r1, =0xedfe0dd0		@ sig is 0xd00dfeed big endian
#else
		ldr	r1, =0xd00dfeed
#endif
		cmp	lr, r1
		bne	dtb_check_done		@ not found

#ifdef CONFIG_ARM_ATAG_DTB_COMPAT
		/*
		 * OK... Let's do some funky business here.
		 * If we do have a DTB appended to zImage, and we do have
		 * an ATAG list around, we want the later to be translated
		 * and folded into the former here.  To be on the safe side,
		 * let's temporarily move  the stack away into the malloc
		 * area.  No GOT fixup has occurred yet, but none of the
		 * code we're about to call uses any global variable.
		*/
		add	sp, sp, #0x10000
		stmfd	sp!, {r0-r3, ip, lr}
		mov	r0, r8
		mov	r1, r6
		sub	r2, sp, r6
		bl	atags_to_fdt

		/*
		 * If returned value is 1, there is no ATAG at the location
		 * pointed by r8.  Try the typical 0x100 offset from start
		 * of RAM and hope for the best.
		 */
		cmp	r0, #1
		sub	r0, r4, #TEXT_OFFSET
		add	r0, r0, #0x100
		mov	r1, r6
		sub	r2, sp, r6
		bleq	atags_to_fdt

		ldmfd	sp!, {r0-r3, ip, lr}
		sub	sp, sp, #0x10000
#endif

		mov	r8, r6			@ use the appended device tree

		/*
		 * Make sure that the DTB doesn't end up in the final
		 * kernel's .bss area. To do so, we adjust the decompressed
		 * kernel size to compensate if that .bss size is larger
		 * than the relocated code.
		 */
		ldr	r5, =_kernel_bss_size
		adr	r1, wont_overwrite
		sub	r1, r6, r1
		subs	r1, r5, r1
		addhi	r9, r9, r1

		/* Get the dtb's size */
		ldr	r5, [r6, #4]
#ifndef __ARMEB__
		/* convert r5 (dtb size) to little endian */
		eor	r1, r5, r5, ror #16
		bic	r1, r1, #0x00ff0000
		mov	r5, r5, ror #8
		eor	r5, r5, r1, lsr #8
#endif

		/* preserve 64-bit alignment */
		add	r5, r5, #7
		bic	r5, r5, #7

		/* relocate some pointers past the appended dtb */
		add	r6, r6, r5
		add	r10, r10, r5
		add	sp, sp, r5
dtb_check_done:
/*!
 * 최신 퀄컴 디바이스에서 사용한다 나중에 읽음.
 */
#endif

/*
 * Check to see if we will overwrite ourselves.
 *   r4  = final kernel address
 *   r9  = size of decompressed image
 *   r10 = end of this image, including  bss/stack/malloc space if non XIP
 * We basically want:
 *   r4 - 16k page directory >= r10 -> OK
 *   r4 + image length <= address of wont_overwrite -> OK
 */


/*!
 * 2013/06/22 여기까지  
 */
		add	r10, r10, #16384
		cmp	r4, r10
		bhs	wont_overwrite
		add	r10, r4, r9
		adr	r9, wont_overwrite
		cmp	r10, r9
		bls	wont_overwrite

/*
 * Relocate ourselves past the end of the decompressed kernel.
 *   r6  = _edata
 *   r10 = end of the decompressed kernel
 * Because we always copy ahead, we need to do it from the end and go
 * backward in case the source and destination overlap.
 */
		/*
		 * Bump to the next 256-byte boundary with the size of
		 * the relocation code added. This avoids overwriting
		 * ourself when the offset is small.
		 */
		add	r10, r10, #((reloc_code_end - restart + 256) & ~255)
		bic	r10, r10, #255

		/* Get start of code we want to copy and align it down. */
		adr	r5, restart
		bic	r5, r5, #31

/* Relocate the hyp vector base if necessary */
#ifdef CONFIG_ARM_VIRT_EXT
		mrs	r0, spsr
		and	r0, r0, #MODE_MASK
		cmp	r0, #HYP_MODE
		bne	1f

		bl	__hyp_get_vectors
		sub	r0, r0, r5
		add	r0, r0, r10
		bl	__hyp_set_vectors
1:
#endif

		sub	r9, r6, r5		@ size to copy
		add	r9, r9, #31		@ rounded up to a multiple
		bic	r9, r9, #31		@ ... of 32 bytes
		add	r6, r9, r5
		add	r9, r9, r10

1:		ldmdb	r6!, {r0 - r3, r10 - r12, lr}
		cmp	r6, r5
		stmdb	r9!, {r0 - r3, r10 - r12, lr}
		bhi	1b

		/* Preserve offset to relocated code. */
		sub	r6, r9, r6

#ifndef CONFIG_ZBOOT_ROM
		/* cache_clean_flush may use the stack, so relocate it */
		add	sp, sp, r6
#endif

		bl	cache_clean_flush

		adr	r0, BSYM(restart)
		add	r0, r0, r6
		mov	pc, r0

wont_overwrite:
/*
 * If delta is zero, we are running at the address we were linked at.
 *   r0  = delta
 *   r2  = BSS start
 *   r3  = BSS end
 *   r4  = kernel execution address
 *   r5  = appended dtb size (0 if not present)
 *   r7  = architecture ID
 *   r8  = atags pointer
 *   r11 = GOT start
 *   r12 = GOT end
 *   sp  = stack pointer
 */
		orrs	r1, r0, r5
		beq	not_relocated

		add	r11, r11, r0
		add	r12, r12, r0

#ifndef CONFIG_ZBOOT_ROM
		/*
		 * If we're running fully PIC === CONFIG_ZBOOT_ROM = n,
		 * we need to fix up pointers into the BSS region.
		 * Note that the stack pointer has already been fixed up.
		 */
		add	r2, r2, r0
		add	r3, r3, r0

		/*
		 * Relocate all entries in the GOT table.
		 * Bump bss entries to _edata + dtb size
		 */
1:		ldr	r1, [r11, #0]		@ relocate entries in the GOT
		add	r1, r1, r0		@ This fixes up C references
		cmp	r1, r2			@ if entry >= bss_start &&
		cmphs	r3, r1			@       bss_end > entry
		addhi	r1, r1, r5		@    entry += dtb size
		str	r1, [r11], #4		@ next entry
		cmp	r11, r12
		blo	1b

		/* bump our bss pointers too */
		add	r2, r2, r5
		add	r3, r3, r5

#else

		/*
		 * Relocate entries in the GOT table.  We only relocate
		 * the entries that are outside the (relocated) BSS region.
		 */
1:		ldr	r1, [r11, #0]		@ relocate entries in the GOT
		cmp	r1, r2			@ entry < bss_start ||
		cmphs	r3, r1			@ _end < entry
		addlo	r1, r1, r0		@ table.  This fixes up the
		str	r1, [r11], #4		@ C references.
		cmp	r11, r12
		blo	1b
#endif

not_relocated:	mov	r0, #0
1:		str	r0, [r2], #4		@ clear bss
		str	r0, [r2], #4
		str	r0, [r2], #4
		str	r0, [r2], #4
		cmp	r2, r3
		blo	1b

/*
 * The C runtime environment should now be setup sufficiently.
 * Set up some pointers, and start decompressing.
 *   r4  = kernel execution address
 *   r7  = architecture ID
 *   r8  = atags pointer
 */
		mov	r0, r4
		mov	r1, sp			@ malloc space above stack
		add	r2, sp, #0x10000	@ 64k max
		mov	r3, r7
		bl	decompress_kernel
		bl	cache_clean_flush
		bl	cache_off
		mov	r1, r7			@ restore architecture number
		mov	r2, r8			@ restore atags pointer

#ifdef CONFIG_ARM_VIRT_EXT
		mrs	r0, spsr		@ Get saved CPU boot mode
		and	r0, r0, #MODE_MASK
		cmp	r0, #HYP_MODE		@ if not booted in HYP mode...
		bne	__enter_kernel		@ boot kernel directly

		adr	r12, .L__hyp_reentry_vectors_offset
		ldr	r0, [r12]
		add	r0, r0, r12

		bl	__hyp_set_vectors
		__HVC(0)			@ otherwise bounce to hyp mode

		b	.			@ should never be reached

		.align	2
.L__hyp_reentry_vectors_offset:	.long	__hyp_reentry_vectors - .
#else
		b	__enter_kernel
#endif

		.align	2
		.type	LC0, #object
LC0:		.word	LC0			@ r1
		.word	__bss_start		@ r2
		.word	_end			@ r3
		.word	_edata			@ r6
		.word	input_data_end - 4	@ r10 (inflated size location)
		.word	_got_start		@ r11
		.word	_got_end		@ ip
		.word	.L_user_stack_end	@ sp
		.size	LC0, . - LC0

#ifdef CONFIG_ARCH_RPC
		.globl	params
params:		ldr	r0, =0x10000100		@ params_phys for RPC
		mov	pc, lr
		.ltorg
		.align
#endif

/*
 * Turn on the cache.  We need to setup some page tables so that we
 * can have both the I and D caches on.
 *
 * We place the page tables 16k down from the kernel execution address,
 * and we hope that nothing else is using it.  If we're using it, we
 * will go pop!
 *
 * On entry,
 *  r4 = kernel execution address
 *  r7 = architecture number
 *  r8 = atags pointer
 * On exit,
 *  r0, r1, r2, r3, r9, r10, r12 corrupted
 * This routine must preserve:
 *  r4, r7, r8
 */
		.align	5
cache_on:	mov	r3, #8			@ cache_on function
               /*! 
                 * call_cache_fn 을 실행하기 전에 r3에 8 을 저장한다.
                 */
		b	call_cache_fn

/*
 * Initialize the highest priority protection region, PR7
 * to cover all 32bit address and cacheable and bufferable.
 */
__armv4_mpu_cache_on:
		mov	r0, #0x3f		@ 4G, the whole
		mcr	p15, 0, r0, c6, c7, 0	@ PR7 Area Setting
		mcr 	p15, 0, r0, c6, c7, 1

		mov	r0, #0x80		@ PR7
		mcr	p15, 0, r0, c2, c0, 0	@ D-cache on
		mcr	p15, 0, r0, c2, c0, 1	@ I-cache on
		mcr	p15, 0, r0, c3, c0, 0	@ write-buffer on

		mov	r0, #0xc000
		mcr	p15, 0, r0, c5, c0, 1	@ I-access permission
		mcr	p15, 0, r0, c5, c0, 0	@ D-access permission

		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4	@ drain write buffer
		mcr	p15, 0, r0, c7, c5, 0	@ flush(inval) I-Cache
		mcr	p15, 0, r0, c7, c6, 0	@ flush(inval) D-Cache mrc	p15, 0, r0, c1, c0, 0	@ read control reg @ ...I .... ..D. WC.M orr	r0, r0, #0x002d		@ .... .... ..1. 11.1 orr	r0, r0, #0x1000		@ ...1 .... .... ....  mcr	p15, 0, r0, c1, c0, 0	@ write control reg mov	r0, #0 mcr	p15, 0, r0, c7, c5, 0	@ flush(inval) I-Cache mcr	p15, 0, r0, c7, c6, 0	@ flush(inval) D-Cache mov	pc, lr __armv3_mpu_cache_on: mov	r0, #0x3f		@ 4G, the whole
		mcr	p15, 0, r0, c6, c7, 0	@ PR7 Area Setting

		mov	r0, #0x80		@ PR7
		mcr	p15, 0, r0, c2, c0, 0	@ cache on
		mcr	p15, 0, r0, c3, c0, 0	@ write-buffer on

		mov	r0, #0xc000
		mcr	p15, 0, r0, c5, c0, 0	@ access permission

		mov	r0, #0
		mcr	p15, 0, r0, c7, c0, 0	@ invalidate whole cache v3
		/*
		 * ?? ARMv3 MMU does not allow reading the control register,
		 * does this really work on ARMv3 MPU?
		 */
		mrc	p15, 0, r0, c1, c0, 0	@ read control reg
						@ .... .... .... WC.M
		orr	r0, r0, #0x000d		@ .... .... .... 11.1
		/* ?? this overwrites the value constructed above? */
		mov	r0, #0
		mcr	p15, 0, r0, c1, c0, 0	@ write control reg

		/* ?? invalidate for the second time? */
		mcr	p15, 0, r0, c7, c0, 0	@ invalidate whole cache v3
		mov	pc, lr

#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
#define CB_BITS 0x08
#else
#define CB_BITS 0x0c
#endif

                /*!
                 * 
                   r4 = 커널 실제 실행할 주소 
                     압축을 풀어서 커널이미지를 올릴 주소
                     0x40008000
                */
__setup_mmu:	sub	r3, r4, #16384		@ Page directory size
		bic	r3, r3, #0xff		@ Align the pointer
		bic	r3, r3, #0x3f00

                /*!
                 * 16KB단위로 Align 하기 위하여 bic 를 수행한다.
                 */
/*
 * Initialise the page tables, turning on the cacheable and bufferable
 * bits for the RAM area only.
 */
		mov	r0, r3
		mov 	r9, r0, lsr #18
		mov	r9, r9, lsl #18		@ start of RAM
                /*!
                 * r9 하위 18비트를 클리어 하여 준다.
                 물리 메모리의 시작을 알려주기 위해서.
                    256MB단위로 만들어준다
                    정렬 단위가 256MB 일듯하다.
                    r9 = RAM start
                 */
		add	r10, r9, #0x10000000	@ a reasonable RAM size
                
                /*!
                 * r10 = end of ram 
                 * 256MB의 RAM 
                 */
		mov	r1, #0x12		@ XN|U + section mapping
		orr	r1, r1, #3 << 10	@ AP=11
                
                /* !
                *
                *   r1 = 0x12 or 1100 0000 0000 
                *  r1 = 0xC12
                *   AP = data access permission
                *   AP = 11 -> full access
                   11 12 번 비트가 AP비트를 설정하는 곳이다.
                */

                /*! sub	r3, r4, #16384		@ Page directory size
                */

		add	r2, r3, #16384 @0x4000
                // r2 = r4 와 동일하다 

1:		cmp	r1, r9			@ if virt > start of RAM
		cmphs	r10, r1			@   && end of RAM > virt
		bic	r1, r1, #0x1c		@ clear XN|U + C + B
                /*!
                  cache, buffer bit clear
                 */
		orrlo	r1, r1, #0x10		@ Set XN|U for non-RAM
                    
                /*!
                 * XN bit set 
                 XN = 1이면은 실행 불가 
                 */
		orrhs	r1, r1, r6		@ set RAM section settings

                /*!
                 * r6 은 0xC 
                 cache , buffer bit set 
                 */
		str	r1, [r0], #4		@ 1:1 mapping
                /*!
                 * r1 값을 r0 넣고 증가 
                     r0 가 가르키는 메모리 주소는 page table start address
                     엔트리 4byte를 계속하여 셋팅
                 */
		add	r1, r1, #1048576
		teq	r0, r2
                /*!
                 * r0 = page table start address
                   r2 = kernel load address
                 */
		bne	1b
/*
 * If ever we are running from Flash, then we surely want the cache
 * to be enabled also for our execution instance...  We map 2MB of it
 * so there is no map overlap problem for up to 1 MB compressed kernel.
 * If the execution is in RAM then we would only be duplicating the above.
 */
		orr	r1, r6, #0x04		@ ensure B is set for this
                /*!
                 * r6 = 0xC, r1 = 0x08
                 */
		orr	r1, r1, #3 << 10
                /*!
                 * r1 에 ap = 11 full access
                   and B  bit set
                 */
		mov	r2, pc
		mov	r2, r2, lsr #20

                /*!
                 * 속성 비트를 전부다 없애버린다. 
                 1M 단위로 셋팅하기 위해서 .
                 pc의 페이지 테이블 엔트리를 구하는 것.
                 r2는 실제적인 물리 주소
                 r0 가 인덱스 값.
                 r1 합성된 주소 .
                 속성 영역을 없애고 위에 주소 값들만을 남겨 놓는다 .
                 section base address만 남겨놓는다.
                 */
		orr	r1, r1, r2, lsl #20
                /*!
                 * r1 이 속성값을 지니고 있고
                  r2 section base address
                 */
		add	r0, r3, r2, lsl #2
                /*!
                 * 엔트리의 주소값을 구하기 위해서.
                 */
		str	r1, [r0], #4
		add	r1, r1, #1048576
		str	r1, [r0]
                /*!
                 * 1M를 2번하여 2M의 속성을 정하여 준다..
                 */
		mov	pc, lr
ENDPROC(__setup_mmu)

@ Enable unaligned access on v6, to allow better code generation
@ fo    r the decompressor C code:
__armv6_mmu_cache_on:
		mrc	p15, 0, r0, c1, c0, 0	@ read SCTLR
		bic	r0, r0, #2		@ A (no unaligned access fault)
		orr	r0, r0, #1 << 22	@ U (v6 unaligned access model)
		mcr	p15, 0, r0, c1, c0, 0	@ write SCTLR
		b	__armv4_mmu_cache_on

__arm926ejs_mmu_cache_on:
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
		mov	r0, #4			@ put dcache in WT mode
		mcr	p15, 7, r0, c15, c0, 0
#endif
    
__armv4_mmu_cache_on:
		mov	r12, lr
#ifdef CONFIG_MMU
		mov	r6, #CB_BITS | 0x12	@ U
		bl	__setup_mmu
		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4	@ drain write buffer
		mcr	p15, 0, r0, c8, c7, 0	@ flush I,D TLBs
		mrc	p15, 0, r0, c1, c0, 0	@ read control reg
		orr	r0, r0, #0x5000		@ I-cache enable, RR cache replacement
		orr	r0, r0, #0x0030
#ifdef CONFIG_CPU_ENDIAN_BE8
		orr	r0, r0, #1 << 25	@ big-endian page tables
#endif
		bl	__common_mmu_cache_on
		mov	r0, #0
		mcr	p15, 0, r0, c8, c7, 0	@ flush I,D TLBs
#endif
		mov	pc, r12

                /*!
                  
                 */
         
__armv7_mmu_cache_on:
                
                /*!
                  cache를 켜기 위해 들어왔다. 2013/06/15 스터디는 여기까지!
                  */
                /*
                   2013/06/22 일 스터디 시작하는 부분
                   */
		mov	r12, lr
#ifdef CONFIG_MMU 
		mrc	p15, 0, r11, c0, c1, 4	@ read ID_MMFR0
		tst	r11, #0xf		@ VMSA 
                /*!
                 * VMSA 버츄얼 메모리 시스템 아키텍쳐 0번비트에서 3번비트까지.
                r11 = 0x5 = 0101 and 1111 
                z bit not setup 
                 */
		movne	r6, #CB_BITS | 0x02	@ !XN
		blne	__setup_mmu
                /*!
                  __setup_mmu
                 * 커널 압축 해제에 필요한 페이지 디렉터리 엔트리를 초기화 하여준다.
                 * 메모리의 256MB영역에 대해서는 cacheable,bufferable로 설정해준다.
                 */
		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4	@ drain write buffer
                /*!
                 * c7 의 c10 , 4 = Data Synchronization Barrier operation
                  ??않써진 데이터를 쓰라고 하는것 같다??
                 */
		tst	r11, #0xf		@ VMSA
		mcrne	p15, 0, r0, c8, c7, 0	@ flush I,D TLBs
                /*!
                 * c8 c7 0 = Invalidate unified TLB
                 TLB를 전체를 flush 하여 준다 .
                 */
#endif
		mrc	p15, 0, r0, c1, c0, 0	@ read control reg
                /*!
                 *   c1 c0 0 =  SCTLR    0x00C50078[a]   
                               System Control Register
                 */
		bic	r0, r0, #1 << 28	@ clear SCTLR.TRE
                /*!
                 * TEX remap enabled 하여준다 . TEX??
                 C비트와 B비트를 같이 쓰여서 속성을 특정 메모리 영역에 대한 access속성을 지정하는것.
                 */
		orr	r0, r0, #0x5000		@ I-cache enable, RR cache replacement

                /*!
                 * I비트와 RR 비트  set되고  
                   I비트는 Instruction cache enable
                   RR비트 1이 되면  Predictable strategy, for example, round-robin replacement
                 */
		orr	r0, r0, #0x003c		@ write buffer
                /*!
                 * C비트와 CP15BEN 을  set 
                   C비트는 cache enable
                   CP15BEN비트는  CP15 barrier operations enabled.

                 */
		bic	r0, r0, #2		@ A (no unaligned access fault)
                 /*!
                *  2번 A비트를 clear 한다 
                   Alignment fault checking disabled
                 */
		orr	r0, r0, #1 << 22	@ U (v6 unaligned access model)
                /*!
                *  U bit set In ARMv7 this bit is RAO/SBOP, 
                */
						@ (needed for ARM1176)
#ifdef CONFIG_MMU
#ifdef CONFIG_CPU_ENDIAN_BE8
		orr	r0, r0, #1 << 25	@ big-endian page tables
#endif
		mrcne   p15, 0, r6, c2, c0, 2   @ read ttb control reg
                /*!
                 * c2 , c0 , 2  Translation Table Base Control Register
                 */
                orrne	r0, r0, #1		@ MMU enabled
		movne	r1, #0xfffffffd		@ domain 0 = client
		bic     r6, r6, #1 << 31        @ 32-bit translation system
                /*!
                 * EAE bit clear 
                  EAE bit 0 
                  Use the 32-bit translation system, with the Short-descriptor translation table format. In
                  this case, the format of the TTBCR is as described in this section.
                 */
		bic     r6, r6, #3 << 0         @ use only ttbr0
                /*!
                 * N bit 0 
                   When N has its reset value of 0, the translation table
                   base is compatible with ARMv5 and ARMv6
                 */
		mcrne	p15, 0, r3, c2, c0, 0	@ load page table pointer
                /*!
                 * r3 = TTBR0
                 */
		mcrne	p15, 0, r1, c3, c0, 0	@ load domain access control
                /*!
                 * r1 = Domain Access Control Register (DACR)
                 */
		mcrne   p15, 0, r6, c2, c0, 2   @ load ttb control
                /*!
                 * r6 = TTBCR
                 page table base address 가 들어간다 .
                 */
#endif
		mcr	p15, 0, r0, c7, c5, 4	@ ISB
                /*!
                 * C7 C5 4 = Instruction Synchronization Barrier operation
                 Barrier가 없으면은 CPU가 이아이들의 값이 써지기 전에 코프로세서로 값을 넘기게 되면은.
                 문제가 발생?
                 읽고나서 써야되는데 그러지 않으면은 순서가 엉켜 버릴수가 있다.
                 */
		mcr	p15, 0, r0, c1, c0, 0	@ load control register
                /*!
                 * r0 = SCR (System Control Register)
                 */
		mrc	p15, 0, r0, c1, c0, 0	@ and read it back
                /*!
                 * 일부로 의존성을 만들어 서 다른부분이 건들이지 못하게 
                 쓰는것을 보장하여 준다.
                 */
		mov	r0, #0
                /*!
                 * R0 의 값을 0으로 
                 */
		mcr	p15, 0, r0, c7, c5, 4	@ ISB
		mov	pc, r12

__fa526_cache_on:
		mov	r12, lr
		mov	r6, #CB_BITS | 0x12	@ U
		bl	__setup_mmu
		mov	r0, #0
		mcr	p15, 0, r0, c7, c7, 0	@ Invalidate whole cache
		mcr	p15, 0, r0, c7, c10, 4	@ drain write buffer
		mcr	p15, 0, r0, c8, c7, 0	@ flush UTLB
		mrc	p15, 0, r0, c1, c0, 0	@ read control reg
		orr	r0, r0, #0x1000		@ I-cache enable
		bl	__common_mmu_cache_on
		mov	r0, #0
		mcr	p15, 0, r0, c8, c7, 0	@ flush UTLB
		mov	pc, r12

__common_mmu_cache_on:
#ifndef CONFIG_THUMB2_KERNEL
#ifndef DEBUG
		orr	r0, r0, #0x000d		@ Write buffer, mmu
#endif
		mov	r1, #-1
		mcr	p15, 0, r3, c2, c0, 0	@ load page table pointer
		mcr	p15, 0, r1, c3, c0, 0	@ load domain access control
		b	1f
		.align	5			@ cache line aligned
1:		mcr	p15, 0, r0, c1, c0, 0	@ load control register
		mrc	p15, 0, r0, c1, c0, 0	@ and read it back to
		sub	pc, lr, r0, lsr #32	@ properly flush pipeline
#endif

#define PROC_ENTRY_SIZE (4*5)

/*
 * Here follow the relocatable cache support functions for the
 * various processors.  This is a generic hook for locating an
 * entry and jumping to an instruction at the specified offset
 * from the start of the block.  Please note this is all position
 * independent code.
 *
 *  r1  = corrupted
 *  r2  = corrupted
 *  r3  = block offset
 *  r9  = corrupted
 *  r12 = corrupted
 */

call_cache_fn:	adr	r12, proc_types
/*!
 * proc_types의 pc상대주소를 r12에 넣는다.
 */
#ifdef CONFIG_CPU_CP15
/*!
 * CONFIG_CPU_CP15 = yes 로 정의되어 있으므로 이곳이 실행된다.
 */
		mrc	p15, 0, r9, c0, c0	@ get processor ID
                /*!
                 * cp15의 c0 레지스터 값을 r9에 저장
                 */
#else
		ldr	r9, =CONFIG_PROCESSOR_ID
#endif
1:		ldr	r1, [r12, #0]		@ get value
		ldr	r2, [r12, #4]		@ get mask
                /*!
                 * proc_types: 에서
		            .word	0x000f0000		@ new CPU Id
		            .word	0x000f0000
                    이므로 r1 = 0x000f0000
                           r2 = 0x000f0000
                 */
		eor	r1, r1, r9		@ (real ^ match)
		tst	r1, r2			@       & mask
    /*!
     * r9와 r1을 exclusive or 연산한 후 r2와 비교하는 것을 반복해서 자신의 아키텍처를 찾는 것
     */
 ARM(		addeq	pc, r12, r3		) @ call cache function
 /*!
  * call_cache_fn:을 시작하기 전에 저장해둔 값. r3 = 8.
  */
 THUMB(		addeq	r12, r3			)
 THUMB(		moveq	pc, r12			) @ call cache function
		add	r12, r12, #PROC_ENTRY_SIZE
		b	1b
                /*!
                 * 비교한 아키텍처가 현재 아키텍처와 다른 경우 PROC_ENTRY_SIZE 만큼 더한 후,
                 * 1: 레이블로 이동하여 다음 아키텍처를 찾는다.
                 */

/*
 * Table for cache operations.  This is basically:
 *   - CPU ID match
 *   - CPU ID mask
 *   - 'cache on' method instruction
 *   - 'cache off' method instruction
 *   - 'cache flush' method instruction
 *
 * We match an entry using: ((real_id ^ match) & mask) == 0
 *
 * Writethrough caches generally only need 'on' and 'off'
 * methods.  Writeback caches _must_ have the flush method
 * defined.
 */
		.align	2
		.type	proc_types,#object
proc_types:
		.word	0x41000000		@ old ARM ID
		.word	0xff00f000
		mov	pc, lr
 THUMB(		nop				)
		mov	pc, lr
 THUMB(		nop				)
		mov	pc, lr
 THUMB(		nop				)

		.word	0x41007000		@ ARM7/710
		.word	0xfff8fe00
		mov	pc, lr
 THUMB(		nop				)
		mov	pc, lr
 THUMB(		nop				)
		mov	pc, lr
 THUMB(		nop				)

		.word	0x41807200		@ ARM720T (writethrough)
		.word	0xffffff00
		W(b)	__armv4_mmu_cache_on
		W(b)	__armv4_mmu_cache_off
		mov	pc, lr
 THUMB(		nop				)

		.word	0x41007400		@ ARM74x
		.word	0xff00ff00
		W(b)	__armv3_mpu_cache_on
		W(b)	__armv3_mpu_cache_off
		W(b)	__armv3_mpu_cache_flush
		
		.word	0x41009400		@ ARM94x
		.word	0xff00ff00
		W(b)	__armv4_mpu_cache_on
		W(b)	__armv4_mpu_cache_off
		W(b)	__armv4_mpu_cache_flush

		.word	0x41069260		@ ARM926EJ-S (v5TEJ)
		.word	0xff0ffff0
		W(b)	__arm926ejs_mmu_cache_on
		W(b)	__armv4_mmu_cache_off
		W(b)	__armv5tej_mmu_cache_flush

		.word	0x00007000		@ ARM7 IDs
		.word	0x0000f000
		mov	pc, lr
 THUMB(		nop				)
		mov	pc, lr
 THUMB(		nop				)
		mov	pc, lr
 THUMB(		nop				)

		@ Everything from here on will be the new ID system.

		.word	0x4401a100		@ sa110 / sa1100
		.word	0xffffffe0
		W(b)	__armv4_mmu_cache_on
		W(b)	__armv4_mmu_cache_off
		W(b)	__armv4_mmu_cache_flush

		.word	0x6901b110		@ sa1110
		.word	0xfffffff0
		W(b)	__armv4_mmu_cache_on
		W(b)	__armv4_mmu_cache_off
		W(b)	__armv4_mmu_cache_flush

		.word	0x56056900
		.word	0xffffff00		@ PXA9xx
		W(b)	__armv4_mmu_cache_on
		W(b)	__armv4_mmu_cache_off
		W(b)	__armv4_mmu_cache_flush

		.word	0x56158000		@ PXA168
		.word	0xfffff000
		W(b)	__armv4_mmu_cache_on
		W(b)	__armv4_mmu_cache_off
		W(b)	__armv5tej_mmu_cache_flush

		.word	0x56050000		@ Feroceon
		.word	0xff0f0000
		W(b)	__armv4_mmu_cache_on
		W(b)	__armv4_mmu_cache_off
		W(b)	__armv5tej_mmu_cache_flush

#ifdef CONFIG_CPU_FEROCEON_OLD_ID
		/* this conflicts with the standard ARMv5TE entry */
		.long	0x41009260		@ Old Feroceon
		.long	0xff00fff0
		b	__armv4_mmu_cache_on
		b	__armv4_mmu_cache_off
		b	__armv5tej_mmu_cache_flush
#endif

		.word	0x66015261		@ FA526
		.word	0xff01fff1
		W(b)	__fa526_cache_on
		W(b)	__armv4_mmu_cache_off
		W(b)	__fa526_cache_flush

		@ These match on the architecture ID

		.word	0x00020000		@ ARMv4T
		.word	0x000f0000
		W(b)	__armv4_mmu_cache_on
		W(b)	__armv4_mmu_cache_off
		W(b)	__armv4_mmu_cache_flush

		.word	0x00050000		@ ARMv5TE
		.word	0x000f0000
		W(b)	__armv4_mmu_cache_on
		W(b)	__armv4_mmu_cache_off
		W(b)	__armv4_mmu_cache_flush

		.word	0x00060000		@ ARMv5TEJ
		.word	0x000f0000
		W(b)	__armv4_mmu_cache_on
		W(b)	__armv4_mmu_cache_off
		W(b)	__armv5tej_mmu_cache_flush

		.word	0x0007b000		@ ARMv6
		.word	0x000ff000
		W(b)	__armv6_mmu_cache_on
		W(b)	__armv4_mmu_cache_off
		W(b)	__armv6_mmu_cache_flush

		.word	0x000f0000		@ new CPU Id
                /*!
                 * r12가 가리키는 부분
                 * r1에 셋팅되는 값
                 */
		.word	0x000f0000
                /*!
                 * r2에 셋팅되는 값
                 */
		W(b)	__armv7_mmu_cache_on
		W(b)	__armv7_mmu_cache_off
		W(b)	__armv7_mmu_cache_flush

		.word	0			@ unrecognised type
		.word	0
		mov	pc, lr
 THUMB(		nop				)
		mov	pc, lr
 THUMB(		nop				)
		mov	pc, lr
 THUMB(		nop				)

		.size	proc_types, . - pro0으로 es

		/*
		 * If you get a "non-constant expression in ".if" statement"
		 * error from the assembler on this line, check that you have
		 * not accidentally written a "b" instruction where you should
		 * have written W(b).
		 */
		.if (. - proc_types) % PROC_ENTRY_SIZE != 0
		.error "The size of one or more proc_types entries is wrong."
		.endif

/*
 * Turn off the Cache and MMU.  ARMv3 does not support
 * reading the control register, but ARMv4 does.
 *
 * On exit,
 *  r0, r1, r2, r3, r9, r12 corrupted
 * This routine must preserve:
 *  r4, r7, r8
 */
		.align	5
cache_off:	mov	r3, #12			@ cache_off function
		b	call_cache_fn

__armv4_mpu_cache_off:
		mrc	p15, 0, r0, c1, c0
		bic	r0, r0, #0x000d
		mcr	p15, 0, r0, c1, c0	@ turn MPU and cache off
		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4	@ drain write buffer
		mcr	p15, 0, r0, c7, c6, 0	@ flush D-Cache
		mcr	p15, 0, r0, c7, c5, 0	@ flush I-Cache
		mov	pc, lr

__armv3_mpu_cache_off:
		mrc	p15, 0, r0, c1, c0
		bic	r0, r0, #0x000d
		mcr	p15, 0, r0, c1, c0, 0	@ turn MPU and cache off
		mov	r0, #0
		mcr	p15, 0, r0, c7, c0, 0	@ invalidate whole cache v3
		mov	pc, lr

__armv4_mmu_cache_off:
#ifdef CONFIG_MMU
		mrc	p15, 0, r0, c1, c0
		bic	r0, r0, #0x000d
		mcr	p15, 0, r0, c1, c0	@ turn MMU and cache off
		mov	r0, #0
		mcr	p15, 0, r0, c7, c7	@ invalidate whole cache v4
		mcr	p15, 0, r0, c8, c7	@ invalidate whole TLB v4
#endif
		mov	pc, lr

__armv7_mmu_cache_off:
		mrc	p15, 0, r0, c1, c0
#ifdef CONFIG_MMU
		bic	r0, r0, #0x000d
#else
		bic	r0, r0, #0x000c
#endif
		mcr	p15, 0, r0, c1, c0	@ turn MMU and cache off
		mov	r12, lr
		bl	__armv7_mmu_cache_flush
		mov	r0, #0
#ifdef CONFIG_MMU
		mcr	p15, 0, r0, c8, c7, 0	@ invalidate whole TLB
#endif
		mcr	p15, 0, r0, c7, c5, 6	@ invalidate BTC
		mcr	p15, 0, r0, c7, c10, 4	@ DSB
		mcr	p15, 0, r0, c7, c5, 4	@ ISB
		mov	pc, r12

/*
 * Clean and flush the cache to maintain consistency.
 *
 * On exit,
 *  r1, r2, r3, r9, r10, r11, r12 corrupted
 * This routine must preserve:
 *  r4, r6, r7, r8
 */
		.align	5
cache_clean_flush:
		mov	r3, #16
		b	call_cache_fn

__armv4_mpu_cache_flush:
		mov	r2, #1
		mov	r3, #0
		mcr	p15, 0, ip, c7, c6, 0	@ invalidate D cache
		mov	r1, #7 << 5		@ 8 segments
1:		orr	r3, r1, #63 << 26	@ 64 entries
2:		mcr	p15, 0, r3, c7, c14, 2	@ clean & invalidate D index
		subs	r3, r3, #1 << 26
		bcs	2b			@ entries 63 to 0
		subs 	r1, r1, #1 << 5
		bcs	1b			@ segments 7 to 0

		teq	r2, #0
		mcrne	p15, 0, ip, c7, c5, 0	@ invalidate I cache
		mcr	p15, 0, ip, c7, c10, 4	@ drain WB
		mov	pc, lr
		
__fa526_cache_flush:
		mov	r1, #0
		mcr	p15, 0, r1, c7, c14, 0	@ clean and invalidate D cache
		mcr	p15, 0, r1, c7, c5, 0	@ flush I cache
		mcr	p15, 0, r1, c7, c10, 4	@ drain WB
		mov	pc, lr

__armv6_mmu_cache_flush:
		mov	r1, #0
		mcr	p15, 0, r1, c7, c14, 0	@ clean+invalidate D
		mcr	p15, 0, r1, c7, c5, 0	@ invalidate I+BTB
		mcr	p15, 0, r1, c7, c15, 0	@ clean+invalidate unified
		mcr	p15, 0, r1, c7, c10, 4	@ drain WB
		mov	pc, lr

__armv7_mmu_cache_flush:
		mrc	p15, 0, r10, c0, c1, 5	@ read ID_MMFR1
		tst	r10, #0xf << 16		@ hierarchical cache (ARMv7)
		mov	r10, #0
		beq	hierarchical
		mcr	p15, 0, r10, c7, c14, 0	@ clean+invalidate D
		b	iflush
hierarchical:
		mcr	p15, 0, r10, c7, c10, 5	@ DMB
		stmfd	sp!, {r0-r7, r9-r11}
		mrc	p15, 1, r0, c0, c0, 1	@ read clidr
		ands	r3, r0, #0x7000000	@ extract loc from clidr
		mov	r3, r3, lsr #23		@ left align loc bit field
		beq	finished		@ if loc is 0, then no need to clean
		mov	r10, #0			@ start clean at cache level 0
loop1:
		add	r2, r10, r10, lsr #1	@ work out 3x current cache level
		mov	r1, r0, lsr r2		@ extract cache type bits from clidr
		and	r1, r1, #7		@ mask of the bits for current cache only
		cmp	r1, #2			@ see what cache we have at this level
		blt	skip			@ skip if no cache, or just i-cache
		mcr	p15, 2, r10, c0, c0, 0	@ select current cache level in cssr
		mcr	p15, 0, r10, c7, c5, 4	@ isb to sych the new cssr&csidr
		mrc	p15, 1, r1, c0, c0, 0	@ read the new csidr
		and	r2, r1, #7		@ extract the length of the cache lines
		add	r2, r2, #4		@ add 4 (line length offset)
		ldr	r4, =0x3ff
		ands	r4, r4, r1, lsr #3	@ find maximum number on the way size
		clz	r5, r4			@ find bit position of way size increment
		ldr	r7, =0x7fff
		ands	r7, r7, r1, lsr #13	@ extract max number of the index size
loop2:
		mov	r9, r4			@ create working copy of max way size
loop3:
 ARM(		orr	r11, r10, r9, lsl r5	) @ factor way and cache number into r11
 ARM(		orr	r11, r11, r7, lsl r2	) @ factor index number into r11
 THUMB(		lsl	r6, r9, r5		)
 THUMB(		orr	r11, r10, r6		) @ factor way and cache number into r11
 THUMB(		lsl	r6, r7, r2		)
 THUMB(		orr	r11, r11, r6		) @ factor index number into r11
		mcr	p15, 0, r11, c7, c14, 2	@ clean & invalidate by set/way
		subs	r9, r9, #1		@ decrement the way
		bge	loop3
		subs	r7, r7, #1		@ decrement the index
		bge	loop2
skip:
		add	r10, r10, #2		@ increment cache number
		cmp	r3, r10
		bgt	loop1
finished:
		ldmfd	sp!, {r0-r7, r9-r11}
		mov	r10, #0			@ swith back to cache level 0
		mcr	p15, 2, r10, c0, c0, 0	@ select current cache level in cssr
iflush:
		mcr	p15, 0, r10, c7, c10, 4	@ DSB
		mcr	p15, 0, r10, c7, c5, 0	@ invalidate I+BTB
		mcr	p15, 0, r10, c7, c10, 4	@ DSB
		mcr	p15, 0, r10, c7, c5, 4	@ ISB
		mov	pc, lr

__armv5tej_mmu_cache_flush:
1:		mrc	p15, 0, r15, c7, c14, 3	@ test,clean,invalidate D cache
		bne	1b
		mcr	p15, 0, r0, c7, c5, 0	@ flush I cache
		mcr	p15, 0, r0, c7, c10, 4	@ drain WB
		mov	pc, lr

__armv4_mmu_cache_flush:
		mov	r2, #64*1024		@ default: 32K dcache size (*2)
		mov	r11, #32		@ default: 32 byte line size
		mrc	p15, 0, r3, c0, c0, 1	@ read cache type
		teq	r3, r9			@ cache ID register present?
		beq	no_cache_id
		mov	r1, r3, lsr #18
		and	r1, r1, #7
		mov	r2, #1024
		mov	r2, r2, lsl r1		@ base dcache size *2
		tst	r3, #1 << 14		@ test M bit
		addne	r2, r2, r2, lsr #1	@ +1/2 size if M == 1
		mov	r3, r3, lsr #12
		and	r3, r3, #3
		mov	r11, #8
		mov	r11, r11, lsl r3	@ cache line size in bytes
no_cache_id:
		mov	r1, pc
		bic	r1, r1, #63		@ align to longest cache line
		add	r2, r1, r2
1:
 ARM(		ldr	r3, [r1], r11		) @ s/w flush D cache
 THUMB(		ldr     r3, [r1]		) @ s/w flush D cache
 THUMB(		add     r1, r1, r11		)
		teq	r1, r2
		bne	1b

		mcr	p15, 0, r1, c7, c5, 0	@ flush I cache
		mcr	p15, 0, r1, c7, c6, 0	@ flush D cache
		mcr	p15, 0, r1, c7, c10, 4	@ drain WB
		mov	pc, lr

__armv3_mmu_cache_flush:
__armv3_mpu_cache_flush:
		mov	r1, #0
		mcr	p15, 0, r1, c7, c0, 0	@ invalidate whole cache v3
		mov	pc, lr

/*
 * Various debugging routines for printing hex characters and
 * memory, which again must be relocatable.
 */
#ifdef DEBUG
		.align	2
		.type	phexbuf,#object
phexbuf:	.space	12
		.size	phexbuf, . - phexbuf

@ phex corrupts {r0, r1, r2, r3}
phex:		adr	r3, phexbuf
		mov	r2, #0
		strb	r2, [r3, r1]
1:		subs	r1, r1, #1
		movmi	r0, r3
		bmi	puts
		and	r2, r0, #15
		mov	r0, r0, lsr #4
		cmp	r2, #10
		addge	r2, r2, #7
		add	r2, r2, #'0'
		strb	r2, [r3, r1]
		b	1b

@ puts corrupts {r0, r1, r2, r3}
puts:		loadsp	r3, r1
1:		ldrb	r2, [r0], #1
		teq	r2, #0
		moveq	pc, lr
2:		writeb	r2, r3
		mov	r1, #0x00020000
3:		subs	r1, r1, #1
		bne	3b
		teq	r2, #'\n'
		moveq	r2, #'\r'
		beq	2b
		teq	r0, #0
		bne	1b
		mov	pc, lr
@ putc corrupts {r0, r1, r2, r3}
putc:
		mov	r2, r0
		mov	r0, #0
		loadsp	r3, r1
		b	2b

@ memdump corrupts {r0, r1, r2, r3, r10, r11, r12, lr}
memdump:	mov	r12, r0
		mov	r10, lr
		mov	r11, #0
2:		mov	r0, r11, lsl #2
		add	r0, r0, r12
		mov	r1, #8
		bl	phex
		mov	r0, #':'
		bl	putc
1:		mov	r0, #' '
		bl	putc
		ldr	r0, [r12, r11, lsl #2]
		mov	r1, #8
		bl	phex
		and	r0, r11, #7
		teq	r0, #3
		moveq	r0, #' '
		bleq	putc
		and	r0, r11, #7
		add	r11, r11, #1
		teq	r0, #7
		bne	1b
		mov	r0, #'\n'
		bl	putc
		cmp	r11, #64
		blt	2b
		mov	pc, r10
#endif

		.ltorg

#ifdef CONFIG_ARM_VIRT_EXT
.align 5
__hyp_reentry_vectors:
		W(b)	.			@ reset
		W(b)	.			@ undef
		W(b)	.			@ svc
		W(b)	.			@ pabort
		W(b)	.			@ dabort
		W(b)	__enter_kernel		@ hyp
		W(b)	.			@ irq
		W(b)	.			@ fiq
#endif /* CONFIG_ARM_VIRT_EXT */

__enter_kernel:
		mov	r0, #0			@ must be 0
 ARM(		mov	pc, r4	)		@ call kernel
 THUMB(		bx	r4	)		@ entry point is always ARM

reloc_code_end:

		.align
		.section ".stack", "aw", %nobits
.L_user_stack:	.space	4096
.L_user_stack_end:
