## Upgrading kernel & installing SPRIGHT dependencies on the master node (**node-0**)
This guideline is for upgrading kernel and installing SPRIGHT dependencies (libbpf, DPDK RTE lib, ...) on the master node (**node-0**). 

We will run SPRIGHT functions on the master node and run the load generator ([Locust](https://locust.io/)) on the worker node
- Note: It can also be done reversely, i.e., running SPRIGHT functions on the worker node and load generator on master node. Or even having a two worker node setup, and running SPRIGHT functions and load generator on different worker nodes.

### Update Kernel of the master node
```bash
$ cd /mydata/spright

spright$ ./sigcomm-experiment/env-setup/001-env_setup_master.sh

# The master node will be rebooted after the script is complete
# Rebooting usually takes 5 - 10 minutes
```

### Re-login to master node after rebooting (**node-0**)

### Install libbpf, dpdk and SPRIGHT
```bash
$ cd /mydata/spright

spright$ ./sigcomm-experiment/env-setup/002-env_setup_master.sh
```