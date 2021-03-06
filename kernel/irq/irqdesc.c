/*
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006, Thomas Gleixner, Russell King
 *
 * This file contains the interrupt descriptor management code
 *
 * Detailed information is available in Documentation/DocBook/genericirq
 *
 */
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/radix-tree.h>
#include <linux/bitmap.h>

#include "internals.h"

/*
 * lockdep: we want to handle all irq_desc locks as a single lock-class:
 */
static struct lock_class_key irq_desc_lock_class;

#if defined(CONFIG_SMP)
static void __init init_irq_default_affinity(void)
{
	alloc_cpumask_var(&irq_default_affinity, GFP_NOWAIT);
	/*! 20140726 아래 들어감 */
	cpumask_setall(irq_default_affinity);
	/*! 20140726 irq_default_affinity->bits 에서 nr_cpumask_bits만큼의 bit를 1로 set */
}
#else
static void __init init_irq_default_affinity(void)
{
}
#endif

#ifdef CONFIG_SMP
/*! 20140726 여기 실행됨 */
static int alloc_masks(struct irq_desc *desc, gfp_t gfp, int node)
{
	if (!zalloc_cpumask_var_node(&desc->irq_data.affinity, gfp, node))
		return -ENOMEM;
	/*! 20140726 desc->irq_data.affinity->bits를 0 으로 clear함 */

#ifdef CONFIG_GENERIC_PENDING_IRQ
	if (!zalloc_cpumask_var_node(&desc->pending_mask, gfp, node)) {
		free_cpumask_var(desc->irq_data.affinity);
		return -ENOMEM;
	}
#endif
	return 0;
}

static void desc_smp_init(struct irq_desc *desc, int node)
{
	desc->irq_data.node = node;
	/*! 20140802 node는 memory가 하나이므로 0이다. */
	cpumask_copy(desc->irq_data.affinity, irq_default_affinity);
	/*! 20140726 desc->irq_data.affinity->bits = irq_default_affinity->bits */
#ifdef CONFIG_GENERIC_PENDING_IRQ
	cpumask_clear(desc->pending_mask);
#endif
}

static inline int desc_node(struct irq_desc *desc)
{
	return desc->irq_data.node;
}

#else
static inline int
alloc_masks(struct irq_desc *desc, gfp_t gfp, int node) { return 0; }
static inline void desc_smp_init(struct irq_desc *desc, int node) { }
static inline int desc_node(struct irq_desc *desc) { return 0; }
#endif

static void desc_set_defaults(unsigned int irq, struct irq_desc *desc, int node,
		struct module *owner)
{
	int cpu;

	desc->irq_data.irq = irq;
	desc->irq_data.chip = &no_irq_chip;
	desc->irq_data.chip_data = NULL;
	desc->irq_data.handler_data = NULL;
	desc->irq_data.msi_desc = NULL;
	/*! 20140726 _IRQ_DEFAULT_INIT_FLAGS: 0 */
	irq_settings_clr_and_set(desc, ~0, _IRQ_DEFAULT_INIT_FLAGS);
	/*! 20140726 desc->status_use_accessors 의 bit를 clear & set */
	irqd_set(&desc->irq_data, IRQD_IRQ_DISABLED);
	/*! 20140726 desc->irq_data->state_use_accessors |= IRQD_IRQ_DISABLED */
	desc->handle_irq = handle_bad_irq;
	desc->depth = 1;
	desc->irq_count = 0;
	desc->irqs_unhandled = 0;
	desc->name = NULL;
	desc->owner = owner;
	for_each_possible_cpu(cpu)
		*per_cpu_ptr(desc->kstat_irqs, cpu) = 0;
	/*! 20140726 cpu별 desc->kstat_irqs 초기화 */
	/*! 20140726 여기까지 스터디함 */
	desc_smp_init(desc, node);
	/*! 20140802 SMP IRQ affinity mask 를 default값(0xffffffff)으로 설정
	 * irq affinity: cpu별 interrupt 허용 mask (1이면 허용, 0이면 불가)
	 */
}

int nr_irqs = NR_IRQS;
/*! 20140726 여기 참조 */
EXPORT_SYMBOL_GPL(nr_irqs);

static DEFINE_MUTEX(sparse_irq_lock);
static DECLARE_BITMAP(allocated_irqs, IRQ_BITMAP_BITS);

#ifdef CONFIG_SPARSE_IRQ

static RADIX_TREE(irq_desc_tree, GFP_KERNEL);
/*! 20140802 struct radix_tree_root irq_desc_tree = { .height = 0, .gfp_mask = (GFP_KERNEL), .rnode = NULL, } */

static void irq_insert_desc(unsigned int irq, struct irq_desc *desc)
{
	radix_tree_insert(&irq_desc_tree, irq, desc);
	/*! 20140802 radix_tree_root 인 irq_desc_tree의 irq index 위치에 desc item을 추가한다. */
}

