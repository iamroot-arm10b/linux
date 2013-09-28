/*
 * Procedures for maintaining information about logical memory blocks.
 *
 * Peter Bergner, IBM Corp.	June 2001.
 * Copyright (C) 2001 Peter Bergner.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/poison.h>
#include <linux/pfn.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/memblock.h>

static struct memblock_region memblock_memory_init_regions[INIT_MEMBLOCK_REGIONS] __initdata_memblock;
static struct memblock_region memblock_reserved_init_regions[INIT_MEMBLOCK_REGIONS] __initdata_memblock;

struct memblock memblock __initdata_memblock = {
	.memory.regions		= memblock_memory_init_regions,
	/*! 20130914 memblock_memory_init_regions를 128개 생성 */
	.memory.cnt		= 1,	/* empty dummy entry */
	.memory.max		= INIT_MEMBLOCK_REGIONS,
	/*! 20130914 INIT_MEMBLOCK_REGIONS = 128 */

	.reserved.regions	= memblock_reserved_init_regions,
	.reserved.cnt		= 1,	/* empty dummy entry */
	.reserved.max		= INIT_MEMBLOCK_REGIONS,

	.current_limit		= MEMBLOCK_ALLOC_ANYWHERE,
};

int memblock_debug __initdata_memblock;
static int memblock_can_resize __initdata_memblock;
/*! 20130914
 * #define __initdata_memblock __meminitdata
 * #define __meminitdata    __section(.meminit.data)
 * 
 * arm에서는 CONFIG_ARCH_DISCARD_MEMBLOCK가 설정되어 있지 않기 때문에
 * __meminitdata 매크로는 무의미해지며 bss 섹션에 들어간다.
 * 
 * 만약 x86처럼 CONFIG_ARCH_DISCARD_MEMBLOCK이 설정되어 있으면
 * __meminitdata가 붙으면 .meminit.data 섹션이 선언되며
 * 결국 INIT_DATA의 MEM_DISCARD로 .init.data에 들어간다.
 * 이 경우에도 선언되지 않은 변수의 초기값은 0이 된다.
 */
static int memblock_memory_in_slab __initdata_memblock = 0;
static int memblock_reserved_in_slab __initdata_memblock = 0;

/* inline so we don't get a warning when pr_debug is compiled out */
static __init_memblock const char *
memblock_type_name(struct memblock_type *type)
{
	if (type == &memblock.memory)
		return "memory";
	else if (type == &memblock.reserved)
		return "reserved";
	else
		return "unknown";
}

/* adjust *@size so that (@base + *@size) doesn't overflow, return new size */
static inline phys_addr_t memblock_cap_size(phys_addr_t base, phys_addr_t *size)
{
	return *size = min(*size, (phys_addr_t)ULLONG_MAX - base);
	/*!
	 * 정상적인 상황이라면 다음 조건을 만족시킨다
	 * (base + size <= 0xffffffff), (size <= 0xffffffff - base)
	 * 반대로 overflow되는 경우는 size가 더 크다.
	 * (base + size > 0xffffffff), (size > 0xffffffff - base)
	 * overflow 시에는 (0xffffffff - base) 값이 overflow되지 않는 최대 크기다.
	 * min 값을 취하면 언제나 overflow되지 않는 size를 구할수 있다.
	 * 
	 * ULL은 64비트의 최대값(0xffff...)이지만 phys_addr_t때문이 32비트로 casting 된다.
	 */
}

/*
 * Address comparison utilities
 */
static unsigned long __init_memblock memblock_addrs_overlap(phys_addr_t base1, phys_addr_t size1,
				       phys_addr_t base2, phys_addr_t size2)
{
	return ((base1 < (base2 + size2)) && (base2 < (base1 + size1)));
}

static long __init_memblock memblock_overlaps_region(struct memblock_type *type,
					phys_addr_t base, phys_addr_t size)
{
	unsigned long i;

	for (i = 0; i < type->cnt; i++) {
		phys_addr_t rgnbase = type->regions[i].base;
		phys_addr_t rgnsize = type->regions[i].size;
		if (memblock_addrs_overlap(base, size, rgnbase, rgnsize))
			break;
	}

	return (i < type->cnt) ? i : -1;
}

/**
 * memblock_find_in_range_node - find free area in given range and node
 * @start: start of candidate range
 * @end: end of candidate range, can be %MEMBLOCK_ALLOC_{ANYWHERE|ACCESSIBLE}
 * @size: size of free area to find
 * @align: alignment of free area to find
 * @nid: nid of the free area to find, %MAX_NUMNODES for any node
 *
 * Find @size free area aligned to @align in the specified range and node.
 *
 * RETURNS:
 * Found address on success, %0 on failure.
 */
