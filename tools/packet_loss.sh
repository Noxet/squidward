#!/bin/bash

IF=$1

function sigint_handle {
	echo "Exiting ..."
	tc qdisc del dev $IF root
	exit 0
}

# restore config when user quits
trap 'sigint_handle' SIGINT

if [ $# -lt 2 ]; then
	echo "Usage: $0 <interface> <packet loss in %>"
	exit 1
fi

tc qdisc add dev $IF root netem loss $2

while [ 1 ]; do
	sleep 1
done

