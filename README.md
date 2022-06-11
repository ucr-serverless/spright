# SPRIGHT

## Description

This artifact runs on c220g5 nodes on NSF Cloudlab. 

## Starting up a 2-node cluster on Cloudlab
1. The following steps require a **bash** environment. Please configure the default shell in your CloudLab account to be bash. For how to configure bash on Cloudlab, Please refer to the post "Choose your shell": https://www.cloudlab.us/portal-news.php?idx=49
2. When starting a new experiment on Cloudlab, select the **small-lan** profile
3. In the profile parameterization page, 
        - Set **Number of Nodes** as **2**
        - Set OS image as **Ubuntu 20.04**
        - Set physical node type as **c220g5**
        - Please check **Temp Filesystem Max Space**
        - Keep **Temporary Filesystem Mount Point** as default (**/mydata**)
4. Wait for the cluster to be initialized (It may take 5 to 10 minutes)
5. Extend the disk. This is because Cloudlab only allocates a 16GB disk space.
On the master node (**node-0**) and worker node (**node-1**), run
```bash
sudo chown -R $(id -u):$(id -g) /mydata
cd /mydata
git clone https://github.com/ucr-serverless/spright.git
export MYMOUNT=/mydata
```

## Install SPRIGHT on the master node (**node-0**) 
### Install dependencies (apt)
```bash
sudo apt install -y flex bison build-essential dwarves libssl-dev libelf-dev \
                    libnuma-dev pkg-config python3-pip python3-pyelftools \
                    libconfig-dev golang clang gcc-multilib uuid-dev
```

### Install dependencies (pip)
```bash
sudo pip3 install meson ninja
```

### Install Linux
```bash
cd /mydata # Use the extended disk with enough space

wget --no-hsts https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.16.tar.xz
tar -xf linux-5.16.tar.xz
cd linux-5.16
make olddefconfig
scripts/config --set-str SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str SYSTEM_REVOCATION_KEYS ""
make -j $(nproc)
find -name *.ko -exec strip --strip-unneeded {} +
sudo make modules_install -j $(nproc)
sudo make install
cd ..
```

### Reboot system
```bash
sudo reboot
```

### Install libbpf
```bash
cd /mydata # Use the extended disk with enough space

git clone --single-branch https://github.com/libbpf/libbpf.git
cd libbpf
git switch --detach v0.6.0
cd src
make -j $(nproc)
sudo make install
echo "/usr/lib64/" | sudo tee -a /etc/ld.so.conf
sudo ldconfig
cd ../..
```

### Install DPDK
```bash
cd /mydata # Use the extended disk with enough space

git clone --single-branch git://dpdk.org/dpdk
cd dpdk
git switch --detach v21.11
meson build
cd build
ninja
sudo ninja install
sudo ldconfig
cd ../..
```

### Set up hugepages
```bash
sudo sysctl -w vm.nr_hugepages=16384
```

### Build SPRIGHT
```bash
cd /mydata # Use the extended disk with enough space

git clone https://github.com/ucr-serverless/spright.git
cd spright/src/cstl && make
cd ../../ && make
```

## Install Kubernetes control plane and Knative
### Setting up the Kubernetes master node (**node-0**).
1. Run `export MYMOUNT=/mydata`
2. Run `./sigcomm-experiment/env-setup/100-docker_install.sh` without *sudo*
3. Run `source ~/.bashrc`
4. Run `./sigcomm-experiment/env-setup/200-k8s_install.sh master 10.10.1.1`. 
5. Once the installation of Kuberentes control plane is done, it will print out an token `kubeadm join ...`. **PLEASE copy and save this token somewhere**. The worker node (**node-1**) needs this token to join the Kuberentes control plane.
6. run `echo 'source <(kubectl completion bash)' >>~/.bashrc && source ~/.bashrc`

### Setting up the Kubernetes worker node (**node-1**).
1. Run `export MYMOUNT=/mydata`
2. Run `./sigcomm-experiment/env-setup/100-docker_install.sh` without *sudo*
3. Run `source ~/.bashrc`
4. Run `./sigcomm-experiment/env-setup/200-k8s_install.sh slave`
5. Use the token returned from the master node (**node-0**) to join the Kubernetes control plane. Run `sudo kubeadm join ...` with the token just saved. Please run the `kubeadm join` command with *sudo*

### Enable pod placement on master node (**node-0**) and taint worker node (**node-1**):
```bash
# Please run the following commands on MASTER node (node-0)
kubectl taint nodes --all node-role.kubernetes.io/master-

kubectl get node # This command will return the names of the master node and worker node

# Replace <master-node-name> with node-0's name returned by "kubectl get node"
kubectl label nodes <master-node-name> location=master

# Replace <slave-node-name> with node-1's name returned by "kubectl get node"
kubectl label nodes <slave-node-name> location=slave

# Replace <slave-node-name> with node-1's name returned by "kubectl get node"
kubectl taint nodes <slave-node-name> location=slave:NoSchedule
```

### Setting up the Knative.
1. On the master node (**node-0**), run
```bash
./sigcomm-experiment/env-setup/300-kn_install.sh
```
2. Disable activator in Knative
```bash
# Enter editing mode of Knative's autoscaler config
kubectl -n knative-serving edit configmap config-autoscaler

# Change target-burst-capacity in config/core/configmaps/autoscaler.yaml to 0
# Add "target-burst-capacity: 0" under the "data" field
# Then save and exit, do :wq
```
3. Configure the Istio ingress gateway


## Experiment Workflow
Note: We will run the SPRIGHT components directly as a binary for fast demonstration and testing purpose. To run SPRIGHT as a function pod, please refer to

### 1 - Online boutique.

### 2 - Motion detection.

### 3 - Parking detection & Charging.

### Compile the eBPF code into an object file
```bash
cd ebpf/
clang -g -O2 -c -target bpf -o sk_msg_kern.o sk_msg_kern.c

# Copy sk_msg_kern.o to obj/
cp sk_msg_kern.o ../obj/

# Debugging purpose: output bpf_printk()
sudo cat  /sys/kernel/debug/tracing/trace_pipe
```