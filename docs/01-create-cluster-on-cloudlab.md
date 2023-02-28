# Creating a 2-node cluster on Cloudlab
This guideline is mainly for creating a cluster on NSF Cloudlab. We have tested several physical node types on NSF Cloudlab, such as [xl170](https://www.utah.cloudlab.us/portal/show-nodetype.php?type=xl170) and [c220g5](https://www.wisc.cloudlab.us/portal/show-nodetype.php?type=c220g5), but it should work on any **x86** nodes on NSF Cloudlab. Please refer to [The Cloud Lab Manual](http://docs.cloudlab.us/hardware.html) to find available node types on NSF Cloudlab and their hardware spec.
- Note: We haven't tested SPRIGHT on any **arm** nodes yet, e.g., [m400](https://www.utah.cloudlab.us/portal/show-nodetype.php?type=m400).

We conduct the experiment on **Ubuntu 20.04**. Using other Ubuntu distributions may lead to unexpect issues. We haven't fully verified this yet.

### Disk space on Cloudlab machines ###
As Cloudlab only allocates 16GB disk space by default, please check **Temp Filesystem Max Space** to maximize the disk space configuration and keep **Temporary Filesystem Mount Point** as default (**/mydata**)

### Guideline ###
1. The following steps require a **bash** environment on Cloudlab. Please configure the default shell in your CloudLab account to be bash. For how to configure bash on Cloudlab, Please refer to the post "Choose your shell": https://www.cloudlab.us/portal-news.php?idx=49

2. When starting a new experiment on Cloudlab, select the **small-lan** profile

3. On the profile parameterization page, 
    - Set **Number of Nodes** as needed
        - We use *node-0* as the master node to run the control plane of Kubernetes, Knative.
        - *node-1* is used as worker nodes.
    - Set OS image as **Ubuntu 20.04**
    - Set physical node type as **c220g5** (or any other preferred node type)
    - Please check **Temp Filesystem Max Space** box
    - Keep **Temporary Filesystem Mount Point** as default (**/mydata**)

4. Wait for the cluster to be initialized (It may take 5 to 10 minutes)

---

5. Extend the disk space on allocated master and worker nodes. This is because Cloudlab only allocates a 16GB disk space.
    - We use `/mydata` as the working directory
    - On the master node and worker nodes, run
```bash
sudo chown -R $(id -u):$(id -g) /mydata
cd /mydata
export MYMOUNT=/mydata
```