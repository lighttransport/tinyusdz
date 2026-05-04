#!/usr/bin/env python3
"""
Fix integer overflow patterns in tydra/render-data-mesh.cc
"""
import re
import sys

def fix_overflow_patterns(filename):
    with open(filename, 'r') as f:
        content = f.read()

    # Pattern 1: dst->data.resize(vdst.size() * src.format_size());
    # Replace with safe multiplication
    pattern1 = r'((\s*))(\w+)->data\.resize\((\w+)\.size\(\) \* (\w+)\.format_size\(\)\);'
    def replace1(m):
        indent, var, left, right = m.group(1), m.group(3), m.group(4), m.group(5)
        return f"""{indent}size_t {var}_size;
{indent}if (!safe::mul({left}.size(), {right}.format_size(), &{var}_size)) {{
{indent}  return nonstd::make_unexpected("Integer overflow: {left}.size() * {right}.format_size()");
{indent}}}
{indent}{var}->data.resize({var}_size);"""
    content = re.sub(pattern1, replace1, content)

    with open(filename, 'w') as f:
        f.write(content)

    print(f"Fixed overflow patterns in {filename}")

if __name__ == "__main__":
    fix_overflow_patterns(sys.argv[1])
