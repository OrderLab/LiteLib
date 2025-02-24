import argparse
import json

parser = argparse.ArgumentParser(description="Process JSON files.")

parser.add_argument("-f", "--filenames", nargs="+", help="The path to the JSON file(s)")

args = parser.parse_args()

cnt = len(args.filenames)
for filename in args.filenames:
    if not filename.endswith(".json"):
        raise argparse.ArgumentTypeError(
            f"Invalid file type: {filename}. Expected a '.json' file."
        )

for i in range(cnt):
    print(f"Processing {args.filenames[i]}")
    stat_file = args.filenames[i]
    with open(stat_file, "r") as f:
        stat = json.load(f)
    del stat["avg_lock_wait_time"]
    del stat["lock_wait_time"]
    del stat["server_lat_list"]
    del stat["agg_lat_list"]
    del stat["tries"]

    json.dump(stat, open(f"{stat_file[:-5]}.pruned.json", "w"))
