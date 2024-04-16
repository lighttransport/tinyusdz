import argparse
import os
import subprocess
import glob

def case_insensitive(ext):
    return '*.' + ''.join('[%s%s]' % (e.lower(), e.upper()) for e in ext)

def run(config):
    cmd = "./tydra_to_renderscene"

    failure_cases = []
    success_cases = []

    glob_pattern = ["**/" + case_insensitive("usd"), "**/" + case_insensitive("usda"), "**/" + case_insensitive("usdc"), "**/" + case_insensitive("usdz")]

    print("Find USD files under: ", config["path"])
    fs = []
    for pat in glob_pattern:
        fs.extend(glob.glob(os.path.join(config["path"], pat), recursive=True))

    for f in fs:
        print(f)
        ret = subprocess.run([cmd, f])
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
    parser = argparse.ArgumentParser(description='tydra_to_renderscene tester.')
    parser.add_argument('usd_path', type=str, nargs='?', default="../models/",
                    help='Path to USD source tree')

    args = parser.parse_args()

    conf["path"] = args.usd_path

    run(conf)

if __name__ == '__main__':
    main()
