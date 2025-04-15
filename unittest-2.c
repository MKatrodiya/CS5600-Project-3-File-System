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

struct {
    const char *name;
    int seen;
} dir_table[] = {
    {"testfile", 0},
    {NULL, 0}
};

int readdir_filler_rootdir(void *ptr, const char *name, const struct stat *stbuf, off_t off)
{
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
    // Create a test file
    int rv = fs_ops.create("/testfile", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    
    // Verify file attributes
    struct stat st;
    rv = fs_ops.getattr("/testfile", &st);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st.st_uid, 500);
    ck_assert_int_eq(st.st_gid, 500);
    ck_assert(S_ISREG(st.st_mode));
    ck_assert_int_eq(st.st_size, 0);
    
    rv = fs_ops.readdir("/", NULL, readdir_filler_rootdir, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table[0].seen, 1);
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
    

    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    
    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    printf("%d tests failed\n", n_failed);
    
    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