phys_addr_t __init_memblock memblock_find_in_range_node(phys_addr_t start,
					phys_addr_t end, phys_addr_t size,
					phys_addr_t align, int nid)
{
	/*! 20130914 새로 node를 할당할 range를 찾는 과정 */
	phys_addr_t this_start, this_end, cand;
	u64 i;

	/* pump up @end */
	if (end == MEMBLOCK_ALLOC_ACCESSIBLE)
		/*! 20130914
		 * MEMBLOCK_ALLOC_ACCESSIBLE = 0
		 * end = 0xffffffff (메모리 사이즈 마지막 값)
		 */
		end = memblock.current_limit;

	/* avoid allocating the first page */
	start = max_t(phys_addr_t, start, PAGE_SIZE);
	/*! 20130914 start가 첫번째 page 영역인 경우에 PAGE_SIZE 로 재설정 */
	end = max(start, end);
	/*! 20130914 만약 end도 PAGE_SIZE보다 작으면 end와 start가 같아짐 */

	for_each_free_mem_range_reverse(i, nid, &this_start, &this_end, NULL) {
		/*! 20130914
		 * for (i = (u64)ULLONG_MAX, __next_free_mem_range_rev(&i, nid, p_start, p_end, p_nid);
		 *      i != (u64)ULLONG_MAX;
		 *      __next_free_mem_range_rev(&i, nid, p_start, p_end, p_nid))
		 */
		this_start = clamp(this_start, start, end);
		this_end = clamp(this_end, start, end);
		/*! 20130928
		 * this_start 와 this_end 를 start와 end 사이의 값인지 확인하여
		 * 범위를 벗어나는 경우 가까운 것으로 셋팅
		 * end: LOW_MEM_LIMIT (LOW Memory 영역까지만 할당하려는 것)
		 * 만약 high memory 영역(760M 이상의 모든 영역)이면 
		 * ( http://blog.daum.net/ckcjck/17 참조 )
		 * 커널 space에 맵핑되어 있지 않기 때문에 포인터 못가지고 온다.
		 * memory map은 Documentation/arm/memory.txt 참조
		 */

		if (this_end < size)
			continue;

		cand = round_down(this_end - size, align);
		if (cand >= this_start)
			return cand;
		/*! 20130928
		 * 할당할 크기만큼 뺀 주소를 align 하여 cand에 넣는데, 
		 * cand 가 this_start보다 작게 align되면 다음 위치를 찾고
		 * this_start 보다 작지 않으면 cand를 리턴한다.
		 */
	}
	return 0;
}

/**
 * memblock_find_in_range - find free area in given range
 * @start: start of candidate range
 * @end: end of candidate range, can be %MEMBLOCK_ALLOC_{ANYWHERE|ACCESSIBLE}
 * @size: size of free area to find
 * @align: alignment of free area to find
 *
 * Find @size free area aligned to @align in the specified range.
 *
 * RETURNS:
 * Found address on success, %0 on failure.
 */
phys_addr_t __init_memblock memblock_find_in_range(phys_addr_t start,
					phys_addr_t end, phys_addr_t size,
					phys_addr_t align)
{
	return memblock_find_in_range_node(start, end, size, align,
					   MAX_NUMNODES);
	/*! 20130928 새로 node를 할당할 range를 찾아서 physical address 를 리턴 */
}

static void __init_memblock memblock_remove_region(struct memblock_type *type, unsigned long r)
{
	type->total_size -= type->regions[r].size;
	memmove(&type->regions[r], &type->regions[r + 1],
		(type->cnt - (r + 1)) * sizeof(type->regions[r]));
	type->cnt--;
	/*! 20130928
	 * r + 1 을 r자리로 move하여 r부분을 지워버린다.
	 */

	/* Special case for empty arrays */
	if (type->cnt == 0) {
		WARN_ON(type->total_size != 0);
		type->cnt = 1;
		type->regions[0].base = 0;
		type->regions[0].size = 0;
		memblock_set_region_node(&type->regions[0], MAX_NUMNODES);
		/*! 20130928 만약 cnt가 0이 된다면 다시 초기화한다. */
	}
}

phys_addr_t __init_memblock get_allocated_memblock_reserved_regions_info(
					phys_addr_t *addr)
{
	if (memblock.reserved.regions == memblock_reserved_init_regions)
		return 0;

	*addr = __pa(memblock.reserved.regions);

	return PAGE_ALIGN(sizeof(struct memblock_region) *
			  memblock.reserved.max);
}

/**
 * memblock_double_array - double the size of the memblock regions array
 * @type: memblock type of the regions array being doubled
 * @new_area_start: starting address of memory range to avoid overlap with
 * @new_area_size: size of memory range to avoid overlap with
 *
 * Double the size of the @type regions array. If memblock is being used to
 * allocate memory for a new reserved regions array and there is a previously
 * allocated memory range [@new_area_start,@new_area_start+@new_area_size]
 * waiting to be reserved, ensure the memory used by the new array does
 * not overlap.
 *
 * RETURNS:
 * 0 on success, -1 on failure.
 */