struct irq_desc *irq_to_desc(unsigned int irq)
{
	return radix_tree_lookup(&irq_desc_tree, irq);
	/*! 20140816 irq_desc_tree의 irq에 해당하는 element 를 가져온다. */
}
EXPORT_SYMBOL(irq_to_desc);

static void delete_irq_desc(unsigned int irq)
{
	radix_tree_delete(&irq_desc_tree, irq);
}

#ifdef CONFIG_SMP
static void free_masks(struct irq_desc *desc)
{
#ifdef CONFIG_GENERIC_PENDING_IRQ
	free_cpumask_var(desc->pending_mask);
#endif
	free_cpumask_var(desc->irq_data.affinity);
}
#else
static inline void free_masks(struct irq_desc *desc) { }
#endif

static struct irq_desc *alloc_desc(int irq, int node, struct module *owner)
{
	struct irq_desc *desc;
	gfp_t gfp = GFP_KERNEL;
	/*! 20140726 GFP_KERNEL: 0xD0 = (__GFP_WAIT | __GFP_IO | __GFP_FS) */

	desc = kzalloc_node(sizeof(*desc), gfp, node);
	/*! 20140726 struct irq_desc 크기의 memory를 할당받는다. */
	if (!desc)
		return NULL;
	/* allocate based on nr_cpu_ids */
	desc->kstat_irqs = alloc_percpu(unsigned int);
	if (!desc->kstat_irqs)
		goto err_desc;
	/*! 20140726 kstat_irqs: percpu별로 존재해야 하는 변수
	 * int 크기만큼의 memory를 cpu별 percpu에서 할당받는다.
	 */

	if (alloc_masks(desc, gfp, node))
		goto err_kstat;
	/*! 20140726 desc->irq_data.affinity->bits를 0 으로 clear함 */

	raw_spin_lock_init(&desc->lock);
	lockdep_set_class(&desc->lock, &irq_desc_lock_class);
	/*! 20140726 아무일도 안함 */

	desc_set_defaults(irq, desc, node, owner);
	/*! 20140802 irq descript 초기화 */

	return desc;

err_kstat:
	free_percpu(desc->kstat_irqs);
err_desc:
	kfree(desc);
	return NULL;
}

static void free_desc(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	unregister_irq_proc(irq, desc);

	mutex_lock(&sparse_irq_lock);
	delete_irq_desc(irq);
	mutex_unlock(&sparse_irq_lock);

	free_masks(desc);
	free_percpu(desc->kstat_irqs);
	kfree(desc);
}

static int alloc_descs(unsigned int start, unsigned int cnt, int node,
		       struct module *owner)
{
	struct irq_desc *desc;
	int i;

	for (i = 0; i < cnt; i++) {
		desc = alloc_desc(start + i, node, owner);
		/*! 20140816 GICD_TYPER register에 설정된 IRQ갯수만큼의 descriptor 할당 */
		if (!desc)
			goto err;
		mutex_lock(&sparse_irq_lock);
		irq_insert_desc(start + i, desc);
		/*! 20140816 생성한 descriptor를 radix tree에 추가 */
		mutex_unlock(&sparse_irq_lock);
	}
	return start;

err:
	for (i--; i >= 0; i--)
		free_desc(start + i);

	mutex_lock(&sparse_irq_lock);
	bitmap_clear(allocated_irqs, start, cnt);
	mutex_unlock(&sparse_irq_lock);
	return -ENOMEM;
}

static int irq_expand_nr_irqs(unsigned int nr)
{
	if (nr > IRQ_BITMAP_BITS)
		/*! 20140816 IRQ_BITMAP_BITS: 16 + 8196 */
		return -ENOMEM;
	nr_irqs = nr;
	return 0;
}

int __init early_irq_init(void)
{
	int i, initcnt, node = first_online_node;
	/*! 20140726 node = 0 */
	struct irq_desc *desc;

	init_irq_default_affinity();
	/*! 20140726 irq_default_affinity->bits 초기화 */

	/* Let arch update nr_irqs and return the nr of preallocated irqs */
	initcnt = arch_probe_nr_irqs();
	/*! 20140726 initcnt = NR_IRQS = 16 */
	printk(KERN_INFO "NR_IRQS:%d nr_irqs:%d %d\n", NR_IRQS, nr_irqs, initcnt);

	/*! 20140726 IRQ_BITMAP_BITS: 16 + 8196 */
	if (WARN_ON(nr_irqs > IRQ_BITMAP_BITS))
		nr_irqs = IRQ_BITMAP_BITS;

	if (WARN_ON(initcnt > IRQ_BITMAP_BITS))
		initcnt = IRQ_BITMAP_BITS;

	if (initcnt > nr_irqs)
		nr_irqs = initcnt;
	/*! 20140726 initcnt 와 전역변수 nr_irqs 값이 다르면 initcnt로 조정한다. */

	for (i = 0; i < initcnt; i++) {
		desc = alloc_desc(i, node, NULL);
		/*! 20140802 irq별 descriptor memory 할당 및 초기화 */
		set_bit(i, allocated_irqs);
		/*! 20140802 allocated_irqs bit map에 i 설정 */
		irq_insert_desc(i, desc);
		/*! 20140802 irq_desc_tree에 desc를 추가한다. */
	}
	return arch_early_irq_init();
	/*! 20140802 항상 0 리턴 */
}

