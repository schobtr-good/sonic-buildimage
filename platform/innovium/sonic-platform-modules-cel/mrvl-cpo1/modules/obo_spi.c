#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/pci.h>
// #include <linux/preempt.h>
#include <linux/delay.h>

#include "obo_spi.h"
#include "fpga_sw.h"

u32 timeout = 300000;
u32 sleeptime = 5000;


u32 wait_till_done(void __iomem *base_addr, u32 reg, u32 bitmask, u32 *done, u32 timeout_us, u32 sleeptime_us);


uint32_t multiple_src_test (void __iomem *base_addr, uint32_t cmis_page_no, uint8_t offset, uint8_t len, uint32_t val)
{
    printk(KERN_INFO "%s %d base_addr: %lx", __func__, __LINE__, base_addr);

    printk(KERN_INFO "%s PIM1_SCRTCHPD: %x", __func__, ioread32(base_addr + PIM_1_BASE_ADDR + PIM_SCRTCHPD_REG_OFFSET_ADDR));

    iowrite32(val, base_addr + PIM_1_BASE_ADDR + PIM_SCRTCHPD_REG_OFFSET_ADDR);

    printk(KERN_INFO "%s PIM1_SCRTCHPD: %x", __func__, ioread32(base_addr + PIM_1_BASE_ADDR + PIM_SCRTCHPD_REG_OFFSET_ADDR));

    // printk(KERN_INFO "%s in_task(): %s", __func__, in_task()? "true":"false");

    return 0x789a;
}

u32 wait_till_done(void __iomem *base_addr, u32 reg, u32 bitmask, u32 *done, u32 timeout_us, u32 sleeptime_us)
{
    u32 i;

    *done = 0;

    u32 timeout_count = timeout_us / sleeptime_us;

    for (i = 0; i < timeout_count; i++) {
        *done = ioread32(base_addr + reg);
        if (*done & bitmask) {
            break;
        }
        usleep_range(sleeptime_us, sleeptime_us+1);
    }

    if (i >= timeout_count) {
        PRINTK(KERN_INFO, "timed out on reading reg 0x%x after %d us\n", reg, timeout_us);
        return -1;
    }

    /*if (verbose) {
        printk(KERN_INFO "done in %d loops, %d us\n", i, i * sleeptime_us);
    }*/
    return 0;
}

void spi_reset_bus(void __iomem *base_addr, u8 pim, u8 rtc_inx)
{
    u32 base = PIM_BASE_ADDR + ((pim - 1) * PIM_REG_SIZE) + SPI_MASTER_CSR_BASE_ADDR + (SPI_CFG_SIZE * rtc_inx);


    // Write 1 to reset the SPI controller. Auto-clear to 0.
    iowrite32(0x01, base_addr + base + SPI_RST_OFFSET_ADDR);
    udelay(1); // Need to check with DOM FPGA specs
}

