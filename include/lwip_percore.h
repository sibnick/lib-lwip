/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Per-core stack state for lwIP in NO_SYS mode.
 */

#ifndef __LWIP_PERCORE_H__
#define __LWIP_PERCORE_H__

#include <uk/config.h>
#include <uk/essentials.h>
#include "lwip/opt.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/prot/ethernet.h"
#include "lwip/timeouts.h"

#if LWIP_SOCKET
#include "lwip/priv/sockets_priv.h"
#endif

#if defined(CONFIG_LIBUKPCPUVAR) && CONFIG_LIBUKPCPUVAR
#include <uk/pcpuvar.h>
#endif

#ifndef LWIP_CORE_MAX
#if defined(CONFIG_UKPLAT_CPU_MAXCOUNT)
#define LWIP_CORE_MAX CONFIG_UKPLAT_CPU_MAXCOUNT
#else
#define LWIP_CORE_MAX 4
#endif
#endif

#ifndef NUM_TCP_PCB_LISTS
#define NUM_TCP_PCB_LISTS 4
#endif

#ifndef ARP_TABLE_SIZE
#define ARP_TABLE_SIZE 10
#endif

#ifndef NUM_SOCKETS
#ifdef MEMP_NUM_NETCONN
#define NUM_SOCKETS MEMP_NUM_NETCONN
#else
#define NUM_SOCKETS 64
#endif
#endif

#ifndef LWIP_MEMP_MAX
#define LWIP_MEMP_MAX 32
#endif

/*
 * Per-core mode needs the per-CPU variable library. Without it,
 * lwip_current_core_id() returns 0 on every core, so all cores share
 * core 0 state. Fail at compile time instead.
 * Ticket 9a44f9e225.
 */
#if LWIP_PERCORE && defined(CONFIG_HAVE_SMP) && \
	(!defined(CONFIG_LIBUKPCPUVAR) || !CONFIG_LIBUKPCPUVAR)
#error "lwIP per-core mode requires CONFIG_LIBUKPCPUVAR"
#endif

UK_CTASSERT(LWIP_CORE_MAX > 0);

/*
 * The SNMP mib code walks the global udp_pcbs list. Per-core mode moves
 * that list into per-core state, so the code cannot compile against it.
 * This port keeps LWIP_SNMP off everywhere. Fail loudly if a
 * configuration turns it on. Ticket 7582e2ea37.
 */
#if LWIP_PERCORE && LWIP_SNMP
#error "lwIP per-core mode does not support LWIP_SNMP"
#endif

struct tcp_pcb;
struct tcp_pcb_listen;
struct memp;
struct lwip_select_cb;
struct etharp_q_entry;
struct udp_pcb;
struct raw_pcb;

#ifndef TCP_LISTEN_PCBS_T_DEFINED
#define TCP_LISTEN_PCBS_T_DEFINED
union tcp_listen_pcbs_t {
	struct tcp_pcb_listen *listen_pcbs;
	struct tcp_pcb *pcbs;
};
#endif

#ifndef ETHARP_ENTRY_DEFINED
#define ETHARP_ENTRY_DEFINED
struct etharp_entry {
#if ARP_QUEUEING
	struct etharp_q_entry *q;
#else
	struct pbuf *q;
#endif
	ip4_addr_t ipaddr;
	struct netif *netif;
	struct eth_addr ethaddr;
	u16_t ctime;
	u8_t state;
};
#endif

/* Per-core lwIP state structure */
struct lwip_core_state {
	/* TCP Protocol Control Block lists */
	struct tcp_pcb *tcp_bound_pcbs;
	union tcp_listen_pcbs_t tcp_listen_pcbs;
	struct tcp_pcb *tcp_active_pcbs;
	struct tcp_pcb *tcp_tw_pcbs;
	struct tcp_pcb **tcp_pcb_lists[NUM_TCP_PCB_LISTS];
	u8_t tcp_active_pcbs_changed;
	struct tcp_pcb *tcp_input_pcb;
	u8_t tcp_timer;
	u8_t tcp_timer_ctr;

	/* TCP Clock and Timers */
	u32_t tcp_ticks;
	struct sys_timeo *next_timeout;
	u32_t current_timeout_due_time;
	int tcpip_tcp_timer_active;

