#!/bin/bash

echo "Starting CPU usage collection..."
cd /mydata

if [ ! -d "online-boutique-results/" ] ; then
    echo "online-boutique-results/ DOES NOT exists."
    mkdir online-boutique-results/
fi

cd online-boutique-results
pidstat 1 180 -G ^server$ > grpc-front-prod.cpu & pidstat 1 180 -C recommend > grpc-recom.cpu & pidstat 1 180 -C service > grpc-others.cpu

echo "CPU usage collection is done!"
