/*
 * IMG Universal Communications Core Platform (UCCP) driver.
 * UCCP driver to allow setting up of memory, loading of binaries, and
 * manipulation of MTX registers from userspace.
 *
 * Copyright (C) 2010  Imagination Technologies
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/uccp.h>
#include <linux/delay.h>

#define UCC_MCREQ_0			0x10
#define UCC_MCREQ_STRIDE		0x10
#define UCC_MCREQ_BULKADDR		0x0
#define UCC_MCREQ_BULKLIMIT		0x4
#define UCC_MCREQ_MEMBASE		0x8
#define UCC_MCREQ_MEMPORT		0xc
#define UCC_MCREQ_MEMPORT_THIS_MTX	0
#define UCC_MCREQ_MEMPORT_OTHER_MTX	1
#define UCC_MCREQ_MEMPORT_NOT_MTX	2

#define MTX_TXUXXRXDT			0xf8
#define MTX_TXUXXRXRQ			0xfc

#define MCP_SYS_HACC_CMD		0x1040
#define MCP_SYS_HACC_CMD_ADDR		0x0003ffff
#define MCP_SYS_HACC_CMD_PACK		0x20000000
#define MCP_SYS_HACC_CMD_RDNW		0x40000000
#define MCP_SYS_HACC_CMD_VAL		0x80000000
#define MCP_SYS_HACC_WDATA		0x1044
#define MCP_SYS_HACC_RDATA		0x1048

#define UCCP_SYSCONTROL			0x400
#define UCCP_RESETCTRL			(UCCP_SYSCONTROL + 0x028)
#define UCCP_RESETCTRL_ALL_EXCEPT_BUS	0xfffe
#define UCCP_COREID			(UCCP_SYSCONTROL + 0x070)
#define UCCP_COREID_GROUPID		0xff000000
#define UCCP_COREID_GROUPID_SHIFT	24
#define UCCP_COREID_COREID		0x00ff0000
#define UCCP_COREID_COREID_SHIFT	16
#define UCCP_COREID_CONFIG		0x0000ffff
#define UCCP_COREID_CONFIG_SHIFT	0
#define UCCP_COREID_GROUPID_UCCP	0xe
#define UCCP_COREID_GROUPID_UCC		0xe
#define UCCP_COREID_COREID_UCCP		0x1
#define UCCP_COREID_COREID_UCC		0x2
#define UCCP_COREREV			(UCCP_SYSCONTROL + 0x078)
#define UCCP_COREREV_MAJOR		0x00ff0000
#define UCCP_COREREV_MAJOR_SHIFT	16
#define UCCP_COREREV_MINOR		0x0000ff00
#define UCCP_COREREV_MINOR_SHIFT	8
#define UCCP_COREREV_REV		0x000000ff
#define UCCP_COREREV_REV_SHIFT		0


static struct uccp_priv {
	struct uccp_pdata *pdata;
	struct class *uccp_class;
	int uccp_major;

	struct uccp_region *full_region;
	void __iomem *region_map;
} priv;

static inline unsigned long get_region_phys(void)
{
	return ((unsigned long)priv.full_region->physical + (PAGE_SIZE-1))
		& PAGE_MASK;
}

static inline unsigned int get_region_size(void)
{
	return ((priv.full_region->size -
			(get_region_phys() - priv.full_region->physical)))
		& PAGE_MASK;
}

static int mmap_uccp(struct file *file, struct vm_area_struct *vma)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

	if (offset >= get_region_size())
		return -EINVAL;

	/* remap_pfn_range will mark the range VM_IO */
	if (remap_pfn_range(vma, vma->vm_start,
			    get_region_phys() >> PAGE_SHIFT,
			    vma->vm_end - vma->vm_start,
			    PAGE_SHARED))
		return -EAGAIN;
	return 0;
}

/* Find a memory region by uccp number and type */
static struct uccp_region *find_uccp_region(int uccp, unsigned int type)
{
	struct uccp_core *core;
	int i;
	if (uccp >= 0) {
		core = &priv.pdata->cores[uccp];
		for (i = 0; i < core->num_regions; ++i)
			if (type == core->regions[i].type)
				return &core->regions[i];
	}
	for (i = 0; i < priv.pdata->num_regions; ++i)
		if (type == priv.pdata->regions[i].type)
			return &priv.pdata->regions[i];
	return NULL;
}

