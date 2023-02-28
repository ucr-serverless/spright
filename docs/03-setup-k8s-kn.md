# Setting up Kubernetes & Knative

### 1 - Setting up the Kubernetes master node (**node-0**).
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

### 2 - Setting up the Kubernetes worker node (**node-1**).
```shell=
$ cd /mydata/spright

spright$ export MYMOUNT=/mydata

spright$ ./sigcomm-experiment/env-setup/100-docker_install.sh

spright$ source ~/.bashrc

spright$ ./sigcomm-experiment/env-setup/200-k8s_install.sh slave

# Use the token returned from the master node (**node-0**) to join the Kubernetes control plane. Run `sudo kubeadm join ...` with the token just saved. Please run the `kubeadm join` command with *sudo*
spright$ sudo kubeadm join <control-plane-token>
```

### 3 - Enable pod placement on master node (**node-0**) and taint worker node (**node-1**):
```shell=
$ cd /mydata/spright
spright$ ./sigcomm-experiment/env-setup/201-taint_nodes.sh
```

### 4 - Setting up the Knative.
1. On the master node (**node-0**), run
```shell=
$ cd /mydata/spright
spright$ ./sigcomm-experiment/env-setup/300-knative_install.sh
```