	/* TCP source port and ISS allocation. Each core keeps its own
	 * counter, so two cores cannot pick the same local port.
	 * Ticket 7582e2ea37. */
	u16_t tcp_port;
	u32_t tcp_iss;

	/* UDP source port, UDP pcb list base, and RAW pcb list base.
	 * Same reason as above. Ticket 7582e2ea37. */
	u16_t udp_port;
	struct udp_pcb *udp_pcbs;
	struct raw_pcb *raw_pcbs;

	/* Memory pool free list heads */
	struct memp *memp_tabs[LWIP_MEMP_MAX];

	/* ARP Cache */
	struct etharp_entry arp_table[ARP_TABLE_SIZE];
#if !LWIP_NETIF_HWADDRHINT
	netif_addr_idx_t etharp_cached_entry;
#endif

	/* Socket table */
#if LWIP_SOCKET
	struct lwip_sock sockets[NUM_SOCKETS];
	struct lwip_select_cb *select_cb_list;
	volatile int select_cb_ctr;
#endif
} __align(64);

extern struct lwip_core_state uk_lwip_core_states[LWIP_CORE_MAX];
extern struct lwip_core_state *lwip_core_override[LWIP_CORE_MAX];

/*
 * A core id outside uk_lwip_core_states is a configuration error: the
 * booting CPU count is above LWIP_CORE_MAX. Sharing core 0 state would
 * corrupt the pcb lists and the memp pools. Report the core id and
 * stop. Ticket 9a44f9e225.
 */
#if defined(__Unikraft__)
void lwip_core_id_overflow(unsigned int id);
#else
static inline void lwip_core_id_overflow(unsigned int id)
{
	(void)id;
	for (;;) { }
}
#endif

#if !defined(__Unikraft__)
extern __thread unsigned int lwip_test_core_id;
static inline void lwip_set_current_core_id(unsigned int id)
{
	lwip_test_core_id = id;
}
#endif

static inline unsigned int lwip_current_core_id(void)
{
#if defined(__Unikraft__)
#if defined(CONFIG_LIBUKPCPUVAR) && CONFIG_LIBUKPCPUVAR
	unsigned int id = (unsigned int)uk_pcpuvar_current_get(uk_pcpuvar_cpu_idx);
	if (unlikely(id >= LWIP_CORE_MAX)) {
		lwip_core_id_overflow(id);
		return 0; /* not reached: the call above stops the core */
	}
	return id;
#else
	return 0;
#endif
#else
	if (unlikely(lwip_test_core_id >= LWIP_CORE_MAX)) {
		lwip_core_id_overflow(lwip_test_core_id);
		return 0; /* not reached: the call above stops the core */
	}
	return lwip_test_core_id;
#endif
}

static inline struct lwip_core_state *lwip_get_core_state(void)
{
	unsigned int id = lwip_current_core_id();
	if (unlikely(id < LWIP_CORE_MAX && lwip_core_override[id] != NULL))
		return lwip_core_override[id];
	return &uk_lwip_core_states[id];
}

/*
 * Return the state of one core, or NULL when the id is outside the
 * table. Earlier code returned core 0 state instead. That let a bad
 * core id read and write another core's data with no lock. Callers
 * must check for NULL. Ticket 9a44f9e225.
 */
static inline struct lwip_core_state *lwip_get_core_state_by_id(unsigned int core_id)
{
	if (unlikely(core_id >= LWIP_CORE_MAX))
		return NULL;
	return &uk_lwip_core_states[core_id];
}

void lwip_core_state_init(unsigned int core_id);
void lwip_core_state_init_all(void);

/* Inline access macros for active PCB lists and timer heads */
#define LWIP_CORE_ACTIVE_PCBS(cs)   ((cs)->tcp_active_pcbs)
#define LWIP_CORE_TW_PCBS(cs)       ((cs)->tcp_tw_pcbs)
#define LWIP_CORE_LISTEN_PCBS(cs)   ((cs)->tcp_listen_pcbs)
#define LWIP_CORE_BOUND_PCBS(cs)    ((cs)->tcp_bound_pcbs)
#define LWIP_CORE_TIMEOUTS(cs)      ((cs)->next_timeout)
#define LWIP_CORE_TCP_TICKS(cs)     ((cs)->tcp_ticks)

#endif /* __LWIP_PERCORE_H__ */
