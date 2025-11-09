import os
import sys
import json
import glob

input_dir = "/mnt/n/data/tinyusdz/mcp/african_slate_quarry/usds/"
output_filepath = "asset-descriptions.json"

if len(sys.argv) > 1:
    input_dir = sys.argv[1]


files = glob.glob(os.path.join(input_dir, "*.usdz"))
files += glob.glob(os.path.join(input_dir, "*.usd"))
files += glob.glob(os.path.join(input_dir, "*.usdc"))
files += glob.glob(os.path.join(input_dir, "*.usda"))

js = {}

for f in files:
    in_json_file = os.path.splitext(f)[0] + ".json"
    print(in_json_file)

    basename = os.path.splitext(os.path.basename(f))[0]
    print(basename)

    j = json.loads(open(in_json_file).read())

    # optional meta
    in_metajson_file = os.path.splitext(f)[0] + "-meta.json"
    print(in_metajson_file)

    if os.path.exists(in_metajson_file):
        meta_j = json.loads(open(in_metajson_file).read())
        j.update(meta_j)

    js[basename] = j

out_j = json.dumps(js)

with open(output_filepath, 'w') as f:
    f.write(out_j)
