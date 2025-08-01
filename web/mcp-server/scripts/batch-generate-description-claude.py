import subprocess
import glob
import os
import sys
from pathlib import Path
import json

claude_cmd = "claude"

usdz_path = "/mnt/n/data/tinyusdz/mcp/african_slate_quarry/usds"
screenshot_path = "/mnt/n/data/tinyusdz/mcp/african_slate_quarry/screenshots"

if len(sys.argv) > 1:
    usdz_path = sys.argv[1]

if len(sys.argv) > 2:
    screenshot_path = sys.argv[2]

prompt_template = "Generate a description from the image. Focus its shape and appearance(PBR material parameter), ignore background and environment: \n\n" 

ps = []

for item in glob.glob(os.path.join(usdz_path, "*.usdz")):
    usdz_file = Path(item)
    screenshot_file = screenshot_path / Path(usdz_file.stem + ".png")

    if not screenshot_file.exists():
        print(f"Screenshot file {screenshot_file} does not exist.")
        continue

    prompt = prompt_template + f"Image: {screenshot_file}\n"
    print("Adding ", usdz_file)

    ps.append((usdz_file, prompt))

print('Total files:', len(ps))

count = 0
for p in ps:
  print(f"Processing {count + 1}/{len(ps)}: {p[0]}")
  cmd = [claude_cmd, "-p", p[1], "--add-dir", screenshot_path, "--output-format", "json"]

  result = subprocess.run(cmd, capture_output=True, text=True)
  if result.returncode != 0:
      print(f"Error processing {p[0]}: {result.stderr}")
      continue
  print(result.stdout)

  j = json.loads(result.stdout)
  description = j.get("result", "")

  outj = {}

  # use basename for usd and screenshot
  basename = os.path.basename(p[0])
  outj['usd_filename'] = str(basename)
  outj['screenshot_filename'] = str(os.path.splitext(basename)[0] + ".png")
  outj['description'] = description

  content = json.dumps(outj, ensure_ascii=True)
  output_file = p[0].with_suffix('.json')
  with open(output_file, 'w') as f:
      f.write(content)  


  count += 1
