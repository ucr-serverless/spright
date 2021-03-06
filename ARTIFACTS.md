# SPRIGHT

## Description

This artifact runs on c220g5 nodes on NSF Cloudlab. We have already created 3 clusters (each with one master node and one worker node) with pre-configured environments. Each cluster is dedicated to one workload experiment in Section 4 to help you proceed evaluation in parallel. 

## Experiment Workflow

### 1 - Online boutique workload.
**Check-List**:
- Program: S-SPRIGHT, D-SPRIGHT, Knative function, gRPC (Kubernetes pod)
- Metrics: RPS, Latency and CPU usage
- Deliverables: Fig. 9, Fig. 10 (a) - (i)
- Time needed to complete experiments: 30 minutes

#### 1.0 ssh to the nodes
On your local machine, open two terminals
- **Terminal-1**: use `ssh -p 22 sqi009@c220g5-111011.wisc.cloudlab.us` to login to **Master node (node-0)**
- **Terminal-2**: use `ssh -p 22 sqi009@c220g5-111008.wisc.cloudlab.us` to login to **Worker node (node-1)**


#### 1.1 Run Online boutique using S-SPRIGHT (SKMSG)

> **Master node (node-0) operations**
```shell=
# Run the following commands on master node (node-0),
# i.e., sqi009@c220g5-111011.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./run_spright.sh s-spright
# Please wait for 1 minutes until SPRIGHT components 
# are booted, then move to run the load generator

# After the experiment is done (~3 minutes)
# Enter Ctrl+B then D to detach from tmux

# Kill the tmux session to stop SPRIGHT components
expt-1-online-boutique$ tmux kill-session -t spright
```
---
> **Worker node (node-1) operations**
```shell=
# Run the following commands on worker node (node-1),
# i.e., sqi009@c220g5-111008.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

# Please make sure SPRIGHT components 
# are booted before running the load generator
expt-1-online-boutique$ ./run_load_generators.sh spright 10.10.1.1 8080

# After the experiment is done (~3 minutes)
# Enter Ctrl+B then D to detach from tmux
```

> **After the load generation on worker node is done, consolidating metric files generated by Locust workers on the worker node (node-1)**
```shell=
# Make sure you run the following commands
# on the worker node (node-1)

$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./consolidate_locust_stats.sh s-spright

```

NOTE: Before moving to 1.2, please make sure you have kill the tmux session 'spright' on **master node (node-0)** by running command `tmux kill-session -t spright`

#### 1.2 Run Online boutique using D-SPRIGHT (DPDK's RTE rings)

> **Master node (node-0) operations**
```shell=
# Run the following commands on master node (node-0),
# i.e., sqi009@c220g5-111011.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./run_spright.sh d-spright

# After the experiment is done (~3 minutes)
# Enter Ctrl+B then D to detach from tmux

# Kill the tmux session to stop SPRIGHT components
expt-1-online-boutique$ tmux kill-session -t spright
```
---
> **Worker node (node-1) operations**
```shell=
# Run the following commands on worker node (node-1),
# i.e., sqi009@c220g5-111008.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

# Please make sure SPRIGHT components 
# are booted before running the load generator
expt-1-online-boutique$ ./run_load_generators.sh spright 10.10.1.1 8080

# After the experiment is done (~3 minutes)
# Enter Ctrl+B then D to detach from tmux
```
> **After the load generation on worker node is done, consolidating metric files generated by Locust workers on the worker node (node-1)**
```shell=
# Make sure you are on the worker node (node-1)
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./consolidate_locust_stats.sh d-spright

```

NOTE: Before moving to 1.3, please make sure you have kill the tmux session 'spright' on **master node (node-0)** by running command `tmux kill-session -t spright`, otherwise, it may affect the measurements in 1.3.

#### 1.3 Run Online boutique using Knative

> **Master node (node-0) operations**

**Step-1**: start Knative functions
```shell=
# Run the following commands on master node (node-0),
# i.e., sqi009@c220g5-111011.wisc.cloudlab.us

$ cd /mydata/spright/

spright$ kubectl apply -f sigcomm-experiment/expt-1-online-boutique/manifests/knative

# Note: the startup of functions will take 1-2 minutes
# please wait until functions are started up
# You can use 'kubectl get pods' to check status
```

