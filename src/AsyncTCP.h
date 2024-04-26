/*
  Asynchronous TCP library for Espressif MCUs

  Copyright (c) 2016 Hristo Gochkov. All rights reserved.
  This file is part of the esp8266 core for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef ASYNCTCP_H_
#define ASYNCTCP_H_

#define ASYNCTCP_VERSION "3.0.2"
#define ASYNCTCP_VERSION_MAJOR 0
#define ASYNCTCP_VERSION_MINOR 0
#define ASYNCTCP_VERSION_REVISION 2
#define ASYNCTCP_FORK_mathieucarbou

#include "IPAddress.h"
#if ESP_IDF_VERSION_MAJOR < 5
#include "IPv6Address.h"
#endif
#include <functional>
#include "lwip/ip_addr.h"
#include "lwip/ip6_addr.h"

#ifndef LIBRETINY
#include "sdkconfig.h"
extern "C" {
    #include "freertos/semphr.h"
    #include "lwip/pbuf.h"
}
#else
extern "C" {
    #include <semphr.h>
    #include <lwip/pbuf.h>
}
#define CONFIG_ASYNC_TCP_RUNNING_CORE -1 //any available core
#define CONFIG_ASYNC_TCP_USE_WDT 0
#endif

//If core is not defined, then we are running in Arduino or PIO
#ifndef CONFIG_ASYNC_TCP_RUNNING_CORE
#define CONFIG_ASYNC_TCP_RUNNING_CORE -1 //any available core
#define CONFIG_ASYNC_TCP_USE_WDT 1 //if enabled, adds between 33us and 200us per event
#endif

#ifndef CONFIG_ASYNC_TCP_STACK_SIZE
#define CONFIG_ASYNC_TCP_STACK_SIZE 8192 * 2
#endif

#ifndef CONFIG_ASYNC_TCP_PRIORITY
#define CONFIG_ASYNC_TCP_PRIORITY 10
#endif

#ifndef CONFIG_ASYNC_TCP_QUEUE_SIZE
#define CONFIG_ASYNC_TCP_QUEUE_SIZE 64
#endif

class AsyncClient;

#define ASYNC_MAX_ACK_TIME 5000
#define ASYNC_WRITE_FLAG_COPY 0x01 //will allocate new buffer to hold the data while sending (else will hold reference to the data given)
#define ASYNC_WRITE_FLAG_MORE 0x02 //will not send PSH flag, meaning that there should be more data to be sent before the application should react.

typedef std::function<void(void*, AsyncClient*)> AcConnectHandler;
typedef std::function<void(void*, AsyncClient*, size_t len, uint32_t time)> AcAckHandler;
typedef std::function<void(void*, AsyncClient*, int8_t error)> AcErrorHandler;
typedef std::function<void(void*, AsyncClient*, void *data, size_t len)> AcDataHandler;
typedef std::function<void(void*, AsyncClient*, struct pbuf *pb)> AcPacketHandler;
typedef std::function<void(void*, AsyncClient*, uint32_t time)> AcTimeoutHandler;

struct tcp_pcb;
struct ip_addr;

class AsyncClient {
  public:
    AsyncClient(tcp_pcb* pcb = 0);
    ~AsyncClient();

    AsyncClient & operator=(const AsyncClient &other);
    AsyncClient & operator+=(const AsyncClient &other);

    bool operator==(const AsyncClient &other);

    bool operator!=(const AsyncClient &other) {
      return !(*this == other);
    }
    bool connect(IPAddress ip, uint16_t port);
#if ESP_IDF_VERSION_MAJOR < 5
    bool connect(IPv6Address ip, uint16_t port);
#endif
    bool connect(const char *host, uint16_t port);
    void close(bool now = false);
    void stop();
    int8_t abort();
    bool free();

    bool canSend();//ack is not pending
    size_t space();//space available in the TCP window
    size_t add(const char* data, size_t size, uint8_t apiflags=ASYNC_WRITE_FLAG_COPY);//add for sending
    bool send();//send all data added with the method above

    //write equals add()+send()
    size_t write(const char* data);
    size_t write(const char* data, size_t size, uint8_t apiflags=ASYNC_WRITE_FLAG_COPY); //only when canSend() == true

    uint8_t state();
    bool connecting();
    bool connected();
    bool disconnecting();
    bool disconnected();
    bool freeable();//disconnected or disconnecting

    uint16_t getMss();

    uint32_t getRxTimeout();
    void setRxTimeout(uint32_t timeout);//no RX data timeout for the connection in seconds

    uint32_t getAckTimeout();
    void setAckTimeout(uint32_t timeout);//no ACK timeout for the last sent packet in milliseconds

    void setNoDelay(bool nodelay);
    bool getNoDelay();

    void setKeepAlive(uint32_t ms, uint8_t cnt);

    uint32_t getRemoteAddress();
    uint16_t getRemotePort();
    uint32_t getLocalAddress();
    uint16_t getLocalPort();
#if LWIP_IPV6
    ip6_addr_t getRemoteAddress6();
    ip6_addr_t getLocalAddress6();
#if ESP_IDF_VERSION_MAJOR < 5
    IPv6Address remoteIP6();
    IPv6Address localIP6();
#else
    IPAddress remoteIP6();
    IPAddress localIP6();
#endif
#endif

    //compatibility
    IPAddress remoteIP();
    uint16_t remotePort();
    IPAddress localIP();
    uint16_t localPort();

    void onConnect(AcConnectHandler cb, void* arg = 0);     //on successful connect
    void onDisconnect(AcConnectHandler cb, void* arg = 0);  //disconnected
    void onAck(AcAckHandler cb, void* arg = 0);             //ack received
    void onError(AcErrorHandler cb, void* arg = 0);         //unsuccessful connect or error
    void onData(AcDataHandler cb, void* arg = 0);           //data received (called if onPacket is not used)
    void onPacket(AcPacketHandler cb, void* arg = 0);       //data received
    void onTimeout(AcTimeoutHandler cb, void* arg = 0);     //ack timeout
    void onPoll(AcConnectHandler cb, void* arg = 0);        //every 125ms when connected

    void ackPacket(struct pbuf * pb);//ack pbuf from onPacket
    size_t ack(size_t len); //ack data that you have not acked using the method below
    void ackLater(){ _ack_pcb = false; } //will not ack the current packet. Call from onData

    const char * errorToString(int8_t error);
    const char * stateToString();

    //Do not use any of the functions below!
    static int8_t _s_poll(void *arg, struct tcp_pcb *tpcb);
    static int8_t _s_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *pb, int8_t err);
    static int8_t _s_fin(void *arg, struct tcp_pcb *tpcb, int8_t err);
    static int8_t _s_lwip_fin(void *arg, struct tcp_pcb *tpcb, int8_t err);
    static void _s_error(void *arg, int8_t err);
    static int8_t _s_sent(void *arg, struct tcp_pcb *tpcb, uint16_t len);
    static int8_t _s_connected(void* arg, void* tpcb, int8_t err);
    static void _s_dns_found(const char *name, struct ip_addr *ipaddr, void *arg);

    int8_t _recv(tcp_pcb* pcb, pbuf* pb, int8_t err);
    tcp_pcb * pcb(){ return _pcb; }

  protected:
    bool _connect(ip_addr_t addr, uint16_t port);

    tcp_pcb* _pcb;
    int8_t  _closed_slot;

    AcConnectHandler _connect_cb;
    void* _connect_cb_arg;
    AcConnectHandler _discard_cb;
    void* _discard_cb_arg;
    AcAckHandler _sent_cb;
    void* _sent_cb_arg;
    AcErrorHandler _error_cb;
    void* _error_cb_arg;
    AcDataHandler _recv_cb;
    void* _recv_cb_arg;
    AcPacketHandler _pb_cb;
    void* _pb_cb_arg;
    AcTimeoutHandler _timeout_cb;
    void* _timeout_cb_arg;
    AcConnectHandler _poll_cb;
    void* _poll_cb_arg;

    bool _ack_pcb;
    uint32_t _tx_last_packet;
    uint32_t _rx_ack_len;
    uint32_t _rx_last_packet;
    uint32_t _rx_timeout;
    uint32_t _rx_last_ack;
    uint32_t _ack_timeout;
    uint16_t _connect_port;

    int8_t _close();
    void _free_closed_slot();
    void _allocate_closed_slot();
    int8_t _connected(void* pcb, int8_t err);
    void _error(int8_t err);
    int8_t _poll(tcp_pcb* pcb);
    int8_t _sent(tcp_pcb* pcb, uint16_t len);
    int8_t _fin(tcp_pcb* pcb, int8_t err);
    int8_t _lwip_fin(tcp_pcb* pcb, int8_t err);
    void _dns_found(struct ip_addr *ipaddr);

  public:
    AsyncClient* prev;
    AsyncClient* next;
};

class AsyncServer {
  public:
    AsyncServer(IPAddress addr, uint16_t port);
#if ESP_IDF_VERSION_MAJOR < 5
    AsyncServer(IPv6Address addr, uint16_t port);
#endif
    AsyncServer(uint16_t port);
    ~AsyncServer();
    void onClient(AcConnectHandler cb, void* arg);
    void begin();
    void end();
    void setNoDelay(bool nodelay);
    bool getNoDelay();
    uint8_t status();

    //Do not use any of the functions below!
    static int8_t _s_accept(void *arg, tcp_pcb* newpcb, int8_t err);
    static int8_t _s_accepted(void *arg, AsyncClient* client);

  protected:
    uint16_t _port;
    bool _bind4 = false;
    bool _bind6 = false;
    IPAddress _addr;
#if ESP_IDF_VERSION_MAJOR < 5
    IPv6Address _addr6;
#endif
    bool _noDelay;
    tcp_pcb* _pcb;
    AcConnectHandler _connect_cb;
    void* _connect_cb_arg;

    int8_t _accept(tcp_pcb* newpcb, int8_t err);
    int8_t _accepted(AsyncClient* client);
};


#endif /* ASYNCTCP_H_ */
