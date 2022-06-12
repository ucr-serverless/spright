# SPRIGHT

## Description

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
### Install dependencies (apt)
```bash
sudo apt update && sudo apt install -y flex bison build-essential dwarves libssl-dev libelf-dev \
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

### Reboot master node (**node-0**)
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
$ sudo sysctl -w vm.nr_hugepages=16384
```

### Build SPRIGHT
```shell=
$ cd /mydata # Use the extended disk with enough space

mydata$ cd spright/src/cstl && make
cstl$ cd ../../ && make
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
<!-- 1. Run `export MYMOUNT=/mydata`
2. Run `./sigcomm-experiment/env-setup/100-docker_install.sh` without *sudo*
3. Run `source ~/.bashrc`
4. Run `./sigcomm-experiment/env-setup/200-k8s_install.sh master 10.10.1.1`. 
5. Once the installation of Kuberentes control plane is done, it will print out an token `kubeadm join ...`. **PLEASE copy and save this token somewhere**. The worker node (**node-1**) needs this token to join the Kuberentes control plane.
6. run `echo 'source <(kubectl completion bash)' >>~/.bashrc && source ~/.bashrc` -->

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
<!-- 1. Run `export MYMOUNT=/mydata`
2. Run `./sigcomm-experiment/env-setup/100-docker_install.sh` without *sudo*
3. Run `source ~/.bashrc`
4. Run `./sigcomm-experiment/env-setup/200-k8s_install.sh slave`
5. Use the token returned from the master node (**node-0**) to join the Kubernetes control plane. Run `sudo kubeadm join ...` with the token just saved. Please run the `kubeadm join` command with *sudo* -->

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
./sigcomm-experiment/env-setup/300-knative_install.sh
```
2. Disable activator in Knative
```bash
# Enter editing mode of Knative's autoscaler config
kubectl -n knative-serving edit configmap config-autoscaler

# Change target-burst-capacity in config/core/configmaps/autoscaler.yaml to 0
# Add "target-burst-capacity: 0" under the "data" field
# Then save and exit, do :wq
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
- **Prerequisite**: Create 1 terminals (Terminal-1) on worker node (**node-1**)

**Terminal-1**: install deps of Locust load generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/load-generator

load-generator$ sudo apt update && sudo apt install -y python3-pip

# Install dependencies
load-generator$ pip3 install -r requirements.txt

# Install Locust
load-generator$ pip3 install locust

# export the locust path to locust
load-generator$ echo export PATH="$HOME/.local/bin:$PATH" >> ~/.bashrc
load-generator$ source ~/.bashrc
```

#### 1.1 Run Online boutique using S-SPRIGHT (SKMSG)

> **Master node (node-0) operations**
- **Prerequisite**: Create 13 terminals (Terminal-1, Terminal-2, Terminal-3, Terminal-4, ... Terminal-12) on master node (**node-0**)

**Terminal-1**: start shared memory manager
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh shm_mgr cfg/online-boutique-concurrency-32.cfg
```

**Terminal-2**: start SPRIGHT gateway
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh gateway
```

**Terminal-3**: start frontend service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh frontendservice 1
```

**Terminal-4**: start currency service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh currencyservice 2
```

**Terminal-5**: start productcatalog service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh productcatalogservice 3
```

**Terminal-6**: start cart service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh cartservice 4
```

**Terminal-7**: start recommendation service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh recommendationservice 5
```

**Terminal-8**: start shipping service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh shippingservice 6
```

**Terminal-9**: start checkout service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh checkoutservice 7
```

**Terminal-10**: start payment service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh paymentservice 8
```

**Terminal-11**: start email service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh emailservice 9
```

**Terminal-12**: start ad service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh adservice 10
```

**Terminal-13**: CPU usage collection
```shell= 
$ cd /mydata

mydatas$ mkdir online-boutique-results

mydatas$ cd online-boutique-results

online-boutique-results$ pidstat 1 180 -C gateway_sk_msg > skmsg_gw.cpu & pidstat 1 180 -C nf_ > skmsg_fn.cpu &
```
---
> **Worker node (node-1) operations**
- **Prerequisite**: Create 17 terminal (Terminal-1, ... ,Terminal-17) on worker node (**node-1**). Terminal-1 is to run the master process of Locust generator. ***Terminal-2 to Terminal-17** are to run the worker process of Locust generator. Each terminal runs only one worker process. The reason we need so many worker process is to distribute load generation to achieve enough load. See: https://docs.locust.io/en/stable/running-distributed.html

