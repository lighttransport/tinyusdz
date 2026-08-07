import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

def run(config):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    cmd = config["app"]

    if not os.path.isfile(cmd) and shutil.which(cmd) is None:
        raise FileNotFoundError(f"tusdcat executable not found: {cmd}")

    failure_cases = []
    success_cases = []

    glob_pattern = ["**/*.usd", "**/*.usda", "**/*.usdc", "**/*.usdz"]

    print("Find USD files under: ", config["path"])
    fs = []
    for pat in glob_pattern:
        fs.extend(glob.glob(os.path.join(config["path"], pat), recursive=True))

    for f in sorted(fs):
        print(f)
        try:
            ret = subprocess.run([cmd, "-l", f], timeout=config["timeout"])
        except subprocess.TimeoutExpired:
            print(f"Timed out after {config['timeout']} seconds")
            failure_cases.append(f)
            print("timeout")
            continue

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

    parser = argparse.ArgumentParser(description='USD parse tester.')
    parser.add_argument('usd_path', type=str, nargs='?', default="../tests/usda",
                        help='Path to USD source tree')
    parser.add_argument('--app', default=os.environ.get('TUSDCAT_PATH',
                        os.path.join(os.path.dirname(__file__), '..', '..',
                                     'build', 'tusdcat')),
                        help='tusdcat executable (default: TUSDCAT_PATH or build/tusdcat)')
    parser.add_argument('--timeout', type=int, default=180,
                        help='Per-file timeout in seconds (default: 180)')
    parser.add_argument('--output', help='Write a JSON summary to this path')

    args = parser.parse_args()

    conf = {"path": args.usd_path, "app": args.app,
            "timeout": args.timeout, "output": args.output}

    return run(conf)

if __name__ == '__main__':
    sys.exit(main())