static unsigned long uccp_host_read(unsigned int uccp, unsigned int regno)
{
	struct uccp_core *core = &priv.pdata->cores[uccp];
	return readl((unsigned long)core->host_sys_bus->start + regno);
}

static void uccp_host_write(unsigned int uccp, unsigned int regno,
			    unsigned int value)
{
	struct uccp_core *core = &priv.pdata->cores[uccp];
	writel(value, (unsigned long)core->host_sys_bus->start + regno);
}

/* Find the mcreq port of a memory area */
static int uccp_mcreq_port(unsigned long uccp, unsigned long physical,
			    unsigned long size)
{
	int c;
	struct uccp_core *core;
	struct uccp_region *mtx;
	for (c = 0; c < priv.pdata->num_cores; ++c) {
		mtx = find_uccp_region(uccp, UCCP_REGION_MTX);
		if (!mtx)
			continue;
		core = &priv.pdata->cores[c];
		if (physical >= mtx->physical &&
		    physical < mtx->physical + mtx->size) {
			/*
			 * Check the specified range fits entirely within the
			 * MTX internal memory - a given mapping cannot cross
			 * port boundaries
			 */
			if (physical + size > mtx->physical + mtx->size)
				return -EINVAL;

			if (c == uccp)
				return UCC_MCREQ_MEMPORT_THIS_MTX;
			else
				return UCC_MCREQ_MEMPORT_OTHER_MTX;
		}
	}
	return UCC_MCREQ_MEMPORT_NOT_MTX;
}

static int uccp_mcreq_clear(unsigned int uccp, unsigned int index)
{
	struct uccp_core *core = &priv.pdata->cores[uccp];
	unsigned long base = core->mc_req->start +
				UCC_MCREQ_0 + index*UCC_MCREQ_STRIDE;

	writel(0xFFFFFFFF, base + UCC_MCREQ_BULKADDR);
	writel(0xFFFFFFFF, base + UCC_MCREQ_BULKLIMIT);
	writel(0xFFFFFFFF, base + UCC_MCREQ_MEMBASE);
	writel(2,          base + UCC_MCREQ_MEMPORT);
	return 0;
}

static int uccp_mcreq_write(unsigned int uccp, unsigned int index,
			     struct uccp_mcreq *mc_req)
{
	struct uccp_core *core = &priv.pdata->cores[uccp];
	unsigned long base = core->mc_req->start +
				UCC_MCREQ_0 + index*UCC_MCREQ_STRIDE;
	unsigned long limit = mc_req->bulk + mc_req->size;
	int port;

	/*
	 * Restrictions imposed by MCREQ registers. Make sure the user isn't
	 * supplying values with bits that will be discarded when writing to
	 * the registers.
	 */
	if ((mc_req->bulk & ~0xB03FF000) ||
	    (limit & ~0xB03FF000) ||
	    (mc_req->bulk > limit) ||
	    (mc_req->physical & ~0xFFFFF000))
		return -EINVAL;

	port = uccp_mcreq_port(uccp, mc_req->physical, mc_req->size);
	if (port < 0)
		return -EINVAL;

	writel(mc_req->bulk >> 2,	base + UCC_MCREQ_BULKADDR);
	writel(limit >> 2,		base + UCC_MCREQ_BULKLIMIT);
	writel(mc_req->physical >> 2,	base + UCC_MCREQ_MEMBASE);
	writel(port,			base + UCC_MCREQ_MEMPORT);
	return 0;
}

static void uccp_mcreq_read(unsigned int uccp, unsigned int index,
			     struct uccp_mcreq *mc_req)
{
	struct uccp_core *core = &priv.pdata->cores[uccp];
	unsigned long base = core->mc_req->start +
				UCC_MCREQ_0 + index*UCC_MCREQ_STRIDE;
	mc_req->bulk = readl(base + UCC_MCREQ_BULKADDR) << 2;
	mc_req->size = (readl(base + UCC_MCREQ_BULKLIMIT) << 2) - mc_req->bulk;
	mc_req->physical = readl(base + UCC_MCREQ_MEMBASE) << 2;
}

