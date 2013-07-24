#ifndef __TANSEN_GTI_H__
#define __TANSEN_GTI_H__

#define GTI_CTRL_REG_ADDR_SHIFT		20
#define GTI_CTRL_REG_ADDR_MASK		0x00000FFF		/* 12 bits */
#define GTI_CTRL_OP_SHIFT		19
#define GTI_CTRL_OP_MASK		0x00000001		/* 1 bit */
#define GTI_CTRL_DATA_SHIFT		8
#define GTI_CTRL_DATA_MASK		0x000000FF		/* 8 bits */

#define GTI_CTRL_OP_WRITE		0x00000001
#define GTI_CTRL_OP_READ		0x00000000

#define GTI_CLK_HI			1
#define GTI_CLK_LO			0
#define GTI_NOT_IN_RESET		1
#define GTI_IN_RESET			0
#define GTI_NOT_TEST_MODE		0
#define GTI_TEST_MODE			1

#endif /* __TANSEN_GTI_H__ */
