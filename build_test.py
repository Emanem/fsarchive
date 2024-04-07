#!/usr/bin/python3
import subprocess
import os
import re
import shutil
import time


FSARCHIVE_BIN = "./fsarchive"
TEST_DATA_DIR = "./test_data"
TEST_DATA_TMPDIR = "./tmp_outdir"


def run_process(cmdline):
    out = subprocess.check_output(cmdline, shell=True, stderr=subprocess.STDOUT)
    return out.decode('utf-8')


def run_fsarchive(opt):
    # always sleep 1 second otherwise archive creation may not work
    time.sleep(1)
    run_process(f"{FSARCHIVE_BIN} {opt}")
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
    rv_files = {}
    rv_dirs = {}
    # rhs -> lhs
    for f, md5sum in lhs['files'].items():
        if f not in rhs['files']:
            rv_files[f] = 'm-rhs'
        if md5sum != rhs['files'][f]:
            rv_files[f] = 'd-rhs'
    # lhs -> rhs
    for f, md5sum in rhs['files'].items():
        if f not in lhs['files']:
            rv_files[f] = 'm-lhs'
        if md5sum != lhs['files'][f]:
            rv_files[f] = 'd-lhs'
    # dirs
    for d in lhs['dirs'].keys():
        if d not in rhs['dirs']:
            rv_dirs[d] = 'm-rhs'
    for d in rhs['dirs'].keys():
        if d not in lhs['dirs']:
            rv_dirs[d] = 'm-lhs'
    return rv_files, rv_dirs


def assert_same_filedata(lhs, rhs):
    rvf, rvd = compare_filedata(lhs, rhs)
    assert len(rvf) == 0, "Difference in files"
    assert len(rvd) == 0, "Diffetence in directories"


def test_cleanup(arc):
    for a in arc:
        os.remove(a)
    shutil.rmtree(TEST_DATA_TMPDIR)
    shutil.rmtree(TEST_DATA_DIR)
    run_process(f"git checkout HEAD {TEST_DATA_DIR}")


def run_test_base():
    arc = run_fsarchive(f"-a . {TEST_DATA_DIR}")
    assert len(arc) == 1, "We should have created one archive"
    # decompress in another subdirectory
    run_fsarchive(f"-d {TEST_DATA_TMPDIR} -r {arc[-1]}")
    # get all the input files
    in_files = get_filedata(TEST_DATA_DIR)
    # get the test output
    out_files = get_filedata(TEST_DATA_DIR, TEST_DATA_TMPDIR)
    # compare the two
    assert_same_filedata(in_files, out_files)
    test_cleanup(arc)


def run_test_add():
    arc = run_fsarchive(f"-a . {TEST_DATA_DIR}")
    assert len(arc) == 1, "We should have created one archive"
    # add one more file
    run_process(f"cat /dev/random | head -c 16 > {TEST_DATA_DIR}/newfile.bin")
    # run another archive, this time expecting to have 2 archives with delta
    arc = run_fsarchive(f"-a . {TEST_DATA_DIR}")
    assert len(arc) == 2, "We should have created two archives"
    # decompress in another subdirectory
    run_fsarchive(f"-d {TEST_DATA_TMPDIR} -r {arc[-1]}")
    # get all the input files
    in_files = get_filedata(TEST_DATA_DIR)
    # get the test output
    out_files = get_filedata(TEST_DATA_DIR, TEST_DATA_TMPDIR)
    # compare the two
    assert_same_filedata(in_files, out_files)
    test_cleanup(arc)


def run_test_rm():
    arc = run_fsarchive(f"-a . {TEST_DATA_DIR}")
    assert len(arc) == 1, "We should have created one archive"
    # remove one file
    os.remove(f"{TEST_DATA_DIR}/a.txt")
    # run another archive, this time expecting to have 2 archives with delta
    arc = run_fsarchive(f"-a . {TEST_DATA_DIR}")
    assert len(arc) == 2, "We should have created two archives"
    # decompress in another subdirectory
    run_fsarchive(f"-d {TEST_DATA_TMPDIR} -r {arc[-1]}")
    # get all the input files
    in_files = get_filedata(TEST_DATA_DIR)
    # get the test output
    out_files = get_filedata(TEST_DATA_DIR, TEST_DATA_TMPDIR)
    # compare the two
    assert_same_filedata(in_files, out_files)
    test_cleanup(arc)


def main():
    # first build
    run_process("make clean && make release -j $(nproc)")
    # run a first test without changing data
    run_test_base()
    # adding a file
    run_test_add()
    # removal of a file
    run_test_rm()


if __name__ == "__main__":
    main()