static int __init_memblock memblock_double_array(struct memblock_type *type,
						phys_addr_t new_area_start,
						phys_addr_t new_area_size)
{
	struct memblock_region *new_array, *old_array;
	phys_addr_t old_alloc_size, new_alloc_size;
	phys_addr_t old_size, new_size, addr;
	int use_slab = slab_is_available(); /*! 20130914 use_slab = 0 */
	int *in_slab;

	/* We don't allow resizing until we know about the reserved regions
	 * of memory that aren't suitable for allocation
	 */
	if (!memblock_can_resize)
		/*! 20130914 memblock_can_resize은 전역변수이므로 0 */
		return -1;

	/* Calculate new doubled size */
	old_size = type->max * sizeof(struct memblock_region);
	new_size = old_size << 1;
	/*! 20130914 new_size는 두배로 늘림 */
	/*
	 * We need to allocated new one align to PAGE_SIZE,
	 *   so we can free them completely later.
	 */
	old_alloc_size = PAGE_ALIGN(old_size);
	new_alloc_size = PAGE_ALIGN(new_size);
	/*! 20130914 size를 PAGE_SIZE 만큼 align 한다. */

	/* Retrieve the slab flag */
	if (type == &memblock.memory)
		in_slab = &memblock_memory_in_slab;
		/*! 20130914
		 * type이 가리키는 주소와 
		 * memblock.memory가 가리키는 주소가 같으면 memblock_memory_in_slab
		 */
	else
		in_slab = &memblock_reserved_in_slab;

	/* Try to find some space for it.
	 *
	 * WARNING: We assume that either slab_is_available() and we use it or
	 * we use MEMBLOCK for allocations. That means that this is unsafe to
	 * use when bootmem is currently active (unless bootmem itself is
	 * implemented on top of MEMBLOCK which isn't the case yet)
	 *
	 * This should however not be an issue for now, as we currently only
	 * call into MEMBLOCK while it's still active, or much later when slab
	 * is active for memory hotplug operations
	 */
	if (use_slab) {
		new_array = kmalloc(new_size, GFP_KERNEL);
		addr = new_array ? __pa(new_array) : 0;
	} else {
		/*! 20130914 현재까지 slap 사용안함 */
		/* only exclude range when trying to double reserved.regions */
		if (type != &memblock.reserved)
			new_area_start = new_area_size = 0;

		/*! 20130928
		 * memblock의 type이 reserved가 아닌 경우 메모리의 처음부터 찾는다.
		 * reserved 인 경우, reserved된 영역 이후 부분에서 
		 * array 확장을 위한 free 메모리 영역의 주소를 찾는다.
		 */
		addr = memblock_find_in_range(new_area_start + new_area_size,
						memblock.current_limit,
						new_alloc_size, PAGE_SIZE);
		/*! 20130928
		 * memblock type이 reserved 이고,
		 * 시작주소 이후에서 할당할 주소를 찾지 못할 경우
		 * 메모리의 영역을 넓혀서 처음부터 시작주소까지 검색한다.
		 */
		if (!addr && new_area_size)
			addr = memblock_find_in_range(0,
				min(new_area_start, memblock.current_limit),
				new_alloc_size, PAGE_SIZE);

		new_array = addr ? __va(addr) : NULL;
		/*! 20130928 address 가 있으면 virtual address로 바꾼다.  */
	}
	if (!addr) {
		pr_err("memblock: Failed to double %s array from %ld to %ld entries !\n",
		       memblock_type_name(type), type->max, type->max * 2);
		return -1;
	}

	memblock_dbg("memblock: %s is doubled to %ld at [%#010llx-%#010llx]",
			memblock_type_name(type), type->max * 2, (u64)addr,
			(u64)addr + new_size - 1);

	/*
	 * Found space, we now need to move the array over before we add the
	 * reserved region since it may be our reserved array itself that is
	 * full.
	 */
	/*! 20130928 기존 array 영역은 복사하고 확장된 영역은 0으로 초기화 */
	memcpy(new_array, type->regions, old_size);
	memset(new_array + type->max, 0, old_size);
	/*! 20130928
	 * 기존 영역을 해제하기 위해 old_array에 현재 영역을 할당받고
	 * new_array를 regions에 할당하고 max를 두배로 증가시킨다.
	 */
	old_array = type->regions;
	type->regions = new_array;
	type->max <<= 1;

	/* Free old array. We needn't free it if the array is the static one */
	if (*in_slab)
		kfree(old_array);
	/*! 20130928 
	 * init section 은 나중에 한꺼번에 해제하기 때문에 나머지만 해제한다.
	 */
	else if (old_array != memblock_memory_init_regions &&
		 old_array != memblock_reserved_init_regions)
		memblock_free(__pa(old_array), old_alloc_size);

	/*
	 * Reserve the new array if that comes from the memblock.  Otherwise, we
	 * needn't do it
	 */
	if (!use_slab)
		BUG_ON(memblock_reserve(addr, new_alloc_size));
	/*! 20130928 addr에서 size만큼 reserved 영역으로 할당 */

	/* Update slab flag */
	*in_slab = use_slab;

	return 0;
}

/**
 * memblock_merge_regions - merge neighboring compatible regions
 * @type: memblock type to scan
 *
 * Scan @type and merge neighboring compatible regions.
 */
static void __init_memblock memblock_merge_regions(struct memblock_type *type)
{
	int i = 0;

	/* cnt never goes below 1 */
	while (i < type->cnt - 1) {
		struct memblock_region *this = &type->regions[i];
		struct memblock_region *next = &type->regions[i + 1];

		if (this->base + this->size != next->base ||
		    memblock_get_region_node(this) !=
		    memblock_get_region_node(next)) {
			/*! 20130914
			 * region의 영역이 연속되지 않았거나,
			 * node가 다른 경우 병합하지 않는다.
			 */
			BUG_ON(this->base + this->size > next->base);
			i++;
			continue;
		}

		this->size += next->size;
		/* move forward from next + 1, index of which is i + 2 */
		memmove(next, next + 1, (type->cnt - (i + 2)) * sizeof(*next));
		type->cnt--;
		/*! 20130914 두 region을 합치고 다음 node를 가리킴 */
	}
}

/**
 * memblock_insert_region - insert new memblock region
 * @type:	memblock type to insert into
 * @idx:	index for the insertion point
 * @base:	base address of the new region
 * @size:	size of the new region
 * @nid:	node id of the new region
 *
 * Insert new memblock region [@base,@base+@size) into @type at @idx.
 * @type must already have extra room to accomodate the new region.
 */
