import argparse
import os
import subprocess
import glob
import sys

def run(config):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    cmd = os.path.join(repo_root, "build_release", "tusdcat")

    if not os.path.isfile(cmd):
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

def main():

    # Assume script is run from <tinyusdz>/build, e.g.:
    #
    # python ../tests/parse_usd/runner.py

    conf = {}
    parser = argparse.ArgumentParser(description='USD parse tester.')
    parser.add_argument('usd_path', type=str, nargs='?', default="../tests/usda",
                    help='Path to USD source tree')
    parser.add_argument('--timeout', type=int, default=180,
                    help='Per-file timeout in seconds (default: 180)')

    args = parser.parse_args()

    conf["path"] = args.usd_path
    conf["timeout"] = args.timeout

    run(conf)

if __name__ == '__main__':
    sys.exit(main())
