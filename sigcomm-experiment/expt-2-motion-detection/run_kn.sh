#!/bin/bash

echo "Starting CPU usage collection..."
cd /mydata

if [ ! -d "motion-detection-results/" ] ; then
    echo "motion-detection-results/ DOES NOT exists."
    mkdir motion-detection-results/
fi

cd motion-detection-results
pidstat 1 3600 -G ^queue$ > kn-queue.motion.cpu &  pidstat 1 180 -G ^nginx$ > kn-gw.motion.cpu & pidstat 1 3600 -G ^server$ > kn-fn.motion.cpu

echo "CPU usage collection is done!"
