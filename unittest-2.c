/*
 * file:        unittest-2.c
 * description: libcheck test skeleton, part 2
 */

#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 26

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>
#include <zlib.h>
#include <fuse.h>
#include <stdlib.h>
#include <errno.h>

#include "fs5600.h"

extern struct fuse_operations fs_ops;
extern void block_init(char *file);

/* mockup for fuse_get_context. you can change ctx.uid, ctx.gid in 
 * tests if you want to test setting UIDs in mknod/mkdir
 */
struct fuse_context ctx = { .uid = 500, .gid = 500};
struct fuse_context *fuse_get_context(void)
{
    return &ctx;
}


int readdir_filler(void *ptr, const char *name, const struct stat *stbuf, off_t off)
{
    struct DirEntry {
        const char *name;
        int seen;
    };
    struct DirEntry *dir_table = ptr;

    for (int i = 0; dir_table[i].name != NULL; i++) {
        if (strcmp(name, dir_table[i].name) == 0) {
            dir_table[i].seen = 1;
            return 0;
        }
    }
    return 0;
}

START_TEST(test_create_file)
{
    // simple create in root directory
    int rv = fs_ops.create("/testfile.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    
    struct stat st;
    rv = fs_ops.getattr("/testfile.txt", &st);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st.st_uid, 500);
    ck_assert_int_eq(st.st_gid, 500);
    ck_assert(S_ISREG(st.st_mode));
    ck_assert_int_eq(st.st_mode & 0777, 0666);
    ck_assert_int_eq(st.st_size, 0);

    struct {
        const char *name;
        int seen;
    } dir_table[] = {
        {"testfile.txt", 0},
        {NULL, 0}
    };
    
    rv = fs_ops.readdir("/", dir_table, readdir_filler, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table[0].seen, 1);

    // testing nonexistent directory create
    rv = fs_ops.create("/nonexistent/testfile.txt", 0100666, NULL);
    ck_assert_msg(rv == -ENOENT, "Create in nonexistent directory is failing");

    // create and recreate the file 
    rv = fs_ops.create("/testfile2.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/testfile2.txt", 0100666, NULL);
    ck_assert_msg(rv == -EEXIST, "Recreating the same file is failing");

    // create and recreate the file same name as dir
    rv = fs_ops.mkdir("/test", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/test", 0100666, NULL);
    ck_assert_msg(rv == -EEXIST, "Recreating the same file is failing");

    // create a file in a sub dir
    rv = fs_ops.mkdir("/test2", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/test2/testfile.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);

    struct stat st2;
    rv = fs_ops.getattr("/test2/testfile.txt", &st2);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st2.st_uid, 500);
    ck_assert_int_eq(st2.st_gid, 500);
    ck_assert(S_ISREG(st2.st_mode));
    ck_assert_int_eq(st2.st_mode & 0777, 0666);
    ck_assert_int_eq(st2.st_size, 0);

    // file as a directory in path
    rv = fs_ops.create("/test3", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/test3/test4.txt", 0100666, NULL);
    ck_assert_msg(rv == -ENOTDIR, "Accessing file as a directory.");


    // create a file with a long name and truncate it
    char long_filename[50] = "/";
    for (int i = 0; i < 30; i++) {
        strcat(long_filename, "a");
    }
    rv = fs_ops.create(long_filename, 0100666, NULL);
    if (rv == 0) {
        char truncated_name[MAX_NAME_LEN + 1] = "/"; // +1 for '/' and 26 characters + null terminator
        for (int i = 0; i < 26; i++) {
            truncated_name[i+1] = 'a';
        }
        truncated_name[27] = '\0'; 
        
        struct stat st_trunc;
        rv = fs_ops.getattr(truncated_name, &st_trunc);
        ck_assert_int_eq(rv, 0);
    } else {
        ck_assert_int_eq(rv, -EINVAL);
    }
}
END_TEST

START_TEST(test_mkdir_directory)
{
    // simple mkdir in root directory
    int rv = fs_ops.mkdir("/testdir", 0777);
    ck_assert_int_eq(rv, 0);
    
    struct stat st;
    rv = fs_ops.getattr("/testdir", &st);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st.st_uid, 500);
    ck_assert_int_eq(st.st_gid, 500);
    ck_assert(S_ISDIR(st.st_mode));
    ck_assert_int_eq(st.st_mode & 0777, 0777);

    struct {
        char *name;
        int seen;
    } dir_table[] = {
        {"testdir", 0},
        {NULL, 0}
    };

    rv = fs_ops.readdir("/", dir_table, readdir_filler, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table[0].seen, 1);


    //subdirectory test
    rv = fs_ops.mkdir("/parentdir", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/parentdir/subdir", 0777);
    ck_assert_int_eq(rv, 0);
    
    struct stat st2;
    rv = fs_ops.getattr("/parentdir/subdir", &st2);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st2.st_uid, 500);
    ck_assert_int_eq(st2.st_gid, 500);
    ck_assert(S_ISDIR(st2.st_mode));
    ck_assert_int_eq(st2.st_mode & 0777, 0777);
    
    struct {
        char *name;
        int seen;
    } dir_table2[] = {
        {"subdir", 0},
        {NULL, 0}
    };
    
    rv = fs_ops.readdir("/parentdir", dir_table2, readdir_filler, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table2[0].seen, 1);

    // creating a directory with the same name as a file
    rv = fs_ops.create("/testfile", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/testfile", 0777);
    ck_assert_msg(rv == -EEXIST, "Fail: Creating a directory with the same name as a");
    
    // creating a directory with the same name as an existing directory
    rv = fs_ops.mkdir("/testdir", 0777);
    ck_assert_msg(rv == -EEXIST, "Fail: Creating a directory with the same name as an existing directory");

    // creating a directory in a nonexistent path
    rv = fs_ops.mkdir("/nonexistent/testdir", 0777);
    ck_assert_msg(rv == -ENOENT, "Fail: Creating a directory in a nonexistent path");

    // creating a directory with a file in the path
    rv = fs_ops.create("/testfile4", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/testfile4/subdir", 0777);
    ck_assert_msg(rv == -ENOTDIR, "Fail: Creating a directory with a file in the path");


    // creating a directory with a long name
    char long_dirname[50] = "/";
    for (int i = 0; i < 30; i++) {
        strcat(long_dirname, "a");
    }
    rv = fs_ops.mkdir(long_dirname, 0777);
    if (rv == 0) {
        char truncated_name[MAX_NAME_LEN + 1] = "/"; // +1 for '/' and 26 characters + null terminator
        for (int i = 0; i < 26; i++) {
            truncated_name[i+1] = 'a';
        }
        truncated_name[27] = '\0'; 
        
        struct stat st_trunc;
        rv = fs_ops.getattr(truncated_name, &st_trunc);
        ck_assert_int_eq(rv, 0);
    } else {
        ck_assert_int_eq(rv, -EINVAL);
    }
}
END_TEST


START_TEST(test_unlink)
{
    // simple file unlink
    int rv = fs_ops.create("/unlinkfile.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    
    struct {
        char *name;
        int seen;
    } dir_table[] = {
        {"unlinkfile.txt", 0},
        {NULL, 0}
    };
    
    rv = fs_ops.readdir("/", dir_table, readdir_filler, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table[0].seen, 1);

    rv = fs_ops.unlink("/unlinkfile.txt");
    ck_assert_int_eq(rv, 0);
    
    struct {
        char *name;
        int seen;
    } dir_table2[] = {
        {"unlinkfile.txt", 0},
        {NULL, 0}
    };
    
    rv = fs_ops.readdir("/", dir_table2, readdir_filler, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table2[0].seen, 0);

    // unlinking a file that does not exist
    rv = fs_ops.unlink("/nonexistent.txt");
    ck_assert_msg(rv == -ENOENT, "Fail: Unlinking a file that does not exist");

    // unlinking a directory
    rv = fs_ops.mkdir("/unlinkdir", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.unlink("/unlinkdir");
    ck_assert_msg(rv == -EISDIR, "Fail: Unlinking a directory should return EISDIR");

    // unlinking a file in a nonexistent directory
    rv = fs_ops.unlink("/nonexistent/unlinkfile.txt");
    ck_assert_msg(rv == -ENOENT, "Fail: Unlinking a file in a nonexistent directory");
}
END_TEST

START_TEST(test_rmdir)
{
    // simple rmdir in root directory
    int rv = fs_ops.mkdir("/rmdirtest", 0777);
    ck_assert_int_eq(rv, 0);
    
    struct stat st;
    rv = fs_ops.getattr("/rmdirtest", &st);
    ck_assert_int_eq(rv, 0);
    ck_assert(S_ISDIR(st.st_mode));
    
    rv = fs_ops.rmdir("/rmdirtest");
    ck_assert_int_eq(rv, 0);
    
    rv = fs_ops.getattr("/rmdirtest", &st);
    ck_assert_msg(rv == -ENOENT, "Fail: Rmdir a directory that should not exist");

    // rmdir a directory that does not exist
    rv = fs_ops.rmdir("/nonexistentdir");
    ck_assert_msg(rv == -ENOENT, "Fail: Rmdir a directory that does not exist");

    // rmdir a file
    rv = fs_ops.create("/rmdirfile.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.rmdir("/rmdirfile.txt");
    ck_assert_msg(rv == -ENOTDIR, "Fail: Rmdir a file should return ENOTDIR");

    // rmdir a directory that is not empty
    rv = fs_ops.mkdir("/rmdirnotempty", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/rmdirnotempty/file.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.rmdir("/rmdirnotempty");
    ck_assert_msg(rv == -ENOTEMPTY, "Fail: Rmdir a directory that is not empty should return ENOTEMPTY");

    // rmdir a directory in nonexistent path
    rv = fs_ops.rmdir("/nonexistent/rmdirtest");
    ck_assert_msg(rv == -ENOENT, "Fail: Rmdir a directory in nonexistent path should return ENOENT");
}
END_TEST

/* note that your tests will call:
 *  fs_ops.getattr(path, struct stat *sb)
 *  fs_ops.readdir(path, NULL, filler_function, 0, NULL)
 *  fs_ops.read(path, buf, len, offset, NULL);
 *  fs_ops.statfs(path, struct statvfs *sv);
 */

extern struct fuse_operations fs_ops;
extern void block_init(char *file);

int main(int argc, char **argv)
{
    system("python gen-disk.py -q disk2.in test2.img");

    block_init("test2.img");
    fs_ops.init(NULL);
    
    Suite *s = suite_create("fs5600");
    TCase *tc = tcase_create("write_mostly");

    tcase_add_test(tc, test_create_file); 
    tcase_add_test(tc, test_mkdir_directory);
    tcase_add_test(tc, test_unlink);
    tcase_add_test(tc, test_rmdir);
    

    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    
    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    printf("%d tests failed\n", n_failed);
    
    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