uint32_t obo_spi_read (void __iomem *base_addr, u32 pim, u32 rtc_idx, uint32_t page, uint32_t start, uint8_t byte_len, uint8_t *buf)
{
    u32 i, done, done_bit, ret;
    u32 desc_h_reg, desc_l_reg, data_w_reg, data_r_reg, desc_l, desc_h;
    u32 pim_base = PIM_BASE_ADDR + (pim - 1) * PIM_REG_SIZE;
    u32 u32_len = byte_len;
    uint8_t read_data[134];
    uint8_t write_ready_byte;


    desc_l_reg = pim_base + SPI_MASTER_CSR_BASE_ADDR + SPI_DSC_L_OFFSET_ADDR + (rtc_idx * SPI_CFG_SIZE);
    desc_h_reg = pim_base + SPI_MASTER_CSR_BASE_ADDR + SPI_DSC_H_OFFSET_ADDR + (rtc_idx * SPI_CFG_SIZE);
    data_w_reg = pim_base + SPI_W_DATA_REG_OFFSET_ADDR + (rtc_idx * SPI_DATA_SIZE);
    data_r_reg = pim_base + SPI_R_DATA_REG_OFFSET_ADDR + (rtc_idx * SPI_DATA_SIZE);

    PRINTK(KERN_INFO, "%s line#%d pim: %d rtc: %d page: %d offset: %d len: %d base_addr: 0x%lx pim_base: 0x%lx desc_l_reg: 0x%lx desc_h_reg: 0x%lx data_w_reg: 0x%lx data_r_reg: 0x%lx\n",
                     __func__, __LINE__, pim, rtc_idx, page, start, byte_len, base_addr, pim_base, desc_l_reg, desc_h_reg, data_w_reg, data_r_reg);

    // Check the SPI controller status
    done_bit = 0x00000001;
    ret = wait_till_done(base_addr, desc_h_reg,
                         done_bit, &done, timeout, sleeptime);
    // printk(KERN_INFO "%s %d %d", __func__, __LINE__, ret);
    if (ret == -1) {
        // write 1 to clear done.
        PRINTK(KERN_INFO, "%s line#%d Write 1 to clear bit done\n", __func__, __LINE__);
        iowrite32(0x01, base_addr + desc_h_reg);
    }

    desc_h = 0x03;
    { // Read transaction
        u32 data_out = 0;
        data_out |= (byte_len - 1) << 8;
        data_out |= (page & 0xff) << 16;
        data_out |= (start & 0xff) << 24;
        iowrite32(data_out, base_addr + data_w_reg);

        desc_l = 0;
        desc_l = 0x80000000                 /* BITS(SPI_DESC_L_VALID, 1) */
                 | 0                        /* BITS(SPI_DESC_L_WR, 0) */
                 | ((byte_len + 6) << 8)    /* BITS(SPI_DESC_L_DATA_BYTELEN, byte_len + 6) */
                 | 0x2                      /* BITS(SPI_DESC_L_ERR_INT, 1) */
                 | 0x1;                     /* BITS(SPI_DESC_L_DONE_INT, 1); */

        iowrite32(desc_h, base_addr + desc_h_reg);

        iowrite32(desc_l, base_addr + desc_l_reg);
        
        // Check the OBO done register.
        done_bit = 0x00000001;
        ret = wait_till_done(base_addr, desc_h_reg,
                         done_bit, &done, timeout, sleeptime);

        if (ret != 0)
        {
            PRINTK(KERN_INFO, "%s line#%d wait failed\n", __func__, __LINE__);
            return -1;
        }
    }
    
    // 8bits access
    // for (i = 0; i < u32_len; i++) {
    //     buf[i] = ioread8(base_addr + data_r_reg + 6 + i);
    // }
    
    // 32bits access
    uint8_t total_bytes = u32_len + 6;
    uint8_t total_read_loop = total_bytes / 4;
    uint32_t buf32[140] = {0};
    uint32_t data_reg_32 = data_r_reg;

    if (total_bytes % 4 != 0)
    {
        total_read_loop += 1;
    }
    PRINTK(KERN_INFO, "total read bytes: %d total read loops: %d\n",total_bytes, total_read_loop);

    for (i = 0; i < total_read_loop; i++)
    {
        buf32[i] = ioread32(base_addr + data_reg_32);
        
        // printk(KERN_INFO "%d %lx %08x\n", i, data_reg_32, buf32[i]);
        data_reg_32 += 4;
    }
    memcpy(buf, ((uint8_t*)buf32)+6, u32_len);


    // for returning the 5th byte (Write Ready status) to spi_check_status
    write_ready_byte = ioread8(base_addr + data_r_reg + 5);
    
    if ( write_ready_byte == 0x00 )
    {
        return 0;
    }
    else
    {
        PRINTK(KERN_INFO, "%s#%d write_ready_byte: %02x\n",__func__, __LINE__, write_ready_byte);
        return 1;
    }
}

