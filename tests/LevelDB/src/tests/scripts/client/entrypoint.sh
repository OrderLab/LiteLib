#!/bin/sh
cd /workspace/client
cargo build --release
tail -f /dev/null