#!/usr/bin/python

import os
import subprocess
import sys

def main():
    cc = os.environ["REALCC"]

    if '-c' not in sys.argv or "conftest.c" in sys.argv:
        os.execv(cc, [cc] + sys.argv[1:])
        # not reached
        raise Exception()

    args = sys.argv[1:]

    emit_args = list(args)
    emit_args.append("-S")
    emit_args.append("-emit-llvm")

    for i in xrange(len(emit_args) - 1):
        if emit_args[i] != '-o':
            continue

        normal_output = emit_args[i + 1]
        ll_output = "../cpython_ll/" + normal_output + ".ll"
        emit_args[i + 1] = ll_output
        break
    else:
        raise Exception("couldn't determine output file")

    if not os.path.exists(os.path.dirname(ll_output)):
        os.makedirs(os.path.dirname(ll_output))

    subprocess.check_call([cc] + emit_args)
    compile_args = ["-O3"]
    if "-fPIC" in args:
        compile_args.append("-fPIC")
    os.execv(cc, [cc, ll_output, "-c", "-o", normal_output] + compile_args)

    # os.execv(cc, [cc] + args)

if __name__ == "__main__":
    main()
