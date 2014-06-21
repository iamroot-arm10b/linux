#include <linux/init_task.h>
#include <linux/export.h>
#include <linux/mqueue.h>
#include <linux/sched.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/rt.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);

/* Initial task structure */
struct task_struct init_task = INIT_TASK(init_task);
/*! 20140621 여기 실행됨 */
EXPORT_SYMBOL(init_task);
/*! 20140621
 * #define __EXPORT_SYMBOL(init_task, "")				\
 *	extern typeof(init_task) init_task;				\
 *	__CRC_SYMBOL(init_task, "")					\
 *	static const char __kstrtab_init_task[]				\
 *	__attribute__((section("__ksymtab_strings"), aligned(1))) = "init_task";	\
 *	static const struct kernel_symbol __ksymtab_init_task __used	\
 *	__attribute__((section("___ksymtab" "+" "init_task"), unused))	\
 *	= { (unsigned long)&init_task, __kstrtab_init_task }
 * => init_task를 EXPORT하고 init_task에 대한 struct kernel_symbol 을 만들어준다.
 */

/*
 * Initial thread structure. Alignment of this is handled by a special
 * linker map entry.
 */
union thread_union init_thread_union __init_task_data =
	{ INIT_THREAD_INFO(init_task) };