static long ioctl_uccp(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct inode *inode = file->f_mapping->host;
	unsigned int minor = MINOR(inode->i_rdev);
	struct uccp_core *core;
	struct uccp_region region;
	struct uccp_region *out_region;
	struct uccp_reg reg;
	struct uccp_mcreq mc_req;
	struct resource *res;
	unsigned int tmp = 0;
	int lstat;

	if (minor >= priv.pdata->num_cores)
		return -EINVAL;

	core = &priv.pdata->cores[minor];

	switch (cmd) {
	default:
		return -EINVAL;
	case UCCPIO_GETREGION:
		if (copy_from_user(&region, argp, sizeof(region)))
			return -EFAULT;

		out_region = find_uccp_region(minor, region.type);
		if (!out_region)
			return -EINVAL;

		if (copy_to_user(argp, out_region, sizeof(*out_region)))
			return -EFAULT;
		break;

	case UCCPIO_WRREG:
		if (copy_from_user(&reg, argp, sizeof(reg)))
			return -EFAULT;

		res = core->host_sys_bus;
		switch (reg.op) {
		case UCCP_REG_DIRECT:
			if (reg.reg % 4 ||
			    reg.reg >= res->end - res->start)
				return -EINVAL;

			uccp_host_write(minor, reg.reg, reg.val);
			break;
		case UCCP_REG_INDIRECT:
			TBI_LOCK(lstat);
			uccp_host_write(minor, MTX_TXUXXRXDT, reg.val);
			uccp_host_write(minor, MTX_TXUXXRXRQ, reg.reg);
			TBI_UNLOCK(lstat);
			break;
		case UCCP_REG_MCPPERIP_PACK:
			tmp |= MCP_SYS_HACC_CMD_PACK;
		case UCCP_REG_MCPPERIP:
			tmp |= MCP_SYS_HACC_CMD_VAL;
			tmp |= (reg.reg & MCP_SYS_HACC_CMD_ADDR);
			TBI_LOCK(lstat);
			uccp_host_write(minor, MCP_SYS_HACC_WDATA, reg.val);
			uccp_host_write(minor, MCP_SYS_HACC_CMD, tmp);
			uccp_host_write(minor, MCP_SYS_HACC_CMD, 0x0);
			TBI_UNLOCK(lstat);
			break;
		default:
			return -EINVAL;
		}
		break;

	case UCCPIO_RDREG:
		if (copy_from_user(&reg, argp, sizeof(reg)))
			return -EFAULT;

		res = core->host_sys_bus;
		switch (reg.op) {
		case UCCP_REG_DIRECT:
			if (reg.reg % 4 ||
			    reg.reg >= res->end - res->start)
				return -EINVAL;

			reg.val = uccp_host_read(minor, reg.reg);
			break;
		case UCCP_REG_MCPPERIP_PACK:
			tmp |= MCP_SYS_HACC_CMD_PACK;
		case UCCP_REG_MCPPERIP:
			tmp |= MCP_SYS_HACC_CMD_VAL;
			tmp |= MCP_SYS_HACC_CMD_RDNW;
			tmp |= (reg.reg & MCP_SYS_HACC_CMD_ADDR);
			TBI_LOCK(lstat);
			uccp_host_write(minor, MCP_SYS_HACC_CMD, tmp);
			reg.val = uccp_host_read(minor, MCP_SYS_HACC_RDATA);
			uccp_host_write(minor, MCP_SYS_HACC_CMD, 0x0);
			TBI_UNLOCK(lstat);
			break;
		default:
			return -EINVAL;
		}
		if (copy_to_user(argp, &reg, sizeof(reg)))
			return -EFAULT;
		break;

	case UCCPIO_CLRMCREQ:
		if (copy_from_user(&mc_req, argp, sizeof(mc_req)))
			return -EFAULT;
		if (mc_req.index < 0 || mc_req.index >= core->num_mc_req)
			return -EINVAL;
		return uccp_mcreq_clear(minor, mc_req.index);

	case UCCPIO_SETMCREQ:
		if (copy_from_user(&mc_req, argp, sizeof(mc_req)))
			return -EFAULT;
		if (mc_req.index < 0 || mc_req.index >= core->num_mc_req)
			return -EINVAL;
		return uccp_mcreq_write(minor, mc_req.index, &mc_req);

	case UCCPIO_GETMCREQ:
		if (copy_from_user(&mc_req, argp, sizeof(mc_req)))
			return -EFAULT;
		if (mc_req.index < 0 || mc_req.index >= core->num_mc_req)
			return -EINVAL;
		uccp_mcreq_read(minor, mc_req.index, &mc_req);

		if (copy_to_user(argp, &mc_req, sizeof(mc_req)))
			return -EFAULT;
		break;

	case UCCPIO_SRST:
		uccp_host_write(minor, UCCP_RESETCTRL,
					UCCP_RESETCTRL_ALL_EXCEPT_BUS);
		uccp_host_write(minor, UCCP_RESETCTRL, 0x0);
		/*
		 * Allow the dash some time to perform it's own reset so we
		 * don't lose any later register writes.
		 */
		mdelay(20);
		break;
	}

	return 0;
}

