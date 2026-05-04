#!/usr/bin/env python3
"""
Fix integer overflow patterns in tydra/render-data-mesh.cc
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

        # Pattern 1: dst.resize(num_vertices * stride_bytes);
        # or similar: dst.resize(X * Y);
        m1 = re.match(r'^(\s*)([a-zA-Z_>]+)\.resize\(([^)]+)\s*\*\s*([^)]+)\);', line)
        if m1:
            indent, var, left, right = m1.groups()
            result.append(f"{indent}size_t {var}_size;\n")
            result.append(f"{indent}if (!safe::mul({left}, {right}, &{var}_size)) {{\n")
            result.append(f"{indent}  return nonstd::make_unexpected(\"Integer overflow: {left} * {right}\");\n")
            result.append(f"{indent}}}\n")
            result.append(f"{indent}{var}.resize({var}_size);\n")
            i += 1
            continue

        # Pattern 2: attr.data.resize(numVerts * sizeof(value::float3));
        m2 = re.match(r'^(\s*)([a-zA-Z_]+)\.data\.resize\(([^)]+)\s*\*\s*sizeof\(([^)]+)\)\);', line)
        if m2:
            indent, var, left, typ = m2.groups()
            result.append(f"{indent}size_t {var}_size;\n")
            result.append(f"{indent}if (!safe::n_to_size<{typ}>({left}, &{var}_size)) {{\n")
            result.append(f"{indent}  return nonstd::make_unexpected(\"Integer overflow: {left} * sizeof({typ})\");\n")
            result.append(f"{indent}}}\n")
            result.append(f"{indent}{var}.data.resize({var}_size);\n")
            i += 1
            continue

        result.append(line)
        i += 1

    with open(filename, 'w') as f:
        f.writelines(result)

    print(f"Fixed overflow patterns in {filename}")

if __name__ == "__main__":
    fix_overflow_patterns(sys.argv[1])
