/*
 *  linux/arch/arm/mm/extable.c
 */
#include <linux/module.h>
#include <linux/uaccess.h>

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	/*! 20140309
	 * instruction_pointer(regs)	(regs)->ARM_pc
	 * fixup 에는 regs->ARM_pc 를 포함하는 exception_table_entry 가 할당된다.
	 */
	fixup = search_exception_tables(instruction_pointer(regs));
	if (fixup)
		regs->ARM_pc = fixup->fixup;
	/*! 20140309
	 * insn이 ARM_pc 값인 exception_table_entry 를 찾아서
	 * ARM_pc값을 fixup 의 주소로 바꾼다.
	 */

	return fixup != NULL;
	/*! 20140309 fixup을 찾았으면 true, 못찾으면 false 리턴 */
}
