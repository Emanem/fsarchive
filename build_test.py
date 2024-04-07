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
            continue
        if md5sum != rhs['files'][f]:
            rv_files[f] = 'd-rhs'
    # lhs -> rhs
    for f, md5sum in rhs['files'].items():
        if f not in lhs['files']:
            rv_files[f] = 'm-lhs'
            continue
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


def test_cleanup(msg = ""):
    files = [f for f in os.listdir('.') if os.path.isfile(os.path.join('.', f))]
    files = [f for f in files if re.match(r'^fsarc_.*\.zip$', f)]
    for f in files:
        os.remove(f)
    shutil.rmtree(TEST_DATA_TMPDIR, ignore_errors=True)
    shutil.rmtree(TEST_DATA_DIR, ignore_errors=True)
    run_process(f"git checkout HEAD {TEST_DATA_DIR}")
    if len(msg) > 0:
        print(msg)


def run_test_base():
    test_cleanup("run_test_base")
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


def run_test_add():
    test_cleanup("run_test_add")
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


def run_test_rm():
    test_cleanup("run_test_rm")
    arc = run_fsarchive(f"-a . {TEST_DATA_DIR}")
    assert len(arc) == 1, "We should have created one archive"
    # remove one file
    file_to_rm = f"{TEST_DATA_DIR}/a.txt"
    os.remove(file_to_rm)
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
    # now remove the out test and restore the previous archive
    shutil.rmtree(TEST_DATA_TMPDIR)
    # decompress in another subdirectory
    run_fsarchive(f"-d {TEST_DATA_TMPDIR} -r {arc[-2]}")
    out_files = get_filedata(TEST_DATA_DIR, TEST_DATA_TMPDIR)
    rvf, rvd = compare_filedata(in_files, out_files)
    assert rvf[file_to_rm] == 'm-lhs', "Previous archive doesn't contain all original files"


def run_test_exclude0():
    test_cleanup("run_test_exclude0")
    arc = run_fsarchive(f"-a . -x \"./?/a.txt\" {TEST_DATA_DIR}")
    assert len(arc) == 1, "We should have created one archive"
    # decompress in another subdirectory
    run_fsarchive(f"-d {TEST_DATA_TMPDIR} -r {arc[-1]}")
    # get all the input files
    in_files = get_filedata(TEST_DATA_DIR)
    # get the test output
    out_files = get_filedata(TEST_DATA_DIR, TEST_DATA_TMPDIR)
    # compare the two
    rvf, rvd = compare_filedata(in_files, out_files)
    assert rvf[f"./test_data/a.txt"] == 'm-rhs', "File is supposed to be excluded"


def run_test_exclude1():
    test_cleanup("run_test_exclude1")
    arc = run_fsarchive(f"-a . -x \"*something.txt\" {TEST_DATA_DIR}")
    assert len(arc) == 1, "We should have created one archive"
    # decompress in another subdirectory
    run_fsarchive(f"-d {TEST_DATA_TMPDIR} -r {arc[-1]}")
    # get all the input files
    in_files = get_filedata(TEST_DATA_DIR)
    # get the test output
    out_files = get_filedata(TEST_DATA_DIR, TEST_DATA_TMPDIR)
    # compare the two
    rvf, rvd = compare_filedata(in_files, out_files)
    assert rvf['./test_data/abc/something.txt'] == 'm-rhs', "File is supposed to be excluded"
    assert rvf['./test_data/abc/def/something.txt'] == 'm-rhs', "File is supposed to be excluded"


def main():
    # first build
    run_process("make clean && make release -j $(nproc)")
    # run a first test without changing data
    run_test_base()
    # adding a file
    run_test_add()
    # removal of a file
    run_test_rm()
    # file exclusion
    run_test_exclude0()
    run_test_exclude1()
    # file test cleanup
    test_cleanup()


if __name__ == "__main__":
    main()
