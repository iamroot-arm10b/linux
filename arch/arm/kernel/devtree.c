/*
 *  linux/arch/arm/kernel/devtree.c
 *
 *  Copyright (C) 2009 Canonical Ltd. <jeremy.kerr@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/cputype.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/smp_plat.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	arm_add_memory(base, size);
	/*! 20130831
	 * base : 0x20000000
	 * size : 0x80000000
	 * memory bank 정보를 기록한다.
	 */
}

/*! 20140104 bootmem에서 device tree를 위한 memory 할당받음 */
void * __init early_init_dt_alloc_memory_arch(u64 size, u64 align)
{
	return alloc_bootmem_align(size, align);
}

void __init arm_dt_memblock_reserve(void)
{
	u64 *reserve_map, base, size;

	if (!initial_boot_params)
		/*! 20131005 device tree 영역이 없으면 리턴 */
		return;

	/* Reserve the dtb region */
	memblock_reserve(virt_to_phys(initial_boot_params),
			 be32_to_cpu(initial_boot_params->totalsize));
	/*! 20131005
	 * be32_to_cpu: big endian을 cpu의 endian으로 바꾸어준다.
	 * device tree 영역을 reserved로 표시한다.
	 */

	/*
	 * Process the reserve map.  This will probably overlap the initrd
	 * and dtb locations which are already reserved, but overlaping
	 * doesn't hurt anything
	 */
	reserve_map = ((void*)initial_boot_params) +
			be32_to_cpu(initial_boot_params->off_mem_rsvmap);
	/*! 20131005
	 * off_mem_rsvmap : 메모리 reserve map의 offset
	 * device tree 에서 정의한 reserved의 영역을 reserve region에 추가한다.
	 */
	while (1) {
		base = be64_to_cpup(reserve_map++);
		size = be64_to_cpup(reserve_map++);
		if (!size)
			break;
		memblock_reserve(base, size);
	}
}

/*
 * arm_dt_init_cpu_maps - Function retrieves cpu nodes from the device tree
 * and builds the cpu logical map array containing MPIDR values related to
 * logical cpus
 *
 * Updates the cpu possible mask with the number of parsed cpu nodes
 */
/*! 20140104
 * cpus device node 자식의 cpu device node의 reg property 값을 가지고
 * dtb 유효성 검사 후 __cpu_logical_map 초기화
 */
