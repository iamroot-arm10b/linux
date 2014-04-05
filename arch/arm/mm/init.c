/*
 *  linux/arch/arm/mm/init.c
 *
 *  Copyright (C) 1995-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mman.h>
#include <linux/export.h>
#include <linux/nodemask.h>
#include <linux/initrd.h>
#include <linux/of_fdt.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/dma-contiguous.h>
#include <linux/sizes.h>

#include <asm/mach-types.h>
#include <asm/memblock.h>
#include <asm/prom.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/tlb.h>
#include <asm/fixmap.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "mm.h"

static phys_addr_t phys_initrd_start __initdata = 0;
static unsigned long phys_initrd_size __initdata = 0;

static int __init early_initrd(char *p)
{
	phys_addr_t start;
	unsigned long size;
	char *endp;

	start = memparse(p, &endp);
	if (*endp == ',') {
		size = memparse(endp + 1, NULL);

		phys_initrd_start = start;
		phys_initrd_size = size;
	}
	return 0;
}
early_param("initrd", early_initrd);

static int __init parse_tag_initrd(const struct tag *tag)
{
	printk(KERN_WARNING "ATAG_INITRD is deprecated; "
		"please update your bootloader.\n");
	phys_initrd_start = __virt_to_phys(tag->u.initrd.start);
	phys_initrd_size = tag->u.initrd.size;
	return 0;
}

__tagtable(ATAG_INITRD, parse_tag_initrd);

static int __init parse_tag_initrd2(const struct tag *tag)
{
	phys_initrd_start = tag->u.initrd.start;
	phys_initrd_size = tag->u.initrd.size;
	return 0;
}

__tagtable(ATAG_INITRD2, parse_tag_initrd2);

#ifdef CONFIG_OF_FLATTREE
void __init early_init_dt_setup_initrd_arch(unsigned long start, unsigned long end)
{
	phys_initrd_start = start;
	phys_initrd_size = end - start;
}
#endif /* CONFIG_OF_FLATTREE */

/*
 * This keeps memory configuration data used by a couple memory
 * initialization functions, as well as show_mem() for the skipping
 * of holes in the memory map.  It is populated by arm_add_memory().
 */
struct meminfo meminfo;

void show_mem(unsigned int filter)
{
	int free = 0, total = 0, reserved = 0;
	int shared = 0, cached = 0, slab = 0, i;
	struct meminfo * mi = &meminfo;

	printk("Mem-info:\n");
	show_free_areas(filter);

	if (filter & SHOW_MEM_FILTER_PAGE_COUNT)
		return;

	for_each_bank (i, mi) {
		struct membank *bank = &mi->bank[i];
		unsigned int pfn1, pfn2;
		struct page *page, *end;

		pfn1 = bank_pfn_start(bank);
		pfn2 = bank_pfn_end(bank);

		page = pfn_to_page(pfn1);
		end  = pfn_to_page(pfn2 - 1) + 1;

		do {
			total++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (PageSlab(page))
				slab++;
			else if (!page_count(page))
				free++;
			else
				shared += page_count(page) - 1;
			page++;
		} while (page < end);
	}

	printk("%d pages of RAM\n", total);
	printk("%d free pages\n", free);
	printk("%d reserved pages\n", reserved);
	printk("%d slab pages\n", slab);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
}

static void __init find_limits(unsigned long *min, unsigned long *max_low,
			       unsigned long *max_high)
{
	struct meminfo *mi = &meminfo;
	int i;

	/* This assumes the meminfo array is properly sorted */
	/*! 20131102 pfn : page frame number */
	*min = bank_pfn_start(&mi->bank[0]);
	for_each_bank (i, mi)
	/*! 20131102 for (iter = 0; i < (mi)->nr_banks; i++) */
		if (mi->bank[i].highmem)
				break;
	/*! 20131102 max_low: lowmem의 끝주소 = highmem의 시작주소 */
	*max_low = bank_pfn_end(&mi->bank[i - 1]);
	/*! 20131102 max_high: highmem의 끝주소 */
	*max_high = bank_pfn_end(&mi->bank[mi->nr_banks - 1]);
}

