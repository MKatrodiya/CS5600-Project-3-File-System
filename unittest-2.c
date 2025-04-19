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

    //------------------------------------------------------------------------//
    // testing nonexistent directory create
    rv = fs_ops.create("/nonexistent/testfile.txt", 0100666, NULL);
    ck_assert_msg(rv == -ENOENT, "Create in nonexistent directory is failing");

    //------------------------------------------------------------------------//
    // create and recreate the file 
    rv = fs_ops.create("/testfile2.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/testfile2.txt", 0100666, NULL);
    ck_assert_msg(rv == -EEXIST, "Recreating the same file is failing");

    //------------------------------------------------------------------------//
    // create and recreate the file same name as dir
    rv = fs_ops.mkdir("/test", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/test", 0100666, NULL);
    ck_assert_msg(rv == -EEXIST, "Recreating the same file is failing");

    //------------------------------------------------------------------------//
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

    //------------------------------------------------------------------------//
    // file as a directory in path
    rv = fs_ops.create("/test3", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/test3/test4.txt", 0100666, NULL);
    ck_assert_msg(rv == -ENOTDIR, "Accessing file as a directory.");

    //------------------------------------------------------------------------//
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

    //------------------------------------------------------------------------//
    // create miltiple files in deep directory structure
    rv = fs_ops.mkdir("/test5", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/test5/deep", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/test5/deep/structure", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/test5/deep/structure/file1.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/test5/deep/structure/file2.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/test5/deep/structure/file3.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    
    struct stat st3;
    rv = fs_ops.getattr("/test5/deep/structure/file1.txt", &st3);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st3.st_uid, 500);
    ck_assert_int_eq(st3.st_gid, 500);
    ck_assert(S_ISREG(st3.st_mode));
    ck_assert_int_eq(st3.st_mode & 0777, 0666);
    ck_assert_int_eq(st3.st_size, 0);

    struct stat st4;
    rv = fs_ops.getattr("/test5/deep/structure/file2.txt", &st4);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st4.st_uid, 500);
    ck_assert_int_eq(st4.st_gid, 500);
    ck_assert(S_ISREG(st4.st_mode));
    ck_assert_int_eq(st4.st_mode & 0777, 0666);
    ck_assert_int_eq(st4.st_size, 0);

    struct stat st5;
    rv = fs_ops.getattr("/test5/deep/structure/file3.txt", &st5);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st5.st_uid, 500);
    ck_assert_int_eq(st5.st_gid, 500);
    ck_assert(S_ISREG(st5.st_mode));
    ck_assert_int_eq(st5.st_mode & 0777, 0666);
    ck_assert_int_eq(st5.st_size, 0);


    struct {
        const char *name;
        int seen;
    } dir_table2[] = {
        {"file1.txt", 0},
        {"file2.txt", 0},
        {"file3.txt", 0},
        {NULL, 0}
    };
    
    rv = fs_ops.readdir("/test5/deep/structure", dir_table2, readdir_filler, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table2[0].seen, 1);
    ck_assert_int_eq(dir_table2[1].seen, 1);
    ck_assert_int_eq(dir_table2[2].seen, 1);
    //------------------------------------------------------------------------//
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

    //------------------------------------------------------------------------//
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

    //------------------------------------------------------------------------//
    // creating a directory with the same name as a file
    rv = fs_ops.create("/mkdirtestfile1", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/mkdirtestfile1", 0777);
    ck_assert_msg(rv == -EEXIST, "Fail: Creating a directory with the same name as a");
    
    //------------------------------------------------------------------------//
    // creating a directory with the same name as an existing directory
    rv = fs_ops.mkdir("/mkdirdir1", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/mkdirdir1", 0777);
    ck_assert_msg(rv == -EEXIST, "Fail: Creating a directory with the same name as an existing directory");

    //------------------------------------------------------------------------//
    // creating a directory in a nonexistent path
    rv = fs_ops.mkdir("/nonexistent/testdir", 0777);
    ck_assert_msg(rv == -ENOENT, "Fail: Creating a directory in a nonexistent path");

    //------------------------------------------------------------------------//
    // creating a directory with a file in the path
    rv = fs_ops.create("/mkdirtestfile2", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/mkdirtestfile2/subdir", 0777);
    ck_assert_msg(rv == -ENOTDIR, "Fail: Creating a directory with a file in the path");

    //------------------------------------------------------------------------//
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

    //------------------------------------------------------------------------//
    // creating a directory in a deep directory structure
    rv = fs_ops.mkdir("/deepdir", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/deepdir/level1", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/deepdir/level1/level2", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/deepdir/level1/level2/level3", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/deepdir/level1/level2/level4", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/deepdir/level1/level2/level5", 0777);
    ck_assert_int_eq(rv, 0);

    struct stat st3;
    rv = fs_ops.getattr("/deepdir/level1/level2/level3", &st3);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st3.st_uid, 500);
    ck_assert_int_eq(st3.st_gid, 500);
    ck_assert(S_ISDIR(st3.st_mode));
    ck_assert_int_eq(st3.st_mode & 0777, 0777);

    struct stat st4;
    rv = fs_ops.getattr("/deepdir/level1/level2/level4", &st4);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st4.st_uid, 500);
    ck_assert_int_eq(st4.st_gid, 500);
    ck_assert(S_ISDIR(st4.st_mode));
    ck_assert_int_eq(st4.st_mode & 0777, 0777);

    struct stat st5;
    rv = fs_ops.getattr("/deepdir/level1/level2/level5", &st5);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st5.st_uid, 500);
    ck_assert_int_eq(st5.st_gid, 500);
    ck_assert(S_ISDIR(st5.st_mode));
    ck_assert_int_eq(st5.st_mode & 0777, 0777);

    struct {
        char *name;
        int seen;
    } dir_table3[] = {
        {"level3", 0},
        {"level4", 0},
        {"level5", 0},
        {NULL, 0}
    };
    rv = fs_ops.readdir("/deepdir/level1/level2", dir_table3, readdir_filler, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table3[0].seen, 1);
    ck_assert_int_eq(dir_table3[1].seen, 1);
    ck_assert_int_eq(dir_table3[2].seen, 1);

    //------------------------------------------------------------------------//
    // 10 level deep directory structure
    rv = fs_ops.mkdir("/verydeepdir", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/verydeepdir/deeplevel1", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/verydeepdir/deeplevel1/deeplevel2", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/verydeepdir/deeplevel1/deeplevel2/deeplevel3", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/verydeepdir/deeplevel1/deeplevel2/deeplevel3/deeplevel4", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/verydeepdir/deeplevel1/deeplevel2/deeplevel3/deeplevel4/deeplevel5", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/verydeepdir/deeplevel1/deeplevel2/deeplevel3/deeplevel4/deeplevel5/deeplevel6", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/verydeepdir/deeplevel1/deeplevel2/deeplevel3/deeplevel4/deeplevel5/deeplevel6/deeplevel7", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/verydeepdir/deeplevel1/deeplevel2/deeplevel3/deeplevel4/deeplevel5/deeplevel6/deeplevel7/deeplevel8", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/verydeepdir/deeplevel1/deeplevel2/deeplevel3/deeplevel4/deeplevel5/deeplevel6/deeplevel7/deeplevel8/deeplevel9", 0777);
    ck_assert_int_eq(rv, 0);

    struct stat stdeep;
    rv = fs_ops.getattr("/verydeepdir/deeplevel1/deeplevel2/deeplevel3/deeplevel4/deeplevel5/deeplevel6/deeplevel7/deeplevel8/deeplevel9", &stdeep);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(stdeep.st_uid, 500);
    ck_assert_int_eq(stdeep.st_gid, 500);
    ck_assert(S_ISDIR(stdeep.st_mode));
    ck_assert_int_eq(stdeep.st_mode & 0777, 0777);

    struct {
        char *name;
        int seen;
    } dir_table_deep[] = {
        {"deeplevel9", 0},
        {NULL, 0}
    };

    rv = fs_ops.readdir("/verydeepdir/deeplevel1/deeplevel2/deeplevel3/deeplevel4/deeplevel5/deeplevel6/deeplevel7/deeplevel8", dir_table_deep, readdir_filler, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table_deep[0].seen, 1);

}
END_TEST


START_TEST(test_unlink)
{
    // simple file unlink
    int rv = fs_ops.create("/unlinkfile.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);

    const char *test_data = "Hi I am Mitul Nakrani, You can call me Lord Mitul.";
    int data_len = strlen(test_data);
    rv = fs_ops.write("/unlinkfile.txt", test_data, data_len, 0, NULL);
    ck_assert_int_eq(rv, data_len);
    
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

    //-----------------------------------------------------------------------------//
    // unlinking a file that does not exist
    rv = fs_ops.unlink("/nonexistent.txt");
    ck_assert_msg(rv == -ENOENT, "Fail: Unlinking a file that does not exist");

    //-----------------------------------------------------------------------------//
    // unlinking a directory
    rv = fs_ops.mkdir("/unlinkdir", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.unlink("/unlinkdir");
    ck_assert_msg(rv == -EISDIR, "Fail: Unlinking a directory should return EISDIR");

    //-----------------------------------------------------------------------------//
    // unlinking a file in a nonexistent directory
    rv = fs_ops.unlink("/nonexistent/unlinkfile.txt");
    ck_assert_msg(rv == -ENOENT, "Fail: Unlinking a file in a nonexistent directory");

    //-----------------------------------------------------------------------------//
    // unling a deep file
    rv = fs_ops.mkdir("/deepfiledir", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/deepfiledir/deepfilesubdir", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.mkdir("/deepfiledir/deepfilesubdir/deepfilesubdir2", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/deepfiledir/deepfilesubdir/deepfilesubdir2/deepfile.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);

    const char *test_data_long = "Hi I am Mitul Nakrani, You can call me Lord Mitul.";
    int data_len_long = strlen(test_data_long);
    rv = fs_ops.write("/deepfiledir/deepfilesubdir/deepfilesubdir2/deepfile.txt", test_data_long, data_len_long, 0, NULL);
    ck_assert_int_eq(rv, data_len);
    
    struct {
        char *name;
        int seen;
    } dir_table3[] = {
        {"deepfile.txt", 0},
        {NULL, 0}
    };
    
    rv = fs_ops.readdir("/deepfiledir/deepfilesubdir/deepfilesubdir2", dir_table3, readdir_filler, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table3[0].seen, 1);

    rv = fs_ops.unlink("/deepfiledir/deepfilesubdir/deepfilesubdir2/deepfile.txt");
    ck_assert_int_eq(rv, 0);
    
    struct {
        char *name;
        int seen;
    } dir_table4[] = {
        {"unlinkfile.txt", 0},
        {NULL, 0}
    };
    
    rv = fs_ops.readdir("/deepfiledir/deepfilesubdir/deepfilesubdir2", dir_table4, readdir_filler, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table4[0].seen, 0);
    //-----------------------------------------------------------------------------//
}
END_TEST

START_TEST(test_rmdir)
{
    //simple rmdir in root directory
    int rv = fs_ops.mkdir("/deepfiledir/rmdirtest99", 0777);
    ck_assert_msg(rv == 0, "Fail: Rmdir a directory that should exist1");
    
    struct stat st;
    rv = fs_ops.getattr("/deepfiledir/rmdirtest99", &st);
    ck_assert_msg(rv == 0, "Fail: Rmdir a directory that should exist2");
    ck_assert(S_ISDIR(st.st_mode));

    struct {
        char *name;
        int seen;
    } dir_table[] = {
        {"rmdirtest99", 0},
        {NULL, 0}
    };

    rv = fs_ops.readdir("/deepfiledir", dir_table, readdir_filler, 0, NULL);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(dir_table[0].seen, 1);
    
    rv = fs_ops.rmdir("/deepfiledir/rmdirtest99");
    ck_assert_msg(rv == 0, "Fail: Rmdir a directory that should exist3");
    
    rv = fs_ops.getattr("/deepfiledir/rmdirtest99", &st);
    ck_assert_msg(rv == -ENOENT, "Fail: Rmdir a directory that should not exist");

    // rmdir a directory that does not exist
    rv = fs_ops.rmdir("/nonexistentdir");
    ck_assert_msg(rv == -ENOENT, "Fail: Rmdir a directory that does not exist4");

    //rmdir a file
    rv = fs_ops.create("/deepfiledir/rmdirfile00.txt", 0100666, NULL);
    ck_assert_msg(rv == 0, "Fail: Create a file for rmdir test");
    rv = fs_ops.rmdir("deepfiledir/rmdirfile00.txt");
    ck_assert_msg(rv == -ENOTDIR, "Fail: Rmdir a file should return ENOTDIR");

    // rmdir a directory that is not empty
    rv = fs_ops.mkdir("/deepfiledir/rmdirnotempty", 0777);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.create("/deepfiledir/rmdirnotempty/file.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    rv = fs_ops.rmdir("/deepfiledir/rmdirnotempty");
    ck_assert_msg(rv == -ENOTEMPTY, "Fail: Rmdir a directory that is not empty should return ENOTEMPTY");

    // rmdir a directory in nonexistent path
    rv = fs_ops.rmdir("/nonexistent/rmdirtest");
    ck_assert_msg(rv == -ENOENT, "Fail: Rmdir a directory in nonexistent path should return ENOENT");
}
END_TEST


START_TEST(test_write)
{
    // Simple write data to the file
    int rv = fs_ops.create("/writefile.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    
    const char *test_data = "Hii, My Name is Mitul Nakrnai, I am a passionate and experienced developer.";
    int data_len = strlen(test_data);
    rv = fs_ops.write("/writefile.txt", test_data, data_len, 0, NULL);
    ck_assert_int_eq(rv, data_len);

    struct stat st;
    rv = fs_ops.getattr("/writefile.txt", &st);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st.st_size, data_len);

    char read_buf[100];
    rv = fs_ops.read("/writefile.txt", read_buf, data_len, 0, NULL);
    ck_assert_int_eq(rv, data_len);
    read_buf[data_len] = '\0';
    
    ck_assert_str_eq(read_buf, test_data);

    //--------------------------------------------------------------------------//
    // write data to a file with an offset
    int rv2 = fs_ops.create("/writeoffset.txt", 0100666, NULL);
    ck_assert_int_eq(rv2, 0);
    
    const char *initial_data = "Hi I am Mitul Nakrani, You can call me Lord Mitul.";
    int initial_len = strlen(initial_data);
    rv2 = fs_ops.write("/writeoffset.txt", initial_data, initial_len, 0, NULL);
    ck_assert_int_eq(rv2, initial_len);
    
    // Write more data at an offset
    const char *append_data = " - You can also call me Legend Mitul.";
    int append_len = strlen(append_data);
    rv2 = fs_ops.write("/writeoffset.txt", append_data, append_len, initial_len, NULL);
    ck_assert_int_eq(rv2, append_len);
    
    struct stat st2;
    rv2 = fs_ops.getattr("/writeoffset.txt", &st2);
    ck_assert_int_eq(rv2, 0);
    ck_assert_int_eq(st2.st_size, initial_len + append_len);
    
    char read_buf2[200];
    rv2 = fs_ops.read("/writeoffset.txt", read_buf2, initial_len + append_len, 0, NULL);
    ck_assert_int_eq(rv2, initial_len + append_len);
    read_buf2[initial_len + append_len] = '\0';
    
    char expected[200];
    strcpy(expected, initial_data);
    strcat(expected, append_data);
    ck_assert_str_eq(read_buf2, expected);

    //-------------------------------------------------------------------------//
    // write data to a file and overwrite it of shorter length
    int rv3 = fs_ops.create("/overwritefile.txt", 0100666, NULL);
    ck_assert_int_eq(rv3, 0);
    
    const char *initial_data_to_overwrite = "Hi I am Mitul Nakrani, You can call me Lord Mitul.";
    int initial_len_data = strlen(initial_data_to_overwrite);
    rv3 = fs_ops.write("/overwritefile.txt", initial_data_to_overwrite, initial_len_data, 0, NULL);
    ck_assert_int_eq(rv3, initial_len_data);
    
    const char *overwrite_data = "You can also call me Legend Mitul.";
    int overwrite_len = strlen(overwrite_data);
    rv3 = fs_ops.write("/overwritefile.txt", overwrite_data, overwrite_len, 0, NULL);
    ck_assert_int_eq(rv3, overwrite_len);
    
    char read_buf3[200];
    rv3 = fs_ops.read("/overwritefile.txt", read_buf3, initial_len_data, 0, NULL);
    ck_assert_int_eq(rv3, initial_len_data);
    read_buf[initial_len_data] = '\0';
    
    char expected2[200];
    if (overwrite_len < initial_len_data) {
        strncpy(expected2, overwrite_data, overwrite_len);
        strncpy(expected2 + overwrite_len, initial_data_to_overwrite + overwrite_len, initial_len_data - overwrite_len);
        expected[initial_len_data] = '\0';
    } else {
        strcpy(expected2, overwrite_data);
    }
    ck_assert_str_eq(read_buf3, expected2);

    //-------------------------------------------------------------------------//
    // test writing a larger file (multiple blocks)
    rv = fs_ops.create("/veryverylargefile.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    
    char *very_large_test_data = malloc(FS_BLOCK_SIZE * 2 + 100);
    for (int i = 0; i < FS_BLOCK_SIZE * 2 + 100; i++) 
    {
        very_large_test_data[i] = 'A' + (i % 26);
    }
    
    rv = fs_ops.write("/veryverylargefile.txt", test_data, FS_BLOCK_SIZE * 2 + 100, 0, NULL);
    ck_assert_int_eq(rv, FS_BLOCK_SIZE * 2 + 100);
    
    struct stat st9;
    rv = fs_ops.getattr("/veryverylargefile.txt", &st9);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st9.st_size, FS_BLOCK_SIZE * 2 + 100);
    
    char *very_large_read_data = malloc(FS_BLOCK_SIZE * 2 + 100);
    int total_read = 0;
    int chunk_size = 1000;
    
    for (int offset = 0; offset < FS_BLOCK_SIZE * 2 + 100; offset += chunk_size) 
    {
        int to_read = chunk_size;
        if (offset + to_read > FS_BLOCK_SIZE * 2 + 100) {
            to_read = FS_BLOCK_SIZE * 2 + 100 - offset;
        }
        
        rv = fs_ops.read("/veryverylargefile.txt", very_large_read_data + offset, to_read, offset, NULL);
        ck_assert_int_eq(rv, to_read);
        total_read += to_read;
    }
    
    ck_assert_int_eq(total_read, FS_BLOCK_SIZE * 2 + 100);
    for (int i = 0; i < FS_BLOCK_SIZE * 2 + 100; i++) {
        ck_assert_int_eq(very_large_read_data[i], test_data[i]);
    }
    
    free(very_large_test_data);
    free(very_large_read_data);
    //-------------------------------------------------------------------------//
}
END_TEST

START_TEST(test_truncate)
{
    // Simple truncate operation
    int rv = fs_ops.create("/truncatefile.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    
    const char *test_data = "Hi this is Meet Katrodiya, You can call me dashing Meet.";
    int data_len = strlen(test_data);
    rv = fs_ops.write("/truncatefile.txt", test_data, data_len, 0, NULL);
    ck_assert_int_eq(rv, data_len);
    
    struct stat st;
    rv = fs_ops.getattr("/truncatefile.txt", &st);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st.st_size, data_len);

    rv = fs_ops.truncate("/truncatefile.txt", 0);
    ck_assert_int_eq(rv, 0);
    
    rv = fs_ops.getattr("/truncatefile.txt", &st);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st.st_size, 0);

    char read_buf[100];
    rv = fs_ops.read("/truncatefile.txt", read_buf, data_len, 0, NULL);
    ck_assert_int_eq(rv, 0);
    
    //-------------------------------------------------------------------------//
    // test non-zero truncate (should return EINVAL)
    rv = fs_ops.create("/trunc2file.txt", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    
    const char *test_data2 = "";
    int data_len2 = strlen(test_data2);
    rv = fs_ops.write("/trunc2file.txt", test_data2, data_len2, 0, NULL);
    ck_assert_int_eq(rv, data_len2);
    
    rv = fs_ops.truncate("/trunc2file.txt", 5);
    ck_assert_int_eq(rv, -EINVAL);

    //-------------------------------------------------------------------------//
}
END_TEST

START_TEST(test_utime)
{
    // simple utime operation
    int rv = fs_ops.create("/utimefile", 0100666, NULL);
    ck_assert_int_eq(rv, 0);
    
    struct utimbuf ut;
    ut.actime = 12345;  // access time
    ut.modtime = 67890; // modification time
    
    rv = fs_ops.utime("/utimefile", &ut);
    ck_assert_int_eq(rv, 0);
    
    struct stat st;
    rv = fs_ops.getattr("/utimefile", &st);
    ck_assert_int_eq(rv, 0);
    ck_assert_int_eq(st.st_mtime, 67890);
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
    tcase_add_test(tc, test_write);
    tcase_add_test(tc, test_truncate);
    tcase_add_test(tc, test_utime);
    

    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    
    srunner_run_all(sr, CK_VERBOSE);
    int n_failed = srunner_ntests_failed(sr);
    printf("%d tests failed\n", n_failed);
    
    srunner_free(sr);
    return (n_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}


