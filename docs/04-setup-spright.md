# Setup SPRIGHT

TODO-1: POBM, K8S, Kn Deployment

TODO-2: Compile SPRIGHT binary

TODO-3: build SPRIGHT docker image

TODO-4: Explaim how to modify yaml file, env, volume mount, etc

TODO-5: Add helm chart support

## Misc.
### Compile the eBPF code into an object file
```bash
cd ebpf/
clang -g -O2 -c -target bpf -o sk_msg_kern.o sk_msg_kern.c

# Copy sk_msg_kern.o to obj/
cp sk_msg_kern.o ../obj/

# Debugging purpose: output bpf_printk()
sudo cat  /sys/kernel/debug/tracing/trace_pipe
```