#!/bin/bash

SPRIGHT_PATH="/mydata/spright/"

if [ -z "$TMUX" ]; then
  if [ -n "`tmux ls | grep spright`" ]; then
    tmux kill-session -t spright
  fi
  tmux new-session -s spright -n demo "./set_tmux_master.sh $SPRIGHT_PATH"
fi