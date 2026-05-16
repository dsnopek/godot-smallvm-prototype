#!/bin/sh
# test_embed.sh — end-to-end smoke test for the embedded VM.
#
# Phase 1: build the binary, then run it with --smoke-test. This mode skips
# the vmLoop and calls each custom primitive directly via doPrimitiveCall,
# proving the primitive set is registered and dispatch works.
#
# Phase 2: launch the VM normally, scrape the pseudo-terminal path it
# prints, then speak the MicroBlocks serial protocol to it:
#   * send a "getVersion" message (short msg, opcode 0x0C)
#   * read back the "version" response (long msg, opcode 0x16)
# This exercises the recvBytes/sendBytes wiring and the IDE protocol on
# both directions, which is the same code path the real MicroBlocks IDE
# would use to load and run scripts.
#
# Exit status is 0 only if both phases succeed.

set -eu
cd "$(dirname "$0")"

if ! [ -x ./vm_embed ]; then
    echo "[test] building first..."
    # The build is driven from the parent project's SCons. We invoke just
    # the "example" alias to avoid pulling in the godot-cpp shared-library
    # build for a quick test run.
    (cd ../../.. && scons example) || exit 1
fi

# --- Phase 1: direct-call smoke test --------------------------------------
echo
echo "==== Phase 1: doPrimitiveCall smoke test ===="
SMOKE_OUTPUT=$(./vm_embed --smoke-test)
echo "$SMOKE_OUTPUT"

echo "$SMOKE_OUTPUT" | grep -q 'hello, World!'                    || { echo "[FAIL] greet did not print"; exit 1; }
echo "$SMOKE_OUTPUT" | grep -q 'add(3, 4) returned: 7'            || { echo "[FAIL] add returned wrong value"; exit 1; }
echo "$SMOKE_OUTPUT" | grep -q 'square(5) returned: 25'           || { echo "[FAIL] square returned wrong value"; exit 1; }
echo "$SMOKE_OUTPUT" | grep -q 'player after moveBy.*x=105 y=197' || { echo "[FAIL] current-object pattern (player) failed"; exit 1; }
echo "$SMOKE_OUTPUT" | grep -q 'enemy  after moveBy.*x=250 y=400' || { echo "[FAIL] current-object switch (enemy) failed"; exit 1; }
echo "[PASS] phase 1: custom primitives dispatched correctly"

# --- Phase 2: serial protocol round-trip through the PTY ------------------
echo
echo "==== Phase 2: PTY + serial-protocol round-trip ===="

# Run the VM in the background, capture its banner, then drive it.
./vm_embed > vm_output.log 2>&1 &
VM_PID=$!
trap 'kill $VM_PID 2>/dev/null || true; rm -f vm_output.log probe.py' EXIT

# Wait for the banner that contains the PTY path.
PTY=""
for _ in $(seq 1 50); do
    if grep -q 'connect IDE on:' vm_output.log 2>/dev/null; then
        PTY=$(grep 'connect IDE on:' vm_output.log | sed 's/.*on: //')
        break
    fi
    sleep 0.1
done
if [ -z "$PTY" ] || ! [ -e "$PTY" ]; then
    echo "[FAIL] vm did not print a pseudo-terminal path"
    cat vm_output.log
    exit 1
fi
echo "[test] VM is listening on $PTY"

# Speak the MicroBlocks serial protocol. See misc/SERIAL_PROTOCOL.md for
# the wire format. Short messages: [0xFA, opcode, arg]. Long replies:
# [0xFB, opcode, chunkID, sizeLSB, sizeMSB, ...payload...].
cat >probe.py <<PY
import os, sys, time, struct

pty_path = "$PTY"
fd = os.open(pty_path, os.O_RDWR | os.O_NOCTTY)

# getVersionMsg = 0x0C (12). The third byte is unused for this op; send 0.
os.write(fd, bytes([0xFA, 0x0C, 0x00]))
time.sleep(0.5)

buf = b""
end = time.time() + 2.0
while time.time() < end:
    try:
        chunk = os.read(fd, 256)
        if chunk:
            buf += chunk
    except BlockingIOError:
        pass
    if b"\xFB\x16" in buf:
        break

if b"\xFB\x16" not in buf:
    print("FAIL: no versionMsg (0x16) in reply:", buf.hex())
    sys.exit(1)

# Parse the long-message payload and pull out the version string.
idx = buf.index(b"\xFB\x16")
size = buf[idx + 3] | (buf[idx + 4] << 8)
payload = buf[idx + 5 : idx + 5 + size]
# Payload is [type-byte, ...string bytes..., terminator]. type=2 -> string.
if not payload:
    print("FAIL: empty versionMsg payload")
    sys.exit(1)
text = payload[1:].rstrip(b"\xFE\x00").decode("utf-8", "replace")
print("VERSION:", text)

# We expect the boardType we set in embed_platform.c.
if "Embed" not in text:
    print("FAIL: boardType 'Embed' not in version reply")
    sys.exit(1)
print("PASS: serial protocol round-trip OK")
PY

python3 probe.py
echo "[PASS] phase 2: IDE protocol works through the embedded PTY"

echo
echo "==== ALL TESTS PASSED ===="