**Terminal-1**: run the master process of Locust generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/load-generator

load-generator$ source ~/.bashrc

load-generator$ locust -u 25000 -r 500 -t 3m --csv kn --csv-full-history -f spright-locustfile.py --headless  -H http://10.10.1.1:8080 --master --expect-workers=16
```

**Terminal-2** to **Terminal-17**: run the worker process of Locust generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/load-generator

# make sure locust can have enough file descriptor to use in each terminal
load-generator$ ulimit -aS
load-generator$ ulimit -HSn 102400

load-generator$ source ~/.bashrc

# Start the worker process of Locust. The master process will wait until all the workers are ready
load-generator$ locust -f spright-locustfile.py --worker
```

#### 1.2 Run Online boutique using D-SPRIGHT (DPDK's RTE rings)

> **Master node (node-0) operations**
- **Prerequisite**: Create 13 terminals (Terminal-1, Terminal-2, Terminal-3, Terminal-4, ... Terminal-12) on master node (**node-0**)

**Terminal-1**: start shared memory manager
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 shm_mgr cfg/online-boutique-concurrency-32.cfg
```

**Terminal-2**: start SPRIGHT gateway
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 gateway
```

**Terminal-3**: start frontend service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 frontendservice 1
```

**Terminal-4**: start currency service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 currencyservice 2
```

**Terminal-5**: start productcatalog service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 productcatalogservice 3
```

**Terminal-6**: start cart service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 cartservice 4
```

**Terminal-7**: start recommendation service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 recommendationservice 5
```

**Terminal-8**: start shipping service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 shippingservice 6
```

**Terminal-9**: start checkout service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 checkoutservice 7
```

**Terminal-10**: start payment service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 paymentservice 8
```

**Terminal-11**: start email service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 emailservice 9
```

**Terminal-12**: start ad service
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh RTE_RING=1 adservice 10
```

**Terminal-13**: CPU usage collection
```shell= 
$ cd /mydata

mydatas$ mkdir online-boutique-results

mydatas$ cd online-boutique-results

online-boutique-results$ pidstat 1 180 -C gateway_rte_rin > dpdk_gw.cpu  & pidstat 1 180 -C nf_ > dpdk_nf.cpu &
```
---
> **Worker node (node-1) operations**
- **Prerequisite**: Create 17 terminal (Terminal-1, ... ,Terminal-17) on worker node (**node-1**). Terminal-1 is to run the master process of Locust generator. ***Terminal-2 to Terminal-17** are to run the worker process of Locust generator. Each terminal runs only one worker process. The reason we need so many worker process is to distribute load generation to achieve enough load. See: https://docs.locust.io/en/stable/running-distributed.html

**Terminal-1**: run the master process of Locust generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/load-generator

load-generator$ source ~/.bashrc

load-generator$ locust -u 25000 -r 500 -t 3m --csv kn --csv-full-history -f spright-locustfile.py --headless  -H http://10.10.1.1:8080 --master --expect-workers=16
```

**Terminal-2** to **Terminal-17**: run the worker process of Locust generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/load-generator

# make sure locust can have enough file descriptor to use in each terminal
load-generator$ ulimit -aS
load-generator$ ulimit -HSn 102400

load-generator$ source ~/.bashrc

# Start the worker process of Locust. The master process will wait until all the workers are ready
load-generator$ locust -f spright-locustfile.py --worker
```

#### 1.3 Run Online boutique using Knative

> **Master node (node-0) operations**
- **Prerequisite**: Create 2 terminals (Terminal-1, Terminal-2) on master node (**node-0**)

**Terminal-1**: start Knative functions
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