static void __init_memblock memblock_insert_region(struct memblock_type *type,
						   int idx, phys_addr_t base,
						   phys_addr_t size, int nid)
{
	/*! 20130914 rbase > base 인 경우에 region의 정보를 삽입 */
	struct memblock_region *rgn = &type->regions[idx];

	BUG_ON(type->cnt >= type->max);
	memmove(rgn + 1, rgn, (type->cnt - idx) * sizeof(*rgn));
	rgn->base = base;
	rgn->size = size;
	memblock_set_region_node(rgn, nid);
	type->cnt++;
	type->total_size += size;
}

/**
 * memblock_add_region - add new memblock region
 * @type: memblock type to add new region into
 * @base: base address of the new region
 * @size: size of the new region
 * @nid: nid of the new region
 *
 * Add new memblock region [@base,@base+@size) into @type.  The new region
 * is allowed to overlap with existing ones - overlaps don't affect already
 * existing regions.  @type is guaranteed to be minimal (all neighbouring
 * compatible regions are merged) after the addition.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */

// memblock_add_region(&memblock.memory, base, size, MAX_NUMNODES);
static int __init_memblock memblock_add_region(struct memblock_type *type,
				phys_addr_t base, phys_addr_t size, int nid)
{
	bool insert = false;
	phys_addr_t obase = base;
	phys_addr_t end = base + memblock_cap_size(base, &size);
	/*! memblock_cap_size 함수는 overflow 되지 않는 size를 반환한다. */
	int i, nr_new;

	if (!size)
		return 0;

	/* special case for empty array */
	if (type->regions[0].size == 0) {
	/*! 20130914 
	 * 처음 진입할때는 type->regions[0].size == 0 이므로 여기 실행 
	 * memblock_type 구조체 초기화
	 */
		WARN_ON(type->cnt != 1 || type->total_size);
		type->regions[0].base = base;
		type->regions[0].size = size;
		memblock_set_region_node(&type->regions[0], nid);
		type->total_size = size;
		return 0;
	}
repeat:
	/*
	 * The following is executed twice.  Once with %false @insert and
	 * then with %true.  The first counts the number of regions needed
	 * to accomodate the new area.  The second actually inserts them.
	 */
	base = obase;
	nr_new = 0;

	for (i = 0; i < type->cnt; i++) {
		struct memblock_region *rgn = &type->regions[i];
		phys_addr_t rbase = rgn->base;
		phys_addr_t rend = rbase + rgn->size;
		/*! 20130907
		 * rbase = 0x20000000
		 * rend =  0x4F800000
		 * 760M 에 대한 값.
		 */
		/*! 20130914
		 * rbase, rend는 i값이 증가하면서 따라 증가하고, 
		 * base, end는 고정되어 있으므로 
		 * rend <= base 인 경우: continue
		 * rbase >= end 인 경우: break
		 * rbase > base 인 경우: insert_region
		 * continue와 break가 아닌 경우: base를 min(rend, end)로 교체
		 */
		if (rbase >= end)
			break;
		if (rend <= base)
			continue;
		/*
		 * @rgn overlaps.  If it separates the lower part of new
		 * area, insert that portion.
		 */
		if (rbase > base) {
			nr_new++;
			if (insert)
				memblock_insert_region(type, i++, base,
						       rbase - base, nid);
			/*! 20130914
			 * rbase - base 크기만큼의(겹치지 않는 크기만큼의) memblock을 resion에 삽입
			 */
		}
		/* area below @rend is dealt with, forget about it */
		base = min(rend, end);
		/*! 20130914 rend가 base보다 크면 base를 rend와 end 중 작은 주소로 대체 */
	}
	/*! 20130914 for문이 끝나면 base는 등록할 위치를 가리킨다.  */

	/* insert the remaining portion */
	if (base < end) {
		nr_new++;
		if (insert)
			memblock_insert_region(type, i, base, end - base, nid);
		/*! 20130914 memblock을 region에 insert한다. */
	}

	/*
	 * If this was the first round, resize array and repeat for actual
	 * insertions; otherwise, merge and return.
	 */
	/*! 20130907 2013/09/07 여기까지...  */
	if (!insert) {
		while (type->cnt + nr_new > type->max)
			if (memblock_double_array(type, obase, size) < 0)
				return -ENOMEM;
		/*! 20130928 
		 * 새로 추가했을때 memblock이 현재 array의 max보다 커지면
		 * array size를 두배로 증가시켜 추가한다.
		 */
		insert = true;
		goto repeat;
	} else {
		memblock_merge_regions(type);
		/*! 20130914 연속된 region을 하나의 region으로 병합 */
		return 0;
	}
}

int __init_memblock memblock_add_node(phys_addr_t base, phys_addr_t size,
				       int nid)
{
	return memblock_add_region(&memblock.memory, base, size, nid);
}

int __init_memblock memblock_add(phys_addr_t base, phys_addr_t size)
{
	return memblock_add_region(&memblock.memory, base, size, MAX_NUMNODES);
}

