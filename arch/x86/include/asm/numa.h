/*
 * Copyright (c) 2016 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _ASM_X86_NUMA_H_
#define _ASM_X86_NUMA_H_

#include <asm/apic.h>

#include <lego/cpumask.h>
#include <lego/nodemask.h>
#include <lego/compiler.h>

#define NR_NODE_MEMBLKS		(MAX_NUMNODES*2)

extern int cpu_to_node_map[NR_CPUS];
extern int __apicid_to_node[MAX_LOCAL_APIC];
extern nodemask_t numa_nodes_parsed;

int apicid_to_node(int apicid);
void set_apicid_to_node(int apicid, int node);
int cpu_to_node(int cpu);
void numa_set_node(int cpu, int node);
void numa_clear_node(int cpu);
void __init init_cpu_to_node(void);

#endif /* _ASM_X86_NUMA_H_ */
