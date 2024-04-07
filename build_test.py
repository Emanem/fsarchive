#!/usr/bin/python3
import subprocess
import re


def run_process(cmdline):
    out = subprocess.check_output(cmdline, shell=True, stderr=subprocess.STDOUT)
    return out.decode('utf-8')


def main():
    # first build
    run_process("make clean && make release -j $(nproc)")


if __name__ == "__main__":
    main()