/**
 * memblock_isolate_range - isolate given range into disjoint memblocks
 * @type: memblock type to isolate range for
 * @base: base of range to isolate
 * @size: size of range to isolate
 * @start_rgn: out parameter for the start of isolated region
 * @end_rgn: out parameter for the end of isolated region
 *
 * Walk @type and ensure that regions don't cross the boundaries defined by
 * [@base,@base+@size).  Crossing regions are split at the boundaries,
 * which may create at most two more regions.  The index of the first
 * region inside the range is returned in *@start_rgn and end in *@end_rgn.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
static int __init_memblock memblock_isolate_range(struct memblock_type *type,
					phys_addr_t base, phys_addr_t size,
					int *start_rgn, int *end_rgn)
{
	phys_addr_t end = base + memblock_cap_size(base, &size);
	/*! 20130928
	 *  size와 ULLONG_MAX - base 중에서 작은 값을 구해서
	 * overflow가 발생하지 않는 size를 더한다.
	 */
	int i;

	*start_rgn = *end_rgn = 0;

	if (!size)
		return 0;

	/* we'll create at most two more regions */
	while (type->cnt + 2 > type->max)
		/*! 20130928
		 * cnt : 현재 사용중인 region의 갯수
		 * max : 사용 가능한 region의 갯수
		 */
		if (memblock_double_array(type, base, size) < 0)
			return -ENOMEM;

	for (i = 0; i < type->cnt; i++) {
		struct memblock_region *rgn = &type->regions[i];
		phys_addr_t rbase = rgn->base;
		phys_addr_t rend = rbase + rgn->size;

		if (rbase >= end)
			break;
		if (rend <= base)
			continue;
		/*! 20130928
		 * 고립시킬 영역이 계속 찾고, 
		 * 고립시킬 영역이 region을 벗어나는 경우 break.
		 */

		if (rbase < base) {
			/*
			 * @rgn intersects from below.  Split and continue
			 * to process the next region - the new top half.
			 */
			rgn->base = base;
			rgn->size -= base - rbase;
			type->total_size -= base - rbase;
			memblock_insert_region(type, i, rbase, base - rbase,
					       memblock_get_region_node(rgn));
			/*! 20130928
			 * 지우려고하는 영역의 시작이 할당된 영역의 시작보다 큰 경우
			 * rbase ~ base 영역을 나누어 insert 하고,
			 * memblock_insert_region 함수 안에서 
			 * 나머지 부분을 1칸씩 밀면서 type->cnt를 증가시킨다.
			 */
		} else if (rend > end) {
			/*
			 * @rgn intersects from above.  Split and redo the
			 * current region - the new bottom half.
			 */
			rgn->base = end;
			rgn->size -= end - rbase;
			type->total_size -= end - rbase;
			memblock_insert_region(type, i--, rbase, end - rbase,
					       memblock_get_region_node(rgn));
			/*! 20130928
			 * 지우려고하는 영역의 끝이 할당된 영역의 끝보다 작은 경우
			 * base ~ end 영역을 나누어 insert 하고,
			 * memblock_insert_region 함수 안에서
			 * 나머지부분을 1칸씩 밀면서 type->cnt를 증가시키고
			 * i를 감소시킨다.
			 */
		} else {
			/* @rgn is fully contained, record it */
			if (!*end_rgn)
				*start_rgn = i;
			*end_rgn = i + 1;
			/*! 20130928
			 * start region과 end region의 index를 정해준다.
			 */
		}
	}

	return 0;
}

static int __init_memblock __memblock_remove(struct memblock_type *type,
					     phys_addr_t base, phys_addr_t size)
{
	int start_rgn, end_rgn;
	int i, ret;

	ret = memblock_isolate_range(type, base, size, &start_rgn, &end_rgn);
	/*! 20130928 제거할 영역의 start와 end 를 정해준다. */
	if (ret)
		return ret;

	for (i = end_rgn - 1; i >= start_rgn; i--)
		memblock_remove_region(type, i);
	/*! 20130928
	 * 지워야할 영역의 end_rgn 부터 start_rgn까지 지운다.
	 * end부터 지우는 것이 memmove를 적게해서 효율적이다.
	 */
	return 0;
}

int __init_memblock memblock_remove(phys_addr_t base, phys_addr_t size)
{
	return __memblock_remove(&memblock.memory, base, size);
}

int __init_memblock memblock_free(phys_addr_t base, phys_addr_t size)
{
	memblock_dbg("   memblock_free: [%#016llx-%#016llx] %pF\n",
		     (unsigned long long)base,
		     (unsigned long long)base + size,
		     (void *)_RET_IP_);

	return __memblock_remove(&memblock.reserved, base, size);
	/*! 20130928 memblock을 해제한다. */
}

int __init_memblock memblock_reserve(phys_addr_t base, phys_addr_t size)
{
	struct memblock_type *_rgn = &memblock.reserved;

	memblock_dbg("memblock_reserve: [%#016llx-%#016llx] %pF\n",
		     (unsigned long long)base,
		     (unsigned long long)base + size,
		     (void *)_RET_IP_);

	return memblock_add_region(_rgn, base, size, MAX_NUMNODES);
}

/**
 * __next_free_mem_range - next function for for_each_free_mem_range()
 * @idx: pointer to u64 loop variable
 * @nid: node selector, %MAX_NUMNODES for all nodes
 * @out_start: ptr to phys_addr_t for start address of the range, can be %NULL
 * @out_end: ptr to phys_addr_t for end address of the range, can be %NULL
 * @out_nid: ptr to int for nid of the range, can be %NULL
 *
 * Find the first free area from *@idx which matches @nid, fill the out
 * parameters, and update *@idx for the next iteration.  The lower 32bit of
 * *@idx contains index into memory region and the upper 32bit indexes the
 * areas before each reserved region.  For example, if reserved regions
 * look like the following,
 *
 *	0:[0-16), 1:[32-48), 2:[128-130)
 *
 * The upper 32bit indexes the following regions.
 *
 *	0:[0-0), 1:[16-32), 2:[48-128), 3:[130-MAX)
 *
 * As both region arrays are sorted, the function advances the two indices
 * in lockstep and returns each intersection.
 */