uint32_t obo_spi_write (void __iomem *base_addr, u32 pim, u32 rtc_idx, uint32_t page, uint32_t start, uint8_t byte_len, uint8_t *buf)
{
    u32 i, j, done, done_bit, ret;
    u32 desc_h_reg, desc_l_reg, data_w_reg, data_r_reg, desc_l, desc_h;
    u32 pim_base = PIM_BASE_ADDR + (pim - 1) * PIM_REG_SIZE;
    u32 u32_len = byte_len;


    desc_l_reg = pim_base + SPI_MASTER_CSR_BASE_ADDR + SPI_DSC_L_OFFSET_ADDR + (rtc_idx * SPI_CFG_SIZE);
    desc_h_reg = pim_base + SPI_MASTER_CSR_BASE_ADDR + SPI_DSC_H_OFFSET_ADDR + (rtc_idx * SPI_CFG_SIZE);
    data_w_reg = pim_base + SPI_W_DATA_REG_OFFSET_ADDR + (rtc_idx * SPI_DATA_SIZE);
    data_r_reg = pim_base + SPI_R_DATA_REG_OFFSET_ADDR + (rtc_idx * SPI_DATA_SIZE);

    PRINTK(KERN_INFO, "%s line#%d pim: %d rtc: %d page: %d offset: %d len: %d base_addr: 0x%lx pim_base: 0x%lx desc_l_reg: 0x%lx desc_h_reg: 0x%lx data_w_reg: 0x%lx data_r_reg: 0x%lx\n",
                      __func__, __LINE__, pim, rtc_idx, page, start, byte_len, base_addr, pim_base, desc_l_reg, desc_h_reg, data_w_reg, data_r_reg);

    // Check the SPI controller status
    done_bit = 0x00000001;
    ret = wait_till_done(base_addr, desc_h_reg,
                         done_bit, &done, timeout, sleeptime);
    // printk(KERN_INFO "%s %d %d", __func__, __LINE__, ret);
    if (ret == -1) {
        // write 1 to clear done.
        PRINTK(KERN_INFO, "%s line#%d Write 1 to clear bit done\n", __func__, __LINE__);
        iowrite32(0x01, base_addr + desc_h_reg);
    }
    
    desc_h = 0x03;
    { // write transaction

        // Reset SPI bus
        spi_reset_bus(base_addr, pim, rtc_idx);

        desc_l = 0;
        desc_l = 0x80000000                 /* BITS(SPI_DESC_L_VALID, 1) */
                 | (1 << 30)                /* BITS(SPI_DESC_L_WR, 1) */
                 | ((u32_len + 6) << 8)     /* BITS(SPI_DESC_L_DATA_BYTELEN, u32_len + 6) */
                 | 0x2                      /* BITS(SPI_DESC_L_ERR_INT, 1) */
                 | 0x1;                     /* BITS(SPI_DESC_L_DONE_INT, 1); */

        // Preliminary SPI Access Protocol
        u32 data_out = 0x80;
        data_out |= (byte_len - 1) << 8;
        data_out |= (page & 0xff) << 16;
        data_out |= (start & 0xff) << 24;
        iowrite32(data_out, base_addr + data_w_reg);

        for (i = 0; i < (byte_len / 4) + 1; data_w_reg += 4, i++) {
            u32 regis_data = 0;
            for (j = 0; j < 4; j++) {
                if ((i == 0) && (j == 0)) {
                    regis_data = 0;
                    j = 1;
                } else {
                    regis_data |= *(buf++) << (8 * j);
                }
            }
            iowrite32(regis_data, base_addr + data_w_reg + 4);
        }

        iowrite32(desc_h, base_addr + desc_h_reg);
        iowrite32(desc_l, base_addr + desc_l_reg);

        // Check the OBO done register.
        done_bit = 0x00000001;
        ret = wait_till_done(base_addr, desc_h_reg,
                         done_bit, &done, timeout, sleeptime);

        if (ret != 0)
        {
            PRINTK(KERN_INFO, "%s line#%d wait failed\n", __func__, __LINE__);
            return -1;
        }
    }
   
    return 0;
}

uint32_t spi_check_status(void __iomem *base_addr, uint32_t pim, uint32_t rtc_idx)
{
    uint32_t offset = 0x80; // Byte 128
    uint32_t byte_len = 1;
    uint32_t page = 0xA0;
    uint32_t bank = 0;
    uint8_t buf[20];
    int ret = 0;
    int i;

    
    for (i = 0; i < 5; i++) {
        ret = obo_spi_read(base_addr, pim, rtc_idx, page, offset, byte_len, buf);
        if (ret == 0) {
            ret = 0;
            break;
        } else {
            ret = -1;
        }
        usleep_range(5, 6);
    }

    return ret;
}
