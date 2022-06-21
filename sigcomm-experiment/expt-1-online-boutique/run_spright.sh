#!/bin/bash

ARCH=$1
SPRIGHT_PATH="/mydata/spright/"

if [ -z "$ARCH" ] ; then
  echo "Usage: $0 < s-spright | d-spright >"
  exit 1
fi

if [ -z "$TMUX" ]; then
  if [ -n "`tmux ls | grep spright`" ]; then
    tmux kill-session -t spright
  fi
  tmux new-session -s spright -n demo "./set_tmux_master.sh $ARCH $SPRIGHT_PATH"
fi