#else /* !CONFIG_SPARSE_IRQ */

struct irq_desc irq_desc[NR_IRQS] __cacheline_aligned_in_smp = {
	[0 ... NR_IRQS-1] = {
		.handle_irq	= handle_bad_irq,
		.depth		= 1,
		.lock		= __RAW_SPIN_LOCK_UNLOCKED(irq_desc->lock),
	}
};

int __init early_irq_init(void)
{
	int count, i, node = first_online_node;
	struct irq_desc *desc;

	init_irq_default_affinity();

	printk(KERN_INFO "NR_IRQS:%d\n", NR_IRQS);

	desc = irq_desc;
	count = ARRAY_SIZE(irq_desc);

	for (i = 0; i < count; i++) {
		desc[i].kstat_irqs = alloc_percpu(unsigned int);
		alloc_masks(&desc[i], GFP_KERNEL, node);
		raw_spin_lock_init(&desc[i].lock);
		lockdep_set_class(&desc[i].lock, &irq_desc_lock_class);
		desc_set_defaults(i, &desc[i], node, NULL);
	}
	return arch_early_irq_init();
}

struct irq_desc *irq_to_desc(unsigned int irq)
{
	return (irq < NR_IRQS) ? irq_desc + irq : NULL;
}

static void free_desc(unsigned int irq)
{
	dynamic_irq_cleanup(irq);
}

static inline int alloc_descs(unsigned int start, unsigned int cnt, int node,
			      struct module *owner)
{
	u32 i;

	for (i = 0; i < cnt; i++) {
		struct irq_desc *desc = irq_to_desc(start + i);

		desc->owner = owner;
	}
	return start;
}

static int irq_expand_nr_irqs(unsigned int nr)
{
	return -ENOMEM;
}

#endif /* !CONFIG_SPARSE_IRQ */

/**
 * generic_handle_irq - Invoke the handler for a particular irq
 * @irq:	The irq number to handle
 *
 */
int generic_handle_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	if (!desc)
		return -EINVAL;
	generic_handle_irq_desc(irq, desc);
	return 0;
}
EXPORT_SYMBOL_GPL(generic_handle_irq);

/* Dynamic interrupt handling */

/**
 * irq_free_descs - free irq descriptors
 * @from:	Start of descriptor range
 * @cnt:	Number of consecutive irqs to free
 */
void irq_free_descs(unsigned int from, unsigned int cnt)
{
	int i;

	if (from >= nr_irqs || (from + cnt) > nr_irqs)
		return;

	for (i = 0; i < cnt; i++)
		free_desc(from + i);

	mutex_lock(&sparse_irq_lock);
	bitmap_clear(allocated_irqs, from, cnt);
	mutex_unlock(&sparse_irq_lock);
}
EXPORT_SYMBOL_GPL(irq_free_descs);

/**
 * irq_alloc_descs - allocate and initialize a range of irq descriptors
 * @irq:	Allocate for specific irq number if irq >= 0
 * @from:	Start the search from this irq number
 * @cnt:	Number of consecutive irqs to allocate.
 * @node:	Preferred node on which the irq descriptor should be allocated
 * @owner:	Owning module (can be NULL)
 *
 * Returns the first irq number or error code
 */
int __ref
__irq_alloc_descs(int irq, unsigned int from, unsigned int cnt, int node,
		  struct module *owner)
{
	int start, ret;

	if (!cnt)
		return -EINVAL;

	if (irq >= 0) {
		if (from > irq)
			return -EINVAL;
		from = irq;
	}

	mutex_lock(&sparse_irq_lock);

	start = bitmap_find_next_zero_area(allocated_irqs, IRQ_BITMAP_BITS,
					   from, cnt, 0);
	/*! 20140816 IRQ_BITMAP_BITS: 16 + 8196
	 * allocated_irqs bitmap에서 0으로 연속된 마지막 공간의 시작 index
	 * ex) 0000000001000...........001000010000  라면,
	 *     ^end    ^-- 여기 지점              ^start
	 */
	ret = -EEXIST;
	if (irq >=0 && start != irq)
		goto err;