**Terminal-2**: CPU usage collection
```shell= 
$ cd /mydata

mydatas$ mkdir online-boutique-results

mydatas$ cd online-boutique-results

online-boutique-results$ pidstat 1 180 -G ^queue$ > kn-queue.cpu &  pidstat 1 180 -C envoy > kn-gw.cpu & pidstat 1 180 -G ^server$ > kn-front-prod.cpu & pidstat 1 180 -C recommend > kn-recom.cpu & pidstat 1 180 -C service > kn-others.cpu &
```
---
> **Worker node (node-1) operations**
- **Prerequisite**: Create 17 terminal (Terminal-1, ... ,Terminal-17) on worker node (**node-1**). Terminal-1 is to run the master process of Locust generator. ***Terminal-2 to Terminal-17** are to run the worker process of Locust generator. Each terminal runs only one worker process. The reason we need so many worker process is to distribute load generation to achieve enough load. See: https://docs.locust.io/en/stable/running-distributed.html

**Terminal-1**: run the master process of Locust generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/load-generator

load-generator$ source ~/.bashrc

load-generator$ locust -u 5000 -r 200 -t 3m --csv kn --csv-full-history -f kn-locustfile.py --headless  -H http://<istio-ingress-ip>:<istio-ingress-port> --master --expect-workers=16
```

**Terminal-2** to **Terminal-17**: run the worker process of Locust generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/load-generator

# make sure locust can have enough file descriptor to use in each terminal
load-generator$ ulimit -aS
load-generator$ ulimit -HSn 102400

load-generator$ source ~/.bashrc

# Start the worker process of Locust. The master process will wait until all the workers are ready
load-generator$ locust -f kn-locustfile.py --worker
```

#### 1.4 Run Online boutique using gRPC

> **Master node (node-0) operations**
- **Prerequisite**: Create 2 terminals (Terminal-1, Terminal-2) on master node (**node-0**)

**Terminal-1**: start Knative functions
```shell=
$ cd /mydata/spright

spright$ kubectl apply -f sigcomm-experiment/expt-1-online-boutique/manifests/kubernetes

# Get the IP of Frontend Service
spright$ kubectl get po -l app=frontend -o wide

# Record the IP of Frontend Service, it will be used by the load generator on worker node
```

**Terminal-2**: CPU usage collection
```shell= 
$ cd /mydata

mydatas$ mkdir online-boutique-results

mydatas$ cd online-boutique-results

online-boutique-results$ pidstat 1 180 -G ^server$ > grpc-front-prod.cpu & pidstat 1 180 -C recommend > grpc-recom.cpu & pidstat 1 180 -C service > grpc-others.cpu &
```
---
> **Worker node (node-1) operations**
- **Prerequisite**: Create 17 terminal (Terminal-1, ... ,Terminal-17) on worker node (**node-1**). Terminal-1 is to run the master process of Locust generator. ***Terminal-2 to Terminal-17** are to run the worker process of Locust generator. Each terminal runs only one worker process. The reason we need so many worker process is to distribute load generation to achieve enough load. See: https://docs.locust.io/en/stable/running-distributed.html

**Terminal-1**: run the master process of Locust generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/load-generator

load-generator$ source ~/.bashrc

load-generator$ locust -u 5000 -r 200 -t 3m --csv kn --csv-full-history -f kn-locustfile.py --headless  -H http://<frontend-service-ip>:8080 --master --expect-workers=16
```

**Terminal-2** to **Terminal-17**: run the worker process of Locust generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/load-generator

# make sure locust can have enough file descriptor to use in each terminal
load-generator$ ulimit -aS
load-generator$ ulimit -HSn 102400

load-generator$ source ~/.bashrc

# Start the worker process of Locust. The master process will wait until all the workers are ready
load-generator$ locust -f kn-locustfile.py --worker
```

### 2 - Motion detection
**Check-List**:
- Program: S-SPRIGHT, Knative function
- Metrics: Latency and CPU usage
- Time needed to complete experiments: 2 hours

#### 2.0 Download motion dataset

> **Worker node (node-1) operations**
- **Prerequisite**: Create 1 terminal (Terminal-1) on worker node (**node-1**)
**Terminal-1**: 
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator

load-generator$ wget --load-cookies /tmp/cookies.txt "https://docs.google.com/uc?export=download&confirm=$(wget --quiet --save-cookies /tmp/cookies.txt --keep-session-cookies --no-check-certificate 'https://docs.google.com/uc?export=download&id=1MwJIxaokG52YQ9xkCxoT4AmZprgBoO0z' -O- | sed -rn 's/.*confirm=([0-9A-Za-z_]+).*/\1\n/p')&id=1MwJIxaokG52YQ9xkCxoT4AmZprgBoO0z" -O motion_dataset.tar.gz && rm -rf /tmp/cookies.txt

