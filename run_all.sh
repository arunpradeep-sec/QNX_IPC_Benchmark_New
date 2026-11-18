#!/bin/sh
RUNS=10
PRIO=50
CPU=0
BURST_US=40000
IDLE_US=40000
cd /data
for POLICY in fifo rr; do
./ipc_server $POLICY $PRIO $CPU &
sleep 2
for PAYLOAD in 32 64 128 512 1024 4096 8192; do
i=1
while [ $i -le $RUNS ]; do
./ipc_client $PAYLOAD $POLICY $PRIO $CPU none $i 10000
i=$((i+1))
done
done
for LOAD in below equal above; do
i=1
while [ $i -le $RUNS ]; do
./ipc_client 32 $POLICY $PRIO $CPU $LOAD $i 100000 $BURST_US $IDLE_US
i=$((i+1))
done
done
slay ipc_server
sleep 1
done
echo done
