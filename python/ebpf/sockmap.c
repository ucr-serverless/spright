#include <linux/bpf.h>
#include <linux/ptrace.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

#define MAX_SOCK_MAP_MAP_ENTRIES 65535

struct bpf_map_def SEC("maps") sock_map = {
        .type = BPF_MAP_TYPE_SOCKHASH,
        .key_size = sizeof(int),
        .value_size = sizeof(int),
        .max_entries = MAX_SOCK_MAP_MAP_ENTRIES,
        .map_flags = 0
};

char _license[] SEC("license") = "GPL";