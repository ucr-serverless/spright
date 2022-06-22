#!/bin/bash

echo "Starting CPU usage collection..."
cd /mydata

if [ ! -d "parking-results/" ] ; then
    echo "parking-results/ DOES NOT exists."
    mkdir parking-results/
fi

cd parking-results
pidstat 1 700 -G ^queue$ > kn-queue.parking.cpu &  pidstat 1 700 -G ^nginx$ > kn-gw.parking.cpu & pidstat 1 700 -G ^server$ > kn-fn.parking.cpu

echo "CPU usage collection is done!"
