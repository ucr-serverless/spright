#!/bin/bash

CPU_SHM_MGR=(0)
CPU_GATEWAY=(0 1 2)
CPU_NF=(3 4 5 6 7 8 9 20 21 22 23 24 25 26 27 28 29)

if [ ${EUID} -ne 0 ]
then
	echo "${0}: Permission denied" 1>&2
	exit 1
fi

if [ ${RTE_RING} ] && [ ${RTE_RING} -eq 1 ]
then
	io=rte_ring
else
	io=sk_msg
fi

print_usage()
{
	echo "usage: ${0} < shm_mgr CFG_FILE | gateway | nf NF_ID >" 1>&2
}

shm_mgr()
{
	if ! [ ${1} ]
	then
		print_usage
		exit 1
	fi

	exec bin/shm_mgr_${io} \
		-l ${CPU_SHM_MGR[0]} \
		--file-prefix=spright \
		--proc-type=primary \
		--no-telemetry \
		--no-pci \
		-- \
		${1}
}

gateway()
{
	exec bin/gateway_${io} \
		-l ${CPU_GATEWAY[0]},${CPU_GATEWAY[1]},${CPU_GATEWAY[2]} \
		--main-lcore=${CPU_GATEWAY[0]} \
		--file-prefix=spright \
		--proc-type=secondary \
		--no-telemetry \
		--no-pci
}

nf()
{
	if ! [ ${1} ]
	then
		print_usage
		exit 1
	fi

	if [ ${GO_NF} ] && [ ${GO_NF} -eq 1 ]
	then
		go="go_"
	else
		go=""
	fi

	exec bin/${go}nf_${io} \
		-l ${CPU_NF[$((${1} - 1))]} \
		--file-prefix=spright \
		--proc-type=secondary \
		--no-telemetry \
		--no-pci \
		-- \
		${1}
}

adservice()
{
	if ! [ ${1} ]
	then
		print_usage
		exit 1
	fi

	if [ ${GO_NF} ] && [ ${GO_NF} -eq 1 ]
	then
		go="go_"
	else
		go=""
	fi

	exec bin/${go}adservice_${io} \
		-l ${CPU_NF[$((${1} - 1))]} \
		--file-prefix=spright \
		--proc-type=secondary \
		--no-telemetry \
		--no-pci \
		-- \
		${1}
}

currencyservice()
{
	if ! [ ${1} ]
	then
		print_usage
		exit 1
	fi

	if [ ${GO_NF} ] && [ ${GO_NF} -eq 1 ]
	then
		go="go_"
	else
		go=""
	fi

	exec bin/${go}currencyservice_${io} \
		-l ${CPU_NF[$((${1} - 1))]} \
		--file-prefix=spright \
		--proc-type=secondary \
		--no-telemetry \
		--no-pci \
		-- \
		${1}
}

emailservice()
{
	if ! [ ${1} ]
	then
		print_usage
		exit 1
	fi

	if [ ${GO_NF} ] && [ ${GO_NF} -eq 1 ]
	then
		go="go_"
	else
		go=""
	fi

	exec bin/${go}emailservice_${io} \
		-l ${CPU_NF[$((${1} - 1))]} \
		--file-prefix=spright \
		--proc-type=secondary \
		--no-telemetry \
		--no-pci \
		-- \
		${1}
}

paymentservice()
{
	if ! [ ${1} ]
	then
		print_usage
		exit 1
	fi

	if [ ${GO_NF} ] && [ ${GO_NF} -eq 1 ]
	then
		go="go_"
	else
		go=""
	fi

	exec bin/${go}paymentservice_${io} \
		-l ${CPU_NF[$((${1} - 1))]} \
		--file-prefix=spright \
		--proc-type=secondary \
		--no-telemetry \
		--no-pci \
		-- \
		${1}
}

shippingservice()
{
	if ! [ ${1} ]
	then
		print_usage
		exit 1
	fi

	if [ ${GO_NF} ] && [ ${GO_NF} -eq 1 ]
	then
		go="go_"
	else
		go=""
	fi

	exec bin/${go}shippingservice_${io} \
		-l ${CPU_NF[$((${1} - 1))]} \
		--file-prefix=spright \
		--proc-type=secondary \
		--no-telemetry \
		--no-pci \
		-- \
		${1}
}

productcatalogservice()
{
	if ! [ ${1} ]
	then
		print_usage
		exit 1
	fi

	if [ ${GO_NF} ] && [ ${GO_NF} -eq 1 ]
	then
		go="go_"
	else
		go=""
	fi

	exec bin/${go}productcatalogservice_${io} \
		-l ${CPU_NF[$((${1} - 1))]} \
		--file-prefix=spright \
		--proc-type=secondary \
		--no-telemetry \
		--no-pci \
		-- \
		${1}
}

cartservice()
{
	if ! [ ${1} ]
	then
		print_usage
		exit 1
	fi

	if [ ${GO_NF} ] && [ ${GO_NF} -eq 1 ]
	then
		go="go_"
	else
		go=""
	fi

	exec bin/${go}cartservice_${io} \
		-l ${CPU_NF[$((${1} - 1))]} \
		--file-prefix=spright \
		--proc-type=secondary \
		--no-telemetry \
		--no-pci \
		-- \
		${1}
}

recommendationservice()
{
	if ! [ ${1} ]
	then
		print_usage
		exit 1
	fi

	if [ ${GO_NF} ] && [ ${GO_NF} -eq 1 ]
	then
		go="go_"
	else
		go=""
	fi

	exec bin/${go}recommendationservice_${io} \
		-l ${CPU_NF[$((${1} - 1))]} \
		--file-prefix=spright \
		--proc-type=secondary \
		--no-telemetry \
		--no-pci \
		-- \
		${1}
}

case ${1} in
	"shm_mgr" )
		shm_mgr ${2}
	;;

	"gateway" )
		gateway
	;;

	"nf" )
		nf ${2}
	;;

	"adservice" )
		adservice ${2}
	;;

	"currencyservice" )
		currencyservice ${2}
	;;

	"emailservice" )
		emailservice ${2}
	;;

	"paymentservice" )
		paymentservice ${2}
	;;

	"shippingservice" )
		shippingservice ${2}
	;;

	"productcatalogservice" )
		productcatalogservice ${2}
	;;

	"cartservice" )
		cartservice ${2}
	;;

	"recommendationservice" )
		recommendationservice ${2}
	;;

	* )
		print_usage
		exit 1
esac
