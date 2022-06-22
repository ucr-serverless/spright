#!/bin/bash

SPRIGHT_PATH=$1

if [ -z "$SPRIGHT_PATH" ] ; then
  echo "Usage: $ <SPRIGHT_PATH>"
  exit 1
fi

tmux set remain-on-exit on

echo "Creating tmux panes..."
for j in {0..8}
do
    tmux split-window -v -p 80 -t ${j}
    tmux select-layout -t ${j} tiled
done

echo "Configuring fds in tmux panes..."
for j in {1..8}
do
    tmux send-keys -t ${j} "cd /mydata/spright/" Enter
    sleep 0.1
done


echo "Testing S-SPRIGHT with parking detection dataset..."
tmux send-keys -t 1 "sudo ./run.sh shm_mgr cfg/parking.cfg" Enter
sleep 1
tmux send-keys -t 2 "sudo ./run.sh gateway" Enter
sleep 10
tmux send-keys -t 3 "sudo ./run.sh nf 1" Enter
sleep 1
tmux send-keys -t 4 "sudo ./run.sh nf 2" Enter
sleep 1
tmux send-keys -t 5 "sudo ./run.sh nf 3" Enter
sleep 1
tmux send-keys -t 6 "sudo ./run.sh nf 4" Enter
sleep 1
tmux send-keys -t 7 "sudo ./run.sh nf 5" Enter

sleep 0.1

echo "Starting CPU usage collection..."
cd /mydata

if [ ! -d "parking-results/" ] ; then
    echo "parking-results/ DOES NOT exists."
    mkdir parking-results/
fi

cd parking-results

pidstat 1 700 -G ^gateway_sk_msg$ > skmsg_gw.parking.cpu & pidstat 1 700 -G ^nf_sk_msg$ > skmsg_fn.parking.cpu

echo "CPU usage collection is done!"