/*! 20131109 boot memory에 대한 bitmap과 pgdata->bdata 자료구조를 초기화한다. */
static void __init arm_bootmem_init(unsigned long start_pfn,
	unsigned long end_pfn)
{
	struct memblock_region *reg;
	unsigned int boot_pages;
	phys_addr_t bitmap;
	pg_data_t *pgdat;

	/*
	 * Allocate the bootmem bitmap page.  This must be in a region
	 * of memory which has already been mapped.
	 */
	 /*! 20131102 boot_pages: 6 으로 표현 가능 */
	boot_pages = bootmem_bootmap_pages(end_pfn - start_pfn);
	/*! 20131102 boot_pages 만큼 align하여 메모리 할당, L1_CACHE_BYTES: 64 */
	bitmap = memblock_alloc_base(boot_pages << PAGE_SHIFT, L1_CACHE_BYTES,
				__pfn_to_phys(end_pfn));

	/*
	 * Initialise the bootmem allocator, handing the
	 * memory banks over to bootmem.
	 */
	node_set_online(0);
	/*! 20131109 node가 1개이기(NUMA가 아니기) 때문에 아무런 역할을 하지 않는다. */
	pgdat = NODE_DATA(0);
	/*! 20131109 &contig_page_data: 전역으로 선언된 zone변수를 포인터로 리턴한다. */
	init_bootmem_node(pgdat, __phys_to_pfn(bitmap), start_pfn, end_pfn);
	/*! 20131109 lowmem 영역을 bootmem 영역에 등록 및 초기화 */

	/* Free the lowmem regions from memblock into bootmem. */
	/*! 20131109 현재 memblock의 memory는 merge되어서 1개밖에 없다. */
	for_each_memblock(memory, reg) {
	/*! 20131109
	 * for (region = memblock.memblock_type.regions;				\
	 * region < (memblock.memblock_type.regions + memblock.memblock_type.cnt);	\
	 * region++)
	 */
		unsigned long start = memblock_region_memory_base_pfn(reg);
		unsigned long end = memblock_region_memory_end_pfn(reg);

		/*! 20131109 전체 memblock을 가져와서
		  * lowmem을 초과하면 end를 lowmem으로 가리킨다.
		  */
		if (end >= end_pfn)
			end = end_pfn;
		if (start >= end)
			break;

		free_bootmem(__pfn_to_phys(start), (end - start) << PAGE_SHIFT);
	}

	/* Reserve the lowmem memblock reserved regions in bootmem. */
	for_each_memblock(reserved, reg) {
		unsigned long start = memblock_region_reserved_base_pfn(reg);
		unsigned long end = memblock_region_reserved_end_pfn(reg);

		if (end >= end_pfn)
			end = end_pfn;
		if (start >= end)
			break;

		/*! 20131109 해당 bootmem을 reserve */
		reserve_bootmem(__pfn_to_phys(start),
			        (end - start) << PAGE_SHIFT, BOOTMEM_DEFAULT);
	}
}

#ifdef CONFIG_ZONE_DMA

unsigned long arm_dma_zone_size __read_mostly;
EXPORT_SYMBOL(arm_dma_zone_size);

/*
 * The DMA mask corresponding to the maximum bus address allocatable
 * using GFP_DMA.  The default here places no restriction on DMA
 * allocations.  This must be the smallest DMA mask in the system,
 * so a successful GFP_DMA allocation will always satisfy this.
 */
phys_addr_t arm_dma_limit;

static void __init arm_adjust_dma_zone(unsigned long *size, unsigned long *hole,
	unsigned long dma_size)
{
	if (size[0] <= dma_size)
		return;

	size[ZONE_NORMAL] = size[0] - dma_size;
	size[ZONE_DMA] = dma_size;
	hole[ZONE_NORMAL] = hole[0];
	hole[ZONE_DMA] = 0;
}
#endif

void __init setup_dma_zone(struct machine_desc *mdesc)
{
#ifdef CONFIG_ZONE_DMA
	if (mdesc->dma_zone_size) {
		arm_dma_zone_size = mdesc->dma_zone_size;
		arm_dma_limit = PHYS_OFFSET + arm_dma_zone_size - 1;
	} else
		arm_dma_limit = 0xffffffff;
#endif
}