static const struct file_operations uccp_fops = {
	.mmap			= mmap_uccp,
	.unlocked_ioctl		= ioctl_uccp,
};

/* Update offsets from physical addresses */
static void complete_uccp_regions(void)
{
	struct uccp_core *core;
	struct uccp_region *full = priv.full_region;
	int i, c;
	for (c = 0; c < priv.pdata->num_cores; ++c) {
		core = &priv.pdata->cores[c];
		for (i = 0; i < core->num_regions; ++i) {
			if (core->regions[i].offset)
				continue;
			core->regions[i].offset = core->regions[i].physical
							- full->physical;
		}
	}
	for (i = 0; i < priv.pdata->num_regions; ++i) {
		if (priv.pdata->regions[i].offset)
			continue;
		priv.pdata->regions[i].offset = priv.pdata->regions[i].physical
						- full->physical;
	}
}

/* Probe an individual UCCP */
static int uccp_probe_core(struct platform_device *pdev,
			   unsigned int i)
{
	struct uccp_core *core = &priv.pdata->cores[i];
	unsigned long core_id, core_revision;
	unsigned int groupid, coreid, config;
	unsigned int majrev, minrev, steprev;
	dev_t dev = MKDEV(priv.uccp_major, i);

	core_id = uccp_host_read(i, UCCP_COREID);
	groupid = (core_id & UCCP_COREID_GROUPID) >> UCCP_COREID_GROUPID_SHIFT;
	coreid  = (core_id & UCCP_COREID_COREID)  >> UCCP_COREID_COREID_SHIFT;
	config  = (core_id & UCCP_COREID_CONFIG)  >> UCCP_COREID_CONFIG_SHIFT;

	if (groupid != UCCP_COREID_GROUPID_UCCP ||
	    coreid  != UCCP_COREID_COREID_UCCP) {
		dev_err(&pdev->dev, "uccp%d: wrong groupid/coreid\n", i);
		return -ENODEV;
	}

	core_revision = uccp_host_read(i, UCCP_COREREV);
	majrev = (core_revision & UCCP_COREREV_MAJOR)
			>> UCCP_COREREV_MAJOR_SHIFT;
	minrev = (core_revision & UCCP_COREREV_MINOR)
			>> UCCP_COREREV_MINOR_SHIFT;
	steprev = (core_revision & UCCP_COREREV_REV)
			>> UCCP_COREREV_REV_SHIFT;

	core->device = device_create(priv.uccp_class, &pdev->dev, dev,
					NULL, "uccp%d", i);
	if (IS_ERR(core->device)) {
		dev_err(&pdev->dev, "unable to create device uccp%d\n", i);
		return PTR_ERR(core->device);
	}

	dev_info(&pdev->dev,
		 "uccp%d detected with config: 0x%04x, rev: %u.%u.%u\n",
		 i, config, majrev, minrev, steprev);
	return 0;
}