void __init arm_dt_init_cpu_maps(void)
{
	/*
	 * Temp logical map is initialized with UINT_MAX values that are
	 * considered invalid logical map entries since the logical map must
	 * contain a list of MPIDR[23:0] values where MPIDR[31:24] must
	 * read as 0.
	 */
	struct device_node *cpu, *cpus;
	u32 i, j, cpuidx = 1;
	/*! 20140104 mpidr[23:0] 값 추출 / MPIDR_HWID_BITMASK 0xFFFFFF */
	u32 mpidr = is_smp() ? read_cpuid_mpidr() & MPIDR_HWID_BITMASK : 0;

	/*! 20140104
	 * 0 ~ NR_CPUS(4)-1 까지의 Index에 MPIDR_INVALID(0xFF000000) 을 삽입
	 * - gcc manual -
	 * To initialize a range of elements to the same value, 
	 * write '[FIRST ...LAST] = VALUE'.  This is a GNU extension.  For example,
	 * ex) int widths[] = { [0 ... 9] = 1, [10 ... 99] = 2, [100] = 3 };
	 */
	u32 tmp_map[NR_CPUS] = { [0 ... NR_CPUS-1] = MPIDR_INVALID };
	bool bootcpu_valid = false;
	/*! 20140104 cpus device node를 찾음 */
	cpus = of_find_node_by_path("/cpus");

	if (!cpus)
		return;

	/*! 20140104
	 * 
	  cpus {
		#address-cells = <0x1>;
		#size-cells = <0x0>;

		cpu@0 {
			device_type = "cpu";
			compatible = "arm,cortex-a15";
			reg = <0x0>;
			clock-frequency = <0x6b49d200>;
		};
	
		cpu@1 {
			device_type = "cpu";
			compatible = "arm,cortex-a15";
			reg = <0x1>;
			clock-frequency = <0x6b49d200>;
		};
		...생략
	 };
	 */
	/*! 20140104
	 * parent = cpus / child = cpu (child 는 각 cpus device node의 cpu device node를 나타냄)
	 * of_get_next_child는 parent의 자식에 대한 device node 또는 자식의 sibling들을 반환 
	 * - for_each_child_of_node -
	 * for (child = of_get_next_child(parent, NULL); child != NULL; 
	 *    child = of_get_next_child(parent, child))
	 */
	for_each_child_of_node(cpus, cpu) {
		u32 hwid;

		/*! 20140104 cpu device node의 type이 'cpu'인지 확인 */
		if (of_node_cmp(cpu->type, "cpu"))
			continue;

		pr_debug(" * %s...\n", cpu->full_name);
		/*
		 * A device tree containing CPU nodes with missing "reg"
		 * properties is considered invalid to build the
		 * cpu_logical_map.
		 */
		/*! 20140104 cpu device node의 reg property의 값을 hwid에 할당 */
		if (of_property_read_u32(cpu, "reg", &hwid)) {
			pr_debug(" * %s missing reg property\n",
				     cpu->full_name);
			return;
		}

		/*
		 * 8 MSBs must be set to 0 in the DT since the reg property
		 * defines the MPIDR[23:0].
		 */
		/*! 20140104
		 * hwid & ~MPIDR_HWID_BITMASK(0xFF000000)
		 * 상위 8bit가 설정되어 있으면 return
		 */
		if (hwid & ~MPIDR_HWID_BITMASK)
			return;

		/*
		 * Duplicate MPIDRs are a recipe for disaster.
		 * Scan all initialized entries and check for
		 * duplicates. If any is found just bail out.
		 * temp values were initialized to UINT_MAX
		 * to avoid matching valid MPIDR[23:0] values.
		 */
		/*! 20140104 동일한 reg 값이 존재하면 중복됫다고 하고 return */
		for (j = 0; j < cpuidx; j++)
			if (WARN(tmp_map[j] == hwid, "Duplicate /cpu reg "
						     "properties in the DT\n"))
				return;

		/*
		 * Build a stashed array of MPIDR values. Numbering scheme
		 * requires that if detected the boot CPU must be assigned
		 * logical id 0. Other CPUs get sequential indexes starting
		 * from 1. If a CPU node with a reg property matching the
		 * boot CPU MPIDR is detected, this is recorded so that the
		 * logical map built from DT is validated and can be used
		 * to override the map created in smp_setup_processor_id().
		 */
		/*! 20140104 hwid == mpidr 과 같으면 booting cpu 보통 첫번째 cpu, i = cpuidx */
		if (hwid == mpidr) {
			i = 0;
			bootcpu_valid = true;
		} else {
			i = cpuidx++;
		}

		/*! 20140104 Kernel config 값인 nr_cpu_ids 값보다 cpuidx가 크면 nr_cpu_ids 이상의 cpu값들은 무시됨 */
		if (WARN(cpuidx > nr_cpu_ids, "DT /cpu %u nodes greater than "
					       "max cores %u, capping them\n",
					       cpuidx, nr_cpu_ids)) {
			cpuidx = nr_cpu_ids;
			break;
		}

		tmp_map[i] = hwid;
	}

	/*! 20140104 0번 cpu가 없는 경우 정상적인 dtb가 아니라고 판단함. */
	if (!bootcpu_valid) {
		pr_warn("DT missing boot CPU MPIDR[23:0], fall back to default cpu_logical_map\n");
		return;
	}

	/*
	 * Since the boot CPU node contains proper data, and all nodes have
	 * a reg property, the DT CPU list can be considered valid and the
	 * logical map created in smp_setup_processor_id() can be overridden
	 */
	for (i = 0; i < cpuidx; i++) {
		set_cpu_possible(i, true);
		/*! 20140104 __cpu_logical_map[cpu] = tmp_map[i] */
		cpu_logical_map(i) = tmp_map[i];
		pr_debug("cpu logical map 0x%x\n", cpu_logical_map(i));
	}
}

