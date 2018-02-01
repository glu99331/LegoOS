/*
 * Copyright (c) 2016-2017 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <lego/sched.h>
#include <lego/init.h>
#include <lego/mm.h>
#include <lego/net.h>
#include <lego/kthread.h>
#include <lego/workqueue.h>
#include <lego/list.h>
#include <lego/string.h>
#include <lego/jiffies.h>
#include <lego/pci.h>
#include <lego/delay.h>
#include <lego/slab.h>
#include <lego/time.h>
#include <lego/timer.h>
#include <lego/kernel.h>
#include <rdma/ib_verbs.h>

#include "fit_internal.h"

/*
 * NOTE:
 * This array specifies hostname of machines you want to use in Lego cluster.
 * Hostnames are listed by the order of FIT node ID. Any wrong configuration
 * lead to an early panic.
 */
static const char *lego_cluster_hostnames[CONFIG_FIT_NR_NODES] = {
	[0]	= 	"wuklabXX",
	[1]	= 	"wuklabXX",
};

/* Built based on node id */
struct fit_machine_info *lego_cluster[CONFIG_FIT_NR_NODES];

static struct fit_machine_info WUKLAB_CLUSTER[] = {
[0]	= {	.hostname =	"wuklab00",	.lid =	2,	.first_qpn =	0,	},
[1]	= {	.hostname =	"wuklab01",	.lid =	6,	.first_qpn =	0,	},
[2]	= {	.hostname =	"wuklab02",	.lid =	8,	.first_qpn =	0,	},
[3]	= {	.hostname =	"wuklab03",	.lid =	9,	.first_qpn =	74,	},
[4]	= {	.hostname =	"wuklab04",	.lid =	7,	.first_qpn =	72,	},
[5]	= {	.hostname =	"wuklab05",	.lid =	3,	.first_qpn =	0,	},
[6]	= {	.hostname =	"wuklab06",	.lid =	5,	.first_qpn =	0,	},
[7]	= {	.hostname =	"wuklab07",	.lid =	4,	.first_qpn =	0,	},
[8]	= {	.hostname =	"wuklab08",	.lid =	10,	.first_qpn =	0,	},
[9]	= {	.hostname =	"wuklab09",	.lid =	12,	.first_qpn =	0,	},
[10]	= {	.hostname =	"wuklab10",	.lid =	11,	.first_qpn =	0,	},
[11]	= {	.hostname =	"wuklab11",	.lid =	14,	.first_qpn =	0,	},
[12]	= {	.hostname =	"wuklab12",	.lid =	13,	.first_qpn =	72,	},
[13]	= {	.hostname =	"wuklab13",	.lid =	15,	.first_qpn =	72,	},
[14]	= {	.hostname =	"wuklab14",	.lid =	16,	.first_qpn =	74,	},
[15]	= {	.hostname =	"wuklab15",	.lid =	17,	.first_qpn =	72,	},
[16]	= {	.hostname =	"wuklab16",	.lid =	20,	.first_qpn =	74,	},
[17]	= {	.hostname =	"wuklab17",	.lid =	21,	.first_qpn =	0,	},
[18]	= {	.hostname =	"wuklab18",	.lid =	19,	.first_qpn =	0,	},
[19]	= {	.hostname =	"wuklab19",	.lid =	18,	.first_qpn =	74,	},
};

/* Indicate machines that are used by lego */
static DECLARE_BITMAP(cluster_used_machines, 32);

unsigned int global_lid[CONFIG_FIT_NR_NODES];
unsigned int first_qpn;

unsigned int get_global_lid(unsigned int nid)
{
	BUG_ON(nid >= CONFIG_FIT_NR_NODES);
	return global_lid[nid];
}

/*
 * Fill the lego_cluster and global_lid array based on nid.
 * Return 0 on success, return 1 if duplicates
 */
static int assign_fit_machine(unsigned int nid, struct fit_machine_info *machine)
{
	unsigned int machine_index;

	machine_index = machine - WUKLAB_CLUSTER;
	if (test_and_set_bit(machine_index, cluster_used_machines))
		return 1;
	BUG_ON(global_lid[nid]);

	lego_cluster[nid] = machine;
	global_lid[nid] = lego_cluster[nid]->lid;

	return 0;
}

static struct fit_machine_info *find_fit_machine(const char *hostname)
{
	struct fit_machine_info *machine;
	int i;

	/* Linear search for a small cluster */
	for (i = 0; i < ARRAY_SIZE(WUKLAB_CLUSTER); i++) {
		machine = &WUKLAB_CLUSTER[i];
		if (!strncmp(hostname, machine->hostname, FIT_NAME_MAX))
			return machine;
	}
	return NULL;
}

static void assign_current_first_qpn(void)
{
	struct fit_machine_info *self;

	self = lego_cluster[CONFIG_FIT_LOCAL_ID];
	if (self->first_qpn == 0) {
		pr_debug("***   WARNING: %s first_qpn not finalized, "
			"default to use 72", self->hostname);
		self->first_qpn = 72;
	}
	first_qpn = self->first_qpn;
}

/*
 * Statically setting LIDs and QPNs now
 * since we don't have socket working
 */
void init_global_lid_qpn(void)
{
	int nid;
	bool bug = false;

#if defined(CONFIG_FIT_LOCAL_ID) && defined(CONFIG_FIT_NR_NODES)
	BUILD_BUG_ON(CONFIG_FIT_LOCAL_ID >= CONFIG_FIT_NR_NODES);
#else
	BUILD_BUG_ON(1);
#endif

	/*
	 * Build the machine list based on user provided
	 * hostnames, including global_lid array and first_qpn.
	 */
	for (nid = 0; nid < CONFIG_FIT_NR_NODES; nid++) {
		struct fit_machine_info *machine;
		const char *hostname = lego_cluster_hostnames[nid];

		if (!hostname) {
			pr_debug("    Empty hostname on node %d\n", nid);
			bug = true;
			continue;
		}

		machine = find_fit_machine(hostname);
		if (!machine) {
			pr_debug("    Wrong hostname %s on node %d\n",
				hostname, nid);
			bug = true;
			continue;
		}

		if (assign_fit_machine(nid, machine)) {
			pr_debug("    Duplicated hostname %s on node %d\n",
				hostname, nid);
			bug = true;
		}
	}
	if (bug)
		panic("Please check your network config!");

	assign_current_first_qpn();
}

void print_gloabl_lid(void)
{
	int nid;

	pr_debug("***  FIT_initial_timeout_s:   %d\n", CONFIG_FIT_INITIAL_SLEEP_TIMEOUT);
	pr_debug("***  FIT_first_qpn:           %d\n", first_qpn);
	pr_debug("***  FIT_local_id:            %d\n", CONFIG_FIT_LOCAL_ID);
	for (nid = 0; nid < CONFIG_FIT_NR_NODES; nid++) {
		pr_debug("***    [%d] %s lid=%2d",
			nid, lego_cluster[nid]->hostname, global_lid[nid]);

		if (nid == CONFIG_FIT_LOCAL_ID)
			pr_cont(" <---\n");
		else
			pr_cont("\n");
	}
}
