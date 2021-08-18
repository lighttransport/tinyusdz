import glob
import subprocess

app = "./usda-parser"

failed = []
false_negatives = []

# success expected
for fname in glob.glob("tests/*.usda"):
    cmd = [app, fname]

    ret = subprocess.call(cmd)
    if ret != 0:
        failed.append(fname)

# failure expected
for fname in glob.glob("tests/fail-case/*.usda"):
    cmd = [app, fname]

    ret = subprocess.call(cmd)
    if ret == 0:
        false_negatives.append(fname)
     
print("=================================")

if len(failed) > 0:
    for fname in failed:
        # failed
        print("parse failed for : ", fname)

if len(false_negatives) > 0:
    for fname in false_negatives:
        print("parse should fail but reported success : ", fname)
