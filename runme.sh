#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

make clean
make

WORKDIR=/tmp/myinit_test
mkdir -p "$WORKDIR"

touch "$WORKDIR/in1" "$WORKDIR/in2" "$WORKDIR/in3"
touch "$WORKDIR/out1" "$WORKDIR/out2" "$WORKDIR/out3"
touch "$WORKDIR/in_one" "$WORKDIR/out_one"

CONFIG="$WORKDIR/myinit.conf"
CONFIG_ONE="$WORKDIR/myinit_one.conf"

cat > "$CONFIG" <<EOF
/bin/sleep 1000 $WORKDIR/in1 $WORKDIR/out1
/bin/sleep 1000 $WORKDIR/in2 $WORKDIR/out2
/bin/sleep 1000 $WORKDIR/in3 $WORKDIR/out3
EOF

cat > "$CONFIG_ONE" <<EOF
/bin/sleep 1000 $WORKDIR/in_one $WORKDIR/out_one
EOF

rm -f /tmp/myinit.pid /tmp/myinit.log

./myinit "$CONFIG"

sleep 2

MPID=$(cat /tmp/myinit.pid)

{
    echo "Test 1: start 3 child proc"
    echo "Expected: 3 child proc"
    ps --ppid "$MPID" -o pid,comm || true
    child_count=$(ps --ppid "$MPID" -o comm= 2>/dev/null | grep -c sleep || true)
    echo "Found: $child_count"
    [ "$child_count" -eq 3 ] && echo "PASS" || echo "FAIL"
    echo

    echo "Test 2: kill child proc 2"
    second=$(ps --ppid "$MPID" -o pid= 2>/dev/null | sed -n '2p' || true)
    echo "Killing pid $second"
    kill "$second"

    sleep 2
    echo "Expected: 3 child proc"
    ps --ppid "$MPID" -o pid,comm || true
    child_count2=$(ps --ppid "$MPID" -o comm= 2>/dev/null | grep -c sleep || true)
    echo "Found: $child_count2"
    [ "$child_count2" -eq 3 ] && echo "PASS" || echo "FAIL"
    echo

    echo "Test 3: change config, SIGHUP, check"
    cp "$CONFIG_ONE" "$CONFIG"
    kill -HUP "$MPID"

    sleep 2
    echo "Expected: 1 child proc"
    ps --ppid "$MPID" -o pid,comm || true
    child_count3=$(ps --ppid "$MPID" -o comm= 2>/dev/null | grep -c sleep || true)
    echo "Found: $child_count3"
    [ "$child_count3" -eq 1 ] && echo "PASS" || echo "FAIL"
    echo

    echo "Test 4: check logs"
    echo "--- log start ---"
    cat /tmp/myinit.log
    echo "--- log end ---"
    starts=$(grep -c "started pid=" /tmp/myinit.log || true)
    ends=$(grep -c -E "exited|killed by signal|killed pid=" /tmp/myinit.log || true)
    echo "Starts: $starts (expected >= 5)"
    echo "Ends: $ends (expected >= 4)"
    [ "$starts" -ge 5 ] && [ "$ends" -ge 4 ] && echo "PASS" || echo "FAIL"

    kill "$MPID" 2>/dev/null || true

} > result.txt 2>&1

echo "Done"
