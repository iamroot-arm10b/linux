
#include <linux/device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/pci_regs.h>
#include <linux/string.h>

/* Max address size we deal with */
#define OF_MAX_ADDR_CELLS	4
#define OF_CHECK_ADDR_COUNT(na)	((na) > 0 && (na) <= OF_MAX_ADDR_CELLS)
/*! 20140809 여기 실행됨 */
#define OF_CHECK_COUNTS(na, ns)	(OF_CHECK_ADDR_COUNT(na) && (ns) > 0)

static struct of_bus *of_match_bus(struct device_node *np);
static int __of_address_to_resource(struct device_node *dev,
		const __be32 *addrp, u64 size, unsigned int flags,
		const char *name, struct resource *r);

/* Debug utility */
#ifdef DEBUG
static void of_dump_addr(const char *s, const __be32 *addr, int na)
{
	printk(KERN_DEBUG "%s", s);
	while (na--)
		printk(" %08x", be32_to_cpu(*(addr++)));
	printk("\n");
}
#else
static void of_dump_addr(const char *s, const __be32 *addr, int na) { }
#endif

/* Callbacks for bus specific translators */
struct of_bus {
	const char	*name;
	const char	*addresses;
	int		(*match)(struct device_node *parent);
	void		(*count_cells)(struct device_node *child,
				       int *addrc, int *sizec);
	u64		(*map)(__be32 *addr, const __be32 *range,
				int na, int ns, int pna);
	int		(*translate)(__be32 *addr, u64 offset, int na);
	unsigned int	(*get_flags)(const __be32 *addr);
};

/*
 * Default translator (generic bus)
 */

static void of_bus_default_count_cells(struct device_node *dev,
				       int *addrc, int *sizec)
{
	if (addrc)
		*addrc = of_n_addr_cells(dev);
		/*! 20140809 "#address-cells" 의 값을 return한다. */
	if (sizec)
		*sizec = of_n_size_cells(dev);
		/*! 20140809 "#size-cells" 의 값을 return한다. */
}

