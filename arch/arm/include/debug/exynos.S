/*
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* pull in the relevant register and map files. */

#define S3C_ADDR_BASE   0xF6000000
#define S3C_VA_UART	S3C_ADDR_BASE + 0x01000000
#define EXYNOS4_PA_UART	0x13800000
#define EXYNOS5_PA_UART	0x12C00000

	/* note, for the boot process to work we have to keep the UART
	 * virtual address aligned to an 1MiB boundary for the L1
	 * mapping the head code makes. We keep the UART virtual address
	 * aligned and add in the offset when we load the value here.
	 */

	.macro addruart, rp, rv, tmp
            /*! addruart라는 매크로를 정의함, rp, rv, tmp는 argument */
            /*! head.S의 호출addruart는 argument 두개인데 여기는 3개: 나중에 확인하기로 함 */
		mrc	p15, 0, \tmp, c0, c0, 0
            /*!  코프로세스 c0(ID레지스터) 값을 tmp로 읽어옴 */
            /*! \는 어셈블리 사용할 때 편하게 쓰기 위한 것. argument를 사용한다는 의미 */
		and	\tmp, \tmp, #0xf0
            /*! core버전을 알기 위해 4~7번 bit만 남기고 나머지 clear */
		teq	\tmp, #0xf0		@@ A15
            /*! exclusive or 연산. 엑시노스 라인 판단 위한 것 */
            /*! @@ 는 gcc 어셈블러의 주석 */
            /*! A15 프로세스의 값이라고 판단함 */
		ldreq	\rp, =EXYNOS5_PA_UART /*! EXYNOS5_PA_UART = 0x12C00000 */
            /*! tmp가 15면 윗라인 실행, 아니면 아랫라인 실행 */
		movne	\rp, #EXYNOS4_PA_UART	@@ EXYNOS4  /*! EXYNOS4_PA_UART = 0x13800000 */
            /*! rp가 uart의 physical address 가 된다. */
		ldr	\rv, =S3C_VA_UART     /*! S3C_VA_UART = S3C_ADDR_BASE + 0x01000000 */

#if CONFIG_DEBUG_S3C_UART != 0
		add	\rp, \rp, #(0x10000 * CONFIG_DEBUG_S3C_UART)
		add	\rv, \rv, #(0x10000 * CONFIG_DEBUG_S3C_UART)
#endif
	.endm

#define fifo_full fifo_full_s5pv210
#define fifo_level fifo_level_s5pv210

#include <debug/samsung.S>
