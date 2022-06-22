#!/bin/bash

ARCH=$1
IP=$2
PORT=$3
LOAD_GEN_PATH=$4

if [ -z "$ARCH" ] ; then
  echo "Usage: $0 <ARCH> <LOAD_GEN_PATH>"
  exit 1
fi

tmux set remain-on-exit on

echo "Creating tmux panes..."
for j in {0..17}
do
    tmux split-window -v -p 80 -t ${j}
    tmux select-layout -t ${j} tiled
done

echo "Configuring fds in tmux panes..."
for j in {1..17}
do
    tmux send-keys -t ${j} "./set_fd.sh" Enter
    tmux send-keys -t ${j} "cd ./locust_worker_${j}/" Enter
    sleep 0.1
done

echo "Testing the locust master in tmux pane 1..."
if [ $ARCH == "spright" ]; then
  tmux send-keys -t 1 "locust --version && echo \"Run SPRIGHT's locust master\" " Enter
else
  tmux send-keys -t 1 "locust --version && echo \"Run Knative's locust master\"" Enter
fi

sleep 0.1

echo "Testing locust workers in each tmux pane..."
for j in {2..17}
do
    if [ $ARCH == "spright" ]; then
      tmux send-keys -t ${j} "locust --version && echo \"Run SPRIGHT's locust worker in pane ${j}\"" Enter
    else
      tmux send-keys -t ${j} "locust --version && echo \"Run Knative's locust worker in pane ${j}\"" Enter
    fi
    sleep 0.1
done

# tmux kill-pane -t 0

######
echo "Run the locust master in tmux pane 1..."
if [ $ARCH == "spright" ]; then
  echo "Run SPRIGHT's locust master"
  tmux send-keys -t 1 "locust -u 25000 -r 500 -t 3m --csv kn --csv-full-history -f spright-locustfile.py --headless  -H http://10.10.1.1:8080 --master --expect-workers=16" Enter
else
  echo "Run Knative's locust master"
  tmux send-keys -t 1 "locust -u 5000 -r 200 -t 3m --csv kn --csv-full-history -f kn-locustfile.py --headless  -H http://${IP}:${PORT} --master --expect-workers=16" Enter
fi

sleep 0.1

echo "Run locust workers in each tmux pane..."
for j in {2..17}
do
    if [ $ARCH == "spright" ]; then
      echo "Run SPRIGHT's locust worker"
      tmux send-keys -t ${j} "locust -f spright-locustfile.py --worker" Enter
    else
      echo "Run Knative's locust worker"
      tmux send-keys -t ${j} "locust -f kn-locustfile.py --worker" Enter
    fi
done

sleep 0.1