void __init_memblock __next_free_mem_range(u64 *idx, int nid,
					   phys_addr_t *out_start,
					   phys_addr_t *out_end, int *out_nid)
{
	struct memblock_type *mem = &memblock.memory;
	struct memblock_type *rsv = &memblock.reserved;
	int mi = *idx & 0xffffffff;
	int ri = *idx >> 32;

	for ( ; mi < mem->cnt; mi++) {
		struct memblock_region *m = &mem->regions[mi];
		phys_addr_t m_start = m->base;
		phys_addr_t m_end = m->base + m->size;

		/* only memory regions are associated with nodes, check it */
		if (nid != MAX_NUMNODES && nid != memblock_get_region_node(m))
			continue;

		/* scan areas before each reservation for intersection */
		for ( ; ri < rsv->cnt + 1; ri++) {
			struct memblock_region *r = &rsv->regions[ri];
			phys_addr_t r_start = ri ? r[-1].base + r[-1].size : 0;
			phys_addr_t r_end = ri < rsv->cnt ? r->base : ULLONG_MAX;

			/* if ri advanced past mi, break out to advance mi */
			if (r_start >= m_end)
				break;
			/* if the two regions intersect, we're done */
			if (m_start < r_end) {
				if (out_start)
					*out_start = max(m_start, r_start);
				if (out_end)
					*out_end = min(m_end, r_end);
				if (out_nid)
					*out_nid = memblock_get_region_node(m);
				/*
				 * The region which ends first is advanced
				 * for the next iteration.
				 */
				if (m_end <= r_end)
					mi++;
				else
					ri++;
				*idx = (u32)mi | (u64)ri << 32;
				return;
			}
		}
	}

	/* signal end of iteration */
	*idx = ULLONG_MAX;
}

/**
 * __next_free_mem_range_rev - next function for for_each_free_mem_range_reverse()
 * @idx: pointer to u64 loop variable
 * @nid: nid: node selector, %MAX_NUMNODES for all nodes
 * @out_start: ptr to phys_addr_t for start address of the range, can be %NULL
 * @out_end: ptr to phys_addr_t for end address of the range, can be %NULL
 * @out_nid: ptr to int for nid of the range, can be %NULL
 *
 * Reverse of __next_free_mem_range().
 */
void __init_memblock __next_free_mem_range_rev(u64 *idx, int nid,
					   phys_addr_t *out_start,
					   phys_addr_t *out_end, int *out_nid)
{
	struct memblock_type *mem = &memblock.memory;
	struct memblock_type *rsv = &memblock.reserved;
	int mi = *idx & 0xffffffff;
	int ri = *idx >> 32;
	/*! 20130914 주소를 상위 32bit(ri)와 하위 32bit(mi)로 나눈다.  */
	/*! 20130928 mi:memory index   ri:reserved index  */

	if (*idx == (u64)ULLONG_MAX) {
		/*! 20130914 처음이면 여기 실행 */
		mi = mem->cnt - 1;
		ri = rsv->cnt;
	}

	for ( ; mi >= 0; mi--) {
		struct memblock_region *m = &mem->regions[mi];
		phys_addr_t m_start = m->base;
		phys_addr_t m_end = m->base + m->size;

		/* only memory regions are associated with nodes, check it */
		if (nid != MAX_NUMNODES && nid != memblock_get_region_node(m))
			/*! 20130914 원하는 node id에 대해서만 검사한다.  */
			continue;

		/* scan areas before each reservation for intersection */
		for ( ; ri >= 0; ri--) {
			struct memblock_region *r = &rsv->regions[ri];
			phys_addr_t r_start = ri ? r[-1].base + r[-1].size : 0;
			phys_addr_t r_end = ri < rsv->cnt ? r->base : ULLONG_MAX;
			/*! 20130914
			 * 처음값 r_start = free 영역의 시작주소
			 *        r_end = ULLONG_MAX
			 * reservation 메모리가 아닌 공간을 찾는 것
			 * negative 배열 선언은 불가능하지만 [-1]처럼 지정하는 것은 가능하다.
			 * 2013/09/14 여기까지
			 */

			/* if ri advanced past mi, break out to advance mi */
			if (r_end <= m_start)
				break;
			/* if the two regions intersect, we're done */
			if (m_end > r_start) {
				if (out_start)
					*out_start = max(m_start, r_start);
				if (out_end)
					*out_end = min(m_end, r_end);
				if (out_nid)
					*out_nid = memblock_get_region_node(m);

				if (m_start >= r_start)
					mi--;
				else
					ri--;
				*idx = (u32)mi | (u64)ri << 32;
				return;
			}
		}
		/*! 20130928
		 * 시스템에 등록된 memory 영역중에서 reserved 영역이 아닌 메모리
		 * 즉 free한 메모리 공간을 구하는 함수
		 * start 주소와 end 주소, idx를 얻어서 변경한다.
		 */
	}

	*idx = ULLONG_MAX;
}

#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
/*
 * Common iterator interface used to define for_each_mem_range().
 */
