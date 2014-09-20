/*
 * Copyright (C) 2012 Thomas Petazzoni
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/of_irq.h>

#include "irqchip.h"

/*
 * This special of_device_id is the sentinel at the end of the
 * of_device_id[] array of all irqchips. It is automatically placed at
 * the end of the array by the linker, thanks to being part of a
 * special section.
 */
static const struct of_device_id
irqchip_of_match_end __used __section(__irqchip_of_end);

extern struct of_device_id __irqchip_begin[];
/*! 20140802 __irqchip_begin: __irqchip_of_table 이라는 section의 시작주소를 가리킨다.
 * include/asm-generic/vmlinux.lds.h 참조
 */

void __init irqchip_init(void)
{
	of_irq_init(__irqchip_begin);
	/*! 20140920
	 * matches(__irqchip_begin)와 일치하는 device tree에서
	 * 일치하는 node에 대해서 인터럽터 초기화 한다.
	 * interrupt 구조가 tree 구조이기 때문에 부모부터 초기화한다.
	 */
}
