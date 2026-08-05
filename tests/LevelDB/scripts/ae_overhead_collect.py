#!/usr/bin/env python3
import argparse, glob, json, os
import numpy as np

parser=argparse.ArgumentParser()
parser.add_argument("root")
parser.add_argument("--output",required=True)
args=parser.parse_args()

latency={}
cpu={}
for mode in ("vanilla","ebpf","checkpoint"):
    vals=[]
    processes={}
    for path in glob.glob(os.path.join(args.root,f"{mode}-*.stat.pruned.json")):
        data=json.load(open(path))
        vals.append(float(np.nanmean(np.asarray(data["avg_agg_lat"])[20:61])))
    for path in glob.glob(os.path.join(args.root,f"monitor.{mode}-*.jsonl")):
        for line in open(path):
            try: row=json.loads(line)
            except: continue
            if not 20 <= row.get("time",-1) <= 60: continue
            for name,info in row.items():
                if name!="time": processes.setdefault(name,[]).append(info["cpu"])
    latency[mode]=float(np.mean(vals))
    cpu[mode]={k:float(np.mean(v)) for k,v in processes.items()}

os.makedirs(args.output,exist_ok=True)
json.dump({"latency":latency,"cpu":cpu},open(os.path.join(args.output,"leveldb.json"),"w"),indent=2)
print(json.dumps({"latency":latency,"cpu":cpu},indent=2))