void __init_memblock __next_mem_pfn_range(int *idx, int nid,
				unsigned long *out_start_pfn,
				unsigned long *out_end_pfn, int *out_nid)
{
	struct memblock_type *type = &memblock.memory;
	struct memblock_region *r;

	while (++*idx < type->cnt) {
		r = &type->regions[*idx];

		if (PFN_UP(r->base) >= PFN_DOWN(r->base + r->size))
			continue;
		if (nid == MAX_NUMNODES || nid == r->nid)
			break;
	}
	if (*idx >= type->cnt) {
		*idx = -1;
		return;
	}

	if (out_start_pfn)
		*out_start_pfn = PFN_UP(r->base);
	if (out_end_pfn)
		*out_end_pfn = PFN_DOWN(r->base + r->size);
	if (out_nid)
		*out_nid = r->nid;
}

/**
 * memblock_set_node - set node ID on memblock regions
 * @base: base of area to set node ID for
 * @size: size of area to set node ID for
 * @nid: node ID to set
 *
 * Set the nid of memblock memory regions in [@base,@base+@size) to @nid.
 * Regions which cross the area boundaries are split as necessary.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int __init_memblock memblock_set_node(phys_addr_t base, phys_addr_t size,
				      int nid)
{
	struct memblock_type *type = &memblock.memory;
	int start_rgn, end_rgn;
	int i, ret;

	ret = memblock_isolate_range(type, base, size, &start_rgn, &end_rgn);
	if (ret)
		return ret;

	for (i = start_rgn; i < end_rgn; i++)
		memblock_set_region_node(&type->regions[i], nid);

	memblock_merge_regions(type);
	return 0;
}
#endif /* CONFIG_HAVE_MEMBLOCK_NODE_MAP */

static phys_addr_t __init memblock_alloc_base_nid(phys_addr_t size,
					phys_addr_t align, phys_addr_t max_addr,
					int nid)
{
	phys_addr_t found;

	if (WARN_ON(!align))
		align = __alignof__(long long);

	/* align @size to avoid excessive fragmentation on reserved array */
	size = round_up(size, align);

	found = memblock_find_in_range_node(0, max_addr, size, align, nid);
	if (found && !memblock_reserve(found, size))
		return found;

	return 0;
}

phys_addr_t __init memblock_alloc_nid(phys_addr_t size, phys_addr_t align, int nid)
{
	return memblock_alloc_base_nid(size, align, MEMBLOCK_ALLOC_ACCESSIBLE, nid);
}

phys_addr_t __init __memblock_alloc_base(phys_addr_t size, phys_addr_t align, phys_addr_t max_addr)
{
	return memblock_alloc_base_nid(size, align, max_addr, MAX_NUMNODES);
}

phys_addr_t __init memblock_alloc_base(phys_addr_t size, phys_addr_t align, phys_addr_t max_addr)
{
	phys_addr_t alloc;

	alloc = __memblock_alloc_base(size, align, max_addr);

	if (alloc == 0)
		panic("ERROR: Failed to allocate 0x%llx bytes below 0x%llx.\n",
		      (unsigned long long) size, (unsigned long long) max_addr);

	return alloc;
}

phys_addr_t __init memblock_alloc(phys_addr_t size, phys_addr_t align)
{
	return memblock_alloc_base(size, align, MEMBLOCK_ALLOC_ACCESSIBLE);
}

phys_addr_t __init memblock_alloc_try_nid(phys_addr_t size, phys_addr_t align, int nid)
{
	phys_addr_t res = memblock_alloc_nid(size, align, nid);

	if (res)
		return res;
	return memblock_alloc_base(size, align, MEMBLOCK_ALLOC_ACCESSIBLE);
}


/*
 * Remaining API functions
 */

phys_addr_t __init memblock_phys_mem_size(void)
{
	return memblock.memory.total_size;
}

phys_addr_t __init memblock_mem_size(unsigned long limit_pfn)
{
	unsigned long pages = 0;
	struct memblock_region *r;
	unsigned long start_pfn, end_pfn;

	for_each_memblock(memory, r) {
		start_pfn = memblock_region_memory_base_pfn(r);
		end_pfn = memblock_region_memory_end_pfn(r);
		start_pfn = min_t(unsigned long, start_pfn, limit_pfn);
		end_pfn = min_t(unsigned long, end_pfn, limit_pfn);
		pages += end_pfn - start_pfn;
	}

	return (phys_addr_t)pages << PAGE_SHIFT;
}

/* lowest address */
phys_addr_t __init_memblock memblock_start_of_DRAM(void)
{
	return memblock.memory.regions[0].base;
}

phys_addr_t __init_memblock memblock_end_of_DRAM(void)
{
	int idx = memblock.memory.cnt - 1;

	return (memblock.memory.regions[idx].base + memblock.memory.regions[idx].size);
}

void __init memblock_enforce_memory_limit(phys_addr_t limit)
{
	unsigned long i;
	phys_addr_t max_addr = (phys_addr_t)ULLONG_MAX;

	if (!limit)
		return;

	/* find out max address */
	for (i = 0; i < memblock.memory.cnt; i++) {
		struct memblock_region *r = &memblock.memory.regions[i];

		if (limit <= r->size) {
			max_addr = r->base + limit;
			break;
		}
		limit -= r->size;
	}

	/* truncate both memory and reserved regions */
	__memblock_remove(&memblock.memory, max_addr, (phys_addr_t)ULLONG_MAX);
	__memblock_remove(&memblock.reserved, max_addr, (phys_addr_t)ULLONG_MAX);
}

