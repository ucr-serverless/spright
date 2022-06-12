#!/bin/bash
mount_path=$MYMOUNT

if [[ $mount_path == "" ]]
then
	echo MYMOUNT env var not defined
	exit 1
fi

echo 'Install Knative Serving v0.22.0'
kubectl apply -f https://github.com/knative/serving/releases/download/v0.22.0/serving-crds.yaml
kubectl apply -f ./sigcomm-experiment/env-setup/serving-core.yaml

echo 'Install Istio v0.22.0'
kubectl apply -f ./sigcomm-experiment/env-setup/istio.yaml # Install a properly configured Istio
kubectl apply -f https://github.com/knative/net-istio/releases/download/v0.22.0/net-istio.yaml # Install the Knative Istio controller
kubectl --namespace istio-system get service istio-ingressgateway # Fetch the External IP or CNAME

# echo 'Install the required CRDs and the core components of Eventing'
# kubectl apply -f https://github.com/knative/eventing/releases/download/v0.22.0/eventing-crds.yaml
# kubectl apply -f https://github.com/knative/eventing/releases/download/v0.22.0/eventing-core.yaml