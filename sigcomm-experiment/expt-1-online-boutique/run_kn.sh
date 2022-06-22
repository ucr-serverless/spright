#!/bin/bash

echo "Starting CPU usage collection..."
cd /mydata

if [ ! -d "online-boutique-results/" ] ; then
    echo "online-boutique-results/ DOES NOT exists."
    mkdir online-boutique-results/
fi

cd online-boutique-results
pidstat 1 180 -G ^queue$ > kn-queue.cpu &  pidstat 1 180 -C envoy > kn-gw.cpu & pidstat 1 180 -G ^server$ > kn-front-prod.cpu & pidstat 1 180 -C recommend > kn-recom.cpu & pidstat 1 180 -C service > kn-others.cpu

echo "CPU usage collection is done!"