static void uccp_remove_core(struct platform_device *pdev, unsigned int i)
{
	dev_t dev = MKDEV(priv.uccp_major, i);
	device_destroy(priv.uccp_class, dev);
}

/* Probe all UCCPs using platform data */
static int uccp_probe(struct platform_device *pdev)
{
	struct uccp_pdata *pdata = pdev->dev.platform_data;
	struct resource *res;
	int error;
	int i;

	if (!pdata) {
		dev_err(&pdev->dev, "no platform data defined\n");
		error = -EINVAL;
		goto err_pdata;
	}
	priv.pdata = pdata;
	/* Must have resources for register regions */
	for (i = 0; ; ++i) {
		int uccp;
		int type;
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			break;

		uccp = UCCP_RES_UCCP(res->flags & IORESOURCE_BITS);
		type = UCCP_RES_TYPE(res->flags & IORESOURCE_BITS);
		if (uccp > pdata->num_cores)
			continue;

		if (type == UCCP_RES_HOSTSYSBUS)
			pdata->cores[uccp].host_sys_bus = res;
		else if (type == UCCP_RES_MCREQ)
			pdata->cores[uccp].mc_req = res;
	}
	for (i = 0; i < pdata->num_cores; ++i) {
		if (!pdata->cores[i].host_sys_bus) {
			dev_err(&pdev->dev, "platform data does not specify a"
				"HOSTSYSBUS memory resource for uccp%d\n", i);
			error = -EINVAL;
			goto err_pdata;
		}
		if (!pdata->cores[i].mc_req) {
			dev_err(&pdev->dev, "platform data does not specify a"
				"MCREQ memory resource for uccp%d\n", i);
			error = -EINVAL;
			goto err_pdata;
		}
	}
	/* Must have global region of type UCCP_REGION_ALL */
	priv.full_region = find_uccp_region(-1, UCCP_REGION_ALL);
	if (!priv.full_region) {
		dev_err(&pdev->dev, "platform data contains no region of type"
			" UCCP_REGION_ALL\n");
		error = -EINVAL;
		goto err_pdata;
	}
	priv.full_region->offset = 0;
	priv.region_map = ioremap(priv.full_region->physical,
				  priv.full_region->size);
	complete_uccp_regions();

	priv.uccp_class = class_create(THIS_MODULE, "uccp");
	if (IS_ERR(priv.uccp_class)) {
		dev_err(&pdev->dev, "unable to create device class\n");
		error = PTR_ERR(priv.uccp_class);
		goto err_class;
	}

	priv.uccp_major = register_chrdev(0, "uccp", &uccp_fops);
	if (priv.uccp_major < 0) {
		dev_err(&pdev->dev, "unable to get major\n");
		error = priv.uccp_major;
		goto err_major;
	}

	for (i = 0; i < pdata->num_cores; ++i) {
		error = uccp_probe_core(pdev, i);
		if (error) {
			/* undo our hard work probing cores */
			int max_remove = i;
			for (i = 0; i < max_remove; ++i)
				uccp_remove_core(pdev, i);
			goto err_dev;
		}
	}

	return 0;
err_dev:
	unregister_chrdev(priv.uccp_major, "uccp");
err_major:
	class_destroy(priv.uccp_class);
err_class:
	iounmap(priv.region_map);
err_pdata:
	return error;
}

static int uccp_remove(struct platform_device *pdev)
{
	int i;
	for (i = 0; i < priv.pdata->num_cores; ++i)
		uccp_remove_core(pdev, i);
	unregister_chrdev(priv.uccp_major, "uccp");
	class_destroy(priv.uccp_class);
	iounmap(priv.region_map);

	return 0;
}

static struct platform_driver uccp_driver = {
	.driver = {
		.name		= "uccp",
		.owner		= THIS_MODULE,
	},
	.probe		= uccp_probe,
	.remove		= uccp_remove,
};

static int __init uccp_init(void)
{
	return platform_driver_register(&uccp_driver);
}

static void __exit uccp_exit(void)
{
	platform_driver_unregister(&uccp_driver);
}

module_init(uccp_init);
module_exit(uccp_exit);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("IMG UCCP Driver");
MODULE_LICENSE("GPL");