static void __init arm_bootmem_free(unsigned long min, unsigned long max_low,
	unsigned long max_high)
{
	/*! 20131130
	 * min: memory 시작주소의 PFN(0x20000)
	 * max_low: lowmem의 끝주소의 PFN, 0x4F800 (size:760M)
	 * max_high: highmem의 끝주소의 PFN, 0xA0000
	 */
	unsigned long zone_size[MAX_NR_ZONES], zhole_size[MAX_NR_ZONES];
	/*! 20131130 MAX_NR_ZONES: 3 */
	struct memblock_region *reg;

	/*
	 * initialise the zones.
	 */
	memset(zone_size, 0, sizeof(zone_size));

	/*
	 * The memory size has already been determined.  If we need
	 * to do anything fancy with the allocation of this memory
	 * to the zones, now is the time to do it.
	 */
	zone_size[0] = max_low - min;
#ifdef CONFIG_HIGHMEM
	zone_size[ZONE_HIGHMEM] = max_high - max_low;
	/*! 20131130 highmem을 위한 zone 영역 나눔. ZONE_HIGHMEM: 1 */
#endif

	/*
	 * Calculate the size of the holes.
	 *  holes = node_size - sum(bank_sizes)
	 */
	memcpy(zhole_size, zone_size, sizeof(zhole_size));
	for_each_memblock(memory, reg) {
	/*! 20131130
	 * for (reg = memory.reg;			\
	 *      reg < (memory.reg + memory.cnt);	\
	 *      reg++)
	 */
		unsigned long start = memblock_region_memory_base_pfn(reg);
		unsigned long end = memblock_region_memory_end_pfn(reg);
		/*! 20131130 현재 가능한 공간의 시작과 끝 PFN을 start, end 에 할당 */
		/*! 20131130
		 * start: 0x20000, end: 0xA0000
		 */

		if (start < max_low) {
			unsigned long low_end = min(end, max_low);
			/*! 20131130 low_end: low_mem 의 끝주소 (0x4f800) */
			zhole_size[0] -= low_end - start;
			/*! 20131130 zhole_size[0]: max_low - min - (low_end - start) = 0 */
		}
#ifdef CONFIG_HIGHMEM
		if (end > max_low) {
			unsigned long high_start = max(start, max_low);
			/*! 20131130 high_start: 0x4f800 */
			zhole_size[ZONE_HIGHMEM] -= end - high_start;
			/*! 20131130 zhole_size[1]: max_high - max_low - (end - high_start) = 0 */
		}
#endif
		/*! 20131130 memblock 이 여러개일 경우에 for문 돌면서 hole을 찾는 것*/
	}

#ifdef CONFIG_ZONE_DMA
	/*
	 * Adjust the sizes according to any special requirements for
	 * this machine type.
	 */
	if (arm_dma_zone_size)
		arm_adjust_dma_zone(zone_size, zhole_size,
			arm_dma_zone_size >> PAGE_SHIFT);
#endif

	free_area_init_node(0, zone_size, min, zhole_size);
}

#ifdef CONFIG_HAVE_ARCH_PFN_VALID
int pfn_valid(unsigned long pfn)
{
	return memblock_is_memory(__pfn_to_phys(pfn));
	/*! 20131207 pfn이 유효한지 확인 */
}
EXPORT_SYMBOL(pfn_valid);
#endif

#ifndef CONFIG_SPARSEMEM
static void __init arm_memory_present(void)
{
}
#else
static void __init arm_memory_present(void)
{
	struct memblock_region *reg;

	for_each_memblock(memory, reg)
	/*! 20131123
	 * for (region = memblock.memblock_type.regions;				\
	 * region < (memblock.memblock_type.regions + memblock.memblock_type.cnt);	\
	 * region++)
	 */
		memory_present(0, memblock_region_memory_base_pfn(reg),
			       memblock_region_memory_end_pfn(reg));
	/*! 20131123
	 * mem_block에 등록되어 있는 메모리 영역을 sparse로 관리할 수 있도록
	 * sparse mem_section 단위로 존재함을 표시. (256MiB, SECTION_SIZE_BITS: 28)
	 * (start ~ end 까지 1로 표시, 초기화를 다 한 건 아님)
	 */
}
#endif

static bool arm_memblock_steal_permitted = true;

phys_addr_t __init arm_memblock_steal(phys_addr_t size, phys_addr_t align)
{
	phys_addr_t phys;

	BUG_ON(!arm_memblock_steal_permitted);
	/*! 20131005 arm_memblock_steal_permitted이 false 이면 패닉 발생 */

	phys = memblock_alloc_base(size, align, MEMBLOCK_ALLOC_ANYWHERE);
	memblock_free(phys, size);
	memblock_remove(phys, size);

	return phys;
}