/**
 * setup_machine_fdt - Machine setup when an dtb was passed to the kernel
 * @dt_phys: physical address of dt blob
 *
 * If a dtb was passed to the kernel in r2, then use it to choose the
 * correct machine_desc and to setup the system.
 */
struct machine_desc * __init setup_machine_fdt(unsigned int dt_phys)
{
	struct boot_param_header *devtree;
	struct machine_desc *mdesc, *mdesc_best = NULL;
	unsigned int score, mdesc_score = ~1;
	unsigned long dt_root;
	const char *model;

#ifdef CONFIG_ARCH_MULTIPLATFORM
	DT_MACHINE_START(GENERIC_DT, "Generic DT based system")
	MACHINE_END

	mdesc_best = (struct machine_desc *)&__mach_desc_GENERIC_DT;
#endif

	/*! 20130810
	 * atag 또는 dt 포인터가 없으면 리턴
	 */
	if (!dt_phys)
		return NULL;

	devtree = phys_to_virt(dt_phys);
	/*! 20130810
	 * device tree 주소를 가상주소로 변환
	 */

	/* check device tree validity */
	/*! 20130810
	 * 32-bits big endian 자료를 현재 CPU의 endian에 맞게 읽어서
	 * HEADER signature를 검사한다.
	 * Documentation/devicetree/booting-without-of.txt  참고
	 * include/linux/of_fdt.h 에 boot_param_header 구조체가 있음
	 */
	if (be32_to_cpu(devtree->magic) != OF_DT_HEADER)
		return NULL;

	/* Search the mdescs for the 'best' compatible value match */
	initial_boot_params = devtree;
	dt_root = of_get_flat_dt_root();
	/*! 20130810
	 * root node를 구했다.
	 * 2013/08/10 여기까지
	 */
	for_each_machine_desc(mdesc) {
	/*! 20130824
	 * for (p = __arch_info_begin; p < __arch_info_end; p++)
	 */
		score = of_flat_dt_match(dt_root, mdesc->dt_compat);
		/*! 20130824
		 * 일치하는 device tree 중에 가장 낮은 score 로 설정
		 */
		if (score > 0 && score < mdesc_score) {
			mdesc_best = mdesc;
			mdesc_score = score;
		}
		/*! 20130824
		 * machine descript 중에서 최적의 대상 선정
		 */
	}
	if (!mdesc_best) {
		/*! 20130824
		 * score가 0 인 경우의 예외처리
		 */
		const char *prop;
		long size;

		early_print("\nError: unrecognized/unsupported "
			    "device tree compatible list:\n[ ");

		prop = of_get_flat_dt_prop(dt_root, "compatible", &size);
		while (size > 0) {
			early_print("'%s' ", prop);
			size -= strlen(prop) + 1;
			prop += strlen(prop) + 1;
		}
		early_print("]\n\n");

		dump_machine_table(); /* does not return */
	}

	model = of_get_flat_dt_prop(dt_root, "model", NULL);
	/*! 20130824
	 * root 노드의 model 이라는 property 가져옴.
	 * 정확한 모델명 설정
	 */
	if (!model)
		model = of_get_flat_dt_prop(dt_root, "compatible", NULL);
	if (!model)
		model = "<unknown>";
	pr_info("Machine: %s, model: %s\n", mdesc_best->name, model);

	/*! 20130824
	 * of_scan_flat_dt: 모든 child 노드에 대한 콜백함수를 불러주는 함수
	 */
	/* Retrieve various information from the /chosen node */
	of_scan_flat_dt(early_init_dt_scan_chosen, boot_command_line);
	/* Initialize {size,address}-cells info */
	of_scan_flat_dt(early_init_dt_scan_root, NULL);
	/* Setup memory, calling early_init_dt_add_memory_arch */
	of_scan_flat_dt(early_init_dt_scan_memory, NULL);

	/* Change machine number to match the mdesc we're using */
	__machine_arch_type = mdesc_best->nr;

	return mdesc_best;
}
