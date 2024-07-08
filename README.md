# SPRIGHT

SPRIGHT is a lightweight, high-performance serverless framework that exploits shared memory processing to improve the scalability of the dataplane of serverless function chains by avoiding unnecessary networking overheads.

For more information, please refer to:
- SIGCOMM 2022: [SPRIGHT: Extracting the Server from Serverless Computing! High-performance eBPF-based Event-driven, Shared-memory Processing](https://dl.acm.org/doi/abs/10.1145/3544216.3544259)

## Installation guideline (on Cloudlab) ##

This guideline is mainly for deploying SPRIGHT on [NSF Cloudlab](https://www.cloudlab.us/). We focus on a single-node deployment to demonstrate the shared memory processing supported by SPRIGHT. Currently SPRIGHT offers several deployment options: Process-on-bare-metal (POBM mode), Kubernetes pod (K8S mode), and Knative functions (Kn mode).

Follow steps below to set up SPRIGHT:
- [Creating a 2-node cluster on Cloudlab](docs/01-create-cluster-on-cloudlab.md)
- [Upgrading kernel & Installing SPRIGHT dependencies](docs/02-upgrade-kernel-install-deps.md)
- [Setting up Kubernetes & Knative](docs/03-setup-k8s-kn.md)
- [Setting up SPRIGHT](docs/04-setup-spright.md)

<!-- ## Documentation ##
Please refer to the [SPRIGHT documentation]() for details on installing, running and deploying SPRIGHT. -->

## SIGCOMM artifact evaluation ##
To reproduce the experiment in [our paper](https://dl.acm.org/doi/abs/10.1145/3544216.3544259), please refer to commit [98434fd](https://github.com/ucr-serverless/spright/tree/98434fd1e4fb9498308151e863975ff6883d5bf7).

## Publication ##
~~~
@inproceedings{spright-sigcomm22,
author = {Qi, Shixiong and Monis, Leslie and Zeng, Ziteng and Wang, Ian-chin and Ramakrishnan, K. K.},
title = {SPRIGHT: Extracting the Server from Serverless Computing! High-Performance EBPF-Based Event-Driven, Shared-Memory Processing},
year = {2022},
isbn = {9781450394208},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
url = {https://doi.org/10.1145/3544216.3544259},
doi = {10.1145/3544216.3544259},
booktitle = {Proceedings of the ACM SIGCOMM 2022 Conference},
pages = {780–794},
numpages = {15},
keywords = {event-driven, eBPF, function chain, serverless},
location = {Amsterdam, Netherlands},
series = {SIGCOMM '22}
}
~~~

<!-- ## Description

This artifact runs on c220g5 nodes on NSF Cloudlab. 

## 1. Starting up a 2-node cluster on Cloudlab
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

## 2. Install SPRIGHT on the master node (**node-0**) 
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

## 3. Install Kubernetes control plane and Knative
### Setting up the Kubernetes master node (**node-0**).
```shell=
$ cd /mydata/spright

spright$ export MYMOUNT=/mydata

spright$ ./sigcomm-experiment/env-setup/100-docker_install.sh

spright$ source ~/.bashrc

spright$ ./sigcomm-experiment/env-setup/200-k8s_install.sh master 10.10.1.1

## Once the installation of Kuberentes control plane is done, 
## it will print out an token `kubeadm join ...`. 
## **PLEASE copy and save this token somewhere**. 
## The worker node (**node-1**) needs this token to join the Kuberentes control plane.

spright$ echo 'source <(kubectl completion bash)' >>~/.bashrc && source ~/.bashrc
```

### Setting up the Kubernetes worker node (**node-1**).
```shell=
$ cd /mydata/spright

spright$ export MYMOUNT=/mydata

spright$ ./sigcomm-experiment/env-setup/100-docker_install.sh

spright$ source ~/.bashrc

spright$ ./sigcomm-experiment/env-setup/200-k8s_install.sh slave

# Use the token returned from the master node (**node-0**) to join the Kubernetes control plane. Run `sudo kubeadm join ...` with the token just saved. Please run the `kubeadm join` command with *sudo*
spright$ sudo kubeadm join <control-plane-token>
```

### Enable pod placement on master node (**node-0**) and taint worker node (**node-1**):
```shell=
$ cd /mydata/spright
spright$ ./sigcomm-experiment/env-setup/201-taint_nodes.sh
```

### Setting up the Knative.
1. On the master node (**node-0**), run
```shell=
$ cd /mydata/spright
spright$ ./sigcomm-experiment/env-setup/300-knative_install.sh
```

## 4. Experiment Workflow
Note: We will run the SPRIGHT components directly as a binary for fast demonstration and testing purpose. To run SPRIGHT as a function pod, please refer to

### 1 - Online boutique.
**Check-List**:
- Program: S-SPRIGHT, D-SPRIGHT, Knative function, gRPC (Kubernetes pod)
- Metrics: RPS, Latency and CPU usage
- Time needed to complete experiments: 2 hours

#### 1.0 Install Deps of Locust generator on worker node (**node-1**)
> **Worker node (node-1) operations**

Install Locust load generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/env-setup/

env-setup$ ./400-install_locust.sh

```

#### 1.1 Run Online boutique using S-SPRIGHT (SKMSG)

> **Master node (node-0) operations**
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./run_spright.sh s-spright

# After the experiment is done (~3 minutes)
# Enter Ctrl+B then D to detach from tmux
```
---
> **Worker node (node-1) operations**
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./run_load_generators.sh spright 10.10.1.1 8080

# After the experiment is done (~3 minutes)
# Enter Ctrl+B then D to detach from tmux
```
---
> **Consolidate metric files generated by Locust workers on the worker node (node-1)**
```shell=
# Make sure you are on the worker node
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./consolidate_locust_stats.sh s-spright

```

#### 1.2 Run Online boutique using D-SPRIGHT (DPDK's RTE rings)

> **Master node (node-0) operations**
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./run_spright.sh d-spright

# After the experiment is done (~3 minutes)
# Enter Ctrl+B then D to detach from tmux
```
---
> **Worker node (node-1) operations**
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./run_load_generators.sh spright 10.10.1.1 8080

# After the experiment is done (~3 minutes)
# Enter Ctrl+B then D to detach from tmux
```
---
> **Consolidate metric files generated by Locust workers on the worker node (node-1)**
```shell=
# Make sure you are on the worker node
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./consolidate_locust_stats.sh d-spright

```

#### 1.3 Run Online boutique using Knative

> **Master node (node-0) operations**

**Step-1**: start Knative functions
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ python3 hack/hack.py && cd /mydata/spright/

spright$ kubectl apply -f sigcomm-experiment/expt-1-online-boutique/manifests/knative

# Get the IP of Istio Ingress
spright$ kubectl get po -l istio=ingressgateway -n istio-system -o jsonpath='{.items[0].status.hostIP}'

# Get the Port of Istio Ingress
spright$ kubectl -n istio-system get service istio-ingressgateway -o jsonpath='{.spec.ports[?(@.name=="http2")].nodePort}'

# Record the IP of parking proxy, the IP of Istio Ingress and the Port of Istio Ingress, they will be used by the load generator on worker node
```

**Step-2**: Start CPU usage collection
```shell= 
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./run_kn.sh
```
---
> **Worker node (node-1) operations**
```shell= 
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

# Please use the IP and Port obtained on master node (Step-1 of Knative online boutique)
expt-1-online-boutique$ ./run_load_generators.sh kn $ISTIO_INGRESS_GW_IP $ISTIO_INGRESS_GW_PORT

```
---
> **Consolidate metric files generated by Locust workers on the worker node (node-1)**
```shell=
# Make sure you are on the worker node
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./consolidate_locust_stats.sh kn

```

#### 1.4 Run Online boutique using gRPC

> **Master node (node-0) operations**

**Step-1**: start gRPC functions
```shell=
$ cd /mydata/spright

spright$ kubectl apply -f sigcomm-experiment/expt-1-online-boutique/manifests/kubernetes

# Get the IP of Frontend Service
spright$ kubectl get po -l app=frontend -o wide

# Record the IP of Frontend Service, it will be used by the load generator on worker node
```

**Step-2**: CPU usage collection
```shell= 
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./run_grpc.sh
```
---
> **Worker node (node-1) operations**
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

# Please use the IP of frontend service obtained on master node (Step-1 of gRPC online boutique)
expt-1-online-boutique$ ./run_load_generators.sh kn $FRONTEND_SVC_IP 8080
```
---
> **Consolidate metric files generated by Locust workers on the worker node (node-1)**
```shell=
# Make sure you are on the worker node
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./consolidate_locust_stats.sh grpc

```

### 2 - Motion detection
**Check-List**:
- Program: S-SPRIGHT, Knative function
- Metrics: Latency and CPU usage
- Time needed to complete experiments: 2 hours

#### 2.0 Download motion dataset
> **Worker node (node-1) operations**
```shell=
$ cd /mydata/spright/sigcomm-experiment/env-setup

env-setup$ ./401-motion_dataset_download.sh
```

#### 2.1 Run Motion Detection using S-SPRIGHT

> **Master node (node-0) operations**
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/

expt-2-motion-detection$ ./run_spright.sh

# After the experiment is done (~60 minutes)
# Enter Ctrl+B then D to detach from tmux
```
---
> **Worker node (node-1) operations**
Run load generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator

load-generator$ python3 spright-motion-generator.py --ip 10.10.1.1 --port 8080
```

#### 2.2 Run Motion Detection using Knative
> **Master node (node-0) operations**
**Step-1**: start knative functions
```shell=
$ cd /mydata/spright

spright$ kubectl apply -f ./sigcomm-experiment/expt-2-motion-detection/manifests/

# Prepare NGINX config files
spright$ cd sigcomm-experiment/expt-2-motion-detection/hack/
hack$ python3 hack.py && cd /mydata/spright

# Waiting until functions are started up
spright$ kubectl cp sigcomm-experiment/expt-2-motion-detection/cfg/default.conf motion-proxy:etc/nginx/conf.d/default.conf
spright$ kubectl cp sigcomm-experiment/expt-2-motion-detection/cfg/nginx.conf motion-proxy:etc/nginx/nginx.conf

# Reload NGINX proxy and install curl inside NGINX
spright$ kubectl exec -it motion-proxy -- sh
/$ apk add curl
/$ nginx -t && nginx -s reload
/$ exit

# Get the IP of motion-proxy pod
spright$ kubectl get pod motion-proxy -o wide

# Record the IP, it will be used by the load generator on worker node
```

**Step-2**: CPU usage collection
```shell= 
$ cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/

expt-2-motion-detection$ ./run_kn.sh
```
---
> **Worker node (node-1) operations**
Run load generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator

# use the IP of motion proxy obtained from master node
load-generator$ python3 kn-motion-generator.py --ip <motion-proxy-ip> --port 80
```

### 3 - Parking detection & Charging.
**Check-List**:
- Program: S-SPRIGHT, Knative function
- Metrics: Latency and CPU usage
- Time needed to complete experiments: 30 minutes
#### 3.1 Run the parking detection function chain using S-SPRIGHT

> **Master node (node-0) operations**
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-3-parking/

expt-3-parking$ ./run_spright.sh

# After the experiment is done (~12 minutes)
# Enter Ctrl+B then D to detach from tmux
```
---
> **Worker node (node-1) operations**
Run load generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-3-parking/load-generator

load-generator$ python3 skmsg-parking.py --ip 10.10.1.1 --port 8080
```

#### 3.2 Run the parking detection function chain using Knative
> **Master node (node-0) operations**
**Step-1**: start knative functions
```shell=
$ cd /mydata/spright

spright$ kubectl apply -f ./sigcomm-experiment/expt-3-parking/manifests/

# Prepare NGINX config files
spright$ cd sigcomm-experiment/expt-3-parking/hack/
hack$ python3 hack.py && cd /mydata/spright

# Waiting until functions are started up
spright$ kubectl cp sigcomm-experiment/expt-3-parking/cfg/default.conf parking-proxy:etc/nginx/conf.d/default.conf
spright$ kubectl cp sigcomm-experiment/expt-3-parking/cfg/nginx.conf parking-proxy:etc/nginx/nginx.conf

# Reload NGINX proxy and install curl inside NGINX
spright$ kubectl exec -it parking-proxy -- sh
/$ apk add curl
/$ nginx -t && nginx -s reload
/$ exit

# Get the IP of motion-proxy pod
spright$ kubectl get pod parking-proxy -o wide

# Get the IP of Istio Ingress
spright$ kubectl get po -l istio=ingressgateway -n istio-system -o jsonpath='{.items[0].status.hostIP}'

# Get the Port of Istio Ingress
spright$ kubectl -n istio-system get service istio-ingressgateway -o jsonpath='{.spec.ports[?(@.name=="http2")].nodePort}'

# Record the IP of parking proxy, the IP of Istio Ingress and the Port of Istio Ingress, they will be used by the load generator on worker node
```

**Step-2**: CPU usage collection
```shell= 
$ cd /mydata/spright/sigcomm-experiment/expt-3-parking/

expt-3-parking$ ./run_kn.sh
```
---
> **Worker node (node-1) operations**
Run load generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-3-parking/load-generator

# use the IP of parking proxy, the IP of Istio Ingress and the Port of Istio Ingress obtained from master node
load-generator$ python3 kn-parking.py --nginxip <motion-proxy-ip> --nginxport 80 --istioip <istio-ingress-ip> --istioport <istio-ingress-port>
```

## 5. Download metric logs to your local machine.
- **Prerequisite**: create a "results" directory on your local machine if you do not have

### Download Online Boutique experiment raw metric files
```shell=
# Go to the "results" directory on your local machine
$ cd $HOME/results

# Creating a directory to put parking experiment related metric files
results$ mkdir online_boutique && cd online_boutique

# Replace the correct user ID and hostname of your cloudlab machine
# Download metric files on the master node
online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/dpdk_gw.cpu ./
online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/dpdk_nf.cpu ./
online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/grpc-recom.cpu ./
online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/grpc-front-prod.cpu ./
online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/grpc-others.cpu ./
online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/kn-gw.cpu ./online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/kn-queue.cpu ./
online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/kn-front-prod.cpu ./
online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/kn-others.cpu ./
online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/kn-recom.cpu ./
online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/skmsg_fn.cpu ./
online_boutique$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/online-boutique-results/skmsg_gw.cpu ./

# Download metric files on the worker node
online_boutique$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/rps_stats_d-spright.csv ./
online_boutique$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/latency_of_each_req_stats_d-spright.csv ./
online_boutique$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/rps_stats_s-spright.csv ./
online_boutique$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/latency_of_each_req_stats_s-spright.csv ./
online_boutique$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/rps_stats_grpc.csv ./
online_boutique$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/latency_of_each_req_stats_grpc.csv ./
online_boutique$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/rps_stats_kn.csv ./
online_boutique$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/latency_of_each_req_stats_kn.csv ./


```

### Download Motion Detection experiment raw metric files
```shell=
# Go to the "results" directory on your local machine
$ cd $HOME/results

# Creating a directory to put parking experiment related metric files
results$ mkdir motion && cd motion

# Replace the correct user ID and hostname of your cloudlab machine
# Download metric files on the master node
motion$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/motion-detection-results/kn-queue.motion.cpu ./
motion$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/motion-detection-results/kn-gw.motion.cpu ./
motion$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/motion-detection-results/kn-fn.motion.cpu ./
motion$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/motion-detection-results/skmsg_gw.motion.cpu ./
motion$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/motion-detection-results/skmsg_fn.motion.cpu ./

# Download metric files on the worker node
motion$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator/skmsg_motion_output.csv ./
motion$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator/kn_motion_output.csv ./
```

### Download Parking experiment raw metric files
```shell=
# Go to the "results" directory on your local machine
$ cd $HOME/results

# Creating a directory to put parking experiment related metric files
results$ mkdir parking && cd parking

# Replace the correct user ID and hostname of your cloudlab machine
# Download metric files on the master node
parking$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/parking-results/kn-queue.parking.cpu ./
parking$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/parking-results/kn-gw.parking.cpu ./
parking$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/parking-results/kn-fn.parking.cpu ./
parking$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/parking-results/skmsg_gw.parking.cpu ./
parking$ scp <cloudlab-userid>@<node-0-hostname>:/mydata/parking-results/skmsg_fn.parking.cpu ./

# Download metric files on the worker node
parking$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-3-parking/load-generator/kn.parking_output.csv ./
parking$ scp <cloudlab-userid>@<node-1-hostname>:/mydata/spright/sigcomm-experiment/expt-3-parking/load-generator/skmsg.parking_output.csv ./
```

## 6. Re-producing figures (camera-ready version) in the manuscript (on your local machine).

### Download script to the 'results' directory
```bash
# Go to the "results" directory on your local machine
$ cd $HOME/results

results$ git clone https://github.com/ucr-serverless/spright-figures.git
results$ cp spright-figures/* ./
```

```shell=
# Figure-9: RPS for online boutique workload: {Knative, gRPC} at 5K & {D-SPRIGHT, S-SPRIGHT } at 25K concurrency.
python3 fig-9.py

# Figure-10 (a) - (i)
python3 fig-10-a.py
python3 fig-10-b.py
python3 fig-10-c.py
python3 fig-10-d.py
python3 fig-10-e.py
python3 fig-10-f.py
python3 fig-10-g.py
python3 fig-10-h.py
python3 fig-10-i.py

# Figure-11 (a), (b)
python3 fig-11-a.py
python3 fig-11-b.py

# Figure-12 (a), (b)
python3 fig-12-a.py
python3 fig-12-b.py
```

## 7. Misc.
### Compile the eBPF code into an object file
```bash
cd ebpf/
clang -g -O2 -c -target bpf -o sk_msg_kern.o sk_msg_kern.c

# Copy sk_msg_kern.o to obj/
cp sk_msg_kern.o ../obj/

# Debugging purpose: output bpf_printk()
sudo cat  /sys/kernel/debug/tracing/trace_pipe
``` -->
