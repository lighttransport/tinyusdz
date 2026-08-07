import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

def case_insensitive(ext):
    return '*.' + ''.join('[%s%s]' % (e.lower(), e.upper()) for e in ext)

def run(config):
    cmd = config["app"]
    if not os.path.isfile(cmd) and shutil.which(cmd) is None:
        raise FileNotFoundError(f"tydra_to_renderscene executable not found: {cmd}")

    failure_cases = []
    success_cases = []

    glob_pattern = ["**/" + case_insensitive("usd"), "**/" + case_insensitive("usda"), "**/" + case_insensitive("usdc"), "**/" + case_insensitive("usdz")]

    print("Find USD files under: ", config["path"])
    fs = []
    for pat in glob_pattern:
        fs.extend(glob.glob(os.path.join(config["path"], pat), recursive=True))

    for f in fs:
        print(f)
        ret = subprocess.run([cmd, f], timeout=config["timeout"])
        if ret.returncode != 0:
            failure_cases.append(f)
        else:
            success_cases.append(f)

        print(ret.returncode)

    print("Success cases: =====================")
    for f in success_cases:
        print(f, "OK")

    print("Failure cases: =====================")
    for f in failure_cases:
        print(f, "Failed")

    result = {"path": os.path.abspath(config["path"]), "app": cmd,
              "passed": len(success_cases), "failed": len(failure_cases),
              "failures": failure_cases}
    if config.get("output"):
        with open(config["output"], "w", encoding="utf-8") as out:
            json.dump(result, out, indent=2)
            out.write("\n")
    return 1 if failure_cases else 0

def main():

    parser = argparse.ArgumentParser(description='tydra_to_renderscene tester.')
    parser.add_argument('usd_path', type=str, nargs='?', default="../models/",
                        help='Path to USD source tree')
    parser.add_argument('--app', default=os.environ.get('TUSDRENDER_APP',
                        'tydra_to_renderscene'),
                        help='tydra_to_renderscene executable')
    parser.add_argument('--timeout', type=int, default=180,
                        help='Per-file timeout in seconds (default: 180)')
    parser.add_argument('--output', help='Write a JSON summary to this path')

    args = parser.parse_args()

    conf = {"path": args.usd_path, "app": args.app,
            "timeout": args.timeout, "output": args.output}

    return run(conf)

if __name__ == '__main__':
    sys.exit(main())
