#!/bin/bash

# Please run the script on MASTER node (node-0)
kubectl taint nodes --all node-role.kubernetes.io/master-

kubectl get node > nodes_name.txt

MASTER_NODE_NAME=$(grep "node0" nodes_name.txt | awk '{print $1}')
WORKER_NODE_NAME=$(grep "node1" nodes_name.txt | awk '{print $1}')

echo $MASTER_NODE_NAME
echo $WORKER_NODE_NAME

kubectl label nodes $MASTER_NODE_NAME location=master
kubectl label nodes $WORKER_NODE_NAME location=slave
kubectl taint nodes $WORKER_NODE_NAME location=slave:NoSchedule