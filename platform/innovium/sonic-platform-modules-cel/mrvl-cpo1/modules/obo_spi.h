#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */

uint32_t multiple_src_test (void __iomem *base_addr, uint32_t cmis_page_no, uint8_t offset, uint8_t len, uint32_t val);

uint32_t obo_spi_read (void __iomem *base_addr, u32 pim, u32 rtc_idx, uint32_t page, uint32_t start, uint8_t byte_len, uint8_t *buf);
uint32_t obo_spi_write (void __iomem *base_addr, u32 pim, u32 rtc_idx, uint32_t page, uint32_t start, uint8_t byte_len, uint8_t *buf);
uint32_t spi_check_status(void __iomem *base_addr, uint32_t pim, uint32_t rtc_idx);
