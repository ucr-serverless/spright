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

	bin/shm_mgr_${io} \
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
	bin/gateway_${io} \
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

	bin/nf_${io} \
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

	* )
		print_usage
		exit 1
esac
