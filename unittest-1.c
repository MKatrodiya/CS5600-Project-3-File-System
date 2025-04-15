/*
 * file:        testing.c
 * description: libcheck test skeleton for file system project
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

START_TEST(test_getattr_all)
{
    struct {
        const char *path;
        int uid, gid;
        mode_t mode;
        off_t size;
        time_t ctime, mtime;
        int expect_success;
        int expected_errno;
    } cases[] = {
        // Valid entries
        // path uid gid mode size ctime mtime expect_success expected_errno
        {"/", 0, 0, 040777, 4096, 1565283152, 1565283167, 1, 0},
        {"/file.1k", 500, 500, 0100666, 1000, 1565283152, 1565283152, 1, 0},
        {"/file.10", 500, 500, 0100666, 10, 1565283152, 1565283167, 1, 0},
        {"/dir-with-long-name", 0, 0, 040777, 4096, 1565283152, 1565283167, 1, 0},
        {"/dir-with-long-name/file.12k+", 0, 500, 0100666, 12289, 1565283152, 1565283167, 1, 0},
        {"/dir2", 500, 500, 040777, 8192, 1565283152, 1565283167, 1, 0},
        {"/dir2/twenty-seven-byte-file-name", 500, 500, 0100666, 1000, 1565283152, 1565283167, 1, 0},
        {"/dir2/file.4k+", 500, 500, 0100777, 4098, 1565283152, 1565283167, 1, 0},
        {"/dir3", 0, 500, 040777, 4096, 1565283152, 1565283167, 1, 0},
        {"/dir3/subdir", 0, 500, 040777, 4096, 1565283152, 1565283167, 1, 0},
        {"/dir3/subdir/file.4k-", 500, 500, 0100666, 4095, 1565283152, 1565283167, 1, 0},
        {"/dir3/subdir/file.8k-", 500, 500, 0100666, 8190, 1565283152, 1565283167, 1, 0},
        {"/dir3/subdir/file.12k", 500, 500, 0100666, 12288, 1565283152, 1565283167, 1, 0},
        {"/dir3/file.12k-", 0, 500, 0100777, 12287, 1565283152, 1565283167, 1, 0},
        {"/file.8k+", 500, 500, 0100666, 8195, 1565283152, 1565283167, 1, 0},
        // Deep path (relative traversal)
        {"/dir3/subdir/../.././file.1k", 500, 500, 0100666, 1000, 1565283152, 1565283152, 1, 0},
        // Error cases
        {"/not-a-file", 0, 0, 0, 0, 0, 0, 0, -ENOENT},
        {"/file.1k/file.0", 0, 0, 0, 0, 0, 0, 0, -ENOTDIR},
        {"/not-a-dir/file.0", 0, 0, 0, 0, 0, 0, 0, -ENOENT},
        {"/dir2/not-a-file", 0, 0, 0, 0, 0, 0, 0, -ENOENT}
    };

    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        struct stat st;
        int rv = fs_ops.getattr(cases[i].path, &st);
        if (cases[i].expect_success) {
            ck_assert_msg(rv == 0, "getattr failed for path %s", cases[i].path);
            ck_assert_int_eq(st.st_uid, cases[i].uid);
            ck_assert_int_eq(st.st_gid, cases[i].gid);
            ck_assert_int_eq(st.st_mode & 0777, cases[i].mode & 0777);
            ck_assert(S_ISDIR(cases[i].mode) ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
            ck_assert_int_eq(st.st_size, cases[i].size);
            ck_assert_int_eq(st.st_ctime, cases[i].ctime);
            ck_assert_int_eq(st.st_mtime, cases[i].mtime);
        } else {
            ck_assert_msg(rv == cases[i].expected_errno, "Expected error for path %s", cases[i].path);
        }
    }
}
END_TEST

struct {
    const char *dir;
    const char *entries[10];
} dir_contents[] = {
    {"/", {"dir2", "dir3", "dir-with-long-name", "file.10", "file.1k", "file.8k+", NULL}},
    {"/dir2", {"twenty-seven-byte-file-name", "file.4k+", NULL}},
    {"/dir3", {"subdir", "file.12k-", NULL}},
    {"/dir3/subdir", {"file.4k-", "file.8k-", "file.12k", NULL}},
    {"/dir-with-long-name", {"file.12k+", NULL}},
    {NULL, {NULL}}
};

struct {
    const char *name;
    int seen;
} entry_table[20]; // max 20 entries per test

int readdir_filler_check(void *ptr, const char *name, const struct stat *stbuf, off_t off)
{
    for (int i = 0; entry_table[i].name != NULL; i++) {
        if (strcmp(name, entry_table[i].name) == 0) {
            entry_table[i].seen = 1;
            return 0;
        }
    }
    return 0;
}

START_TEST(test_readdir_all_dirs)
{
    for (int d = 0; dir_contents[d].dir != NULL; d++) {
        // fill entry table for this dir
        int i;
        for (i = 0; dir_contents[d].entries[i] != NULL; i++) {
            entry_table[i].name = dir_contents[d].entries[i];
            entry_table[i].seen = 0;
        }
        entry_table[i].name = NULL; // null-terminate

        int rv = fs_ops.readdir(dir_contents[d].dir, NULL, readdir_filler_check, 0, NULL);
        ck_assert_msg(rv == 0, "readdir failed for dir %s", dir_contents[d].dir);

        for (i = 0; entry_table[i].name != NULL; i++) {
            ck_assert_msg(entry_table[i].seen, "entry '%s' missing in dir '%s'", entry_table[i].name, dir_contents[d].dir);
        }
    }

    // non-existent path (ENOENT)
    int rv = fs_ops.readdir("/no-path", NULL, readdir_filler_check, 0, NULL);
    ck_assert_int_eq(rv, -ENOENT);

    // call readdir on a file (ENOTDIR)
    rv = fs_ops.readdir("/file.1k", NULL, readdir_filler_check, 0, NULL);
    ck_assert_int_eq(rv, -ENOTDIR);
}
END_TEST

unsigned read_file_in_chunks(const char *path, int size, int chunk_size)
{
    char big_buf[20000]; // large enough for all files
    char tmp_buf[4096];  // buffer for each read
    int total_read = 0;

    while (total_read < size) {
        int to_read = chunk_size;
        if (total_read + to_read > size)
            to_read = size - total_read;

        int rv = fs_ops.read(path, tmp_buf, to_read, total_read, NULL);
        ck_assert_msg(rv == to_read, "read %d bytes at offset %d failed (got %d)", to_read, total_read, rv);

        memcpy(big_buf + total_read, tmp_buf, rv);
        total_read += rv;
    }

    return crc32(0, (unsigned char *)big_buf, size);
}

START_TEST(test_read_file_1k_big)
{
    char buf[20000]; // bigger than max file
    int rv = fs_ops.read("/file.1k", buf, sizeof(buf), 0, NULL);
    ck_assert_msg(rv == 1000, "expected to read 1000 bytes, got %d", rv);

    unsigned cksum = crc32(0, (unsigned char *)buf, 1000);
    ck_assert_int_eq(cksum, 1726121896);
}
END_TEST

START_TEST(test_read_file_1k_chunks)
{
    int file_size = 1000;
    unsigned expected_cksum = 1726121896;
    int chunk_sizes[] = {17, 100, 1000, 1024, 1970, 3000};

    for (int i = 0; i < sizeof(chunk_sizes)/sizeof(int); i++) {
        unsigned cksum = read_file_in_chunks("/file.1k", file_size, chunk_sizes[i]);
        ck_assert_msg(cksum == expected_cksum, "checksum mismatch with chunk size %d (got %u)", chunk_sizes[i], cksum);
    }
}
END_TEST

START_TEST(test_statfs_values)
{
    struct statvfs sv;
    int rv = fs_ops.statfs("/", &sv);
    ck_assert_int_eq(rv, 0);

    ck_assert_int_eq(sv.f_bsize, 4096);
    ck_assert_int_eq(sv.f_blocks, 398); // total number of blocks // f_blocks = total image - (superblock + block map)
    ck_assert_int_eq(sv.f_bfree, 353); // free blocks // -2 for superblock and bitmap
    ck_assert_int_eq(sv.f_bavail, 353); // available blocks = free blocks // -2 for superblock and bitmap
    ck_assert_int_eq(sv.f_namemax, 27);
}
END_TEST


START_TEST(test_chmod_file_and_dir)
{
    int rv_file = fs_ops.chmod("/file.1k", 0755);
    ck_assert_int_eq(rv_file, 0);

    struct stat st_file;
    int rv_get_file = fs_ops.getattr("/file.1k", &st_file);
    ck_assert_int_eq(rv_get_file, 0);
    ck_assert(S_ISREG(st_file.st_mode));                  
    ck_assert_int_eq(st_file.st_mode & 0777, 0755);      
    
    ck_assert_int_eq(fs_ops.chmod("/file.1k", 0666), 0); // Revert permissions

    // chmod on a directory
    int rv_dir = fs_ops.chmod("/dir2", 0700);
    ck_assert_int_eq(rv_dir, 0);

    struct stat st_dir;
    int rv_get_dir = fs_ops.getattr("/dir2", &st_dir);
    ck_assert_int_eq(rv_get_dir, 0);
    ck_assert(S_ISDIR(st_dir.st_mode));        
    ck_assert_int_eq(st_dir.st_mode & 0777, 0700);

    ck_assert_int_eq(fs_ops.chmod("/dir2", 0777), 0); // Revert permissions
}
END_TEST


START_TEST(test_rename_file_and_directory)
{
    // --- Rename file and check ---
    int rv = fs_ops.rename("/file.10", "/new_file.10");
    ck_assert_int_eq(rv, 0);

    struct stat st_old;
    rv = fs_ops.getattr("/file.10", &st_old);
    ck_assert_msg(rv == -ENOENT, "Old file '/file.10' still exists after rename");

    struct stat st_new;
    rv = fs_ops.getattr("/new_file.10", &st_new);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st_new.st_size, 10);
    ck_assert_int_eq(st_new.st_uid, 500);
    ck_assert_int_eq(st_new.st_gid, 500);
    ck_assert(S_ISREG(st_new.st_mode));

    // Rename back
    rv = fs_ops.rename("/new_file.10", "/file.10");
    ck_assert_int_eq(rv, 0);

    rv = fs_ops.rename("/dir2/../file.10", "/dir3/../file.10"); // rename using path traversal with ..
    ck_assert_msg(rv == 0, "Rename with '..' path traversal failed");

    rv = fs_ops.rename("/file.10", "/file.1k"); // rename to existing file
    ck_assert_int_eq(rv, -EEXIST);

    rv = fs_ops.rename("/file.10", "/dir2/file.10"); // rename across directories (invalid)
    ck_assert_int_eq(rv, -EINVAL);

    // --- Rename directory ---
    rv = fs_ops.rename("/dir-with-long-name", "/renamed-dir");
    ck_assert_int_eq(rv, 0);

    rv = fs_ops.getattr("/dir-with-long-name", &st_old); // make sure old name is not present
    ck_assert_int_eq(rv, -ENOENT);

    char path[256] = "/renamed-dir/file.12k+"; // make sure file inside renamed dir is still there
    struct stat st_inside;
    rv = fs_ops.getattr(path, &st_inside);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st_inside.st_size, 12289);
    ck_assert(S_ISREG(st_inside.st_mode));

    rv = fs_ops.rename("/renamed-dir", "/dir-with-long-name");  // Rename directory back
    ck_assert_int_eq(rv, 0);
}
END_TEST

/* note that your tests will call:
 *  fs_ops.getattr(path, struct stat *sb)
 *  fs_ops.readdir(path, NULL, filler_function, 0, NULL)
 *  fs_ops.read(path, buf, len, offset, NULL);
 *  fs_ops.statfs(path, struct statvfs *sv);
 */


int main(int argc, char **argv)
{
    system("python gen-disk.py -q disk1.in test.img");

    block_init("test.img");
    fs_ops.init(NULL);
    
    Suite *s = suite_create("fs5600");
    TCase *tc = tcase_create("read_mostly");

    //read tests - part 1 tests
    tcase_add_test(tc, test_getattr_all);
    tcase_add_test(tc, test_readdir_all_dirs);
    tcase_add_test(tc, test_read_file_1k_big);
    tcase_add_test(tc, test_read_file_1k_chunks);
    tcase_add_test(tc, test_statfs_values);
    tcase_add_test(tc, test_chmod_file_and_dir);
    tcase_add_test(tc, test_rename_file_and_directory);

    
    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    
    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    printf("%d tests failed\n", n_failed);
    
    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
