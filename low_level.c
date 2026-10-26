#include "adsp_sc594_emac.h"

/* 32-bit Aligned Descriptors and Buffers in L2 System Memory */
static EMAC_DMA_DESC rx_ring[NUM_RX_DESCS] __attribute__((aligned(32)));
static EMAC_DMA_DESC tx_desc __attribute__((aligned(32)));

static uint8_t rx_buffer_pool[NUM_RX_DESCS][MAX_ETH_FRAME_SIZE] __attribute__((aligned(32)));
static uint8_t tx_frame_buffer[MAX_ETH_FRAME_SIZE] __attribute__((aligned(32)));

static volatile uint32_t rx_tail_idx = 0;
static NetInterface *adsp_net_interface = NULL;

static error_t adspSc594EmacInit(NetInterface *interface);
static error_t adspSc594EmacSendPacket(NetInterface *interface, const NetBuffer *buffer, size_t offset, NetTxAncillary *ancillary);

/* Cyclone TCP Driver Structure Binding */
const NicDriver adspSc594NicDriver = {
    .type                 = NIC_TYPE_ETHERNET,
    .mtu                  = 1500,
    .init                 = adspSc594EmacInit,
    .tick                 = NULL,
    .enableIrq            = NULL,
    .disableIrq           = NULL,
    .eventHandler         = NULL,
    .sendPacket           = adspSc594EmacSendPacket,
    .updateMacAddrFilter  = NULL,
    .autoPadding          = TRUE,
    .autoCrcCalc          = TRUE,
    .autoCrcVerif         = TRUE,
    .autoCrcStrip         = TRUE
};

static error_t adspSc594EmacInit(NetInterface *interface) {
    adsp_net_interface = interface;

    /* 1. Select RGMII Mode & Release PHY Reset */
    *pREG_PADS0_PCFG0 |= (1U << 2); 

    /* 2. Execute EMAC DMA Software Reset */
    *pREG_EMAC_DMA_BUS_MODE |= 0x01;
    while (*pREG_EMAC_DMA_BUS_MODE & 0x01);

    rx_tail_idx = 0;

    /* 3. Program MAC Address (0E:00:22:33:11:DD) */
    uint8_t *mac = interface->macAddr.b;
    *pREG_EMAC_MAC_ADDR0_LO = ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) |
                              ((uint32_t)mac[1] << 8)  | (uint32_t)mac[0];
    *pREG_EMAC_MAC_ADDR0_HI = ((uint32_t)mac[5] << 8)  | (uint32_t)mac[4];

    /* 4. Construct Circular Rx Descriptor Ring */
    for (int i = 0; i < NUM_RX_DESCS; i++) {
        rx_ring[i].buffer1_addr = (uint32_t)rx_buffer_pool[i];
        rx_ring[i].length       = (1U << 14) | MAX_ETH_FRAME_SIZE;
        uint32_t next_idx       = (i + 1) % NUM_RX_DESCS;
        rx_ring[i].next_desc    = (uint32_t)&rx_ring[next_idx];
        rx_ring[i].status       = EMAC_DES0_OWN | EMAC_DES0_IC;
    }
    sys_cache_flush_data_pointer((void *)rx_ring, sizeof(rx_ring));

    /* 5. Initialize Transmit Descriptor */
    tx_desc.buffer1_addr = (uint32_t)tx_frame_buffer;
    tx_desc.next_desc    = (uint32_t)&tx_desc;
    tx_desc.status       = 0;

    *pREG_EMAC_DMA_RX_BASE = (uint32_t)&rx_ring;
    *pREG_EMAC_DMA_TX_BASE = (uint32_t)&tx_desc;

    /* 6. Configure MAC Core for 1000 Mbps RGMII (PS=0), Full Duplex, Auto CRC Strip */
    *pREG_EMAC_MAC_FRAME_FLTR = 0x00000001; // Promiscuous Mode
    *pREG_EMAC_MAC_CFG = (1U << 11) | (1U << 7) | (1U << 3) | (1U << 2); 

    /* 7. Route SEC Interrupt (SEC ID 223 -> Core 0) */
    *pREG_SEC0_SCTL223 = (0U << 24) | (1U << 2) | (1U << 0);
    *pREG_SEC0_CCTL0  |= (1U << 0);
    *pREG_SEC0_GCTL   |= (1U << 0);

    /* 8. Enable DMA Channels & Start */
    *pREG_EMAC_DMA_INT_EN |= EMAC_DMA_INT_NIE | EMAC_DMA_INT_RIE | EMAC_DMA_INT_TIE;
    *pREG_EMAC_DMA_OPMODE |= (1U << 13) | (1U << 1);

    /* Kick off Rx DMA */
    *pREG_EMAC_DMA_RX_POLL = 0x01;

    return NO_ERROR;
}

