#include <linux/bpf.h>
#include <linux/ptrace.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

/* user accessible metadata for SK_MSG packet hook, new fields must
 * be added to the end of this structure
 */
//struct sk_msg_md {
//    __bpf_md_ptr(void *, data);
//    __bpf_md_ptr(void *, data_end);
//
//    __u32 family;
//    __u32 remote_ip4;	/* Stored in network byte order */
//    __u32 local_ip4;	/* Stored in network byte order */
//    __u32 remote_ip6[4];	/* Stored in network byte order */
//    __u32 local_ip6[4];	/* Stored in network byte order */
//    __u32 remote_port;	/* Stored in network byte order */
//    __u32 local_port;	/* stored in host byte order */
//    __u32 size;		/* Total size of sk_msg */
//
//    __bpf_md_ptr(struct bpf_sock *, sk); /* current socket */
//};

#define MAX_FUNC 100 // A kubernetes worker node can have up to 100 pods
#define MAX_SOCK_MAP_MAP_ENTRIES 65535

/* This is the data record stored in the map */
struct datarec {
    __u64 rx_packets;
};

struct bpf_map_def SEC("maps") skmsg_stats_map = {
    .type        = BPF_MAP_TYPE_PERCPU_ARRAY,
    .key_size    = sizeof(int),
    .value_size  = sizeof(struct datarec),
    .max_entries = MAX_FUNC,
};

struct bpf_map_def SEC("maps") sock_map = {
    .type = BPF_MAP_TYPE_SOCKMAP,
    .key_size = sizeof(int),
    .value_size = sizeof(int),
    .max_entries = MAX_SOCK_MAP_MAP_ENTRIES,
    .map_flags = 0
};

SEC("sk_msg_tx")
int bpf_skmsg_tx(struct sk_msg_md *msg)
{
    char* data = msg->data;
    char* data_end = msg->data_end;
    bpf_printk("[sk_msg_tx] get a skmsg of length %d", msg->size);

    if (data + 4 > data_end) {
        return SK_DROP;
    }

    int next_fn_id = *((int*)data);
    bpf_printk("[sk_msg_tx] redirect to socket of Fn#%d", next_fn_id);

    struct datarec *rec = bpf_map_lookup_elem(&skmsg_stats_map, &next_fn_id);
    if (!rec) {
        bpf_printk("Fn#%d has no entry in skmsg_stats_map", next_fn_id);
        return SK_DROP;
    }

    rec->rx_packets++;

    int ret = 0;
    ret = bpf_msg_redirect_map(msg, &sock_map, next_fn_id, BPF_F_INGRESS);

    bpf_printk("TRY REDIRECT TO Fn#%d", next_fn_id);
    if (ret != SK_PASS) {
        bpf_printk("Fn#%d is not locally available, redirect to Gateway", next_fn_id);

        next_fn_id = 0;
        ret = bpf_msg_redirect_map(msg, &sock_map, next_fn_id, BPF_F_INGRESS);

        if (ret != SK_PASS)
            bpf_printk("TRY REDIRECT TO Gateway failed");
    }

    return ret;
}

char _license[] SEC("license") = "GPL";