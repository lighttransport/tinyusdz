import argparse
import os
import subprocess
import glob

def run(config):
    cmd = "./usda_parser"

    failure_cases = []
    success_cases = []

    print("Find USDA files under: ", config["path"])
    fs = glob.glob(os.path.join(config["path"], "*.usda"))
    for f in fs:
        print(f)
        ret = subprocess.run([cmd, f])
        if ret.returncode != 0:
            if f.startswith('fail-case'):
                # fail expected
                success_cases.append(f)
            else:
                failure_cases.append(f)
        else:
            if f.startswith('fail-case'):
                failure_cases.append(f)
            else:
                success_cases.append(f)

        print(ret.returncode)

    print("Failure caess: =====================")
    for f in failure_cases:
        print(f)

def main():

    # Assume script is run from <tinyusdz>/build, e.g.:
    #
    # python ../tests/parse_ascii/runner.py

    conf = {}
    parser = argparse.ArgumentParser(description='USDA parser tester.')
    parser.add_argument('usda_path', type=str, nargs='?', default="../tests/usda",
                    help='Path to USDA source tree')

    args = parser.parse_args()

    conf["path"] = args.usda_path

    run(conf)

if __name__ == '__main__':
    main()
