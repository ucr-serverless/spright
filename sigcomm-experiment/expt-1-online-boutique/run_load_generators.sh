#!/bin/bash

ARCH=$1
LOAD_GEN_PATH="./load-generator/"

if [ -z "$ARCH" ] ; then
  echo "Usage: $0 <ARCH>"
  exit 1
fi

echo "Creating separate directory for each Locust worker..."
for j in {1..17}
do
    if [ ! -d "locust_worker_${j}/" ] ; then
      echo "locust_worker_${j}/ DOES NOT exists."
      cp -r $LOAD_GEN_PATH locust_worker_${j}/
    fi
    # sleep 1
done

if [ -z "$TMUX" ]; then
  if [ -n "`tmux ls | grep spright`" ]; then
    tmux kill-session -t spright
  fi
  tmux new-session -s spright -n demo "./set_tmux_worker.sh $ARCH $LOAD_GEN_PATH"
fi