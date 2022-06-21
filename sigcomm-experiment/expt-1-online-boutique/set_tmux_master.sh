#!/bin/bash

ARCH=$1
SPRIGHT_PATH=$2

if [ -z "$ARCH" ] ; then
  echo "Usage: $0 <ARCH> <LOAD_GEN_PATH>"
  exit 1
fi

tmux set remain-on-exit on

echo "Creating tmux panes..."
for j in {0..14}
do
    tmux split-window -v -p 80 -t ${j}
    tmux select-layout -t ${j} tiled
done

echo "Configuring fds in tmux panes..."
for j in {1..14}
do
    tmux send-keys -t ${j} "cd /mydata/spright/" Enter
    sleep 0.1
done


if [ $ARCH == "s-spright" ]; then
  echo "Testing S-SPRIGHT..."
  tmux send-keys -t 1 "sudo ./run.sh shm_mgr cfg/online-boutique-concurrency-32.cfg" Enter
  tmux send-keys -t 2 "sudo ./run.sh gateway" Enter
  sleep 10
  tmux send-keys -t 3 "sudo ./run.sh frontendservice 1" Enter
  sleep 1
  tmux send-keys -t 4 "sudo ./run.sh currencyservice 2" Enter
  sleep 1
  tmux send-keys -t 5 "sudo ./run.sh productcatalogservice 3" Enter
  sleep 1
  tmux send-keys -t 6 "sudo ./run.sh cartservice 4" Enter
  sleep 1
  tmux send-keys -t 7 "sudo ./run.sh recommendationservice 5" Enter
  sleep 1
  tmux send-keys -t 8 "sudo ./run.sh shippingservice 6" Enter
  sleep 1
  tmux send-keys -t 9 "sudo ./run.sh checkoutservice 7" Enter
  sleep 1
  tmux send-keys -t 10 "sudo ./run.sh paymentservice 8" Enter
  sleep 1
  tmux send-keys -t 11 "sudo ./run.sh emailservice 9" Enter
  sleep 1
  tmux send-keys -t 12 "sudo ./run.sh adservice 10" Enter
else
  echo "Testing D-SPRIGHT..."
  tmux send-keys -t 1 "sudo RTE_RING=1 ./run.sh shm_mgr cfg/online-boutique-concurrency-32.cfg" Enter
  tmux send-keys -t 2 "sudo RTE_RING=1 ./run.sh gateway" Enter
  sleep 10
  tmux send-keys -t 3 "sudo RTE_RING=1 ./run.sh frontendservice 1" Enter
  sleep 1
  tmux send-keys -t 4 "sudo RTE_RING=1 ./run.sh currencyservice 2" Enter
  sleep 1
  tmux send-keys -t 5 "sudo RTE_RING=1 ./run.sh productcatalogservice 3" Enter
  sleep 1
  tmux send-keys -t 6 "sudo RTE_RING=1 ./run.sh cartservice 4" Enter
  sleep 1
  tmux send-keys -t 7 "sudo RTE_RING=1 ./run.sh recommendationservice 5" Enter
  sleep 1
  tmux send-keys -t 8 "sudo RTE_RING=1 ./run.sh shippingservice 6" Enter
  sleep 1
  tmux send-keys -t 9 "sudo RTE_RING=1 ./run.sh checkoutservice 7" Enter
  sleep 1
  tmux send-keys -t 10 "sudo RTE_RING=1 ./run.sh paymentservice 8" Enter
  sleep 1
  tmux send-keys -t 11 "sudo RTE_RING=1 ./run.sh emailservice 9" Enter
  sleep 1
  tmux send-keys -t 12 "sudo RTE_RING=1 ./run.sh adservice 10" Enter
fi

sleep 0.1

echo "Starting CPU usage collection..."
cd /mydata

if [ ! -d "online-boutique-results/" ] ; then
    echo "online-boutique-results/ DOES NOT exists."
    mkdir online-boutique-results/
fi

cd online-boutique-results
pidstat 1 180 -C gateway_sk_msg > skmsg_gw.cpu & pidstat 1 180 -C nf_ > skmsg_fn.cpu

echo "CPU usage collection is done!"
# tmux kill-pane -t 0
