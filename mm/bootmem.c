/*
 *  bootmem - A boot-time physical memory allocator and configurator
 *
 *  Copyright (C) 1999 Ingo Molnar
 *                1999 Kanoj Sarcar, SGI
 *                2008 Johannes Weiner
 *
 * Access to this subsystem has to be serialized externally (which is true
 * for the boot process anyway).
 */
#include <linux/init.h>
#include <linux/pfn.h>
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/export.h>
#include <linux/kmemleak.h>
#include <linux/range.h>
#include <linux/memblock.h>

#include <asm/bug.h>
#include <asm/io.h>
#include <asm/processor.h>

#include "internal.h"

#ifndef CONFIG_NEED_MULTIPLE_NODES
struct pglist_data __refdata contig_page_data = {
	.bdata = &bootmem_node_data[0]
};
EXPORT_SYMBOL(contig_page_data);
#endif

unsigned long max_low_pfn;
unsigned long min_low_pfn;
unsigned long max_pfn;

bootmem_data_t bootmem_node_data[MAX_NUMNODES] __initdata;

static struct list_head bdata_list __initdata = LIST_HEAD_INIT(bdata_list);

static int bootmem_debug;

static int __init bootmem_debug_setup(char *buf)
{
	bootmem_debug = 1;
	return 0;
}
early_param("bootmem_debug", bootmem_debug_setup);

#define bdebug(fmt, args...) ({				\
	if (unlikely(bootmem_debug))			\
		printk(KERN_INFO			\
			"bootmem::%s " fmt,		\
			__func__, ## args);		\
})

static unsigned long __init bootmap_bytes(unsigned long pages)
{
	/*! 20131102 lowmem을 표현하기 위해 필요한 byte 계산 */
	unsigned long bytes = DIV_ROUND_UP(pages, 8);

	return ALIGN(bytes, sizeof(long));
}

/**
 * bootmem_bootmap_pages - calculate bitmap size in pages
 * @pages: number of pages the bitmap has to represent
 */
unsigned long __init bootmem_bootmap_pages(unsigned long pages)
{
	/*! 20131102 190k개의 page만큼 bitmap 할당, 24320byte(6개 page)로 표현 가능 */
	unsigned long bytes = bootmap_bytes(pages);

	return PAGE_ALIGN(bytes) >> PAGE_SHIFT;
}

/*
 * link bdata in order
 */
static void __init link_bootmem(bootmem_data_t *bdata)
{
	bootmem_data_t *ent;

	/*! 20131109 bdata_list를 검색하여 bdata를 오름차순으로 정렬된 위치에 추가 */
	list_for_each_entry(ent, &bdata_list, list) {
	/*! 20131109
	 * for (pos = list_entry((head)->next, typeof(*pos), member);	\
	 *      &pos->member != (head); 	\
	 *      pos = list_entry(pos->member.next, typeof(*pos), member))
	 */
		if (bdata->node_min_pfn < ent->node_min_pfn) {
			list_add_tail(&bdata->list, &ent->list);
			return;
		}
	}

	list_add_tail(&bdata->list, &bdata_list);
}

/*
 * Called once to set up the allocator itself.
 */
static unsigned long __init init_bootmem_core(bootmem_data_t *bdata,
	unsigned long mapstart, unsigned long start, unsigned long end)
{
	unsigned long mapsize;

	mminit_validate_memmodel_limits(&start, &end);
	/*! 20131109 start와 end가 32비트 안에 속하는지 확인 */
	bdata->node_bootmem_map = phys_to_virt(PFN_PHYS(mapstart));
	/*! 20131109 bitmap 시작주소(mapstart)를 va로 바꾸어서 node_bootmem_map에 대입한다. */
	bdata->node_min_pfn = start;
	bdata->node_low_pfn = end;
	/*! 20131109 start, end: pfn으로 나눈 bootmem의 시작주소와 끝주소 */
	link_bootmem(bdata);
	/*! 20131109 bdata를 node_min_pfn 순서로 정렬하여 bdata_list에 추가 */

	/*
	 * Initially all pages are reserved - setup_arch() has to
	 * register free RAM areas explicitly.
	 */
	mapsize = bootmap_bytes(end - start);
	/*! 20131109 bitmap bytes를 구한다. mapsize: 0x5f00 byte */
	memset(bdata->node_bootmem_map, 0xff, mapsize);
	/*! 20131109 6개 page에 대한 bitmap을 1로 초기화 */

	bdebug("nid=%td start=%lx map=%lx end=%lx mapsize=%lx\n",
		bdata - bootmem_node_data, start, mapstart, end, mapsize);

	return mapsize;
	/*! 20131109 크기를 리턴 */
}