	if (start + cnt > nr_irqs) {
		ret = irq_expand_nr_irqs(start + cnt);
		/*! 20140816 nr_irqs값을 start + cnt 로 재설정 */
		if (ret)
			goto err;
	}

	bitmap_set(allocated_irqs, start, cnt);
	/*! 20140816 allocated_irqs에서 start에서 cnt만큼 1로 set */
	mutex_unlock(&sparse_irq_lock);
	return alloc_descs(start, cnt, node, owner);
	/*! 20140816 GICD_TYPER register에 설정된 IRQ갯수만큼의 descriptor를 생성하여 radix tree에 추가 */

err:
	mutex_unlock(&sparse_irq_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(__irq_alloc_descs);

/**
 * irq_reserve_irqs - mark irqs allocated
 * @from:	mark from irq number
 * @cnt:	number of irqs to mark
 *
 * Returns 0 on success or an appropriate error code
 */
int irq_reserve_irqs(unsigned int from, unsigned int cnt)
{
	unsigned int start;
	int ret = 0;

	if (!cnt || (from + cnt) > nr_irqs)
		return -EINVAL;

	mutex_lock(&sparse_irq_lock);
	start = bitmap_find_next_zero_area(allocated_irqs, nr_irqs, from, cnt, 0);
	/*! 20140823 allocated_irqs에서 from ~ from + cnt bit 중에서 nr_irqs만큼 검색하면서 index가 0인 bit 리턴 */
	if (start == from)
		bitmap_set(allocated_irqs, start, cnt);
	else
		ret = -EEXIST;
	/*! 20140823 찾아진 start bit가 from 과 같으면 해당 영역 Set, 같지 않으면 -EEXIST 리턴 */
	mutex_unlock(&sparse_irq_lock);
	return ret;
}

/**
 * irq_get_next_irq - get next allocated irq number
 * @offset:	where to start the search
 *
 * Returns next irq number after offset or nr_irqs if none is found.
 */
unsigned int irq_get_next_irq(unsigned int offset)
{
	return find_next_bit(allocated_irqs, nr_irqs, offset);
}

struct irq_desc *
__irq_get_desc_lock(unsigned int irq, unsigned long *flags, bool bus,
		    unsigned int check)
{
	struct irq_desc *desc = irq_to_desc(irq);
	/*! 20140823 irq 번호에 맞는 descriptor pointer 획득 */

	if (desc) {
		if (check & _IRQ_DESC_CHECK) {
			if ((check & _IRQ_DESC_PERCPU) &&
			    !irq_settings_is_per_cpu_devid(desc))
				return NULL;

			if (!(check & _IRQ_DESC_PERCPU) &&
			    irq_settings_is_per_cpu_devid(desc))
				return NULL;
		}

		if (bus)
			chip_bus_lock(desc);
			/*! 20140823 아무일도 안함 */
		raw_spin_lock_irqsave(&desc->lock, *flags);
		/*! 20140823 lock 획득 */
	}
	return desc;
}

void __irq_put_desc_unlock(struct irq_desc *desc, unsigned long flags, bool bus)
{
	raw_spin_unlock_irqrestore(&desc->lock, flags);
	/*! 20140823 획득했던 lock 해제 */
	if (bus)
		chip_bus_sync_unlock(desc);
		/*! 20140823 아무일도 안함 */
}

int irq_set_percpu_devid(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	/*! 20140823 irq_desc_tree의 irq에 해당하는 element 를 가져온다. */

	if (!desc)
		return -EINVAL;

	if (desc->percpu_enabled)
		return -EINVAL;

	desc->percpu_enabled = kzalloc(sizeof(*desc->percpu_enabled), GFP_KERNEL);
	/*! 20140823 percpu_enabled를 위한 공간 할당 */

	if (!desc->percpu_enabled)
		return -ENOMEM;

	irq_set_percpu_devid_flags(irq);
	/*! 20140823 irq 번호의 desc->irq_data 에 flag 설정 */
	return 0;
}

/**
 * dynamic_irq_cleanup - cleanup a dynamically allocated irq
 * @irq:	irq number to initialize
 */
void dynamic_irq_cleanup(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long flags;

	raw_spin_lock_irqsave(&desc->lock, flags);
	desc_set_defaults(irq, desc, desc_node(desc), NULL);
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

unsigned int kstat_irqs_cpu(unsigned int irq, int cpu)
{
	struct irq_desc *desc = irq_to_desc(irq);

	return desc && desc->kstat_irqs ?
			*per_cpu_ptr(desc->kstat_irqs, cpu) : 0;
}

unsigned int kstat_irqs(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	int cpu;
	int sum = 0;

	if (!desc || !desc->kstat_irqs)
		return 0;
	for_each_possible_cpu(cpu)
		sum += *per_cpu_ptr(desc->kstat_irqs, cpu);
	return sum;
}
