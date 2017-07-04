#ifndef _VIDEO_TESTDEV_H_
#define _VIDEO_TESTDEV_H_

#define TESTDEV "/dev/video_testdev"

#define	SIZE_2G		(2*1024*1024*1024UL)
#define SIZE_1G		(1*1024*1024*1024UL)
#define SIZE_512M	(512*1024*1024UL)
#define SIZE_256M	(256*1024*1024UL)
#define SIZE_128M	(128*1024*1024UL)
#define SIZE_64M	(64*1024*1024UL)
#define SIZE_32M	(32*1024*1024UL)
#define SIZE_16M	(16*1024*1024UL)
#define SIZE_8M		(8*1024*1024UL)
#define SIZE_4M		(4*1024*1024UL)
#define SIZE_1M		(1*1024*1024UL)
#define SIZE_512K	(512*1024UL)
#define SIZE_16K	(16*1024UL)
#define SIZE_8K		(8*1024UL)

#define VMEM_SIZE	SIZE_2G
#define DC_REG_SIZE	SIZE_1M
#define DMAC_REG_SIZE	SIZE_1M
#define VPU_REG_SIZE	SIZE_512K
#define GPU_REG_SIZE	SIZE_1M
#define I2C_REG_SIZE	SIZE_16K	//I2C0 & I2C1
#define HMEM_SIZE	SIZE_1G		//GPU Test, High 1G Mem

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;



enum {
	VMEM_PGOFF,
	DC_PGOFF,
	DMAC_PGOFF,
	VPU_PGOFF,
	GPU_PGOFF,
	I2C_PGOFF,
	HMEM_PGOFF
};

#define	VMEM_OFFSET	(VMEM_PGOFF << 13)
#define	DC_OFFSET	(DC_PGOFF << 13)
#define	DMAC_OFFSET	(DMAC_PGOFF << 13)
#define	VPU_OFFSET	(VPU_PGOFF << 13)
#define	GPU_OFFSET	(GPU_PGOFF << 13)
#define	I2C_OFFSET	(I2C_PGOFF << 13)
#define	HMEM_OFFSET	(HMEM_PGOFF << 13)

static inline unsigned int readl(const volatile void *a)
{
        unsigned int ret = *(const volatile unsigned int *)a;
        asm("memb");
        return ret;
}

static inline void writel(unsigned int b, volatile void *a)
{
        *(volatile unsigned int *)a = b;
        asm("memb");
}

static inline unsigned char readb(const volatile void *a)
{
        unsigned char ret = *(const volatile unsigned char *)a;
        asm("memb");
        return ret;
}

static inline void writeb(unsigned char b, volatile void *a)
{
        *(volatile unsigned char *)a = b;
        asm("memb");
}

#endif