/**
 * init_bootmem_node - register a node as boot memory
 * @pgdat: node to register
 * @freepfn: pfn where the bitmap for this node is to be placed
 * @startpfn: first pfn on the node
 * @endpfn: first pfn after the node
 *
 * Returns the number of bytes needed to hold the bitmap for this node.
 */
unsigned long __init init_bootmem_node(pg_data_t *pgdat, unsigned long freepfn,
				unsigned long startpfn, unsigned long endpfn)
{
	return init_bootmem_core(pgdat->bdata, freepfn, startpfn, endpfn);
	/*! 20131109 입력받은 부트 메모리 노드 추가, bitmap(freepfn) 초기화 */
}

/**
 * init_bootmem - register boot memory
 * @start: pfn where the bitmap is to be placed
 * @pages: number of available physical pages
 *
 * Returns the number of bytes needed to hold the bitmap.
 */
unsigned long __init init_bootmem(unsigned long start, unsigned long pages)
{
	max_low_pfn = pages;
	min_low_pfn = start;
	return init_bootmem_core(NODE_DATA(0)->bdata, start, 0, pages);
}

/*
 * free_bootmem_late - free bootmem pages directly to page allocator
 * @addr: starting physical address of the range
 * @size: size of the range in bytes
 *
 * This is only useful when the bootmem allocator has already been torn
 * down, but we are still initializing the system.  Pages are given directly
 * to the page allocator, no bootmem metadata is updated because it is gone.
 */
void __init free_bootmem_late(unsigned long physaddr, unsigned long size)
{
	unsigned long cursor, end;

	kmemleak_free_part(__va(physaddr), size);

	cursor = PFN_UP(physaddr);
	end = PFN_DOWN(physaddr + size);

	for (; cursor < end; cursor++) {
		__free_pages_bootmem(pfn_to_page(cursor), 0);
		totalram_pages++;
	}
}

static unsigned long __init free_all_bootmem_core(bootmem_data_t *bdata)
{
	struct page *page;
	unsigned long start, end, pages, count = 0;

	if (!bdata->node_bootmem_map)
		return 0;

	start = bdata->node_min_pfn;
	end = bdata->node_low_pfn;

	bdebug("nid=%td start=%lx end=%lx\n",
		bdata - bootmem_node_data, start, end);

	while (start < end) {
		unsigned long *map, idx, vec;
		unsigned shift;

		map = bdata->node_bootmem_map;
		idx = start - bdata->node_min_pfn;
		shift = idx & (BITS_PER_LONG - 1);
		/*
		 * vec holds at most BITS_PER_LONG map bits,
		 * bit 0 corresponds to start.
		 */
		vec = ~map[idx / BITS_PER_LONG];

		if (shift) {
			vec >>= shift;
			if (end - start >= BITS_PER_LONG)
				vec |= ~map[idx / BITS_PER_LONG + 1] <<
					(BITS_PER_LONG - shift);
		}
		/*
		 * If we have a properly aligned and fully unreserved
		 * BITS_PER_LONG block of pages in front of us, free
		 * it in one go.
		 */
		if (IS_ALIGNED(start, BITS_PER_LONG) && vec == ~0UL) {
			int order = ilog2(BITS_PER_LONG);

			__free_pages_bootmem(pfn_to_page(start), order);
			count += BITS_PER_LONG;
			start += BITS_PER_LONG;
		} else {
			unsigned long cur = start;

			start = ALIGN(start + 1, BITS_PER_LONG);
			while (vec && cur != start) {
				if (vec & 1) {
					page = pfn_to_page(cur);
					__free_pages_bootmem(page, 0);
					count++;
				}
				vec >>= 1;
				++cur;
			}
		}
	}

	page = virt_to_page(bdata->node_bootmem_map);
	pages = bdata->node_low_pfn - bdata->node_min_pfn;
	pages = bootmem_bootmap_pages(pages);
	count += pages;
	while (pages--)
		__free_pages_bootmem(page++, 0);

	bdebug("nid=%td released=%lx\n", bdata - bootmem_node_data, count);

	return count;
}

static int reset_managed_pages_done __initdata;

static inline void __init reset_node_managed_pages(pg_data_t *pgdat)
{
	struct zone *z;

	if (reset_managed_pages_done)
		return;

	for (z = pgdat->node_zones; z < pgdat->node_zones + MAX_NR_ZONES; z++)
		z->managed_pages = 0;
}