static u64 of_bus_default_map(__be32 *addr, const __be32 *range,
		int na, int ns, int pna)
{
	u64 cp, s, da;

	cp = of_read_number(range, na);
	s  = of_read_number(range + na + pna, ns);
	da = of_read_number(addr, na);

	pr_debug("OF: default map, cp=%llx, s=%llx, da=%llx\n",
		 (unsigned long long)cp, (unsigned long long)s,
		 (unsigned long long)da);

	/*
	 * If the number of address cells is larger than 2 we assume the
	 * mapping doesn't specify a physical address. Rather, the address
	 * specifies an identifier that must match exactly.
	 */
	if (na > 2 && memcmp(range, addr, na * 4) != 0)
		return OF_BAD_ADDR;

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_default_translate(__be32 *addr, u64 offset, int na)
{
	u64 a = of_read_number(addr, na);
	memset(addr, 0, na * 4);
	a += offset;
	if (na > 1)
		addr[na - 2] = cpu_to_be32(a >> 32);
	addr[na - 1] = cpu_to_be32(a & 0xffffffffu);

	return 0;
}

static unsigned int of_bus_default_get_flags(const __be32 *addr)
{
	return IORESOURCE_MEM;
	/*! 20140809 IORESOURCE_MEM: 0x00000200 */
}

#ifdef CONFIG_PCI
/*
 * PCI bus specific translator
 */

static int of_bus_pci_match(struct device_node *np)
{
	/*
	 * "vci" is for the /chaos bridge on 1st-gen PCI powermacs
	 * "ht" is hypertransport
	 */
	return !strcmp(np->type, "pci") || !strcmp(np->type, "vci") ||
		!strcmp(np->type, "ht");
}

static void of_bus_pci_count_cells(struct device_node *np,
				   int *addrc, int *sizec)
{
	if (addrc)
		*addrc = 3;
	if (sizec)
		*sizec = 2;
}

static unsigned int of_bus_pci_get_flags(const __be32 *addr)
{
	unsigned int flags = 0;
	u32 w = be32_to_cpup(addr);

	switch((w >> 24) & 0x03) {
	case 0x01:
		flags |= IORESOURCE_IO;
		break;
	case 0x02: /* 32 bits */
	case 0x03: /* 64 bits */
		flags |= IORESOURCE_MEM;
		break;
	}
	if (w & 0x40000000)
		flags |= IORESOURCE_PREFETCH;
	return flags;
}

static u64 of_bus_pci_map(__be32 *addr, const __be32 *range, int na, int ns,
		int pna)
{
	u64 cp, s, da;
	unsigned int af, rf;

	af = of_bus_pci_get_flags(addr);
	rf = of_bus_pci_get_flags(range);

	/* Check address type match */
	if ((af ^ rf) & (IORESOURCE_MEM | IORESOURCE_IO))
		return OF_BAD_ADDR;

	/* Read address values, skipping high cell */
	cp = of_read_number(range + 1, na - 1);
	s  = of_read_number(range + na + pna, ns);
	da = of_read_number(addr + 1, na - 1);

	pr_debug("OF: PCI map, cp=%llx, s=%llx, da=%llx\n",
		 (unsigned long long)cp, (unsigned long long)s,
		 (unsigned long long)da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_pci_translate(__be32 *addr, u64 offset, int na)
{
	return of_bus_default_translate(addr + 1, offset, na - 1);
}

const __be32 *of_get_pci_address(struct device_node *dev, int bar_no, u64 *size,
			unsigned int *flags)
{
	const __be32 *prop;
	unsigned int psize;
	struct device_node *parent;
	struct of_bus *bus;
	int onesize, i, na, ns;

	/* Get parent & match bus type */
	parent = of_get_parent(dev);
	if (parent == NULL)
		return NULL;
	bus = of_match_bus(parent);
	if (strcmp(bus->name, "pci")) {
		of_node_put(parent);
		return NULL;
	}
	bus->count_cells(dev, &na, &ns);
	of_node_put(parent);
	if (!OF_CHECK_ADDR_COUNT(na))
		return NULL;

	/* Get "reg" or "assigned-addresses" property */
	prop = of_get_property(dev, bus->addresses, &psize);
	if (prop == NULL)
		return NULL;
	psize /= 4;

	onesize = na + ns;
	for (i = 0; psize >= onesize; psize -= onesize, prop += onesize, i++) {
		u32 val = be32_to_cpu(prop[0]);
		if ((val & 0xff) == ((bar_no * 4) + PCI_BASE_ADDRESS_0)) {
			if (size)
				*size = of_read_number(prop + na, ns);
			if (flags)
				*flags = bus->get_flags(prop);
			return prop;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(of_get_pci_address);

int of_pci_address_to_resource(struct device_node *dev, int bar,
			       struct resource *r)
{
	const __be32	*addrp;
	u64		size;
	unsigned int	flags;

	addrp = of_get_pci_address(dev, bar, &size, &flags);
	if (addrp == NULL)
		return -EINVAL;
	return __of_address_to_resource(dev, addrp, size, flags, NULL, r);
}
EXPORT_SYMBOL_GPL(of_pci_address_to_resource);

int of_pci_range_parser_init(struct of_pci_range_parser *parser,
				struct device_node *node)
{
	const int na = 3, ns = 2;
	int rlen;

	parser->node = node;
	parser->pna = of_n_addr_cells(node);
	parser->np = parser->pna + na + ns;

	parser->range = of_get_property(node, "ranges", &rlen);
	if (parser->range == NULL)
		return -ENOENT;

	parser->end = parser->range + rlen / sizeof(__be32);

	return 0;
}
EXPORT_SYMBOL_GPL(of_pci_range_parser_init);

struct of_pci_range *of_pci_range_parser_one(struct of_pci_range_parser *parser,
						struct of_pci_range *range)
{
	const int na = 3, ns = 2;

	if (!range)
		return NULL;

	if (!parser->range || parser->range + parser->np > parser->end)
		return NULL;

	range->pci_space = parser->range[0];
	range->flags = of_bus_pci_get_flags(parser->range);
	range->pci_addr = of_read_number(parser->range + 1, ns);
	range->cpu_addr = of_translate_address(parser->node,
				parser->range + na);
	range->size = of_read_number(parser->range + parser->pna + na, ns);

	parser->range += parser->np;

	/* Now consume following elements while they are contiguous */
	while (parser->range + parser->np <= parser->end) {
		u32 flags, pci_space;
		u64 pci_addr, cpu_addr, size;

		pci_space = be32_to_cpup(parser->range);
		flags = of_bus_pci_get_flags(parser->range);
		pci_addr = of_read_number(parser->range + 1, ns);
		cpu_addr = of_translate_address(parser->node,
				parser->range + na);
		size = of_read_number(parser->range + parser->pna + na, ns);

		if (flags != range->flags)
			break;
		if (pci_addr != range->pci_addr + range->size ||
		    cpu_addr != range->cpu_addr + range->size)
			break;

		range->size += size;
		parser->range += parser->np;
	}

	return range;
}
EXPORT_SYMBOL_GPL(of_pci_range_parser_one);

#endif /* CONFIG_PCI */

/*
 * ISA bus specific translator
 */

static int of_bus_isa_match(struct device_node *np)
{
	/*! 20140809 여기 실행됨 */
	return !strcmp(np->name, "isa");
}

static void of_bus_isa_count_cells(struct device_node *child,
				   int *addrc, int *sizec)
{
	if (addrc)
		*addrc = 2;
	if (sizec)
		*sizec = 1;
}

static u64 of_bus_isa_map(__be32 *addr, const __be32 *range, int na, int ns,
		int pna)
{
	u64 cp, s, da;

	/* Check address type match */
	if ((addr[0] ^ range[0]) & cpu_to_be32(1))
		return OF_BAD_ADDR;

	/* Read address values, skipping high cell */
	cp = of_read_number(range + 1, na - 1);
	s  = of_read_number(range + na + pna, ns);
	da = of_read_number(addr + 1, na - 1);

	pr_debug("OF: ISA map, cp=%llx, s=%llx, da=%llx\n",
		 (unsigned long long)cp, (unsigned long long)s,
		 (unsigned long long)da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_isa_translate(__be32 *addr, u64 offset, int na)
{
	return of_bus_default_translate(addr + 1, offset, na - 1);
}

static unsigned int of_bus_isa_get_flags(const __be32 *addr)
{
	unsigned int flags = 0;
	u32 w = be32_to_cpup(addr);

	if (w & 1)
		flags |= IORESOURCE_IO;
	else
		flags |= IORESOURCE_MEM;
	return flags;
}

/*
 * Array of bus specific translators
 */

static struct of_bus of_busses[] = {
#ifdef CONFIG_PCI
	/* PCI */
	{
		.name = "pci",
		.addresses = "assigned-addresses",
		.match = of_bus_pci_match,
		.count_cells = of_bus_pci_count_cells,
		.map = of_bus_pci_map,
		.translate = of_bus_pci_translate,
		.get_flags = of_bus_pci_get_flags,
	},
#endif /* CONFIG_PCI */
	/*! 20140809 여기 실행됨 */
	/* ISA */
	{
		.name = "isa",
		.addresses = "reg",
		.match = of_bus_isa_match,
		.count_cells = of_bus_isa_count_cells,
		.map = of_bus_isa_map,
		.translate = of_bus_isa_translate,
		.get_flags = of_bus_isa_get_flags,
	},
	/* Default */
	{
		.name = "default",
		.addresses = "reg",
		.match = NULL,
		.count_cells = of_bus_default_count_cells,
		.map = of_bus_default_map,
		.translate = of_bus_default_translate,
		.get_flags = of_bus_default_get_flags,
	},
};

static struct of_bus *of_match_bus(struct device_node *np)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(of_busses); i++)
		if (!of_busses[i].match || of_busses[i].match(np))
			/*! 20140809 of_busses[0].match: of_bus_isa_match
			 * of_bus_isa_match에서 np->name이 isa가 아니므로
			 * of_busses[1].match: NULL 이므로 return &of_busses[1] 
			 */
			return &of_busses[i];
	BUG();
	return NULL;
}

static int of_translate_one(struct device_node *parent, struct of_bus *bus,
			    struct of_bus *pbus, __be32 *addr,
			    int na, int ns, int pna, const char *rprop)
{
	const __be32 *ranges;
	unsigned int rlen;
	int rone;
	u64 offset = OF_BAD_ADDR;

	/* Normally, an absence of a "ranges" property means we are
	 * crossing a non-translatable boundary, and thus the addresses
	 * below the current not cannot be converted to CPU physical ones.
	 * Unfortunately, while this is very clear in the spec, it's not
	 * what Apple understood, and they do have things like /uni-n or
	 * /ht nodes with no "ranges" property and a lot of perfectly
	 * useable mapped devices below them. Thus we treat the absence of
	 * "ranges" as equivalent to an empty "ranges" property which means
	 * a 1:1 translation at that level. It's up to the caller not to try
	 * to translate addresses that aren't supposed to be translated in
	 * the first place. --BenH.
	 *
	 * As far as we know, this damage only exists on Apple machines, so
	 * This code is only enabled on powerpc. --gcl
	 */
	ranges = of_get_property(parent, rprop, &rlen);
#if !defined(CONFIG_PPC)
	if (ranges == NULL) {
		pr_err("OF: no ranges; cannot translate\n");
		return 1;
	}
#endif /* !defined(CONFIG_PPC) */
	if (ranges == NULL || rlen == 0) {
		offset = of_read_number(addr, na);
		memset(addr, 0, pna * 4);
		pr_debug("OF: empty ranges; 1:1 translation\n");
		goto finish;
	}

	pr_debug("OF: walking ranges...\n");

	/* Now walk through the ranges */
	rlen /= 4;
	rone = na + pna + ns;
	for (; rlen >= rone; rlen -= rone, ranges += rone) {
		offset = bus->map(addr, ranges, na, ns, pna);
		if (offset != OF_BAD_ADDR)
			break;
	}
	if (offset == OF_BAD_ADDR) {
		pr_debug("OF: not found !\n");
		return 1;
	}
	memcpy(addr, ranges + na, 4 * pna);

 finish:
	of_dump_addr("OF: parent translation for:", addr, pna);
	pr_debug("OF: with offset: %llx\n", (unsigned long long)offset);

	/* Translate it into parent bus space */
	return pbus->translate(addr, offset, pna);
}

/*
 * Translate an address from the device-tree into a CPU physical address,
 * this walks up the tree and applies the various bus mappings on the
 * way.
 *
 * Note: We consider that crossing any level with #size-cells == 0 to mean
 * that translation is impossible (that is we are not dealing with a value
 * that can be mapped to a cpu physical address). This is not really specified
 * that way, but this is traditionally the way IBM at least do things
 */
static u64 __of_translate_address(struct device_node *dev,
				  const __be32 *in_addr, const char *rprop)
{
	struct device_node *parent = NULL;
	struct of_bus *bus, *pbus;
	__be32 addr[OF_MAX_ADDR_CELLS];
	/*! 20140809 OF_MAX_ADDR_CELLS: 4 */
	int na, ns, pna, pns;
	u64 result = OF_BAD_ADDR;

	pr_debug("OF: ** translation for device %s **\n", dev->full_name);

	/* Increase refcount at current level */
	of_node_get(dev);
	/*! 20140809 아무것도 안한다. */

	/* Get parent & match bus type */
	parent = of_get_parent(dev);
	if (parent == NULL)
		goto bail;
	bus = of_match_bus(parent);

	/* Count address cells & copy address locally */
	bus->count_cells(dev, &na, &ns);
	/*! 20140809 na, ns는 1 */
	if (!OF_CHECK_COUNTS(na, ns)) {
		printk(KERN_ERR "prom_parse: Bad cell count for %s\n",
		       dev->full_name);
		goto bail;
	}
	memcpy(addr, in_addr, na * 4);

	pr_debug("OF: bus is %s (na=%d, ns=%d) on %s\n",
	    bus->name, na, ns, parent->full_name);
	of_dump_addr("OF: translating address:", addr, na);

	/* Translate */
	for (;;) {
		/* Switch to parent bus */
		of_node_put(dev);
		/*! 20140809 아무것도 안함 */
		dev = parent;
		parent = of_get_parent(dev);
		/*! 20140809 현재 dev는 root이므로 parent는 NULL */

		/* If root, we have finished */
		if (parent == NULL) {
			pr_debug("OF: reached root node\n");
			result = of_read_number(addr, na);
			/*! 20140809 addr 가 가리키는 곳의 값을 읽는다. */
			break;
		}

		/* Get new parent bus and counts */
		pbus = of_match_bus(parent);
		pbus->count_cells(dev, &pna, &pns);
		if (!OF_CHECK_COUNTS(pna, pns)) {
			printk(KERN_ERR "prom_parse: Bad cell count for %s\n",
			       dev->full_name);
			break;
		}

		pr_debug("OF: parent bus is %s (na=%d, ns=%d) on %s\n",
		    pbus->name, pna, pns, parent->full_name);

		/* Apply bus translation */
		if (of_translate_one(dev, bus, pbus, addr, na, ns, pna, rprop))
			break;

		/* Complete the move up one level */
		na = pna;
		ns = pns;
		bus = pbus;

		of_dump_addr("OF: one level translation:", addr, na);
	}
 bail:
	of_node_put(parent);
	of_node_put(dev);

	return result;
	/*! 20140809 현재 in_addr 값을 현재 아키텍처에 맞는 little endian으로 변환하여 리턴 */
}

u64 of_translate_address(struct device_node *dev, const __be32 *in_addr)
{
	return __of_translate_address(dev, in_addr, "ranges");
	/*! 20140809 in_addr 값을 현재 아키텍처에 맞는 little endian으로 변환하여 리턴 */
}
EXPORT_SYMBOL(of_translate_address);

u64 of_translate_dma_address(struct device_node *dev, const __be32 *in_addr)
{
	return __of_translate_address(dev, in_addr, "dma-ranges");
}
EXPORT_SYMBOL(of_translate_dma_address);

bool of_can_translate_address(struct device_node *dev)
{
	struct device_node *parent;
	struct of_bus *bus;
	int na, ns;

	parent = of_get_parent(dev);
	if (parent == NULL)
		return false;

	bus = of_match_bus(parent);
	bus->count_cells(dev, &na, &ns);

	of_node_put(parent);

	return OF_CHECK_COUNTS(na, ns);
}
EXPORT_SYMBOL(of_can_translate_address);

const __be32 *of_get_address(struct device_node *dev, int index, u64 *size,
		    unsigned int *flags)
{
	const __be32 *prop;
	unsigned int psize;
	struct device_node *parent;
	struct of_bus *bus;
	int onesize, i, na, ns;

	/* Get parent & match bus type */
	parent = of_get_parent(dev);
	/*! 20140809 dev의 parent node를 가져온다 */
	if (parent == NULL)
		return NULL;
	bus = of_match_bus(parent);
	/*! 20140809 현재 bus는 아래값을 가진다.
	 * {
	 * 	.name = "default",
	 * 	.addresses = "reg",
	 * 	.match = NULL,
	 * 	.count_cells = of_bus_default_count_cells,
	 * 	.map = of_bus_default_map,
	 * 	.translate = of_bus_default_translate,
	 * 	.get_flags = of_bus_default_get_flags,
	 * }
	 */
	bus->count_cells(dev, &na, &ns);
	/*! 20140809 of_bus_default_count_cells 함수가 호출됨
	 * na에는 device tree에서 dev->parent node의 #address-cells 의 값(1)을 가져온다.
	 * ns에는 device tree에서 dev->parent node의 #size-cells 의 값(1)을 가져온다.
	 */
	of_node_put(parent);
	/*! 20140809 아무일도 안함 */
	if (!OF_CHECK_ADDR_COUNT(na))
		return NULL;

	/* Get "reg" or "assigned-addresses" property */
	prop = of_get_property(dev, bus->addresses, &psize);
	/*! 20140809 dev node에 bus->addresses="reg"값을 가지는 property 찾음 
	 * reg = <0x10481000 0x1000 0x10482000 0x1000 0x10484000 0x2000 0x10486000 0x2000>;
	 */
	if (prop == NULL)
		return NULL;
	psize /= 4;

	onesize = na + ns;
	/*! 20140809 onesize = 2 */
	for (i = 0; psize >= onesize; psize -= onesize, prop += onesize, i++)
		/*! 20140809 psize: 8 */
		if (i == index) {
			if (size)
				*size = of_read_number(prop + na, ns);
			/*! 20140809 index는 0, na와 ns는 1이고, prop가 아래와 같을때,
			 * prop = <0x10481000 0x1000 0x10482000 0x1000 0x10484000 0x2000 0x10486000 0x2000>;
			 * size = 첫번째 0x1000
			 */
			if (flags)
				*flags = bus->get_flags(prop);
			/*! 20140809 bus->get_flags = of_bus_default_get_flags 함수 호출
			 * flags = IORESOURCE_MEM 
			 */
			return prop;
		}
	return NULL;
}
EXPORT_SYMBOL(of_get_address);

static int __of_address_to_resource(struct device_node *dev,
		const __be32 *addrp, u64 size, unsigned int flags,
		const char *name, struct resource *r)
{
	u64 taddr;

	if ((flags & (IORESOURCE_IO | IORESOURCE_MEM)) == 0)
		return -EINVAL;
	taddr = of_translate_address(dev, addrp);
	/*! 20140809 addrp 값을 little endian으로 변환 */
	if (taddr == OF_BAD_ADDR)
		return -EINVAL;
	memset(r, 0, sizeof(struct resource));
	if (flags & IORESOURCE_IO) {
		unsigned long port;
		port = pci_address_to_pio(taddr);
		if (port == (unsigned long)-1)
			return -EINVAL;
		r->start = port;
		r->end = port + size - 1;
	} else {
		/*! 20140809 flags가 IORESOURCE_MEM 이므로 여기 실행 */
		r->start = taddr;
		r->end = taddr + size - 1;
	}
	r->flags = flags;
	r->name = name ? name : dev->full_name;

	return 0;
}

/**
 * of_address_to_resource - Translate device tree address and return as resource
 *
 * Note that if your address is a PIO address, the conversion will fail if
 * the physical address can't be internally converted to an IO token with
 * pci_address_to_pio(), that is because it's either called to early or it
 * can't be matched to any host bridge IO space
 */
int of_address_to_resource(struct device_node *dev, int index,
			   struct resource *r)
{
	const __be32	*addrp;
	u64		size;
	unsigned int	flags;
	const char	*name = NULL;

	addrp = of_get_address(dev, index, &size, &flags);
	/*! 20140809 dev node의 reg property[index]의 두 값(addrp, size)과 flag를 return한다. */
	if (addrp == NULL)
		return -EINVAL;

	/* Get optional "reg-names" property to add a name to a resource */
	of_property_read_string_index(dev, "reg-names",	index, &name);
	/*! 20140809 property reg-names[index] 문자열을 찾는다. */

	return __of_address_to_resource(dev, addrp, size, flags, name, r);
	/*! 20140809 dev node 에 해당하는 struct resource 값 초기화 */
}
EXPORT_SYMBOL_GPL(of_address_to_resource);

struct device_node *of_find_matching_node_by_address(struct device_node *from,
					const struct of_device_id *matches,
					u64 base_address)
{
	struct device_node *dn = of_find_matching_node(from, matches);
	struct resource res;

	while (dn) {
		if (of_address_to_resource(dn, 0, &res))
			continue;
		if (res.start == base_address)
			return dn;
		dn = of_find_matching_node(dn, matches);
	}

	return NULL;
}


/**
 * of_iomap - Maps the memory mapped IO for a given device_node
 * @device:	the device whose io range will be mapped
 * @index:	index of the io range
 *
 * Returns a pointer to the mapped memory
 */
void __iomem *of_iomap(struct device_node *np, int index)
{
	struct resource res;

	if (of_address_to_resource(np, index, &res))
		return NULL;
	/*! 20140809 np의 property reg[index] 의 값을 res 구조체에 설정한다. */

	return ioremap(res.start, resource_size(&res));
	/*! 20140809 res.start를 va로 변환하여 리턴 */
}
EXPORT_SYMBOL(of_iomap);
