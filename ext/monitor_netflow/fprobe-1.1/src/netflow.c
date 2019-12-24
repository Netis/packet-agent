/*
	Copyright (C) Slava Astashonok <sla@0n.ru>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License.

	$Id: netflow.c,v 1.2.2.4 2004/02/02 08:06:24 sla Exp $
*/

#include <common.h>

#include <netflow.h>

static uint16_t NetFlow1_Header[] = {
	NETFLOW_VERSION,
	NETFLOW_COUNT,
	NETFLOW_UPTIME,
	NETFLOW_UNIX_SECS,
	NETFLOW_UNIX_NSECS
};

static uint16_t NetFlow1_Flow[] = {
	NETFLOW_IPV4_SRC_ADDR,
	NETFLOW_IPV4_DST_ADDR,
	NETFLOW_IPV4_NEXT_HOP,
	NETFLOW_INPUT_SNMP,
	NETFLOW_OUTPUT_SNMP,
	NETFLOW_PKTS_32,
	NETFLOW_BYTES_32,
	NETFLOW_FIRST_SWITCHED,
	NETFLOW_LAST_SWITCHED,
	NETFLOW_L4_SRC_PORT,
	NETFLOW_L4_DST_PORT,
	NETFLOW_PAD16,
	NETFLOW_PROT,
	NETFLOW_SRC_TOS,
	NETFLOW_TCP_FLAGS,
	NETFLOW_PAD8, NETFLOW_PAD8, NETFLOW_PAD8,
	NETFLOW_PAD32
};

static uint16_t NetFlow5_Header[] = {
	NETFLOW_VERSION,
	NETFLOW_COUNT,
	NETFLOW_UPTIME,
	NETFLOW_UNIX_SECS,
	NETFLOW_UNIX_NSECS,
	NETFLOW_FLOW_SEQUENCE,
	NETFLOW_ENGINE_TYPE,
	NETFLOW_ENGINE_ID,
	NETFLOW_PAD16
};

static uint16_t NetFlow5_Flow[] = {
	NETFLOW_IPV4_SRC_ADDR,
	NETFLOW_IPV4_DST_ADDR,
	NETFLOW_IPV4_NEXT_HOP,
	NETFLOW_INPUT_SNMP,
	NETFLOW_OUTPUT_SNMP,
	NETFLOW_PKTS_32,
	NETFLOW_BYTES_32,
	NETFLOW_FIRST_SWITCHED,
	NETFLOW_LAST_SWITCHED,
	NETFLOW_L4_SRC_PORT,
	NETFLOW_L4_DST_PORT,
	NETFLOW_PAD8,
	NETFLOW_TCP_FLAGS,
	NETFLOW_PROT,
	NETFLOW_SRC_TOS,
	NETFLOW_SRC_AS,
	NETFLOW_DST_AS,
	NETFLOW_SRC_MASK,
	NETFLOW_DST_MASK,
	NETFLOW_PAD16
};

static uint16_t NetFlow7_Header[] = {
	NETFLOW_VERSION,
	NETFLOW_COUNT,
	NETFLOW_UPTIME,
	NETFLOW_UNIX_SECS,
	NETFLOW_UNIX_NSECS,
	NETFLOW_FLOW_SEQUENCE,
	NETFLOW_PAD32
};

static uint16_t NetFlow7_Flow[] = {
	NETFLOW_IPV4_SRC_ADDR,
	NETFLOW_IPV4_DST_ADDR,
	NETFLOW_IPV4_NEXT_HOP,
	NETFLOW_INPUT_SNMP,
	NETFLOW_OUTPUT_SNMP,
	NETFLOW_PKTS_32,
	NETFLOW_BYTES_32,
	NETFLOW_FIRST_SWITCHED,
	NETFLOW_LAST_SWITCHED,
	NETFLOW_L4_SRC_PORT,
	NETFLOW_L4_DST_PORT,
	NETFLOW_FLAGS7_1,
	NETFLOW_TCP_FLAGS,
	NETFLOW_PROT,
	NETFLOW_SRC_TOS,
	NETFLOW_SRC_AS,
	NETFLOW_DST_AS,
	NETFLOW_SRC_MASK,
	NETFLOW_DST_MASK,
	NETFLOW_FLAGS7_2,
	NETFLOW_ROUTER_SC
};

struct NetFlow NetFlow1 = {
	NETFLOW1_VERSION,
	NETFLOW1_HEADER_SIZE,
	NETFLOW1_MAX_FLOWS,
	NETFLOW1_FLOW_SIZE,
	NETFLOW1_SEQ_OFFSET,
	sizeof(NetFlow1_Header) / sizeof(uint16_t),
	NetFlow1_Header,
	sizeof(NetFlow1_Flow) / sizeof(uint16_t),
	NetFlow1_Flow
};

struct NetFlow NetFlow5 = {
	NETFLOW5_VERSION,
	NETFLOW5_HEADER_SIZE,
	NETFLOW5_MAX_FLOWS,
	NETFLOW5_FLOW_SIZE,
	NETFLOW5_SEQ_OFFSET,
	sizeof(NetFlow5_Header) / sizeof(uint16_t),
	NetFlow5_Header,
	sizeof(NetFlow5_Flow) / sizeof(uint16_t),
	NetFlow5_Flow
};

struct NetFlow NetFlow7 = {
	NETFLOW7_VERSION,
	NETFLOW7_HEADER_SIZE,
	NETFLOW7_MAX_FLOWS,
	NETFLOW7_FLOW_SIZE,
	NETFLOW7_SEQ_OFFSET,
	sizeof(NetFlow7_Header) / sizeof(uint16_t),
	NetFlow7_Header,
	sizeof(NetFlow7_Flow) / sizeof(uint16_t),
	NetFlow7_Flow
};