static error_t adspSc594EmacSendPacket(NetInterface *interface, const NetBuffer *buffer, size_t offset, NetTxAncillary *ancillary) {
    size_t length = netBufferGetLength(buffer) - offset;

    if (length == 0 || length > MAX_ETH_FRAME_SIZE) return ERROR_INVALID_LENGTH;
    if (tx_desc.status & EMAC_DES0_OWN) return ERROR_FAILURE;

    netBufferRead(tx_frame_buffer, buffer, offset, length);
    sys_cache_flush_data_pointer(tx_frame_buffer, length);

    tx_desc.length = (length & 0x1FFF) | EMAC_DES1_TX_CHAIN;
    tx_desc.status = EMAC_DES0_OWN | EMAC_DES0_TX_FS | EMAC_DES0_TX_LS | EMAC_DES0_TX_IC;
    sys_cache_flush_data_pointer((void *)&tx_desc, sizeof(EMAC_DMA_DESC));

    *pREG_EMAC_DMA_TX_POLL = 0x01; // Kick Transmit DMA
    return NO_ERROR;
}

/**
 * @brief System Event Controller Interrupt Service Routine (EMAC0 DMA)
 */
void EMAC0_DMA_ISR(void) {
    uint32_t active_sid = *pREG_SEC0_CSID0;
    *pREG_SEC0_CSID0 = active_sid;

    uint32_t dma_stat = *pREG_EMAC_DMA_STAT;

    if (dma_stat & EMAC_DMA_STAT_RI) {
        *pREG_EMAC_DMA_STAT = EMAC_DMA_STAT_RI;

        while (!(rx_ring[rx_tail_idx].status & EMAC_DES0_OWN)) {
            uint16_t frame_len = (rx_ring[rx_tail_idx].status >> 16) & 0x3FFF;

            sys_cache_invalidate_data_pointer(rx_buffer_pool[rx_tail_idx], frame_len);

            if (adsp_net_interface != NULL) {
                /* Hand frame to Cyclone TCP Intake Pipeline */
                nicProcessPacket(adsp_net_interface, rx_buffer_pool[rx_tail_idx], frame_len, NULL);
            }

            /* Re-arm descriptor */
            rx_ring[rx_tail_idx].status = EMAC_DES0_OWN | EMAC_DES0_IC;
            sys_cache_flush_data_pointer((void *)&rx_ring[rx_tail_idx], sizeof(EMAC_DMA_DESC));

            rx_tail_idx = (rx_tail_idx + 1) % NUM_RX_DESCS;
        }
    }

    if (dma_stat & EMAC_DMA_STAT_TI) {
        *pREG_EMAC_DMA_STAT = EMAC_DMA_STAT_TI;
    }

    *pREG_SEC0_END = active_sid;
}

/* --- MDIO Registers & Dynamic MAC Configuration --- */
void AdspSc594_WritePhyReg(uint8_t opcode, uint8_t phyAddr, uint8_t regAddr, uint16_t data) {
    while (*pREG_EMAC_MAC_MDIO_ADDR & 0x01);
    *pREG_EMAC_MAC_MDIO_DATA = (uint32_t)data & 0xFFFF;
    *pREG_EMAC_MAC_MDIO_ADDR = ((uint32_t)phyAddr << 11) | ((uint32_t)regAddr << 6) | (1U << 3) | (1U << 1) | 0x01;
    while (*pREG_EMAC_MAC_MDIO_ADDR & 0x01);
}

uint16_t AdspSc594_ReadPhyReg(uint8_t opcode, uint8_t phyAddr, uint8_t regAddr) {
    while (*pREG_EMAC_MAC_MDIO_ADDR & 0x01);
    *pREG_EMAC_MAC_MDIO_ADDR = ((uint32_t)phyAddr << 11) | ((uint32_t)regAddr << 6) | (1U << 3) | 0x01;
    while (*pREG_EMAC_MAC_MDIO_ADDR & 0x01);
    return (uint16_t)(*pREG_EMAC_MAC_MDIO_DATA & 0xFFFF);
}

error_t AdspSc594_UpdateMacConfig(NetInterface *interface) {
    uint32_t mac_cfg = *pREG_EMAC_MAC_CFG;

    if (interface->duplexMode == NIC_FULL_DUPLEX_MODE) mac_cfg |= EMAC_MAC_CFG_DM;
    else mac_cfg &= ~EMAC_MAC_CFG_DM;

    if (interface->linkSpeed == NIC_LINK_SPEED_1GBPS) {
        mac_cfg &= ~EMAC_MAC_CFG_PS;  // 1000 Mbps RGMII
    } else if (interface->linkSpeed == NIC_LINK_SPEED_100MBPS) {
        mac_cfg |= EMAC_MAC_CFG_PS | EMAC_MAC_CFG_FES; // 100 Mbps
    } else {
        mac_cfg |= EMAC_MAC_CFG_PS; mac_cfg &= ~EMAC_MAC_CFG_FES; // 10 Mbps
    }

    *pREG_EMAC_MAC_CFG = mac_cfg;
    return NO_ERROR;
}
