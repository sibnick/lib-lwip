/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Per-core state storage and initialization for lwIP in NO_SYS mode.
 */

#include <uk/config.h>
#include <string.h>
#include <uk/assert.h>
#include <uk/crash.h>
#include <uk/print.h>
#include "lwip_percore.h"

struct lwip_core_state uk_lwip_core_states[LWIP_CORE_MAX];
struct lwip_core_state *lwip_core_override[LWIP_CORE_MAX];

#if defined(__Unikraft__) && defined(CONFIG_UKPLAT_CPU_MAXCOUNT)
/*
 * The platform numbers CPUs below CONFIG_UKPLAT_CPU_MAXCOUNT. The
 * state table must cover every slot the platform can hand out, or a
 * valid CPU id would fall outside uk_lwip_core_states.
 * Ticket 9a44f9e225.
 */
UK_CTASSERT(LWIP_CORE_MAX >= CONFIG_UKPLAT_CPU_MAXCOUNT);
#endif

/*
 * A core reached lwIP with an id above LWIP_CORE_MAX. Continuing here
 * would let the core use core 0 state while core 0 runs at the same
 * time. Stop the machine instead. The next lines name the bad id and
 * the fix. [Tickets 9a44f9e225, 2fc4e3e0be]
 */
void lwip_core_id_overflow(unsigned int id)
{
	uk_pr_crit("lwip: core id %u above LWIP_CORE_MAX %u: boot with at most %u CPUs or raise CONFIG_UKPLAT_CPU_MAXCOUNT\n",
		   id, (unsigned int)LWIP_CORE_MAX,
		   (unsigned int)LWIP_CORE_MAX);
	UK_CRASH("lwip: core id overflow\n");
	for (;;) { }
}

#if !defined(__Unikraft__)
__thread unsigned int lwip_test_core_id = 0;
#endif

void lwip_core_state_init(unsigned int core_id)
{
	struct lwip_core_state *cs;

	if (core_id >= LWIP_CORE_MAX)
		return;

	lwip_core_override[core_id] = NULL;

	cs = &uk_lwip_core_states[core_id];
	memset(cs, 0, sizeof(*cs));

	cs->tcp_pcb_lists[0] = &cs->tcp_listen_pcbs.pcbs;
	cs->tcp_pcb_lists[1] = &cs->tcp_bound_pcbs;
	cs->tcp_pcb_lists[2] = &cs->tcp_active_pcbs;
	cs->tcp_pcb_lists[3] = &cs->tcp_tw_pcbs;
}

void lwip_core_state_init_all(void)
{
	unsigned int i;

	for (i = 0; i < LWIP_CORE_MAX; i++)
		lwip_core_state_init(i);

	uk_pr_info("lwip: per-core state ready (%u core slots)\n",
		   (unsigned int)LWIP_CORE_MAX);
}
