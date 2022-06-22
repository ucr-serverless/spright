#!/bin/bash

ALTERNATIVE=$1

if [ -z "$ALTERNATIVE" ] ; then
  echo "Usage: $0 <s-spright | d-spright | kn | grpc>"
  exit 1
fi

STAT_PATH="/mydata/spright/sigcomm-experiment/expt-1-online-boutique/"

cd $STAT_PATH

echo "Consolidating Locust stats files from each Locust worker..."
for j in {2..17}
do
    if [ ! -d "locust_worker_${j}/" ] ; then
      echo "locust_worker_${j}/ DOES NOT exists."
      cat $STAT_PATH/locust_worker_${j}/stats.csv >> $STAT_PATH/stats_${ALTERNATIVE}.csv
    fi
done
