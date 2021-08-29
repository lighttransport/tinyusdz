#!/usr/bin/env python

from itertools import groupby
import argparse


def _print_macro_line(f, s, terminal=False, cols=90):
    if len(s) >= cols - 2:
        raise RuntimeError("Too long line!")
    if terminal:
        f.write(s + "\n")
        return

    f.write(s + " " * (cols - 2 - len(s)) + "\\\n")


def _print_comma_list(f, args, indent, grouping_factor=10):
    args = list(args)
    args_comma = [x + "," for x in args[:-1]] + args[-1:]
    for _, line in groupby(
            enumerate(args_comma), lambda x: x[0] / grouping_factor):
        _print_macro_line(f, " " * indent + " ".join(x[1] for x in line))


def _print_macro_func(f, func_name, args):

    _print_macro_line(f, "#define " + func_name + "(")
    _print_comma_list(f, args, 4)
    _print_macro_line(f, "  )")


def _print_arg_counter(f, max_enums):
    _print_macro_func(f, "WISE_ENUM_IMPL_ARG_N",
                      ["_{}".format(i)
                       for i in range(1, max_enums + 1)] + ["N", "..."])
    _print_macro_line(f, "  N")
    f.write('\n')

    _print_macro_line(f, "#define WISE_ENUM_IMPL_RSEQ_N()")
    _print_comma_list(f,
                      reversed(["{}".format(i)
                                for i in range(max_enums + 1)]), 2)
    f.write('\n')


def _print_loop(f, max_enums):
    prefix = "WISE_ENUM_IMPL_LOOP_"
    _print_macro_line(
        f, "#define {}1(M, C, D, x) M(C, x)".format(prefix), terminal=True)
    f.write('\n')

    for i in range(2, max_enums + 1):
        _print_macro_line(f,
                          "#define {}{}(M, C, D, x, ...) M(C, x) D()".format(
                              prefix, i))
        _print_macro_line(
            f,
            "  WISE_ENUM_IMPL_EXPAND({}{}(M, C, D, __VA_ARGS__))".format(prefix, i - 1),
            terminal=True)
        f.write('\n')


def main(num, filename):

    with open(filename, 'w') as f:

        _print_arg_counter(f, num)
        _print_loop(f, num)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description=
        "Generate file needed for smart enums, up to some maximum number of enumerations"
    )
    parser.add_argument('num', type=int)
    parser.add_argument('output_filename')
    args = parser.parse_args()
    main(args.num, args.output_filename)
