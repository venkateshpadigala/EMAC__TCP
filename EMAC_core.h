#ifndef ADSP_SC594_EMAC_H
#define ADSP_SC594_EMAC_H

#include <sys/platform.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "core/net.h"
#include "nic.h"
#include "drivers/phy/lan8831_driver.h"

/* --- ADSP-SC594 Register Base Addresses --- */
#define EMAC0_BASE               0x310C0000 
#define SEC0_BASE                0x31080000
#define PADS0_BASE               0x31000000

/* --- Memory-Mapped Register Offsets --- */
#define pREG_PADS0_PCFG0         ((volatile uint32_t *)(PADS0_BASE + 0x0000))
#define pREG_EMAC_MAC_CFG        ((volatile uint32_t *)(EMAC0_BASE + 0x0000))
#define pREG_EMAC_MAC_FRAME_FLTR ((volatile uint32_t *)(EMAC0_BASE + 0x0004))
#define pREG_EMAC_MAC_ADDR0_HI   ((volatile uint32_t *)(EMAC0_BASE + 0x0040))
#define pREG_EMAC_MAC_ADDR0_LO   ((volatile uint32_t *)(EMAC0_BASE + 0x0044))
#define pREG_EMAC_MAC_MDIO_ADDR  ((volatile uint32_t *)(EMAC0_BASE + 0x0010))
#define pREG_EMAC_MAC_MDIO_DATA  ((volatile uint32_t *)(EMAC0_BASE + 0x0014))

#define pREG_EMAC_DMA_BUS_MODE   ((volatile uint32_t *)(EMAC0_BASE + 0x1000))
#define pREG_EMAC_DMA_TX_POLL    ((volatile uint32_t *)(EMAC0_BASE + 0x1004))
#define pREG_EMAC_DMA_RX_POLL    ((volatile uint32_t *)(EMAC0_BASE + 0x1008))
#define pREG_EMAC_DMA_RX_BASE    ((volatile uint32_t *)(EMAC0_BASE + 0x100C))
#define pREG_EMAC_DMA_TX_BASE    ((volatile uint32_t *)(EMAC0_BASE + 0x1010))
#define pREG_EMAC_DMA_STAT       ((volatile uint32_t *)(EMAC0_BASE + 0x1014))
#define pREG_EMAC_DMA_OPMODE     ((volatile uint32_t *)(EMAC0_BASE + 0x1018))
#define pREG_EMAC_DMA_INT_EN     ((volatile uint32_t *)(EMAC0_BASE + 0x101C))

#define pREG_SEC0_GCTL           ((volatile uint32_t *)(SEC0_BASE + 0x0000))
#define pREG_SEC0_CCTL0          ((volatile uint32_t *)(SEC0_BASE + 0x0004)) // Core 0
#define pREG_SEC0_CSID0          ((volatile uint32_t *)(SEC0_BASE + 0x0014))
#define pREG_SEC0_END            ((volatile uint32_t *)(SEC0_BASE + 0x0024))
#define pREG_SEC0_SCTL223        ((volatile uint32_t *)(SEC0_BASE + 0x0400 + (223 * 4))) // SEC ID 223: EMAC0_DMA0

/* --- Bit Definitions --- */
#define EMAC_DES0_OWN            (1U << 31)  // 1 = DMA owns descriptor
#define EMAC_DES0_IC             (1U << 30)  // Interrupt on Completion
#define EMAC_DES0_TX_LS          (1U << 29)  // Last Segment
#define EMAC_DES0_TX_FS          (1U << 28)  // First Segment
#define EMAC_DES1_TX_CHAIN       (1U << 24)  // Chained Mode

#define EMAC_MAC_CFG_DM          (1U << 11)  // Full Duplex
#define EMAC_MAC_CFG_FES         (1U << 14)  // Fast Ethernet Speed
#define EMAC_MAC_CFG_PS          (1U << 15)  // Port Select (0 = 1000 Mbps RGMII)

#define EMAC_DMA_INT_NIE         (1U << 16)
#define EMAC_DMA_INT_RIE         (1U << 6)
#define EMAC_DMA_INT_TIE         (1U << 0)
#define EMAC_DMA_STAT_RI         (1U << 6)
#define EMAC_DMA_STAT_TI         (1U << 0)

#define NUM_RX_DESCS             4
#define MAX_ETH_FRAME_SIZE       1536

typedef struct {
    volatile uint32_t status;       // DES0
    volatile uint32_t length;       // DES1
    volatile uint32_t buffer1_addr; // DES2
    volatile uint32_t next_desc;    // DES3
} EMAC_DMA_DESC;

extern const NicDriver adspSc594NicDriver;

void EMAC0_DMA_ISR(void);
void AdspSc594_WritePhyReg(uint8_t opcode, uint8_t phyAddr, uint8_t regAddr, uint16_t data);
uint16_t AdspSc594_ReadPhyReg(uint8_t opcode, uint8_t phyAddr, uint8_t regAddr);
error_t AdspSc594_UpdateMacConfig(NetInterface *interface);

#endif