/*! 20131109 arm_memblock_init(&meminfo, mdesc); */
void __init arm_memblock_init(struct meminfo *mi, struct machine_desc *mdesc)
{
	int i;

	/*! 20130914 현재 nr_banks = 2 로 본다.  */
	for (i = 0; i < mi->nr_banks; i++)
		memblock_add(mi->bank[i].start, mi->bank[i].size);
		/*! 20130928
		 * 두개의 bank에 대한 memblock을 추가한다. 
		 * bank는 두개로 나누고 memblock은 합친다. 
		 * 2013/09/28 여기까지!
		 */

	/* Register the kernel text, kernel data and initrd with memblock. */
#ifdef CONFIG_XIP_KERNEL
	memblock_reserve(__pa(_sdata), _end - _sdata);
#else
	/*! 20130907 여기 실행 */
	memblock_reserve(__pa(_stext), _end - _stext);
	/*! 20131005 
	 * _stext: 0xC0008000 
	 * __pa(_stext): 0x20008000 
	 * _end - _stext = 커널의 크기
	 * 커널 코드영역을 memblock의 reserved region에 추가한다.
	 */
#endif

#ifdef CONFIG_BLK_DEV_INITRD 
	/*! 20130907 여기 실행 */
	if (phys_initrd_size &&
	    !memblock_is_region_memory(phys_initrd_start, phys_initrd_size)) {
		/*! 20131005 부트로더에서 전달받은 initrd 주소가 memblock.memory에 속해 있는지 확인 */
		pr_err("INITRD: 0x%08llx+0x%08lx is not a memory region - disabling initrd\n",
		       (u64)phys_initrd_start, phys_initrd_size);
		phys_initrd_start = phys_initrd_size = 0;
		/*! 20131005 만약 유효하지 않은 값이면 에러로그 출력하고 0으로 초기화한다.  */
	}
	if (phys_initrd_size &&
	    memblock_is_region_reserved(phys_initrd_start, phys_initrd_size)) {
		pr_err("INITRD: 0x%08llx+0x%08lx overlaps in-use memory region - disabling initrd\n",
		       (u64)phys_initrd_start, phys_initrd_size);
		phys_initrd_start = phys_initrd_size = 0;
		/*! 20131005
		 * 등록하려는 부분이 reserved가 아니어야 한다.
		 * 등록하려는 부분이 겹치면 에러로그 출력하고 0으로 초기화한다.
		 */
	}
	if (phys_initrd_size) {
		memblock_reserve(phys_initrd_start, phys_initrd_size);
		/*! 20131005 reserved region에 initrd 영역을 추가한다. */

		/* Now convert initrd to virtual addresses */
		initrd_start = __phys_to_virt(phys_initrd_start);
		initrd_end = initrd_start + phys_initrd_size;
		/*! 20131005 initrd_start와 initrd_end 를 virtual address로 변환하여 셋팅
		  */
	}
#endif

	arm_mm_memblock_reserve();
	/*! 20131005 
	 * swapper_pg_dir (the virtual address of the initial page table.)에서
	 * 16k만큼 reserved로 마킹한다.
	 */
	arm_dt_memblock_reserve();
	/*! 20131005
	 * device tree 에서 정의한 reserved의 영역을 reserve region에 추가한다.
	 */

	/* reserve any platform specific memblock areas */
	if (mdesc->reserve)
		mdesc->reserve();
	/*! 20131005
	 * exynos5_reserve() 함수 실행한다. ( .reserve = exynos5_reserve,)
	 * dtb file에서 지정한 mfc 관련 reserve영역을 찾아 reserve 영역에 추가한다.
	 */

	/*
	 * reserve memory for DMA contigouos allocations,
	 * must come from DMA area inside low memory
	 */
	dma_contiguous_reserve(min(arm_dma_limit, arm_lowmem_limit));
	/*! 20131005
	 * arm_dma_limit = ((phys_addr_t)~0) = 0xFFFFFFFF
	 * arm_lowmem_limit = bank_end;
	 * CONFIG_CMA_SIZE_~~ 상수들이 정의되지 않아 아무일도 안한다.
	 * 
	 * boot parameter에 cma= 옵션이 들어올 때는 cma가 활성화 된다.
	 * 자세한 사항은 아래 두 파일을 참조
	 * Documentation/kernel-parameters.txt
	 * include/linux/dma-contiguous.h
	 */

	arm_memblock_steal_permitted = false;
	/*! 20131005 memblock에서 해당영역을 지우지 못하게 하는 것 */
	memblock_allow_resize();
	/*! 20131005
	 * memblock_can_resize = 1;
	 * mm/memblock.c의 memblock_double_array에서 double array로의 확장이 가능해진다.
	 */
	memblock_dump_all();
	/*! 20131005 여지까지 했던 memblock 작업의 결과를 모두 출력한다.  */
}