void __init reset_all_zones_managed_pages(void)
{
	struct pglist_data *pgdat;

	for_each_online_pgdat(pgdat)
		reset_node_managed_pages(pgdat);
	reset_managed_pages_done = 1;
}

/**
 * free_all_bootmem - release free pages to the buddy allocator
 *
 * Returns the number of pages actually released.
 */
unsigned long __init free_all_bootmem(void)
{
	unsigned long total_pages = 0;
	bootmem_data_t *bdata;

	reset_all_zones_managed_pages();

	list_for_each_entry(bdata, &bdata_list, list)
		total_pages += free_all_bootmem_core(bdata);

	totalram_pages += total_pages;

	return total_pages;
}

static void __init __free(bootmem_data_t *bdata,
			unsigned long sidx, unsigned long eidx)
{
	unsigned long idx;

	bdebug("nid=%td start=%lx end=%lx\n", bdata - bootmem_node_data,
		sidx + bdata->node_min_pfn,
		eidx + bdata->node_min_pfn);

	/*! 20131109 hint_idx는아직 초기화 되지 않은 것으로 보이며, 추후 확인 필요. */
	if (bdata->hint_idx > sidx)
		bdata->hint_idx = sidx;

	/*! 20131109 node_bootmem_map의 sidx부터 eidx까지 bit를 clear */
	for (idx = sidx; idx < eidx; idx++)
		if (!test_and_clear_bit(idx, bdata->node_bootmem_map))
			BUG();
}

/*! 20131109 해당 page의 bitmap을 reserve(1)으로 만든다. */
static int __init __reserve(bootmem_data_t *bdata, unsigned long sidx,
			unsigned long eidx, int flags)
{
	unsigned long idx;
	int exclusive = flags & BOOTMEM_EXCLUSIVE;

	bdebug("nid=%td start=%lx end=%lx flags=%x\n",
		bdata - bootmem_node_data,
		sidx + bdata->node_min_pfn,
		eidx + bdata->node_min_pfn,
		flags);

	/*! 20131109 node_bootmem_map의 sidx부터 eidx까지 bit를 set */
	for (idx = sidx; idx < eidx; idx++)
		/*! 20131109 원래 reserve가 이미 되어 있는 경우 free한 후 다시 set함 */
		if (test_and_set_bit(idx, bdata->node_bootmem_map)) {
			if (exclusive) {
				/*! 20131109  exclusive가 set 되어있으면 충돌시 rollback한다. */
				__free(bdata, sidx, idx);
				return -EBUSY;
			}
			bdebug("silent double reserve of PFN %lx\n",
				idx + bdata->node_min_pfn);
		}
	return 0;
}

/*! 20131109 start부터 end까지를 reserve 값으로 reserve(1) 또는 free(0) 한다. */
static int __init mark_bootmem_node(bootmem_data_t *bdata,
				unsigned long start, unsigned long end,
				int reserve, int flags)
{
	unsigned long sidx, eidx;

	/*! 20131109
	 * %td는 memory point값을 type offset처럼 출력 
	 * ex) int a[5]; printf("%td",a[3]-a[0]);//3이 출력.
	 */
	bdebug("nid=%td start=%lx end=%lx reserve=%d flags=%x\n",
		bdata - bootmem_node_data, start, end, reserve, flags);
	/*! 20131109
	 * nid=3(bdata array의 3번째 offset) start=0x20000 end=0x4f800 reserve=0 flags=0
	 * .bdata = &bootmem_node_data[0] 이므로 nid는 0
	 */

	/*! 20131109 lowmem 범위를 벗어나면 panic */
	BUG_ON(start < bdata->node_min_pfn);
	BUG_ON(end > bdata->node_low_pfn);

	/*! 20131109 lowmem 시작부터의 start, end index */
	sidx = start - bdata->node_min_pfn;
	eidx = end - bdata->node_min_pfn;

	if (reserve)
		return __reserve(bdata, sidx, eidx, flags);
	else
		/*! 20131109 bdata(bitmap)의 node_bootmem_map의 sidx~eidx bit clear. */
		__free(bdata, sidx, eidx);
		/*! 20131109 bdata의 node_bootmem_map의 sidx ~ eidx까지의 bit를 clear */
	return 0;
}

