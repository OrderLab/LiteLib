Compiling the eBPF program:

```
cd cascade/src/include
clang -O2 -g -target bpf -D__TARGET_ARCH_x86  -c litesys.bpf.c -o litesys.bpf.o
bpftool gen skeleton litesys.bpf.o > litesys.skel.h
```
Compiling litesys:

```
cd cascade/tests/LevelDB/src/lite-version/Build
make
```

Run the code in the same way that litesys is usually run. Using client script from node 1 also works. Pull the latest client script code for ebpf inclusions.

Current code has 2 versions:

Version 1:

The raw socket is created that receives all the traffic. The eBPF program is attached to this socket. The program drops all the traffic but creates ring buffer events for the redis requests and responses that is captured by the litesys program. This is the current code that runs when you compile and run the code.

Version 2:

Instead of creating a raw socket, the eBPF program is attached to the redis accept socket. The program does not drop any of the packet traffic but creates ring buffer events for the required redis requests and responses that is captured by the litesys program. This version is incomplete and as seen before it only captures requests. To run prelimnary version of this:

1) Comment out lines 218-229 in ebpf_worker_impl.hpp
2) Change the litesys.bpf.c program to return -1 everywhere instead of 0
3) In litesys.bpf.c remove eth and ip header parts and change bpf_skb_load_bytes offsets appropriately
4) Remove parsing of tcp data and make update in ebpf_worker_impl.hpp so that incomplete code does not fail
5) Recompile and run the code