void __init bootmem_init(void)
{
	unsigned long min, max_low, max_high;

	max_low = max_high = 0;

	/*! 20131102 
	 * min: memory 시작주소, max_low: lowmem의 끝주소에 해당하는 pfn, 
	 * max_high: highmem의 끝주소 설정
	 */
	find_limits(&min, &max_low, &max_high);

	/*! 20131109 boot memory에 대한 bitmap과 pgdata->bdata 자료구조를 초기화한다. */
	arm_bootmem_init(min, max_low);
	/*! 20131109 오늘 여기까지 */

	/*
	 * Sparsemem tries to allocate bootmem in memory_present(),
	 * so must be done after the fixed reservations
	 */
	arm_memory_present();
	/*! 20131123
	 * mem_block에 등록되어 있는 메모리 영역을 sparse로 관리할 수 있도록 
	 * sparse mem_section 단위(256MiB)로 존재함을 표시
	 * 0x20000000 번지 ~ 0xA0000000 번지 까지 표시
	 */

	/*
	 * sparse_init() needs the bootmem allocator up and running.
	 */
	sparse_init();
	/*! 20131130 전체 메모리 공간을 section 단위로 나누어서 존재하는 section 별 메모리존재 여부 & usemap 초기화 */

	/*
	 * Now free the memory - free_area_init_node needs
	 * the sparse mem_map arrays initialized by sparse_init()
	 * for memmap_init_zone(), otherwise all PFNs are invalid.
	 */
	arm_bootmem_free(min, max_low, max_high);
	/*! 20131214 zone 구조체 초기화, zone별 page 초기화 및 사용가능으로 셋팅 */

	/*
	 * This doesn't seem to be used by the Linux memory manager any
	 * more, but is used by ll_rw_block.  If we can get rid of it, we
	 * also get rid of some of the stuff above as well.
	 *
	 * Note: max_low_pfn and max_pfn reflect the number of _pages_ in
	 * the system, not the maximum PFN.
	 */
	max_low_pfn = max_low - PHYS_PFN_OFFSET;
	/*! 20131214 PHYS_PFN_OFFSET: 0x00020000 */
	/*! 20131214 max_low_pfn: low mem에 해당하는 page의 갯수 */
	max_pfn = max_high - PHYS_PFN_OFFSET;
	/*! 20131214 max_pfn: highmem을 포함한 전체 page의 갯수 */
}

/*
 * Poison init memory with an undefined instruction (ARM) or a branch to an
 * undefined instruction (Thumb).
 */
static inline void poison_init_mem(void *s, size_t count)
{
	u32 *p = (u32 *)s;
	for (; count != 0; count -= 4)
		*p++ = 0xe7fddef0;
}

static inline void
free_memmap(unsigned long start_pfn, unsigned long end_pfn)
{
	struct page *start_pg, *end_pg;
	phys_addr_t pg, pgend;

	/*
	 * Convert start_pfn/end_pfn to a struct page pointer.
	 */
	start_pg = pfn_to_page(start_pfn - 1) + 1;
	/*! 20140315 struct page 의 시작주소 */
	end_pg = pfn_to_page(end_pfn - 1) + 1;

	/*
	 * Convert to physical addresses, and
	 * round start upwards and end downwards.
	 */
	pg = PAGE_ALIGN(__pa(start_pg));
	pgend = __pa(end_pg) & PAGE_MASK;

	/*
	 * If there are free pages between these,
	 * free the section of the memmap array.
	 */
	if (pg < pgend)
		free_bootmem(pg, pgend - pg);
	/*! 20140315 pgend - pg 만큼의 메모리를 free한다. */
}

