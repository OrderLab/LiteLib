#!/bin/sh
cp /workspace/scripts/.ssh ~ -r
chmod 600 ~/.ssh/id_ed25519
cd /workspace/client
cargo build --release
tail -f /dev/null