#!/usr/bin/env python3
"""
Fix integer overflow patterns in crate-reader.cc
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

        # Pattern 1: size_t var = size_t(n) * sizeof(Type);
        # Replace with SafeSizeForN<Type>(n, &var)
        m1 = re.match(r'^(\s*)size_t (\w+) = size_t\(n\) \* sizeof\(([\w:]+)\);', line)
        if m1:
            indent, varname, typ = m1.groups()
            result.append(f"{indent}size_t {varname};\n")
            result.append(f"{indent}if (!SafeSizeForN<{typ}>(n, &{varname})) {{\n")
            result.append(f"{indent}  PUSH_ERROR_AND_RETURN_TAG(kTag, \"Integer overflow: n * sizeof({typ})\");\n")
            result.append(f"{indent}}}\n")
            i += 1
            continue

        # Pattern 2: size_t var = sizeof(Type) * size_t(n);
        m2 = re.match(r'^(\s*)size_t (\w+) = sizeof\(([\w:]+)\) \* size_t\(n\);', line)
        if m2:
            indent, varname, typ = m2.groups()
            result.append(f"{indent}size_t {varname};\n")
            result.append(f"{indent}if (!SafeSizeForN<{typ}>(n, &{varname})) {{\n")
            result.append(f"{indent}  PUSH_ERROR_AND_RETURN_TAG(kTag, \"Integer overflow: sizeof({typ}) * n\");\n")
            result.append(f"{indent}}}\n")
            i += 1
            continue

        # Pattern 3: CHECK_MEMORY_USAGE(sizeof(Type) * n)
        m3 = re.match(r'^(\s*)CHECK_MEMORY_USAGE\(sizeof\(([\w:]+)\) \* (n)\);', line)
        if m3:
            indent, typ, nvar = m3.groups()
            varname = f"{typ.replace(':', '_')}_size"
            result.append(f"{indent}size_t {varname};\n")
            result.append(f"{indent}if (!SafeSizeForN<{typ}>({nvar}, &{varname})) {{\n")
            result.append(f"{indent}  PUSH_ERROR_AND_RETURN_TAG(kTag, \"Integer overflow in CHECK_MEMORY_USAGE\");\n")
            result.append(f"{indent}}}\n")
            result.append(f"{indent}CHECK_MEMORY_USAGE({varname});\n")
            i += 1
            continue

        # Pattern 4: _sr->read(sizeof(Type) * n, ...)
        # This is more complex - need to compute the size safely before the call
        # For now, skip this pattern and handle manually

        result.append(line)
        i += 1

    with open(filename, 'w') as f:
        f.writelines(result)

    print(f"Fixed overflow patterns in {filename}")

if __name__ == "__main__":
    fix_overflow_patterns(sys.argv[1])
