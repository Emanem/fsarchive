#!/usr/bin/python3
import subprocess
import os
import re
import shutil


def run_process(cmdline):
    out = subprocess.check_output(cmdline, shell=True, stderr=subprocess.STDOUT)
    return out.decode('utf-8')


def run_fsarchive(opt):
    run_process(f"./fsarchive {opt}")
    # get latest zip in the directory
    files = [f for f in os.listdir('.') if os.path.isfile(os.path.join('.', f))]
    return sorted([f for f in files if re.match(r'^fsarc_.*\.zip$', f)])


def get_filedata(path, base_path = '.'):
    cwd = os.getcwd()
    os.chdir(base_path)
    x = run_process(f"find {path} -exec md5sum {{}} \;")
    rv = {'files': {}, 'dirs': {}}
    for l in x.split('\n'):
        m = re.match('^([0-9a-f]+)\s+(.*)$', l)
        if m:
            rv['files'][m.group(2)] = m.group(1)
            continue
        m = re.match('^md5sum:\s+(.*):\s+Is a directory$', l)
        if m:
            rv['dirs'][m.group(1)] = None
    os.chdir(cwd)
    return rv


def compare_filedata(lhs, rhs):
    if sorted(lhs['files'].keys()) != sorted(rhs['files'].keys()):
        return False, "Different files"
    if sorted(lhs['dirs'].keys()) != sorted(rhs['dirs'].keys()):
        return False, "Different directories"
    for f, md5sum in lhs['files'].items():
        if md5sum != rhs['files'][f]:
            return False, f"Different file content '{f}'"
    return True, ""


def run_test_base():
    arc = run_fsarchive("-a . ./test_data")
    assert len(arc) == 1, "We should have created at one archive"
    # decompress in another subdirectory
    run_fsarchive(f"-d ./tmp_outdir -r {arc[-1]}")
    # get all the input files
    in_files = get_filedata("./test_data")
    # get the test output
    out_files = get_filedata("./test_data", "./tmp_outdir")
    # compare the two
    rv, msg = compare_filedata(in_files, out_files)
    assert rv, f"Expecting to have same files, instead: {msg}"
    os.remove(arc[-1])
    shutil.rmtree("./tmp_outdir")


def main():
    # first build
    run_process("make clean && make release -j $(nproc)")
    # run a first test without changing data
    run_test_base()


if __name__ == "__main__":
    main()