static int __init mark_bootmem(unsigned long start, unsigned long end,
				int reserve, int flags)
{
	unsigned long pos;
	bootmem_data_t *bdata;

	pos = start;
	/*! 20131109 연속적이지 않은 메모리인 경우에도 */
	list_for_each_entry(bdata, &bdata_list, list) {
		int err;
		unsigned long max;

		/*! 20131109 min~ low 영역을 벗어날 때 panic */
		if (pos < bdata->node_min_pfn ||
		    pos >= bdata->node_low_pfn) {
			BUG_ON(pos != start);
			continue;
		}

		/*! 20131109 max는 lowmem을 넘어설수 없다. 결코 */
		max = min(bdata->node_low_pfn, end);

		/*! 20131109 reserve: 0, flags: 0 */
		err = mark_bootmem_node(bdata, pos, max, reserve, flags);
		/*! 20131109 lowmem 영역을 모두 free 함 */
		if (reserve && err) {
			mark_bootmem(start, pos, 0, 0);
			return err;
		}

		if (max == end)
			return 0;
		pos = bdata->node_low_pfn;
	}
	BUG();
}

/**
 * free_bootmem_node - mark a page range as usable
 * @pgdat: node the range resides on
 * @physaddr: starting address of the range
 * @size: size of the range in bytes
 *
 * Partial pages will be considered reserved and left as they are.
 *
 * The range must reside completely on the specified node.
 */
void __init free_bootmem_node(pg_data_t *pgdat, unsigned long physaddr,
			      unsigned long size)
{
	unsigned long start, end;

	kmemleak_free_part(__va(physaddr), size);

	start = PFN_UP(physaddr);
	end = PFN_DOWN(physaddr + size);

	mark_bootmem_node(pgdat->bdata, start, end, 0, 0);
}

/**
 * free_bootmem - mark a page range as usable
 * @addr: starting physical address of the range
 * @size: size of the range in bytes
 *
 * Partial pages will be considered reserved and left as they are.
 *
 * The range must be contiguous but may span node boundaries.
 */
void __init free_bootmem(unsigned long physaddr, unsigned long size)
{
	unsigned long start, end;

	/*! 20131109 debug feature enable 되었을 때만 실행되는 함수다.  */
	kmemleak_free_part(__va(physaddr), size);
	/*! 20131109 설정되었다면 메모리 사용시 detect 된다. */

	start = PFN_UP(physaddr);
	end = PFN_DOWN(physaddr + size);

	mark_bootmem(start, end, 0, 0);
	/*! 20131109 start ~ end 의 모든 페이지 메모리를 free시킨다. */
}

/**
 * reserve_bootmem_node - mark a page range as reserved
 * @pgdat: node the range resides on
 * @physaddr: starting address of the range
 * @size: size of the range in bytes
 * @flags: reservation flags (see linux/bootmem.h)
 *
 * Partial pages will be reserved.
 *
 * The range must reside completely on the specified node.
 */
int __init reserve_bootmem_node(pg_data_t *pgdat, unsigned long physaddr,
				 unsigned long size, int flags)
{
	unsigned long start, end;

	start = PFN_DOWN(physaddr);
	end = PFN_UP(physaddr + size);

	return mark_bootmem_node(pgdat->bdata, start, end, 1, flags);
}

/**
 * reserve_bootmem - mark a page range as reserved
 * @addr: starting address of the range
 * @size: size of the range in bytes
 * @flags: reservation flags (see linux/bootmem.h)
 *
 * Partial pages will be reserved.
 *
 * The range must be contiguous but may span node boundaries.
 */

/*! 20131109 해당 bootmem을 reserve */
int __init reserve_bootmem(unsigned long addr, unsigned long size,
			    int flags)
{
	unsigned long start, end;

	start = PFN_DOWN(addr);
	end = PFN_UP(addr + size);

	return mark_bootmem(start, end, 1, flags);
	/*! 20131109 bdata_list의 bootmem을 reserve한다. */
}

static unsigned long __init align_idx(struct bootmem_data *bdata,
				      unsigned long idx, unsigned long step)
{
	unsigned long base = bdata->node_min_pfn;

	/*
	 * Align the index with respect to the node start so that the
	 * combination of both satisfies the requested alignment.
	 */

	return ALIGN(base + idx, step) - base;
}

static unsigned long __init align_off(struct bootmem_data *bdata,
				      unsigned long off, unsigned long align)
{
	unsigned long base = PFN_PHYS(bdata->node_min_pfn);

	/* Same as align_idx for byte offsets */

	return ALIGN(base + off, align) - base;
}