load-generator$ tar -zxvf motion_dataset.tar.gz && rm motion_dataset.tar.gz
```


#### 2.1 Run Motion Detection using S-SPRIGHT

> **Master node (node-0) operations**
- **Prerequisite**: Create 5 terminals (Terminal-1, Terminal-2, Terminal-3, Terminal-4, Terminal-5) on master node (**node-0**)

**Terminal-1**: start shared memory manager
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh shm_mgr cfg/motion-detection.cfg
```

**Terminal-2**: start SPRIGHT gateway
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh gateway
```

**Terminal-3**: start sensor function
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh nf 1
```

**Terminal-4**: start actuator function
```shell= 
$ cd /mydata/spright

spright$ sudo ./run.sh nf 2
```

**Terminal-4**: CPU usage collection
```shell= 
$ cd /mydata

mydatas$ mkdir motion-detection-results

mydatas$ cd motion-detection-results

motion-detection-results$ pidstat 1 3600 -G ^gateway_sk_msg$ > skmsg_gw.motion.cpu & pidstat 1 3600 -G ^nf_sk_msg$ > skmsg_fn.motion.cpu &
```
---
> **Worker node (node-1) operations**
- **Prerequisite**: Create 1 terminal (Terminal-1) on worker node (**node-1**)

**Terminal-1**: run load generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator

load-generator$ python3 motion-generator.py --ip 10.10.1.1 --port 8080
```
---
> **Download metric logs**
- **Prerequisite**: create a "results" directory on your local machine if you do not have
```shell=
$ cd $HOME/results
results$ mkdir motion && cd motion
motion$ scp <user>@<node-0>:/mydata/<path-to-cpu-logfile>/skmsg_gw.motion.cpu ./
motion$ scp <user>@<node-0>:/mydata/<path-to-cpu-logfile>/skmsg_fn.motion.cpu ./

motion$ scp <user>@<node-1>:/mydata/<path-to-latency-logfile>/motion_output.csv ./
```

#### 2.2 Run Motion Detection using Knative
> **Master node (node-0) operations**
- **Prerequisite**: Create 2 terminals (Terminal-1, Terminal-2) on master node (**node-0**)

**Terminal-1**: start knative functions
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

**Terminal-2**: CPU usage collection
```shell= 
# Make sure you have created "motion-detection-results"
$ cd /mydata/motion-detection-results

motion-detection-results$ pidstat 1 3600 -G ^queue$ > kn-queue.motion.cpu &  pidstat 1 180 -G ^nginx$ > kn-gw.motion.cpu & pidstat 1 3600 -G ^server$ > kn-fn.motion.cpu &
```
---
> **Worker node (node-1) operations**
- **Prerequisite**: Create 1 terminal (Terminal-1) on worker node (**node-1**)

**Terminal-1**: run load generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator

# use the IP of motion proxy obtained from master node
load-generator$ python3 motion-generator.py --ip <motion-proxy-ip> --port 80
```
---
> **Download metric logs**
- **Prerequisite**: create a "results" directory on your local machine if you do not have
```shell=
$ cd $HOME/results
results$ mkdir motion && cd motion
motion$ scp <user>@<node-0>:/mydata/<path-to-cpu-logfile>/kn-queue.motion.cpu ./
motion$ scp <user>@<node-0>:/mydata/<path-to-cpu-logfile>/kn-gw.motion.cpu ./
motion$ scp <user>@<node-0>:/mydata/<path-to-cpu-logfile>/kn-fn.motion.cpu ./

motion$ scp <user>@<node-1>:/mydata/<path-to-latency-logfile>/motion.cpu ./
```

### 3 - Parking detection & Charging.
**Check-List**:
- Program: S-SPRIGHT, Knative function
- Metrics: Latency and CPU usage
- Time needed to complete experiments: 30 minutes
#### 3.1 Run the parking detection function chain using S-SPRIGHT

> **Master node (node-0) operations**
- **Prerequisite**: Create 8 terminals (Terminal-1, Terminal-2, Terminal-3, Terminal-4, Terminal-5, Terminal-6, Terminal-7, Terminal-8) on master node (**node-0**)

