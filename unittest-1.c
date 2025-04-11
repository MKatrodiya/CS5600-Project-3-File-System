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


/* change test name and make it do something useful */
START_TEST(a_test)
{
    ck_assert_int_eq(1, 1);
}
END_TEST

START_TEST(test_getattr_file_1k)
{
    struct stat st;
    int rv = fs_ops.getattr("/file.1k", &st);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st.st_uid, 500);
    ck_assert_int_eq(st.st_gid, 500);
    ck_assert(S_ISREG(st.st_mode));
    ck_assert_int_eq(st.st_size, 1000);
}
END_TEST


struct {
    const char *name;
    int seen;
} root_dir_table[] = {
    {"dir2", 0},
    {"dir3", 0},
    {"dir-with-long-name", 0},
    {"file.10", 0},
    {"file.1k", 0},
    {"file.8k+", 0},
    {NULL, 0}
};

int readdir_filler_rootdir(void *ptr, const char *name, const struct stat *stbuf, off_t off)
{
    for (int i = 0; root_dir_table[i].name != NULL; i++) {
        if (strcmp(name, root_dir_table[i].name) == 0) {
            root_dir_table[i].seen = 1;
            return 0;
        }
    }
    return 0;
}

START_TEST(test_readdir_root)
{
    int rv = fs_ops.readdir("/", NULL, readdir_filler_rootdir, 0, NULL);
    ck_assert_int_eq(rv, 0);

    for (int i = 0; root_dir_table[i].name != NULL; i++) {
        ck_assert(root_dir_table[i].seen);
        root_dir_table[i].seen = 0;
    }
}
END_TEST

START_TEST(test_read_file_1k)
{
    char buf[15000];
    int rv = fs_ops.read("/file.1k", buf, 1000, 0, NULL);
    ck_assert_int_eq(rv, 1000);

    unsigned cksum = crc32(0, (unsigned char *)buf, 1000);
    ck_assert_int_eq(cksum, 1726121896);
}
END_TEST

START_TEST(test_statfs_values)
{
    struct statvfs sv;
    int rv = fs_ops.statfs("/", &sv);
    ck_assert_int_eq(rv, 0);

    ck_assert_int_eq(sv.f_bsize, FS_BLOCK_SIZE);
    ck_assert(sv.f_blocks > 0);
    ck_assert(sv.f_bfree <= sv.f_blocks);
    ck_assert_int_eq(sv.f_bavail, sv.f_bfree);
    ck_assert_int_eq(sv.f_namemax, MAX_NAME_LEN);
}
END_TEST


/* this is an example of a callback function for readdir
 */
int empty_filler(void *ptr, const char *name, const struct stat *stbuf, off_t off)
{
    /* FUSE passes you the entry name and a pointer to a 'struct stat' 
     * with the attributes. Ignore the 'ptr' and 'off' arguments 
     * 
     */
    return 0;
}

/* note that your tests will call:
 *  fs_ops.getattr(path, struct stat *sb)
 *  fs_ops.readdir(path, NULL, filler_function, 0, NULL)
 *  fs_ops.read(path, buf, len, offset, NULL);
 *  fs_ops.statfs(path, struct statvfs *sv);
 */



int main(int argc, char **argv)
{
    block_init("test.img");
    fs_ops.init(NULL);
    
    Suite *s = suite_create("fs5600");
    TCase *tc = tcase_create("read_mostly");

    tcase_add_test(tc, a_test); /* see START_TEST above */
    tcase_add_test(tc, test_getattr_file_1k);
    tcase_add_test(tc, test_readdir_root);
    tcase_add_test(tc, test_read_file_1k);
    tcase_add_test(tc, test_statfs_values);

    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    
    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    printf("%d tests failed\n", n_failed);
    
    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