/*
 * The mem_map array can get very big.  Free the unused area of the memory map.
 */
static void __init free_unused_memmap(struct meminfo *mi)
{
	unsigned long bank_start, prev_bank_end = 0;
	unsigned int i;

	/*
	 * This relies on each bank being in address order.
	 * The banks are sorted previously in bootmem_init().
	 */
	for_each_bank(i, mi) {
		/*! 20140309 lowmem, highmem 두번 for문 수행 */
		struct membank *bank = &mi->bank[i];

		bank_start = bank_pfn_start(bank);

#ifdef CONFIG_SPARSEMEM
		/*
		 * Take care not to free memmap entries that don't exist
		 * due to SPARSEMEM sections which aren't present.
		 */
		bank_start = min(bank_start,
				 ALIGN(prev_bank_end, PAGES_PER_SECTION));
		/*! 20140309 
		 * PAGES_PER_SECTION=1<<16
		 * ALIGN 은 올림 후 정렬하게된다
		 * bank_start 결정
		 */
#else
		/*
		 * Align down here since the VM subsystem insists that the
		 * memmap entries are valid from the bank start aligned to
		 * MAX_ORDER_NR_PAGES.
		 */
		bank_start = round_down(bank_start, MAX_ORDER_NR_PAGES);
#endif
		/*
		 * If we had a previous bank, and there is a space
		 * between the current bank and the previous, free it.
		 */
		if (prev_bank_end && prev_bank_end < bank_start)
			free_memmap(prev_bank_end, bank_start);

		/*
		 * Align up here since the VM subsystem insists that the
		 * memmap entries are valid from the bank end aligned to
		 * MAX_ORDER_NR_PAGES.
		 */
		prev_bank_end = ALIGN(bank_pfn_end(bank), MAX_ORDER_NR_PAGES);
		/*! 20140309 MAX_ORDER_NR_PAGES = 1024, bank의 끝 pfn 값을 1024로 align */
	}

#ifdef CONFIG_SPARSEMEM
	if (!IS_ALIGNED(prev_bank_end, PAGES_PER_SECTION))
		/*! 20140309 prev_bank_end 이 1<<16으로 Align 되어있지 않으면 아래 수행 */
		/*! 20140309 스터디 여기까지 */
		free_memmap(prev_bank_end,
			    ALIGN(prev_bank_end, PAGES_PER_SECTION));
	/*! 20140315
	 * PAGES_PER_SECTION = 64k
	 * prev_bank_end ~ (64k align 된 prev_bank_end) 만큼의 메모리를 free한다.
	 */
#endif
}

#ifdef CONFIG_HIGHMEM
static inline void free_area_high(unsigned long pfn, unsigned long end)
{
	for (; pfn < end; pfn++)
		free_highmem_page(pfn_to_page(pfn));
	/*! 20140405 highmem page의 reserved가 아닌 공간을 모두 free시켜서 buddy시스템에 넣는다. */
}
#endif

static void __init free_highpages(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long max_low = max_low_pfn + PHYS_PFN_OFFSET;
	struct memblock_region *mem, *res;

	/* set highmem page free */
	for_each_memblock(memory, mem) {
	/*! 20140329 for (mem = memblock.memory.regions;
	 *		mem < (memblock.memory.regions + memblock.memory.cnt);
	 *		mem++)
	 */
		unsigned long start = memblock_region_memory_base_pfn(mem);
		unsigned long end = memblock_region_memory_end_pfn(mem);
		/*! 20140329  memblock의 시작주소와 끝주소에 해당하는 pfn을 start, end 에 할당한다. */

		/* Ignore complete lowmem entries */
		if (end <= max_low)
			continue;

		/* Truncate partial highmem entries */
		if (start < max_low)
			start = max_low;

		/* Find and exclude any reserved regions */
		for_each_memblock(reserved, res) {
		/*! 20140329 for (res = memblock.reserved.regions;
		 *		res < (memblock.reserved.regions + memblock.reserved.cnt);
		 *		res++)
		 */
			unsigned long res_start, res_end;

			res_start = memblock_region_reserved_base_pfn(res);
			res_end = memblock_region_reserved_end_pfn(res);
			/*! 20140329 reserved의 시작주소와 끝주소에 해당하는 pfn을 res_start, res_end 에 할당한다. */

			if (res_end < start)
				/*! 20140329 res_end가 start보다 작으면 영역의 앞부분이므로 pass */
				continue;
			if (res_start < start)
				/*! 20140329 res_start가 start보다 작으면 res_start = start */
				res_start = start;
			if (res_start > end)
				/*! 20140329 res_start가 start보다 크면 영역의 뒷부분이므로 res_start = end */
				res_start = end;
			if (res_end > end)
				res_end = end;
			if (res_start != start)
				/*! 20140329 resered 사이의 free 공간만 buddy의 free list에 추가한다. */
				free_area_high(start, res_start);
			start = res_end;
			if (start == end)
				break;
		}

		/* And now free anything which remains */
		if (start < end)
			free_area_high(start, end);
		/*! 20140405 남아있는 memory의 free 공간을 buddy의 free list에 추가한다. */
	}
#endif
}