static void * __init alloc_bootmem_bdata(struct bootmem_data *bdata,
					unsigned long size, unsigned long align,
					unsigned long goal, unsigned long limit)
{
	unsigned long fallback = 0;
	unsigned long min, max, start, sidx, midx, step;

	bdebug("nid=%td size=%lx [%lu pages] align=%lx goal=%lx limit=%lx\n",
		bdata - bootmem_node_data, size, PAGE_ALIGN(size) >> PAGE_SHIFT,
		align, goal, limit);

	BUG_ON(!size);
	BUG_ON(align & (align - 1));
	BUG_ON(limit && goal + size > limit);

	if (!bdata->node_bootmem_map)
		return NULL;

	min = bdata->node_min_pfn;
	max = bdata->node_low_pfn;

	goal >>= PAGE_SHIFT;
	limit >>= PAGE_SHIFT;

	if (limit && max > limit)
		max = limit;
	if (max <= min)
		return NULL;

	step = max(align >> PAGE_SHIFT, 1UL);

	if (goal && min < goal && goal < max)
		start = ALIGN(goal, step);
	else
		start = ALIGN(min, step);

	sidx = start - bdata->node_min_pfn;
	midx = max - bdata->node_min_pfn;

	if (bdata->hint_idx > sidx) {
		/*
		 * Handle the valid case of sidx being zero and still
		 * catch the fallback below.
		 */
		fallback = sidx + 1;
		sidx = align_idx(bdata, bdata->hint_idx, step);
	}

	while (1) {
		int merge;
		void *region;
		unsigned long eidx, i, start_off, end_off;
find_block:
		sidx = find_next_zero_bit(bdata->node_bootmem_map, midx, sidx);
		sidx = align_idx(bdata, sidx, step);
		eidx = sidx + PFN_UP(size);

		if (sidx >= midx || eidx > midx)
			break;

		for (i = sidx; i < eidx; i++)
			if (test_bit(i, bdata->node_bootmem_map)) {
				sidx = align_idx(bdata, i, step);
				if (sidx == i)
					sidx += step;
				goto find_block;
			}

		if (bdata->last_end_off & (PAGE_SIZE - 1) &&
				PFN_DOWN(bdata->last_end_off) + 1 == sidx)
			start_off = align_off(bdata, bdata->last_end_off, align);
		else
			start_off = PFN_PHYS(sidx);

		merge = PFN_DOWN(start_off) < sidx;
		end_off = start_off + size;

		bdata->last_end_off = end_off;
		bdata->hint_idx = PFN_UP(end_off);

		/*
		 * Reserve the area now:
		 */
		if (__reserve(bdata, PFN_DOWN(start_off) + merge,
				PFN_UP(end_off), BOOTMEM_EXCLUSIVE))
			BUG();

		region = phys_to_virt(PFN_PHYS(bdata->node_min_pfn) +
				start_off);
		memset(region, 0, size);
		/*
		 * The min_count is set to 0 so that bootmem allocated blocks
		 * are never reported as leaks.
		 */
		kmemleak_alloc(region, size, 0, 0);
		return region;
	}

	if (fallback) {
		sidx = align_idx(bdata, fallback - 1, step);
		fallback = 0;
		goto find_block;
	}

	return NULL;
}

static void * __init alloc_bootmem_core(unsigned long size,
					unsigned long align,
					unsigned long goal,
					unsigned long limit)
{
	bootmem_data_t *bdata;
	void *region;

	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc(size, GFP_NOWAIT);

	list_for_each_entry(bdata, &bdata_list, list) {
		if (goal && bdata->node_low_pfn <= PFN_DOWN(goal))
			continue;
		if (limit && bdata->node_min_pfn >= PFN_DOWN(limit))
			break;

		region = alloc_bootmem_bdata(bdata, size, align, goal, limit);
		if (region)
			return region;
	}

	return NULL;
}

static void * __init ___alloc_bootmem_nopanic(unsigned long size,
					      unsigned long align,
					      unsigned long goal,
					      unsigned long limit)
{
	void *ptr;

restart:
	ptr = alloc_bootmem_core(size, align, goal, limit);
	if (ptr)
		return ptr;
	if (goal) {
		goal = 0;
		goto restart;
	}

	return NULL;
}

/**
 * __alloc_bootmem_nopanic - allocate boot memory without panicking
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * Returns NULL on failure.
 */
void * __init __alloc_bootmem_nopanic(unsigned long size, unsigned long align,
					unsigned long goal)
{
	unsigned long limit = 0;

	return ___alloc_bootmem_nopanic(size, align, goal, limit);
}

