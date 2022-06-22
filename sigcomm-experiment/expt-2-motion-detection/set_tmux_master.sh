#!/bin/bash

SPRIGHT_PATH=$1

if [ -z "$SPRIGHT_PATH" ] ; then
  echo "Usage: $ <SPRIGHT_PATH>"
  exit 1
fi

tmux set remain-on-exit on

echo "Creating tmux panes..."
for j in {0..5}
do
    tmux split-window -v -p 80 -t ${j}
    tmux select-layout -t ${j} tiled
done

echo "Configuring fds in tmux panes..."
for j in {1..5}
do
    tmux send-keys -t ${j} "cd /mydata/spright/" Enter
    sleep 0.1
done


echo "Testing S-SPRIGHT with motion detection dataset..."
tmux send-keys -t 1 "sudo ./run.sh shm_mgr cfg/motion-detection.cfg" Enter
sleep 1
tmux send-keys -t 2 "sudo ./run.sh gateway" Enter
sleep 10
tmux send-keys -t 3 "sudo ./run.sh nf 1" Enter
sleep 1
tmux send-keys -t 4 "sudo ./run.sh nf 2" Enter

sleep 0.1

echo "Starting CPU usage collection..."
cd /mydata

if [ ! -d "motion-detection-results/" ] ; then
    echo "motion-detection-results/ DOES NOT exists."
    mkdir motion-detection-results/
fi

cd motion-detection-results

pidstat 1 3600 -G ^gateway_sk_msg$ > skmsg_gw.motion.cpu & pidstat 1 3600 -G ^nf_sk_msg$ > skmsg_fn.motion.cpu

echo "CPU usage collection is done!"
