## SPRIGHT: Process-on-bare-metal (POBM) mode

This guideline is mainly for deploying SPRIGHT as Processes-on-bare-metal, which can ease the development and testing.

SPRIGHT supports two distinct operating modes, including the DPDK RTE RING mode (called D-SPRIGHT) and the SK_MSG model (called S-SPRIGHT). D-SPRIGHT depends on DPDK's [RTE RING](https://doc.dpdk.org/guides/prog_guide/ring_lib.html) to support descriptor delivery within the data plane, while S-SPRIGHT uses eBPF's SK_MSG to support descriptor delivery.

Both use the same shared memory backend provided by DPDK's [MEMPOOL library](https://doc.dpdk.org/guides/prog_guide/mempool_lib.html). Although SPRIGHT could work with other shared memory backends, such as POSIX shared memory in Linux, we chose DPDK's shared memory for various performance reasons. For a detailed comparison of DPDK's shared memory and POSIX shared memory, see the [following link](https://www.dpdk.org/blog/2019/08/21/memory-in-dpdk-part-1-general-concepts/).

We list the commands used to run the D-SPRIGHT. To run S-SPRIGHT, simply remove the `RTE_RING=1` from the D-SPRIGHT's commands.

**NOTE: make sure you are under the root directory of SPRIGHT, where the `run.sh` locates**

To start up the SPRIGHT's shared memory data plane, we first need to start up the shared memory manager, which is responsible for initializing the shared memory pool and buffers. We also need to input a configuration file of the function chain (`*.cfg`) to the shared memory manager. The configuration file defines the number of functions and the routes within the chain.
```shell=
# I use the example.cfg, which defined 4 dummy functions
sudo RTE_RING=1 ./run.sh shm_mgr cfg/example.cfg
```

Next, we need to bring up the SPRIGHT gateway that coordinates between the external clients and the backend SPRIGHT functions
```shell=
sudo RTE_RING=1 ./run.sh gateway
```

We then need to start up SPRIGHT functions. Since the `example.cfg` specifies 4 functions, we need to create 4 functions.
```shell=
# You may need to have multiple terminals
# Run each function in a different terminals or use tmux
# USAGE: sudo RTE_RING=1 ./run.sh nf <function_id>

# Remember to use the function id specified in the `example.cfg`

# Terminal 1
sudo RTE_RING=1 ./run.sh nf 1

# Terminal 2
sudo RTE_RING=1 ./run.sh nf 2

# Terminal 3
sudo RTE_RING=1 ./run.sh nf 3

# Terminal 4
sudo RTE_RING=1 ./run.sh nf 4
```

We can now test if the SPRIGHT function chain is able to work, using the `curl` command or apache-benchmark. Note that the port number of SPRIGHT gateway is `8080`.
```shell=
# USAGE: curl http://<ip_address_of_SPRIGHT_gateway>/

curl http://10.10.1.1:8080/
```