**Step-2**: Start CPU usage collection
```shell= 
# Run the following commands on master node (node-0),
# i.e., sqi009@c220g5-111011.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

# Please enter ./run_kn.sh just before
# ./run_load_generator.sh command on worker node. 
# E.g., you may have both commands typed
# on each terminal. Then enter ./run_kn.sh on 
# the master node and 
# afterwards enter ./run_load_generator.sh on worker node

expt-1-online-boutique$ ./run_kn.sh
```
---
> **Worker node (node-1) operations**
```shell= 
# Run the following commands on worker node (node-1),
# i.e., sqi009@c220g5-111008.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./run_load_generators.sh kn 128.105.144.181 31235

# After the experiment is done (~3 minutes)
# Enter Ctrl+B then D to detach from tmux

```
> **After the load generation on worker node is done, consolidating metric files generated by Locust workers on the worker node (node-1)**
```shell=
# Make sure you are on the worker node (node-1)
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./consolidate_locust_stats.sh kn

```
---
NOTE: Before moving to 1.4, run the following command on **master node (node-0)** to clean up Knative functions.
```shell=
# Make sure you are on the master node (node-0)
$ cd /mydata/spright/

spright$ kubectl delete -f sigcomm-experiment/expt-1-online-boutique/manifests/knative
```


#### 1.4 Run Online boutique using gRPC

> **Master node (node-0) operations**

**Step-1**: start gRPC functions
```shell=
# Run the following commands on master node (node-0),
# i.e., sqi009@c220g5-111011.wisc.cloudlab.us

$ cd /mydata/spright

spright$ kubectl apply -f sigcomm-experiment/expt-1-online-boutique/manifests/kubernetes

# It may 1 minutes to startup functions

# Get the IP of Frontend Service
spright$ kubectl get po -l app=frontend -o wide

# Output will be like:
# sqi009@node0:/mydata/spright$ kubectl get po -l app=frontend -o wide
# NAME                        READY   STATUS    RESTARTS   AGE   IP                NODE                                                   NOMINATED NODE   READINESS GATES
# frontend-6c946cc7c6-mpnsb   1/1     Running   0          30s   192.168.121.232   node0.spright-expt-1.kkprojects-pg0.wisc.cloudlab.us   <none>           <none>
# DO NOT USE the IP in this example, it may differ after you re-create the frontend function.

# Record the IP of Frontend Service, it will be used by the load generator on worker node
```

**Step-2**: CPU usage collection
```shell= 
# Run the following commands on master node (node-0),
# i.e., sqi009@c220g5-111011.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

# Please enter ./run_grpc.sh just before
# ./run_load_generator.sh command on worker node. 
# E.g., you may have both commands typed
# on each terminal. Then enter ./run_grpc.sh on 
# the master node and 
# afterwards enter ./run_load_generator.sh on worker node

expt-1-online-boutique$ ./run_grpc.sh
```
---
> **Worker node (node-1) operations**
```shell=
# Run the following commands on worker node (node-1),
# i.e., sqi009@c220g5-111008.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

# Please use the IP of frontend service obtained on master node (Step-1 of gRPC online boutique)
expt-1-online-boutique$ ./run_load_generators.sh kn $FRONTEND_SVC_IP 8080

# After the experiment is done (~3 minutes)
# Enter Ctrl+B then D to detach from tmux
```
> **After the load generation on worker node is done, consolidate metric files generated by Locust workers on the worker node (node-1)**
```shell=
# Make sure you are on the worker node (node-1)
$ cd /mydata/spright/sigcomm-experiment/expt-1-online-boutique/

expt-1-online-boutique$ ./consolidate_locust_stats.sh grpc
```
---
Clean up gRPC functions: run the following command on **master node (node-0)**.
```shell=
# Make sure you are on the master node (node-0)
$ cd /mydata/spright/

spright$ kubectl delete -f sigcomm-experiment/expt-1-online-boutique/manifests/kubernetes
```