**Terminal-1**: start shared memory manager
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh shm_mgr cfg/parking.cfg
```

**Terminal-2**: start SPRIGHT gateway
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh gateway
```

**Terminal-3**: start Detection function
```shell=
$ cd /mydata/spright

spright$ sudo ./run.sh nf 1
```

**Terminal-4**: start Search function
```shell= 
$ cd /mydata/spright

spright$ sudo ./run.sh nf 2
```

**Terminal-5**: start Index function
```shell= 
$ cd /mydata/spright

spright$ sudo ./run.sh nf 3
```

**Terminal-6**: start Charging function
```shell= 
$ cd /mydata/spright

spright$ sudo ./run.sh nf 4
```

**Terminal-7**: start Persist function
```shell= 
$ cd /mydata/spright

spright$ sudo ./run.sh nf 5
```

**Terminal-8**: CPU usage collection
```shell= 
$ cd /mydata

mydatas$ mkdir parking-results

mydatas$ cd parking-results

parking-results$ pidstat 1 700 -G ^gateway_sk_msg$ > skmsg_gw.parking.cpu & pidstat 1 700 -G ^nf_sk_msg$ > skmsg_fn.parking.cpu &
```
---
> **Worker node (node-1) operations**
- **Prerequisite**: Create 1 terminal (Terminal-1) on worker node (**node-1**)

**Terminal-1**: run load generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-3-parking/load-generator

load-generator$ python3 skmsg-parking.py --ip 10.10.1.1 --port 8080
```
---
> **Download metric logs**
- **Prerequisite**: create a "results" directory on your local machine if you do not have
```shell=
$ cd $HOME/results
results$ mkdir parking && cd parking
parking$ scp <user>@<node-0>:/mydata/<path-to-cpu-logfile>/skmsg_gw.parking.cpu ./
parking$ scp <user>@<node-0>:/mydata/<path-to-cpu-logfile>/skmsg_fn.parking.cpu ./

parking$ scp <user>@<node-1>:/mydata/<path-to-latency-logfile>/skmsg.parking_output.csv ./
```

#### 3.2 Run the parking detection function chain using Knative
> **Master node (node-0) operations**
- **Prerequisite**: Create 2 terminals (Terminal-1, Terminal-2) on master node (**node-0**)

**Terminal-1**: start knative functions
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

**Terminal-2**: CPU usage collection
```shell= 
# Make sure you have created "parking-results"
$ cd /mydata/parking-results

parking-results$ pidstat 1 700 -G ^queue$ > kn-queue.parking.cpu &  pidstat 1 700 -G ^nginx$ > kn-gw.parking.cpu & pidstat 1 700 -G ^server$ > kn-fn.parking.cpu &
```
---
> **Worker node (node-1) operations**
- **Prerequisite**: Create 1 terminal (Terminal-1) on worker node (**node-1**)

**Terminal-1**: run load generator
```shell=
$ cd /mydata/spright/sigcomm-experiment/expt-3-parking/load-generator

# use the IP of parking proxy, the IP of Istio Ingress and the Port of Istio Ingress obtained from master node
load-generator$ python3 kn-parking.py --nginxip <motion-proxy-ip> --nginxport 80 --istioip <istio-ingress-ip> --istioport <istio-ingress-port>
```
---
> **Download metric logs**
- **Prerequisite**: create a "results" directory on your local machine if you do not have
```shell=
$ cd $HOME/results
results$ mkdir parking && cd parking
parking$ scp <user>@<node-0>:/mydata/<path-to-cpu-logfile>/kn-queue.parking.cpu ./
parking$ scp <user>@<node-0>:/mydata/<path-to-cpu-logfile>/kn-gw.parking.cpu ./
parking$ scp <user>@<node-0>:/mydata/<path-to-cpu-logfile>/kn-fn.parking.cpu ./

parking$ scp <user>@<node-1>:/mydata/<path-to-latency-logfile>/kn.parking_output.csv ./
```

## 5. Misc.
### Compile the eBPF code into an object file
```bash
cd ebpf/
clang -g -O2 -c -target bpf -o sk_msg_kern.o sk_msg_kern.c

# Copy sk_msg_kern.o to obj/
cp sk_msg_kern.o ../obj/

# Debugging purpose: output bpf_printk()
sudo cat  /sys/kernel/debug/tracing/trace_pipe
```