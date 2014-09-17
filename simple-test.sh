#!/usr/bin/env bash

#
# Do some simple operations with simplefs
#
# - create fs
# - create files in it
# - create dirs in it
# - read files in it
# - write files in it
#
# TODO: add support of perms into simplefs,
# to avoid calling this script from root.
#

set -e

echo 1 >| /sys/module/jbd2/parameters/jbd2_debug

root_pwd="$PWD"
test_dir="test-dir-$RANDOM"
test_mount_point="test-mount-point-$RANDOM"
test_journal_dev=""

function create_journal()
{
    dd bs=1M count=10 if=/dev/zero of="$1"
    mke2fs -b 4096 -O journal_dev "$1"
}
function create_test_image()
{
    dd bs=4096 count=100 if=/dev/zero of="$1"
    ./mkfs-simplefs "$1"
}
function mount_fs_image()
{
    insmod simplefs.ko
    test_journal_dev=$(losetup -f --show "$1")
    mount -o loop,owner,group,users,journal_path="$test_journal_dev" -t simplefs "$2" "$3"
    dmesg | tail -n20
}
function unmount_fs()
{
    umount "$1"
    losetup -d $test_journal_dev
    rmmod simplefs.ko
    dmesg | tail -n20
}
function do_some_operations()
{
    cd "$1"

    ls
    cat vanakkam

    cp vanakkam hello
    cat hello

    echo "Hello World" > hello
    cat hello

    mkdir dir1 && cd dir1

    cp ../hello .
    cat hello

    echo "First level directory" > hello
    cat hello

    mkdir dir2 && cd dir2

    touch hello
    cat hello

    echo "Second level directory" > hello
    cat hello

    cp hello hello_smaller
    echo "directory" > hello_smaller
    cat hello_smaller
}
function do_read_operations()
{
    cd "$1"
    ls -lR

    cat vanakkam
    cat hello

    cat hello

    cd dir1
    cat hello

    cd dir2
    cat hello
    cat hello_smaller
}
function cleanup()
{
    cd "$root_pwd"
    [ -d "$test_mount_point" ] && umount -t simplefs "$test_mount_point"
    lsmod | grep -q simplefs && rmmod "$root_pwd/simplefs.ko"

    # TODO: prompt deletion
    rm -fR "$test_dir" "$test_mount_point"
}


# Trace all commands
set -x

make

cleanup
trap cleanup SIGINT EXIT
mkdir "$test_dir" "$test_mount_point"
create_test_image "$test_dir/image"
create_journal "$test_dir/journal"

# 1
mount_fs_image "$test_dir/journal" "$test_dir/image" "$test_mount_point"
do_some_operations "$test_mount_point"
cd "$root_pwd"
unmount_fs "$test_mount_point"

# 2
mount_fs_image "$test_dir/journal" "$test_dir/image" "$test_mount_point"
do_read_operations "$test_mount_point"
cd "$root_pwd"
unmount_fs "$test_mount_point"

dmesg | tail -n40

cleanup