static void * __init ___alloc_bootmem(unsigned long size, unsigned long align,
					unsigned long goal, unsigned long limit)
{
	void *mem = ___alloc_bootmem_nopanic(size, align, goal, limit);

	if (mem)
		return mem;
	/*
	 * Whoops, we cannot satisfy the allocation request.
	 */
	printk(KERN_ALERT "bootmem alloc of %lu bytes failed!\n", size);
	panic("Out of memory");
	return NULL;
}

/**
 * __alloc_bootmem - allocate boot memory
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem(unsigned long size, unsigned long align,
			      unsigned long goal)
{
	unsigned long limit = 0;

	return ___alloc_bootmem(size, align, goal, limit);
}

void * __init ___alloc_bootmem_node_nopanic(pg_data_t *pgdat,
				unsigned long size, unsigned long align,
				unsigned long goal, unsigned long limit)
{
	void *ptr;

	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc(size, GFP_NOWAIT);
again:

	/* do not panic in alloc_bootmem_bdata() */
	if (limit && goal + size > limit)
		limit = 0;

	ptr = alloc_bootmem_bdata(pgdat->bdata, size, align, goal, limit);
	if (ptr)
		return ptr;

	ptr = alloc_bootmem_core(size, align, goal, limit);
	if (ptr)
		return ptr;

	if (goal) {
		goal = 0;
		goto again;
	}

	return NULL;
}

void * __init __alloc_bootmem_node_nopanic(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, pgdat->node_id);

	return ___alloc_bootmem_node_nopanic(pgdat, size, align, goal, 0);
}

void * __init ___alloc_bootmem_node(pg_data_t *pgdat, unsigned long size,
				    unsigned long align, unsigned long goal,
				    unsigned long limit)
{
	void *ptr;

	ptr = ___alloc_bootmem_node_nopanic(pgdat, size, align, goal, 0);
	if (ptr)
		return ptr;

	printk(KERN_ALERT "bootmem alloc of %lu bytes failed!\n", size);
	panic("Out of memory");
	return NULL;
}

/**
 * __alloc_bootmem_node - allocate boot memory from a specific node
 * @pgdat: node to allocate from
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may fall back to any node in the system if the specified node
 * can not hold the requested memory.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem_node(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, pgdat->node_id);

	return  ___alloc_bootmem_node(pgdat, size, align, goal, 0);
}

void * __init __alloc_bootmem_node_high(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
#ifdef MAX_DMA32_PFN
	unsigned long end_pfn;

	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, pgdat->node_id);

	/* update goal according ...MAX_DMA32_PFN */
	end_pfn = pgdat->node_start_pfn + pgdat->node_spanned_pages;

	if (end_pfn > MAX_DMA32_PFN + (128 >> (20 - PAGE_SHIFT)) &&
	    (goal >> PAGE_SHIFT) < MAX_DMA32_PFN) {
		void *ptr;
		unsigned long new_goal;

		new_goal = MAX_DMA32_PFN << PAGE_SHIFT;
		ptr = alloc_bootmem_bdata(pgdat->bdata, size, align,
						 new_goal, 0);
		if (ptr)
			return ptr;
	}
#endif

	return __alloc_bootmem_node(pgdat, size, align, goal);

}

#ifndef ARCH_LOW_ADDRESS_LIMIT
#define ARCH_LOW_ADDRESS_LIMIT	0xffffffffUL
#endif

/**
 * __alloc_bootmem_low - allocate low boot memory
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem_low(unsigned long size, unsigned long align,
				  unsigned long goal)
{
	return ___alloc_bootmem(size, align, goal, ARCH_LOW_ADDRESS_LIMIT);
}

void * __init __alloc_bootmem_low_nopanic(unsigned long size,
					  unsigned long align,
					  unsigned long goal)
{
	return ___alloc_bootmem_nopanic(size, align, goal,
					ARCH_LOW_ADDRESS_LIMIT);
}

/**
 * __alloc_bootmem_low_node - allocate low boot memory from a specific node
 * @pgdat: node to allocate from
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may fall back to any node in the system if the specified node
 * can not hold the requested memory.
 *
 * The function panics if the request can not be satisfied.
 */
void * __init __alloc_bootmem_low_node(pg_data_t *pgdat, unsigned long size,
				       unsigned long align, unsigned long goal)
{
	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, pgdat->node_id);

	return ___alloc_bootmem_node(pgdat, size, align,
				     goal, ARCH_LOW_ADDRESS_LIMIT);
}
