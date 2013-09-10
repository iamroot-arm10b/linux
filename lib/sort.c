/*
 * A fast, small, non-recursive O(nlog n) sort for the Linux kernel
 *
 * Jan 23 2005  Matt Mackall <mpm@selenic.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sort.h>
#include <linux/slab.h>

static void u32_swap(void *a, void *b, int size)
{
	u32 t = *(u32 *)a;
	*(u32 *)a = *(u32 *)b;
	*(u32 *)b = t;
}

static void generic_swap(void *a, void *b, int size)
{
	char t;

	do {
		t = *(char *)a;
		*(char *)a++ = *(char *)b;
		*(char *)b++ = t;
	} while (--size > 0);
}

/**
 * sort - sort an array of elements
 * @base: pointer to data to sort
 * @num: number of elements
 * @size: size of each element
 * @cmp_func: pointer to comparison function
 * @swap_func: pointer to swap function or NULL
 *
 * This function does a heapsort on the given array. You may provide a
 * swap_func function optimized to your element type.
 *
 * Sorting time is O(n log n) both on average and worst-case. While
 * qsort is about 20% faster on average, it suffers from exploitable
 * O(n*n) worst-case behavior and extra memory requirements that make
 * it less suitable for kernel use.
 */

//sort(&meminfo.bank, meminfo.nr_banks, sizeof(meminfo.bank[0]), meminfo_cmp, NULL);
void sort(void *base, size_t num, size_t size,
	  int (*cmp_func)(const void *, const void *),
	  void (*swap_func)(void *, void *, int size))
{
	/* pre-scale counters for performance */
	int i = (num/2 - 1) * size, n = num * size, c, r;
	/*! i는 자식 node가 있는 최후의 값을 가리킨다.  */
	if (!swap_func)
		swap_func = (size == 4 ? u32_swap : generic_swap);
	/*! 크기가 4면 u32로 swap, 아니면 크기에 상관없는 범용 swap 함수를 선택 */

	/* heapify */
	/*! 20130907
	 * heapify 과정으로부모가 자식보다 큰 이진트리의 속성으로 만든다.
	 */
	for ( ; i >= 0; i -= size) {
		/*! 외부 for문부터 거꾸로 올라간다. */
		for (r = i; r * 2 + size < n; r  = c) {
			/*! 20130907 r: root, c: child 
			 * child node를 검색한다. 부모가 더 크도록 swap하며
			 * 만약 swap 되었으면 그 아래의 child들을 계속 타고 compare/swap한다.
			 */
			c = r * 2 + size;
			if (c < n - size &&
					cmp_func(base + c, base + c + size) < 0)
				c += size;
			/*! 자식 노드 둘(c,c+1)을 비교해 큰 값을 선택한다. */
			if (cmp_func(base + r, base + c) >= 0)
				break;
			/*! 부모 노드(r)가 더 크면 break하고 자식 노드(c)가 더 크면 swap한다. */
			swap_func(base + r, base + c, size);
		}
	}

	/* sort */
	for (i = n - size; i > 0; i -= size) {
		/*! 마지막부터 heap tree를 유지하며 최대값(or 최소값)으로 채워나간다. */
		swap_func(base, base + i, size);
		for (r = 0; r * 2 + size < i; r = c) {
			c = r * 2 + size;
			if (c < i - size &&
					cmp_func(base + c, base + c + size) < 0)
				c += size;
			if (cmp_func(base + r, base + c) >= 0)
				break;
			swap_func(base + r, base + c, size);
		}
	}
}

EXPORT_SYMBOL(sort);

#if 0
/* a simple boot-time regression test */

int cmpint(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static int sort_test(void)
{
	int *a, i, r = 1;

	a = kmalloc(1000 * sizeof(int), GFP_KERNEL);
	BUG_ON(!a);

	printk("testing sort()\n");

	for (i = 0; i < 1000; i++) {
		r = (r * 725861) % 6599;
		a[i] = r;
	}

	sort(a, 1000, sizeof(int), cmpint, NULL);

	for (i = 0; i < 999; i++)
		if (a[i] > a[i+1]) {
			printk("sort() failed!\n");
			break;
		}

	kfree(a);

	return 0;
}

module_init(sort_test);
#endif
