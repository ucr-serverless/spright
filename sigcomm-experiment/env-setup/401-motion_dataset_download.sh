#!/bin/bash

echo "Downloading motion dataset..."
cd /mydata/spright/sigcomm-experiment/expt-2-motion-detection/load-generator
wget --load-cookies /tmp/cookies.txt "https://docs.google.com/uc?export=download&confirm=$(wget --quiet --save-cookies /tmp/cookies.txt --keep-session-cookies --no-check-certificate 'https://docs.google.com/uc?export=download&id=1MwJIxaokG52YQ9xkCxoT4AmZprgBoO0z' -O- | sed -rn 's/.*confirm=([0-9A-Za-z_]+).*/\1\n/p')&id=1MwJIxaokG52YQ9xkCxoT4AmZprgBoO0z" -O motion_dataset.tar.gz && rm -rf /tmp/cookies.txt
tar -zxvf motion_dataset.tar.gz && rm motion_dataset.tar.gz