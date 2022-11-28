#include <linux/bpf.h>
#include <linux/ptrace.h>
// #include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

#define MAX_FUNC 100 // A kubernetes worker node can have up to 100 pods
#define MAX_SOCK_MAP_MAP_ENTRIES 65535

/* This is the data record stored in the map */
struct datarec {
	__u64 rx_packets;
};

BPF_PERCPU_HASH(skmsg_stats_map, int, struct datarec, MAX_FUNC);
BPF_SOCKHASH(sock_map, int, MAX_SOCK_MAP_MAP_ENTRIES);

int bpf_skmsg_tx(struct sk_msg_md *msg)
{
    char* data = msg->data;
    char* data_end = msg->data_end;

    if(data + 4 > data_end) {
        return SK_DROP;
    }
    int next_fn_id = *((int*)data);

    struct datarec *rec = skmsg_stats_map.lookup(&next_fn_id);
	if (!rec)
		return SK_DROP;

    rec->rx_packets++; // Warning: this value may cause race conditions
    
    int ret = 0;
    ret = sock_map.msg_redirect_hash(msg, &next_fn_id, BPF_F_INGRESS);
    bpf_trace_printk("try redirect to fn#%d\\n", next_fn_id);
    if (ret != SK_PASS)
        bpf_trace_printk("redirect to fn#%d failed\\n", next_fn_id);

    return ret;
}

// char _license[] SEC("license") = "GPL";