/*
 * mem_init() marks the free areas in the mem_map and tells us how much
 * memory is free.  This is done after various parts of the system have
 * claimed their memory after the kernel image.
 */
void __init mem_init(void)
{
#ifdef CONFIG_HAVE_TCM
	/* These pointers are filled in on TCM detection */
	extern u32 dtcm_end;
	extern u32 itcm_end;
#endif

	max_mapnr   = pfn_to_page(max_pfn + PHYS_PFN_OFFSET) - mem_map;
	/*! 20140309 
	 * memmap의 마지막 요소의 page 구조체 시작주소:  pfn_to_page(max_pfn + PHYS_PFN_OFFSET)
	 * PHYS_PFN_OFFSET: 0x00020000
	 * mem_map: 0 (TODO: 맞는값인지 추후 확인 필요!!)
	 * max_mapnr: 전체 물리 page의 개수
	 */

	/* this will put all unused low memory onto the freelists */
	/*! 20140309
		struct meminfo {
			int nr_banks;
			struct membank bank[NR_BANKS];
		};
	 */
	free_unused_memmap(&meminfo);
	/*! 20140315 사용하지 않는 공간을 위해 할당했던 mem_map(struct page) 공간을 free한다. */
	free_all_bootmem();
	/*! 20140329 할당되지 않은 bootmem page를 모두 buddy 시스템으로 전환한다. */

#ifdef CONFIG_SA1111
	/* now that our DMA memory is actually so designated, we can free it */
	free_reserved_area(__va(PHYS_OFFSET), swapper_pg_dir, -1, NULL);
#endif

	free_highpages();
	/*! 20140405 highmem 영역의 모든 free공간을 buddy의 free list에 추가한다.  */

	mem_init_print_info(NULL);
	/*! 20140405 지금까지 초기화한 memory 정보를 출력한다. */

#define MLK(b, t) b, t, ((t) - (b)) >> 10
#define MLM(b, t) b, t, ((t) - (b)) >> 20
#define MLK_ROUNDUP(b, t) b, t, DIV_ROUND_UP(((t) - (b)), SZ_1K)

	printk(KERN_NOTICE "Virtual kernel memory layout:\n"
			"    vector  : 0x%08lx - 0x%08lx   (%4ld kB)\n"
#ifdef CONFIG_HAVE_TCM
/*! 20140405 실행안됨. (TCM: Tightly-coupled memory) */
			"    DTCM    : 0x%08lx - 0x%08lx   (%4ld kB)\n"
			"    ITCM    : 0x%08lx - 0x%08lx   (%4ld kB)\n"
#endif
			"    fixmap  : 0x%08lx - 0x%08lx   (%4ld kB)\n"
			"    vmalloc : 0x%08lx - 0x%08lx   (%4ld MB)\n"
			"    lowmem  : 0x%08lx - 0x%08lx   (%4ld MB)\n"
#ifdef CONFIG_HIGHMEM
			"    pkmap   : 0x%08lx - 0x%08lx   (%4ld MB)\n"
#endif
#ifdef CONFIG_MODULES
			"    modules : 0x%08lx - 0x%08lx   (%4ld MB)\n"
#endif
			"      .text : 0x%p" " - 0x%p" "   (%4d kB)\n"
			"      .init : 0x%p" " - 0x%p" "   (%4d kB)\n"
			"      .data : 0x%p" " - 0x%p" "   (%4d kB)\n"
			"       .bss : 0x%p" " - 0x%p" "   (%4d kB)\n",

			MLK(UL(CONFIG_VECTORS_BASE), UL(CONFIG_VECTORS_BASE) +
				(PAGE_SIZE)),
#ifdef CONFIG_HAVE_TCM
			MLK(DTCM_OFFSET, (unsigned long) dtcm_end),
			MLK(ITCM_OFFSET, (unsigned long) itcm_end),
#endif
			MLK(FIXADDR_START, FIXADDR_TOP),
			MLM(VMALLOC_START, VMALLOC_END),
			MLM(PAGE_OFFSET, (unsigned long)high_memory),
#ifdef CONFIG_HIGHMEM
			MLM(PKMAP_BASE, (PKMAP_BASE) + (LAST_PKMAP) *
				(PAGE_SIZE)),
#endif
#ifdef CONFIG_MODULES
			MLM(MODULES_VADDR, MODULES_END),
#endif

			MLK_ROUNDUP(_text, _etext),
			MLK_ROUNDUP(__init_begin, __init_end),
			MLK_ROUNDUP(_sdata, _edata),
			MLK_ROUNDUP(__bss_start, __bss_stop));

#undef MLK
#undef MLM
#undef MLK_ROUNDUP

	/*
	 * Check boundaries twice: Some fundamental inconsistencies can
	 * be detected at build time already.
	 */
#ifdef CONFIG_MMU
	BUILD_BUG_ON(TASK_SIZE				> MODULES_VADDR);
	BUG_ON(TASK_SIZE 				> MODULES_VADDR);
#endif

#ifdef CONFIG_HIGHMEM
	BUILD_BUG_ON(PKMAP_BASE + LAST_PKMAP * PAGE_SIZE > PAGE_OFFSET);
	BUG_ON(PKMAP_BASE + LAST_PKMAP * PAGE_SIZE	> PAGE_OFFSET);
	/*! 20140405 PKMAP_BASE:3G-2M LAST_PKMAP:512, PAGE_SIZE:4k, PAGE_OFFSET:0xC0000000 이어야 한다. */
#endif

	if (PAGE_SIZE >= 16384 && get_num_physpages() <= 128) {
		extern int sysctl_overcommit_memory;
		/*
		 * On a machine this small we won't get
		 * anywhere without overcommit, so turn
		 * it on by default.
		 */
		sysctl_overcommit_memory = OVERCOMMIT_ALWAYS;
		/*! 20140405 OVERCOMMIT_ALWAYS: 1, page갯수가 적을 경우 할당못할 수 있으므로 메모리 있는 경우  */
		/*! 20140405 OVERCOMMIT
		 * 0 : heuristic에 따라 overcommit 할 수 있고, 하지 않을 수도 있다.(디폴트) 
		 *     메모리 요구가 있을 때 여유 공간이 없는 경우 실행중인 프로세스를 강제 종료 메모리를 억지로 확보함.
		 * 1 : 항상 overcommit 함. 메모리를 다 사용했는데도 충분한 메모리가있는 것처럼 처리 됨. 그 외에는 0과 같음.
		 * 2 : overcommit하지 않음. 메모리가 부족할 경우 메모리 확보시 에러 발생시킴.
		 * (참고 http://mimul.com/pebble/default/2013/05/10/1368171783727.html)
		 */
	}
}

void free_initmem(void)
{
#ifdef CONFIG_HAVE_TCM
	extern char __tcm_start, __tcm_end;

	poison_init_mem(&__tcm_start, &__tcm_end - &__tcm_start);
	free_reserved_area(&__tcm_start, &__tcm_end, -1, "TCM link");
#endif

	poison_init_mem(__init_begin, __init_end - __init_begin);
	if (!machine_is_integrator() && !machine_is_cintegrator())
		free_initmem_default(-1);
}

#ifdef CONFIG_BLK_DEV_INITRD

static int keep_initrd;

void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (!keep_initrd) {
		poison_init_mem((void *)start, PAGE_ALIGN(end) - start);
		free_reserved_area((void *)start, (void *)end, -1, "initrd");
	}
}

static int __init keepinitrd_setup(char *__unused)
{
	keep_initrd = 1;
	return 1;
}

__setup("keepinitrd", keepinitrd_setup);
#endif