### 2 - Motion detection
**Check-List**:
- Program: S-SPRIGHT, Knative function
- Metrics: Latency and CPU usage
- Deliverables: Fig. 11 (a) and (b)
- Time needed to complete experiments: 2 hours

#### 2.0 ssh to the nodes
On your local machine, open two terminals
- **Terminal-1**: use `ssh -p 22 sqi009@c220g5-111203.wisc.cloudlab.us` to login to **Master node (node-0)**
- **Terminal-2**: use `ssh -p 22 sqi009@c220g5-111216.wisc.cloudlab.us` to login to **Worker node (node-1)**

#### 2.1 Run Motion Detection using S-SPRIGHT

> **Master node (node-0) operations**
```shell=
# Run the following commands on master node (node-0)
# i.e., ssh -p 22 sqi009@c220g5-111203.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/

expt-2-motion-detection$ ./run_spright.sh

# After the experiment is done (~60 minutes)
# Enter Ctrl+B then D to detach from tmux
```
---
> **Worker node (node-1) operations**
Run load generator. NOTE: make sure SPRIGHT components are started up on master node before running the load generator on worker node
```shell=
# Run the following commands on worker node (node-1)
# i.e., ssh -p 22 sqi009@c220g5-111216.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator

load-generator$ python3 spright-motion-generator.py --ip 10.10.1.1 --port 8080
```

#### 2.2 Run Motion Detection using Knative
> **Master node (node-0) operations**
I've pre-configure the motion proxy, so you can go ahead to CPU usage collection
```shell= 
# Run the following commands on master node (node-0)
# i.e., ssh -p 22 sqi009@c220g5-111203.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/

# Make sure you have the load generator command on the worker node ready, 
# so you can execute both at the same time
expt-2-motion-detection$ ./run_kn.sh
```
---
> **Worker node (node-1) operations**
Run load generator
```shell=
# Run the following commands on worker node (node-1)
# i.e., ssh -p 22 sqi009@c220g5-111216.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator

load-generator$ python3 kn-motion-generator.py --ip 192.168.7.141 --port 80
```

### 3 - Parking detection & Charging.
**Check-List**:
- Program: S-SPRIGHT, Knative function
- Metrics: Latency and CPU usage
- Deliverables: Fig. 12 (a) and (b)
- Time needed to complete experiments: 30 minutes

#### 3.0 ssh to the nodes
On your local machine, open two terminals
- **Terminal-1**: use `ssh -p 22 sqi009@c220g5-110911.wisc.cloudlab.us` to login to **Master node (node-0)**
- **Terminal-2**: use `ssh -p 22 sqi009@c220g5-110922.wisc.cloudlab.us` to login to **Worker node (node-1)**

#### 3.1 Run the parking detection function chain using S-SPRIGHT
> **Master node (node-0) operations**
```shell=
# Please run the following commands on master node (node-0)
# i.e., sqi009@c220g5-110911.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-3-parking/

expt-3-parking$ ./run_spright.sh

# After the experiment is done (~12 minutes)
# Enter Ctrl+B then D to detach from tmux
```
---
> **Worker node (node-1) operations**
Run load generator. Note: make sure you have SPRIGHT components on master node fully started before running the load generator on the worker node.
```shell=
# Please run the following commands on worker node (node-1)
# i.e., sqi009@c220g5-110922.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-3-parking/load-generator

load-generator$ python3 skmsg-parking.py --ip 10.10.1.1 --port 8080
```

#### 3.2 Run the parking detection function chain using Knative
> **Master node (node-0) operations**
I've already configured the parking proxy on master node, so you can go ahead to the CPU usage collection
```shell= 
# Please run the following commands on master node (node-0)
# i.e., sqi009@c220g5-110911.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-3-parking/

expt-3-parking$ ./run_kn.sh
```
---
> **Worker node (node-1) operations**
Run load generator
```shell=
# Please run the following commands on worker node (node-1)
# i.e., sqi009@c220g5-110922.wisc.cloudlab.us

$ cd /mydata/spright/sigcomm-experiment/expt-3-parking/load-generator

load-generator$ python3 kn-parking.py --nginxip 192.168.166.209 --nginxport 80 --istioip 128.105.144.149 --istioport 30955
```

