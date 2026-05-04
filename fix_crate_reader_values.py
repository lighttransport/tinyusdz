#!/usr/bin/env python3
"""
Fix integer overflow patterns in crate-reader-values.cc
"""
import re
import sys

def fix_overflow_patterns(filename):
    with open(filename, 'r') as f:
        lines = f.readlines()

    result = []
    i = 0
    while i < len(lines):
        line = lines[i]

        # Pattern: CHECK_MEMORY_USAGE(n * sizeof(Type));
        m = re.match(r'^(\s*)CHECK_MEMORY_USAGE\(n \* sizeof\(([\w:]+)\)\);', line)
        if m:
            indent, typ = m.groups()
            varname = typ.replace('::', '_') + '_size'
            result.append(f"{indent}size_t {varname};\n")
            result.append(f"{indent}if (!safe::n_to_size<{typ}>(n, &{varname})) {{\n")
            result.append(f"{indent}  PUSH_ERROR_AND_RETURN_TAG(kTag, \"Integer overflow: n * sizeof({typ})\");\n")
            result.append(f"{indent}}}\n")
            result.append(f"{indent}CHECK_MEMORY_USAGE({varname});\n")
            i += 1
            continue

        result.append(line)
        i += 1

    with open(filename, 'w') as f:
        f.writelines(result)

    print(f"Fixed overflow patterns in {filename}")

if __name__ == "__main__":
    fix_overflow_patterns(sys.argv[1])
