#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "adsp_sc594_emac.h"
#include "core/net.h"
#include "core/bsd_socket.h"

#define ADSP_SERVER_IP         IPV4_ADDR(192, 168, 16, 10)
#define SUBNET_MASK            IPV4_ADDR(255, 255, 255, 0)
#define DEFAULT_GATEWAY        IPV4_ADDR(192, 168, 16, 1)

#define SERVER_PORT            2024
#define PAYLOAD_SIZE           100
#define PERIOD_MS              40

extern const PhyDriver lan8831PhyDriver;

/* Cyclone TCP Global Context and Settings */
static NetContext netContext;
static NetSettings netSettings;
static NetInterface netInterface;

static Socket *server_socket = NULL;
static Socket *client_socket = NULL;

uint8_t rx_100b_buffer[PAYLOAD_SIZE];
uint8_t tx_100b_buffer[PAYLOAD_SIZE];

void Prepare_Tx_Data(void) {
    for (int i = 0; i < PAYLOAD_SIZE; i++) {
        tx_100b_buffer[i] = (uint8_t)(i + 1); // Test pattern: 1, 2, 3... 100
    }
    memset(rx_100b_buffer, 0, sizeof(rx_100b_buffer));
}

int main(void) {
    error_t error;
    size_t read_bytes = 0;
    size_t written_bytes = 0;
    systime_t last_40ms_tick = 0;

    Prepare_Tx_Data();

    /* 1. Configure Cyclone TCP Settings and NetContext */
    netGetDefaultSettings(&netSettings);
    netSettings.interfaces = &netInterface;
    netSettings.numInterfaces = 1;

    /* 2. Initialize Cyclone TCP Core with Arguments */
    error = netInit(&netContext, &netSettings);
    if (error != NO_ERROR) return -1;

    /* 3. Bind LAN8831 PHY Driver & ADSP-SC594 NIC Driver */
    netSetInterfaceName(&netInterface, "eth0");
    netSetMacAddr(&netInterface, &(MacAddr){{0x0E, 0x00, 0x22, 0x33, 0x11, 0xDD}});
    
    netInterface.nicDriver                  = &adspSc594NicDriver;
    netInterface.phyDriver                  = &lan8831PhyDriver;
    netInterface.phyAddr                    = 0;
    netInterface.nicDriver->writePhyReg     = AdspSc594_WritePhyReg;
    netInterface.nicDriver->readPhyReg      = AdspSc594_ReadPhyReg;
    netInterface.nicDriver->updateMacConfig = AdspSc594_UpdateMacConfig;

    error = netConfigInterface(&netInterface);
    if (error != NO_ERROR) return -1;

    /* 4. Assign Static IP: 192.168.16.10 */
    netSetIpv4Addr(&netInterface, ADSP_SERVER_IP);
    netSetIpv4SubnetMask(&netInterface, SUBNET_MASK);
    netSetIpv4DefaultGateway(&netInterface, DEFAULT_GATEWAY);

    /* 5. Open Non-Blocking TCP Server Socket on Port 2024 */
    server_socket = socketOpen(SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP);
    if (server_socket == NULL) return -1;

    socketSetTimeout(server_socket, 0); // Non-blocking mode
    socketBind(server_socket, &IP_ADDR_ANY, SERVER_PORT);
    socketListen(server_socket, 4);

    last_40ms_tick = osGetSystemTime();

    /* =========================================================================
       BAREMETAL POLLING EXECUTION LOOP (No RTOS)
       ========================================================================= */
    while (1) {
        /* 
         * Step A: Process TCP/IP Stack Events, ARP, and ICMP Ping Responses.
         * Automatic Command Prompt Pings ('ping 192.168.16.10') are answered here.
         */
        netTask(&netContext);

        /* Step B: PHY IC Check - Poll Link Status, Speed (1000M), & Duplex Mode */
        lan8831Tick(&netInterface);

        /* Step C: Accept Connection from ANY Client Computer on the Subnet */
        if (client_socket == NULL) {
            client_socket = socketAccept(server_socket, NULL, NULL);
            if (client_socket != NULL) {
                socketSetTimeout(client_socket, 0); // Non-blocking client socket
            }
        }

        /* Step D: 40 ms Periodic 100-Byte Transmission and Reception */
        systime_t current_time = osGetSystemTime();
        if (client_socket != NULL && (current_time - last_40ms_tick >= PERIOD_MS)) {
            last_40ms_tick = current_time;

            /* Receive 100 Bytes from Computer Client */
            error = socketReceive(client_socket, rx_100b_buffer, PAYLOAD_SIZE, &read_bytes, 0);
            if (error == NO_ERROR && read_bytes == PAYLOAD_SIZE) {
                // 100 bytes received successfully into rx_100b_buffer
            }

            /* Transmit 100 Bytes to Computer Client */
            error = socketSend(client_socket, tx_100b_buffer, PAYLOAD_SIZE, &written_bytes, 0);

            /* Handle Client Disconnection or Socket Failures */
            if (error != NO_ERROR && error != ERROR_WOULD_BLOCK) {
                socketClose(client_socket);
                client_socket = NULL;
            }
        }
    }

    socketClose(server_socket);
    return 0;
}