## 5. Download metric logs to your local machine.
- **Prerequisite**: create a "results" directory on your local machine if you do not have

### Download Online Boutique experiment raw metric files
```shell=
# Go to the "results" directory on your local machine
$ cd $HOME/results

# Creating a directory to put Online Boutique experiment related metric files
results$ mkdir online_boutique && cd online_boutique

# Run the folllowing commands in the 'online_boutique' directory
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/dpdk_gw.cpu ./
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/dpdk_nf.cpu ./
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/grpc-recom.cpu ./
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/grpc-front-prod.cpu ./
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/grpc-others.cpu ./
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/kn-gw.cpu ./
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/kn-queue.cpu ./
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/kn-front-prod.cpu ./
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/kn-others.cpu ./
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/kn-recom.cpu ./
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/skmsg_fn.cpu ./
scp sqi009@c220g5-111011.wisc.cloudlab.us:/mydata/online-boutique-results/skmsg_gw.cpu ./

scp sqi009@c220g5-111008.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/rps_stats_d-spright.csv ./
scp sqi009@c220g5-111008.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/latency_of_each_req_stats_d-spright.csv ./
scp sqi009@c220g5-111008.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/rps_stats_s-spright.csv ./
scp sqi009@c220g5-111008.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/latency_of_each_req_stats_s-spright.csv ./
scp sqi009@c220g5-111008.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/rps_stats_grpc.csv ./
scp sqi009@c220g5-111008.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/latency_of_each_req_stats_grpc.csv ./
scp sqi009@c220g5-111008.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/rps_stats_kn.csv ./
scp sqi009@c220g5-111008.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-1-online-boutique/latency_of_each_req_stats_kn.csv ./
```

### Download Motion Detection experiment raw metric files
```shell=
# Go to the "results" directory on your local machine
$ cd $HOME/results

# Creating a directory to put Motion Detection experiment related metric files
results$ mkdir motion && cd motion

# Run the following commands in the 'motion' directory
scp sqi009@c220g5-111203.wisc.cloudlab.us:/mydata/motion-detection-results/kn-queue.motion.cpu ./
scp sqi009@c220g5-111203.wisc.cloudlab.us:/mydata/motion-detection-results/kn-gw.motion.cpu ./
scp sqi009@c220g5-111203.wisc.cloudlab.us:/mydata/motion-detection-results/kn-fn.motion.cpu ./
scp sqi009@c220g5-111203.wisc.cloudlab.us:/mydata/motion-detection-results/skmsg_gw.motion.cpu ./
scp sqi009@c220g5-111203.wisc.cloudlab.us:/mydata/motion-detection-results/skmsg_fn.motion.cpu ./

scp sqi009@c220g5-111216.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator/skmsg_motion_output.csv ./
scp sqi009@c220g5-111216.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator/kn_motion_output.csv ./
```

### Download Parking experiment raw metric files
```shell=
# Go to the "results" directory on your local machine
$ cd $HOME/results

# Creating a directory to put parking experiment related metric files
results$ mkdir parking && cd parking

# Run the folllowing commands in the 'parking' directory
scp sqi009@c220g5-110911.wisc.cloudlab.us:/mydata/parking-results/kn-queue.parking.cpu ./
scp sqi009@c220g5-110911.wisc.cloudlab.us:/mydata/parking-results/kn-gw.parking.cpu ./
scp sqi009@c220g5-110911.wisc.cloudlab.us:/mydata/parking-results/kn-fn.parking.cpu ./
scp sqi009@c220g5-110911.wisc.cloudlab.us:/mydata/parking-results/skmsg_gw.parking.cpu ./
scp sqi009@c220g5-110911.wisc.cloudlab.us:/mydata/parking-results/skmsg_fn.parking.cpu ./

scp sqi009@c220g5-110922.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-3-parking/load-generator/kn.parking_output.csv ./
scp sqi009@c220g5-110922.wisc.cloudlab.us:/mydata/spright/sigcomm-experiment/expt-3-parking/load-generator/skmsg.parking_output.csv ./
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