static int __init_memblock memblock_search(struct memblock_type *type, phys_addr_t addr)
{
	unsigned int left = 0, right = type->cnt;

	do {
		unsigned int mid = (right + left) / 2;

		if (addr < type->regions[mid].base)
			right = mid;
		else if (addr >= (type->regions[mid].base +
				  type->regions[mid].size))
			left = mid + 1;
		else
			return mid;
	} while (left < right);
	return -1;
}

int __init memblock_is_reserved(phys_addr_t addr)
{
	return memblock_search(&memblock.reserved, addr) != -1;
}

int __init_memblock memblock_is_memory(phys_addr_t addr)
{
	return memblock_search(&memblock.memory, addr) != -1;
}

/**
 * memblock_is_region_memory - check if a region is a subset of memory
 * @base: base of region to check
 * @size: size of region to check
 *
 * Check if the region [@base, @base+@size) is a subset of a memory block.
 *
 * RETURNS:
 * 0 if false, non-zero if true
 */
int __init_memblock memblock_is_region_memory(phys_addr_t base, phys_addr_t size)
{
	int idx = memblock_search(&memblock.memory, base);
	phys_addr_t end = base + memblock_cap_size(base, &size);

	if (idx == -1)
		return 0;
	return memblock.memory.regions[idx].base <= base &&
		(memblock.memory.regions[idx].base +
		 memblock.memory.regions[idx].size) >= end;
}

/**
 * memblock_is_region_reserved - check if a region intersects reserved memory
 * @base: base of region to check
 * @size: size of region to check
 *
 * Check if the region [@base, @base+@size) intersects a reserved memory block.
 *
 * RETURNS:
 * 0 if false, non-zero if true
 */
int __init_memblock memblock_is_region_reserved(phys_addr_t base, phys_addr_t size)
{
	memblock_cap_size(base, &size);
	return memblock_overlaps_region(&memblock.reserved, base, size) >= 0;
}

void __init_memblock memblock_trim_memory(phys_addr_t align)
{
	int i;
	phys_addr_t start, end, orig_start, orig_end;
	struct memblock_type *mem = &memblock.memory;

	for (i = 0; i < mem->cnt; i++) {
		orig_start = mem->regions[i].base;
		orig_end = mem->regions[i].base + mem->regions[i].size;
		start = round_up(orig_start, align);
		end = round_down(orig_end, align);

		if (start == orig_start && end == orig_end)
			continue;

		if (start < end) {
			mem->regions[i].base = start;
			mem->regions[i].size = end - start;
		} else {
			memblock_remove_region(mem, i);
			i--;
		}
	}
}

void __init_memblock memblock_set_current_limit(phys_addr_t limit)
{
	memblock.current_limit = limit;
}

static void __init_memblock memblock_dump(struct memblock_type *type, char *name)
{
	unsigned long long base, size;
	int i;

	pr_info(" %s.cnt  = 0x%lx\n", name, type->cnt);

	for (i = 0; i < type->cnt; i++) {
		struct memblock_region *rgn = &type->regions[i];
		char nid_buf[32] = "";

		base = rgn->base;
		size = rgn->size;
#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
		if (memblock_get_region_node(rgn) != MAX_NUMNODES)
			snprintf(nid_buf, sizeof(nid_buf), " on node %d",
				 memblock_get_region_node(rgn));
#endif
		pr_info(" %s[%#x]\t[%#016llx-%#016llx], %#llx bytes%s\n",
			name, i, base, base + size - 1, size, nid_buf);
	}
}

void __init_memblock __memblock_dump_all(void)
{
	pr_info("MEMBLOCK configuration:\n");
	pr_info(" memory size = %#llx reserved size = %#llx\n",
		(unsigned long long)memblock.memory.total_size,
		(unsigned long long)memblock.reserved.total_size);

	memblock_dump(&memblock.memory, "memory");
	memblock_dump(&memblock.reserved, "reserved");
}

void __init memblock_allow_resize(void)
{
	memblock_can_resize = 1;
}

static int __init early_memblock(char *p)
{
	if (p && strstr(p, "debug"))
		memblock_debug = 1;
	return 0;
}
early_param("memblock", early_memblock);

#if defined(CONFIG_DEBUG_FS) && !defined(CONFIG_ARCH_DISCARD_MEMBLOCK)

static int memblock_debug_show(struct seq_file *m, void *private)
{
	struct memblock_type *type = m->private;
	struct memblock_region *reg;
	int i;

	for (i = 0; i < type->cnt; i++) {
		reg = &type->regions[i];
		seq_printf(m, "%4d: ", i);
		if (sizeof(phys_addr_t) == 4)
			seq_printf(m, "0x%08lx..0x%08lx\n",
				   (unsigned long)reg->base,
				   (unsigned long)(reg->base + reg->size - 1));
		else
			seq_printf(m, "0x%016llx..0x%016llx\n",
				   (unsigned long long)reg->base,
				   (unsigned long long)(reg->base + reg->size - 1));

	}
	return 0;
}

static int memblock_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, memblock_debug_show, inode->i_private);
}

static const struct file_operations memblock_debug_fops = {
	.open = memblock_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init memblock_init_debugfs(void)
{
	struct dentry *root = debugfs_create_dir("memblock", NULL);
	if (!root)
		return -ENXIO;
	debugfs_create_file("memory", S_IRUGO, root, &memblock.memory, &memblock_debug_fops);
	debugfs_create_file("reserved", S_IRUGO, root, &memblock.reserved, &memblock_debug_fops);

	return 0;
}
__initcall(memblock_init_debugfs);

#endif /* CONFIG_DEBUG_FS */
