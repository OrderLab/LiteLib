#!/usr/bin/env python3
import argparse
import json
import os
import shutil


def load(path):
    with open(path) as stream:
        return json.load(stream)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-latency", required=True)
    parser.add_argument("--base-cpu", required=True)
    parser.add_argument("--leveldb")
    parser.add_argument("--redis")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)
    latency_path = os.path.join(args.output, "latency.json")
    cpu_path = os.path.join(args.output, "cpu.json")
    shutil.copyfile(args.base_latency, latency_path)
    shutil.copyfile(args.base_cpu, cpu_path)
    latency = load(latency_path)
    cpu = load(cpu_path)

    if args.leveldb:
        data = load(args.leveldb)
        latency["LevelDB"] = {
            "full": data["latency"]["vanilla"],
            "ebpf": data["latency"]["ebpf"],
            "checkpoint": data["latency"]["checkpoint"],
        }
        cpu["LevelDB"] = {
            "full": data["cpu"]["vanilla"]["redis-leveldb-vanilla"],
            "ebpf": {
                "full": data["cpu"]["ebpf"]["redis-leveldb"],
                "lite": data["cpu"]["ebpf"]["LiteLevelDB"],
            },
            "checkpoint": data["cpu"]["checkpoint"]["redis-leveldb-vanilla"],
        }

    if args.redis:
        data = load(args.redis)
        latency["Redis"] = {
            "full": data["latency"]["vanilla"],
            "embedded": data["latency"]["embedded"],
            "replica": data["latency"]["replica"],
        }
        cpu["Redis"] = {
            "full": data["cpu"]["vanilla"],
            "embedded": data["cpu"]["embedded"],
            "replica": data["cpu"]["replica"],
        }

    with open(latency_path, "w") as stream:
        json.dump(latency, stream, indent=2)
    with open(cpu_path, "w") as stream:
        json.dump(cpu, stream, indent=2)
    print(f"wrote {latency_path}")
    print(f"wrote {cpu_path}")


if __name__ == "__main__":
    main()
