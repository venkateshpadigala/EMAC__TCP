#ifndef NET_CONFIG_H
#define NET_CONFIG_H

#define ENABLED              1
#define DISABLED             0

/* Network Stack Subsystems */
#define IPV4_SUPPORT         ENABLED
#define ICMP_SUPPORT         ENABLED   /* Enables ICMP Echo Responder (Ping Option) */
#define TCP_SUPPORT          ENABLED   /* Enables TCP Sockets */
#define UDP_SUPPORT          DISABLED

/* Socket and Memory Allocations */
#define SOCKET_MAX_COUNT     4         /* Maximum simultaneous sockets */
#define BSD_SOCKET_SUPPORT   ENABLED   /* Enable standard BSD socket APIs */

#endif
