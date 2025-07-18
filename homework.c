/*
 * file: homework.c
 * description: skeleton file for CS 5600 system
 *
 * CS 5600, Computer Systems, Northeastern
 */

#define FUSE_USE_VERSION 27
#define _FILE_OFFSET_BITS 64

#define MAX_PATH_LEN 10
#define MAX_NAME_LEN 27
#define S_IFMT 0170000 
#define S_IFDIR 0040000

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <math.h>

#include "fs5600.h"

/* if you don't understand why you can't use these system calls here, 
 * you need to read the assignment description another time
 */
#define stat(a,b) error do not use stat()
#define open(a,b) error do not use open()
#define read(a,b,c) error do not use read()
#define write(a,b,c) error do not use write()

/* disk access. All access is in terms of 4KB blocks; read and
 * write functions return 0 (success) or -EIO.
 */
extern int block_read(void *buf, int lba, int nblks);
extern int block_write(void *buf, int lba, int nblks);

/*
   Global variables and structures used by the filesystem.
 */
static struct fs_super superblock;      // global superblock
static unsigned char *bitmap;   // global block bitmap

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* bitmap functions
 */
void bit_set(unsigned char *map, int i)
{
	map[i/8] |= (1 << (i%8));
}
void bit_clear(unsigned char *map, int i)
{
	map[i/8] &= ~(1 << (i%8));
}
int bit_test(unsigned char *map, int i)
{
	return map[i/8] & (1 << (i%8));
}

/* init - this is called once by the FUSE framework at startup. Ignore
 * the 'conn' argument.
 * recommended actions:
 *   - read superblock
 *   - allocate memory, read bitmaps and inodes
 */
void* fs_init(struct fuse_conn_info *conn)
{
	char buffer[FS_BLOCK_SIZE];
	if (block_read(buffer, 0, 1) != 0) 
	{
		fprintf(stderr, "[fs_init]: superblock read failed\n");
		return NULL;
	}
	memcpy(&superblock, buffer, sizeof(struct fs_super));

	if (superblock.magic != FS_MAGIC) {
		fprintf(stderr, "[fs_init]: invalid magic number\n");
		return NULL;
	}

	bitmap = malloc(FS_BLOCK_SIZE); // Allocate memory for block bitmap
	if (!bitmap) {
		fprintf(stderr, "[fs_init]: bitmap malloc failed\n");
		return NULL;
	}

	if (block_read(bitmap, 1, 1) != 0) {
		fprintf(stderr, "[fs_init]: bitmap read failed\n");
		free(bitmap);
		bitmap = NULL;
		return NULL;
	}

	return NULL;
}

/* Note on path translation errors:
 * In addition to the method-specific errors listed below, almost
 * every method can return one of the following errors if it fails to
 * locate a file or directory corresponding to a specified path.
 *
 * ENOENT - a component of the path doesn't exist.
 * ENOTDIR - an intermediate component of the path (e.g. 'b' in
 *           /a/b/c) is not a directory
 */

/* note on splitting the 'path' variable:
 * the value passed in by the FUSE framework is declared as 'const',
 * which means you can't modify it. The standard mechanisms for
 * splitting strings in C (strtok, strsep) modify the string in place,
 * so you have to copy the string and then free the copy when you're
 * done. One way of doing this:
 *
 *    char *_path = strdup(path);
 *    int inum = translate(_path);
 *    free(_path);
 */

int read_inode(uint32_t inum, struct fs_inode *inode) 
{
	char buffer[FS_BLOCK_SIZE];
	if (block_read(buffer, inum, 1) != 0) 
	{
		return -1;
	}  
	memcpy(inode, buffer, sizeof(struct fs_inode));
	return 0;
}

/* splits the path in components.
 * returns the number of components. -1, if error. 
 */
int pathparse(const char *path, char **components) 
{
	char *token;
	int i = 0;
	char *path_copy = strdup(path);
	if (!path_copy) 
	{
		return -1;
	}

	token = strtok(path_copy, "/");
	while (token != NULL && i < MAX_PATH_LEN) 
	{
		components[i] = strdup(token);
		i++;
		token = strtok(NULL, "/");
	}
	free(path_copy);
	return i;
}

/* translate - translate a path into an inode number.
 *  success - return 0
 *  errors - ENOENT, ENOTDIR, EIO
 */
int translate(const char *path, uint32_t *inum, struct fs_inode *inode) 
{
	char *components[MAX_PATH_LEN];
	int num_components = pathparse(path, components);
	uint32_t current_inum = 2;

	uint32_t parent_stack[MAX_PATH_LEN]; // Stack to keep track of parent inodes
	int stack_pos = 0;
	parent_stack[0] = 2; // Root's parent is itself

	// Handle the case where the path is empty ("") or path is ("/")
	if (num_components == 0) {
		if (read_inode(current_inum, inode) != 0) {
			fprintf(stderr, "[translate]: read_inode failed for root\n");
			return -EIO;
		}
		*inum = current_inum;
		return 0;
	}

	for (int i = 0; i < num_components; i++) 
	{
		if (strcmp(components[i], ".") == 0) {
			continue;
		}

		if (strcmp(components[i], "..") == 0) {
			if (stack_pos > 0) {
				stack_pos--;
				current_inum = parent_stack[stack_pos]; // Go to parent
			}
			continue;
		}

		struct fs_inode current_inode;
		if (read_inode(current_inum, &current_inode) != 0) 
		{
			fprintf(stderr, "[translate]: read_inode failed\n");
			return -EIO;
		}

		if (!S_ISDIR(current_inode.mode)) 
		{
			fprintf(stderr, "[translate]: not a directory\n");
			return -ENOTDIR;
		}
		int found = 0;
		for (int j = 0; j < current_inode.size / FS_BLOCK_SIZE; j++) 
		{
			char block[FS_BLOCK_SIZE];
			if (block_read(block, current_inode.ptrs[j], 1) != 0) 
			{
				fprintf(stderr, "[translate]: block read failed\n");
				return -EIO;
			}
			struct fs_dirent *entries = (struct fs_dirent *)block;
			for (int k = 0; k < FS_BLOCK_SIZE / sizeof(struct fs_dirent); k++) 
			{
				if (entries[k].valid && strcmp(entries[k].name, components[i]) == 0) 
				{
					parent_stack[stack_pos + 1] = current_inum;
					stack_pos++;
					current_inum = entries[k].inode;
					found = 1;
					break;
				}
			}
			if (found) 
			{
				break;
			}
		}
		if (!found) 
		{
			return -ENOENT;
		}
	}

	if (read_inode(current_inum, inode) != 0) 
	{
		fprintf(stderr, "[translate]: read_inode failed for final inode\n");
		return -EIO;
	}
	*inum = current_inum;
	return 0;
}

/* resolve_path - resolve the path components to get the simplified final path.
 *  success - return number of resolved components
 * 
 */
int resolve_path(char **components, int num_components, char **resolved) 
{
	if (num_components <= 0) {
		return 0;
	}

	int resolved_index = 0;
	for (int i = 0; i < num_components; i++) 
	{
		if (strcmp(components[i], ".") == 0) 
		{
			continue;
		}

		if (strcmp(components[i], "..") == 0) 
		{
			if (resolved_index > 0) 
			{
				free(resolved[resolved_index - 1]);
				resolved_index--;
			}
			continue;
		}

		resolved[resolved_index] = strdup(components[i]);
		if (!resolved[resolved_index]) 
		{
			for (int j = 0; j < resolved_index; j++) 
			{
				free(resolved[j]);
			}
			return -1;
		}
		resolved_index++;
	}

	return resolved_index;
}

/*	 free_components - free the memory allocated for components.
 *  success - return 0
 */ 
void free_components(char **components, int num_components)
{
	if (components) {
		for (int i = 0; i < num_components; i++) {
			if (components[i]) {
				free(components[i]);
				components[i] = NULL;
			}
		}
	}
}

/* setstat - set the fields of 'struct stat' from the inode.
 *  success - return 0
 */
void setstat(struct fs_inode inode, struct stat *sb) {	
	sb->st_uid = inode.uid;
	sb->st_gid = inode.gid;
	sb->st_mode = inode.mode;
	sb->st_size = inode.size;
	sb->st_nlink = 1;
	sb->st_atime = inode.mtime;
	sb->st_mtime = inode.mtime;
	sb->st_ctime = inode.ctime;
	sb->st_blocks = ceil((double)(inode.size) / FS_BLOCK_SIZE);
}

/* getattr - get file or directory attributes. For a description of
 *  the fields in 'struct stat', see 'man lstat'.
 *
 * Note - for several fields in 'struct stat' there is no corresponding
 *  information in our file system:
 *    st_nlink - always set it to 1
 *    st_atime, st_ctime - set to same value as st_mtime
 *
 * success - return 0
 * errors - path translation, ENOENT
 * hint - factor out inode-to-struct stat conversion - you'll use it
 *        again in readdir
 */
int fs_getattr(const char *path, struct stat *sb)
{
	uint32_t inum;
	struct fs_inode inode;
	int res = translate(path, &inum, &inode);
	if (res != 0) 
	{
		return res;
	}

	setstat(inode, sb);

	return 0;
}

/* readdir - get directory contents.
 *
 * call the 'filler' function once for each valid entry in the 
 * directory, as follows:
 *     filler(buf, <name>, <statbuf>, 0)
 * where <statbuf> is a pointer to a struct stat
 * success - return 0
 * errors - path resolution, ENOTDIR, ENOENT
 * 
 * hint - check the testing instructions if you don't understand how
 *        to call the filler function
 */
int fs_readdir(const char *path, void *ptr, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	uint32_t inum;
	struct fs_inode inode;
	int res = translate(path, &inum, &inode);
	if (res != 0)
	{
		fprintf(stderr, "[fs_readdir]: translate failed\n");
		return res;
	}
	if (!S_ISDIR(inode.mode)) 
	{
		fprintf(stderr, "[fs_readdir]: not a directory\n");
		return -ENOTDIR;
	}

	for (int i = 0; i < inode.size / FS_BLOCK_SIZE; i++) 
	{
		char block[FS_BLOCK_SIZE];
		if (block_read(block, inode.ptrs[i], 1) != 0) 
		{
			fprintf(stderr, "[fs_readdir]: block read failed\n");
			return -EIO;
		}
		struct fs_dirent *entries = (struct fs_dirent *)block;
		for (int j = 0; j < FS_BLOCK_SIZE / sizeof(struct fs_dirent); j++) 
		{
			if (entries[j].valid) {
				struct stat st;
				memset(&st, 0, sizeof(st));

				struct fs_inode entry_inode;
				if (read_inode(entries[j].inode, &entry_inode) != 0) 
				{
					continue;
				}

				setstat(entry_inode, &st);

				filler(ptr, entries[j].name, &st, 0);
			}

		}
	}
	return 0;
}

/* create - create a new file with specified permissions
 *
 * success - return 0
 * errors - path resolution, EEXIST
 *          in particular, for create("/a/b/c") to succeed,
 *          "/a/b" must exist, and "/a/b/c" must not.
 *
 * Note that 'mode' will already have the S_IFREG bit set, so you can
 * just use it directly. Ignore the third parameter.
 *
 * If a file or directory of this name already exists, return -EEXIST.
 * If there are already 128 entries in the directory (i.e. it's filled an
 * entire block), you are free to return -ENOSPC instead of expanding it.
 */
int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	char *components[MAX_PATH_LEN];
	int num_components = pathparse(path, components);

	char *resolved_components[MAX_PATH_LEN];
	int resolved_count = resolve_path(components, num_components, resolved_components);

	char *filename = resolved_components[resolved_count - 1];

	char parent_path[256] = "/";
	for (int i = 0; i < resolved_count - 1; i++) 
	{
		strcat(parent_path, resolved_components[i]);
		if (i < resolved_count - 2) {
			strcat(parent_path, "/");
		}
	}

	uint32_t parent_inum;
	struct fs_inode parent_inode;
	int res = translate(parent_path, &parent_inum, &parent_inode);
	if (res != 0) 
	{
		return res;
	}

	if (!S_ISDIR(parent_inode.mode)) 
	{
		return -ENOTDIR;
	}

	for (int i = 0; i < parent_inode.size / FS_BLOCK_SIZE; i++) 
	{
		char block[FS_BLOCK_SIZE];
		if (block_read(block, parent_inode.ptrs[i], 1) != 0) 
		{
			return -EIO;
		}
		struct fs_dirent *entries = (struct fs_dirent *)block;
		for (int j = 0; j < FS_BLOCK_SIZE / sizeof(struct fs_dirent); j++) 
		{
			if (entries[j].valid && strcmp(entries[j].name, filename) == 0)
			{
				free_components(components, num_components);
				free_components(resolved_components, resolved_count);
				return -EEXIST;
			}
		}
	}

	uint32_t inum = 0;
	for (int i = 2; i < superblock.disk_size; i++) 
	{
		if (!bit_test(bitmap, i)) 
		{ 
			inum = i; 
			break; 
		}
	}
	if (!inum) 
	{
		free_components(components, num_components);
		free_components(resolved_components, resolved_count);
		return -ENOSPC;
	}
	bit_set(bitmap, inum);
	if (block_write(bitmap, 1, 1) != 0) 
	{
		return -EIO;
	}

	struct fs_inode new_inode;
	memset(&new_inode, 0, sizeof(struct fs_inode));

	struct fuse_context *ctx = fuse_get_context();
	new_inode.uid = ctx->uid;
	new_inode.gid = ctx->gid;
	new_inode.mode = mode;
	new_inode.ctime = time(NULL);
	new_inode.mtime = new_inode.ctime;
	new_inode.size = 0;

	// setting file inode
	char inode_block[FS_BLOCK_SIZE];
	memset(inode_block, 0, FS_BLOCK_SIZE);
	memcpy(inode_block, &new_inode, sizeof(new_inode));
	if (block_write(inode_block, inum, 1) != 0) 
	{
		bit_clear(bitmap, inum);
		block_write(bitmap, 1, 1);
		return -EIO;
	}

	// add the new file to the parent directory
	for (int i = 0; i < parent_inode.size / FS_BLOCK_SIZE; i++) 
	{
		char block[FS_BLOCK_SIZE];
		if (block_read(block, parent_inode.ptrs[i], 1) != 0) 
		{
			return -EIO;
		}
		struct fs_dirent *entries = (struct fs_dirent *)block;
		for (int j = 0; j < FS_BLOCK_SIZE / sizeof(struct fs_dirent); j++) 
		{
			if (!entries[j].valid) 
			{
				entries[j].valid = 1;
				entries[j].inode = inum;
				strncpy(entries[j].name, filename, MAX_NAME_LEN);
				entries[j].name[MAX_NAME_LEN - 1] = '\0';
				if (block_write(block, parent_inode.ptrs[i], 1) != 0) 
				{
					return -EIO;
				}
				return 0;
			}
		}
	}
	free_components(components, num_components);
	free_components(resolved_components, resolved_count);
	return -ENOSPC;
}

/* mkdir - create a directory with the given mode.
 *
 * WARNING: unlike fs_create, @mode only has the permission bits. You
 * have to OR it with S_IFDIR before setting the inode 'mode' field.
 *
 * success - return 0
 * Errors - path resolution, EEXIST
 * Conditions for EEXIST are the same as for create. 
 */ 
int fs_mkdir(const char *path, mode_t mode)
{
	char *components[MAX_PATH_LEN];
	int num_components = pathparse(path, components);

	char *resolved_components[MAX_PATH_LEN];
	int resolved_count = resolve_path(components, num_components, resolved_components);

	char *dirname = resolved_components[resolved_count - 1];

	char parent_path[256] = "/";
	for (int i = 0; i < resolved_count - 1; i++) 
	{
		strcat(parent_path, resolved_components[i]);
		if (i < resolved_count - 2) {
			strcat(parent_path, "/");
		}
	}

	uint32_t parent_inum;
	struct fs_inode parent_inode;
	int res = translate(parent_path, &parent_inum, &parent_inode);
	if (res != 0) 
	{
		return res;
	}

	if (!S_ISDIR(parent_inode.mode)) 
	{
		return -ENOTDIR;
	}

	for (int i = 0; i < parent_inode.size / FS_BLOCK_SIZE; i++) 
	{
		char block[FS_BLOCK_SIZE];
		if (block_read(block, parent_inode.ptrs[i], 1) != 0) 
		{
			return -EIO;
		}
		struct fs_dirent *entries = (struct fs_dirent *)block;
		for (int j = 0; j < FS_BLOCK_SIZE / sizeof(struct fs_dirent); j++) 
		{
			if (entries[j].valid && strcmp(entries[j].name, dirname) == 0)
			{
				return -EEXIST;
			}
		}
	}

	uint32_t dir_inum = 0, data_block = 0;
	for (int i = 2; i < superblock.disk_size; i++) 
	{
		if (!dir_inum && !bit_test(bitmap, i)) 
		{
			dir_inum = i;
		} else if (!data_block && !bit_test(bitmap, i)) 
		{
			data_block = i;
			break;
		}
	}
	if (!dir_inum || !data_block) 
	{
		return -ENOSPC;
	}

	bit_set(bitmap, dir_inum);
	bit_set(bitmap, data_block);
	if (block_write(bitmap, 1, 1) != 0) 
	{
		return -EIO;
	}

	struct fs_inode dir_inode;
	memset(&dir_inode, 0, sizeof(struct fs_inode));

	struct fuse_context *ctx = fuse_get_context();
	dir_inode.uid = ctx->uid;
	dir_inode.gid = ctx->gid;
	dir_inode.mode = S_IFDIR | mode;
	dir_inode.ctime = time(NULL);
	dir_inode.mtime = dir_inode.ctime;
	dir_inode.size = FS_BLOCK_SIZE;
	dir_inode.ptrs[0] = data_block;  

	char inode_block[FS_BLOCK_SIZE];
	memset(inode_block, 0, FS_BLOCK_SIZE);
	memcpy(inode_block, &dir_inode, sizeof(dir_inode));
	if (block_write(inode_block, dir_inum, 1) != 0) 
	{
		bit_clear(bitmap, dir_inum);
		bit_clear(bitmap, data_block);
		block_write(bitmap, 1, 1);
		return -EIO;
	}

	char dirents[FS_BLOCK_SIZE];
	memset(dirents, 0, FS_BLOCK_SIZE);
	if (block_write(dirents, data_block, 1) != 0)  
	{
		bit_clear(bitmap, dir_inum);
		bit_clear(bitmap, data_block);
		block_write(bitmap, 1, 1);
		return -EIO;
	}

	for (int i = 0; i < parent_inode.size / FS_BLOCK_SIZE; i++) 
	{
		char block[FS_BLOCK_SIZE];
		if (block_read(block, parent_inode.ptrs[i], 1) != 0) 
		{
			return -EIO;
		}
		struct fs_dirent *entries = (struct fs_dirent *)block;
		for (int j = 0; j < FS_BLOCK_SIZE / sizeof(struct fs_dirent); j++) 
		{
			if (!entries[j].valid) 
			{
				entries[j].valid = 1;
				entries[j].inode = dir_inum;
				strncpy(entries[j].name, dirname, MAX_NAME_LEN);
				entries[j].name[MAX_NAME_LEN - 1] = '\0';
				if (block_write(block, parent_inode.ptrs[i], 1) != 0) 
				{
					return -EIO;
				}
				return 0;
			}
		}
	}
	free_components(components, num_components);
	free_components(resolved_components, resolved_count);
	return -EOPNOTSUPP;
}

/* unlink - delete a file
 *  success - return 0
 *  errors - path resolution, ENOENT, EISDIR
 */
int fs_unlink(const char *path)
{
	uint32_t inum;
	struct fs_inode inode;
	int inoderes = translate(path, &inum, &inode);
	if (inoderes != 0) 
	{
		return inoderes;
	}
	if (S_ISDIR(inode.mode)) 
	{
		return -EISDIR;
	}

	char *components[MAX_PATH_LEN];
	int num_components = pathparse(path, components);

	char *resolved_components[MAX_PATH_LEN];
	int resolved_count = resolve_path(components, num_components, resolved_components);

	char *filename = resolved_components[resolved_count - 1];

	char parent_path[256] = "/";
	for (int i = 0; i < resolved_count - 1; i++) 
	{
		strcat(parent_path, resolved_components[i]);
		if (i < resolved_count - 2) {
			strcat(parent_path, "/");
		}

	}

	uint32_t parent_inum;
	struct fs_inode parent_inode;
	int res = translate(parent_path, &parent_inum, &parent_inode);
	if (res != 0) 
	{
		return res;
	}

	if (!S_ISDIR(parent_inode.mode)) 
	{
		return -ENOTDIR;
	}

	int found = 0;
	for (int i = 0; i < parent_inode.size / FS_BLOCK_SIZE; i++) 
	{
		char block[FS_BLOCK_SIZE];
		if (block_read(block, parent_inode.ptrs[i], 1) != 0) 
		{
			return -EIO;
		}
		struct fs_dirent *entries = (struct fs_dirent *)block;
		for (int j = 0; j < FS_BLOCK_SIZE / sizeof(struct fs_dirent); j++) 
		{
			if (entries[j].valid && strcmp(entries[j].name, filename) == 0)
			{
				entries[j].valid = 0;
				if (block_write(block, parent_inode.ptrs[i], 1) != 0) 
				{
					return -EIO;
				}
				found = 1;
				break;
			}
		}
		if (found) 
		{
			break;
		}
	}

	int num_blocks = ceil((double)inode.size / FS_BLOCK_SIZE);
	for (int i = 0; i < num_blocks; i++) 
	{
		if (inode.ptrs[i]) 
		{
			bit_clear(bitmap, inode.ptrs[i]);
		}
	}
	bit_clear(bitmap, inum);
	if (block_write(bitmap, 1, 1) != 0) 
	{
		return -EIO;
	}

	free_components(components, num_components);
	free_components(resolved_components, resolved_count);
	return 0;
}

/* rmdir - remove a directory
 *  success - return 0
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 */
int fs_rmdir(const char *path)
{
	uint32_t inum;
	struct fs_inode inode;
	int inode_res = translate(path, &inum, &inode);
	if (inode_res != 0) 
	{
		return inode_res;
	}
	if (!S_ISDIR(inode.mode)) 
	{
		return -ENOTDIR;
	}

	char block[FS_BLOCK_SIZE];
	if (block_read(block, inode.ptrs[0], 1) != 0) // directory will have only one block
	{
		return -EIO;
	}
	struct fs_dirent *entries = (struct fs_dirent *)block;
	for (int i = 0; i < FS_BLOCK_SIZE / sizeof(struct fs_dirent); i++) 
	{
		if (entries[i].valid) 
		{
			return -ENOTEMPTY;
		}
	}

	char *components[MAX_PATH_LEN];
	int num_components = pathparse(path, components);

	char *resolved_components[MAX_PATH_LEN];
	int resolved_count = resolve_path(components, num_components, resolved_components);

	char *filename = resolved_components[resolved_count - 1];

	char parent_path[256] = "/";
	for (int i = 0; i < resolved_count - 1; i++) 
	{
		strcat(parent_path, resolved_components[i]);
		if (i < resolved_count - 2) {
			strcat(parent_path, "/");
		}
	}

	uint32_t parent_inum;
	struct fs_inode parent_inode;
	int res = translate(parent_path, &parent_inum, &parent_inode);
	if (res != 0) 
	{
		return res;
	}

	if (!S_ISDIR(parent_inode.mode)) 
	{
		return -ENOTDIR;
	}

	int found = 0;
	for (int i = 0; i < parent_inode.size / FS_BLOCK_SIZE; i++) 
	{
		char block[FS_BLOCK_SIZE];
		if (block_read(block, parent_inode.ptrs[i], 1) != 0) 
		{
			return -EIO;
		}
		struct fs_dirent *entries = (struct fs_dirent *)block;
		for (int j = 0; j < FS_BLOCK_SIZE / sizeof(struct fs_dirent); j++) 
		{
			if (entries[j].valid && strcmp(entries[j].name, filename) == 0)
			{
				entries[j].valid = 0;
				if (block_write(block, parent_inode.ptrs[i], 1) != 0) 
				{
					return -EIO;
				}
				found = 1;
				break;
			}
		}
		if (found) 
		{
			break;
		}
	}

	bit_clear(bitmap, inode.ptrs[0]);
	bit_clear(bitmap, inum);
	if (block_write(bitmap, 1, 1) != 0) 
	{
		return -EIO;
	}
	free_components(components, num_components);
	free_components(resolved_components, resolved_count);
	return 0;
}

/* rename - rename a file or directory
 * success - return 0
 * Errors - path resolution, ENOENT, EINVAL, EEXIST
 *
 * ENOENT - source does not exist
 * EEXIST - destination already exists
 * EINVAL - source and destination are not in the same directory
 *
 * Note that this is a simplified version of the UNIX rename
 * functionality - see 'man 2 rename' for full semantics. In
 * particular, the full version can move across directories, replace a
 * destination file, and replace an empty directory with a full one.
 */
int fs_rename(const char *src_path, const char *dst_path)
{
	uint32_t src_inum;
	struct fs_inode src_inode;
	if (translate(src_path, &src_inum, &src_inode) != 0)
	{
		return -ENOENT;
	}

	char *src_components[MAX_PATH_LEN], *dst_components[MAX_PATH_LEN];
	int src_count = pathparse(src_path, src_components);
	int dst_count = pathparse(dst_path, dst_components);

	char *resolved_srcpath[MAX_PATH_LEN], *resolved_dstpath[MAX_PATH_LEN];
	int resolved_count_src = resolve_path(src_components, src_count, resolved_srcpath);
	int resolved_count_dst = resolve_path(dst_components, dst_count, resolved_dstpath);

	char *src_name = resolved_srcpath[resolved_count_src - 1];
	char *dst_name = resolved_dstpath[resolved_count_dst - 1];

	char parent_src_path[256] = "/";
	char parent_dst_path[256] = "/";

	for (int i = 0; i < resolved_count_src - 1; i++) 
	{
		strcat(parent_src_path, resolved_srcpath[i]);
		if (i < resolved_count_src - 2) {
			strcat(parent_src_path, "/");
		}
	}
	for (int i = 0; i < resolved_count_dst - 1; i++) 
	{
		strcat(parent_dst_path, resolved_dstpath[i]);
		if (i < resolved_count_dst - 2) {
			strcat(parent_dst_path, "/");
		}
	}

	uint32_t src_parent_inum, dst_parent_inum;
	struct fs_inode parent_inode;

	if (translate(parent_src_path, &src_parent_inum, &parent_inode) != 0 ||
			translate(parent_dst_path, &dst_parent_inum, &parent_inode) != 0) {
		return -ENOENT;
	}

	if (src_parent_inum != dst_parent_inum) {
		return -EINVAL;
	}

	if (!S_ISDIR(parent_inode.mode)) 
	{
		return -ENOTDIR;
	}

	struct fs_dirent *src_entry = NULL;
	struct fs_dirent *dst_entry = NULL;

	for (int i = 0; i < parent_inode.size / FS_BLOCK_SIZE; i++) 
	{
		char block[FS_BLOCK_SIZE];
		if (block_read(block, parent_inode.ptrs[i], 1) != 0) 
		{
			fprintf(stderr, "[fs_rename]: block read failed\n");
			return -EIO;
		}
		struct fs_dirent *entries = (struct fs_dirent *)block;
		for (int j = 0; j < FS_BLOCK_SIZE / sizeof(struct fs_dirent); j++) 
		{
			if (!entries[j].valid) 
			{
				continue;
			}
			if (strcmp(entries[j].name, src_name) == 0) 
			{
				src_entry = &entries[j];
			} 
			else if (strcmp(entries[j].name, dst_name) == 0) 
			{
				dst_entry = &entries[j];
			}
		}

		if (src_entry && dst_entry) 
		{
			return -EEXIST;
		}

		if (src_entry && !dst_entry) 
		{
			strncpy(src_entry->name, dst_name, MAX_NAME_LEN); // Rename source entry
			src_entry->name[MAX_NAME_LEN - 1] = '\0'; // Ensure null termination

			if (block_write(block, parent_inode.ptrs[i], 1) != 0) 
			{
				fprintf(stderr, "[fs_rename]: block write failed\n");
				return -EIO;
			}

			free_components(src_components, src_count);
			free_components(dst_components, dst_count);
			free_components(resolved_srcpath, resolved_count_src); 
			free_components(resolved_dstpath, resolved_count_dst);
			return 0;
		}
	}

	return -EOPNOTSUPP;
}

/* chmod - change file permissions
 * utime - change access and modification times
 *         (for definition of 'struct utimebuf', see 'man utime')
 *
 * success - return 0
 * Errors - path resolution, ENOENT.
 */
int fs_chmod(const char *path, mode_t mode)
{
	uint32_t inum;
	struct fs_inode inode;
	int res = translate(path, &inum, &inode);
	if (res != 0) 
	{
		perror("In fs_chmod: translate failed");
		return res;
	}
	inode.mode = (inode.mode & S_IFMT) | (mode & 0777);
	char block[FS_BLOCK_SIZE];
	memcpy(block, &inode, sizeof(inode));

	inode.mtime = time(NULL);

	if (block_write(block, inum, 1) != 0) 
	{
		perror("In fs_chmod: block write failed");
		return -EIO;
	}
	return 0;
}

int fs_utime(const char *path, struct utimbuf *ut)
{
	uint32_t inum;
	struct fs_inode inode;
	int res = translate(path, &inum, &inode);
	if (res != 0) 
	{
		perror("In fs_utime: translate failed");
		return res;
	}
	inode.mtime = ut->modtime;
	// there is no access time in the inode
	char block[FS_BLOCK_SIZE];
	memcpy(block, &inode, sizeof(inode));

	if (block_write(block, inum, 1) != 0) 
	{
		perror("In fs_chmod: block write failed");
		return -EIO;
	}
	return 0;
}

/* truncate - truncate file to exactly 'len' bytes
 * success - return 0
 * Errors - path resolution, ENOENT, EISDIR, EINVAL
 *    return EINVAL if len > 0.
 */
int fs_truncate(const char *path, off_t len)
{
	/* you can cheat by only implementing this for the case of len==0,
	 * and an error otherwise.
	 */
	if (len != 0)
	{
		return -EINVAL;		/* invalid argument */
	}
	uint32_t inum;
	struct fs_inode inode;
	int res = translate(path, &inum, &inode);
	if (res != 0) 
	{
		return res;
	}
	if (S_ISDIR(inode.mode)) 
	{
		return -EISDIR;
	}

	int num_blocks = (int)ceil((double)inode.size / FS_BLOCK_SIZE); // find number of blocks used by the file
	for (int i = 0; i < num_blocks; i++) // clear the blocks used by the file
	{
		if (inode.ptrs[i]) 
		{
			bit_clear(bitmap, inode.ptrs[i]);
		}
	}
	memset(inode.ptrs, 0, sizeof(inode.ptrs)); // clear the pointers to the blocks
	inode.size = 0;
	inode.mtime = time(NULL);	

	char inode_block[FS_BLOCK_SIZE];
	memcpy(inode_block, &inode, sizeof(inode));
	if (block_write(inode_block, inum, 1) != 0) // write the inode back
	{
		return -EIO;
	}
	if (block_write(bitmap, 1, 1) != 0)  // write the bitmap back
	{
		return -EIO;
	}

	return 0;
}

/* read - read data from an open file.
 * success: should return exactly the number of bytes requested, except:
 *   - if offset >= file len, return 0
 *   - if offset+len > file len, return #bytes from offset to end
 *   - on error, return <0
 * Errors - path resolution, ENOENT, EISDIR
 */
int fs_read(const char *path, char *buf, size_t len, off_t offset, struct fuse_file_info *fi)
{
	uint32_t inum;
	struct fs_inode inode;
	int res = translate(path, &inum, &inode);
	if (res != 0) 
	{
		return res;
	}
	if (!S_ISREG(inode.mode)) 
	{
		return -EISDIR;
	}
	if (offset >= inode.size) 
	{
		return 0;
	}
	if (offset + len > inode.size) 
	{
		len = inode.size - offset;
	}

	size_t bytes_read = 0;
	while (bytes_read < len) 
	{
		int block_index = (offset + bytes_read) / FS_BLOCK_SIZE;
		int block_offset = (offset + bytes_read) % FS_BLOCK_SIZE;
		uint32_t block = inode.ptrs[block_index];
		char block_data[FS_BLOCK_SIZE];
		if (block_read(block_data, block, 1) != 0) 
		{
			fprintf(stderr, "[fs_read]: block read failed\n");
			return -EIO;
		}

		size_t copy_len = FS_BLOCK_SIZE - block_offset;
		if (copy_len > len - bytes_read) 
		{
			copy_len = len - bytes_read;
		}
		memcpy(buf + bytes_read, block_data + block_offset, copy_len);
		bytes_read += copy_len;
	}
	return bytes_read;
}

/* write - write data to a file
 * success - return number of bytes written. (this will be the same as
 *           the number requested, or else it's an error)
 * Errors - path resolution, ENOENT, EISDIR
 *  return EINVAL if 'offset' is greater than current file length.
 *  (POSIX semantics support the creation of files with "holes" in them, 
 *   but we don't)
 */
int fs_write(const char *path, const char *buf, size_t len, off_t offset, struct fuse_file_info *fi)
{
	uint32_t inum;
	struct fs_inode inode;
	int inode_res = translate(path, &inum, &inode);
	if (inode_res != 0) 
	{
		return inode_res;
	}
	if (S_ISDIR(inode.mode)) 
	{
		return -EISDIR;
	}
	if (offset > inode.size) 
	{
		return -EINVAL;
	}

	size_t new_size = offset + len;
	uint32_t new_blocks = (uint32_t)ceil((double)new_size / FS_BLOCK_SIZE); // how many blocks are needed for the new size
	uint32_t current_blocks = (uint32_t)ceil((double)inode.size / FS_BLOCK_SIZE); // how many blocks are currently used by the file
	uint32_t new_blocks_needed = new_blocks - current_blocks;
	if (new_blocks_needed < 0) 
	{
		new_blocks_needed = 0;
	}

	if (new_blocks_needed > 0) // if new allocation needed
	{
		for (int i = 0; i < new_blocks_needed; i++) 
		{
			uint32_t new_block = 0;
			for (uint32_t j = 2; j < superblock.disk_size; j++)  // start from 2 to skip superblock and bitmap
			{
				if (!bit_test(bitmap, j)) 
				{
					new_block = j;
					break;
				}
			}

			if (new_block == 0) {
				return -ENOSPC;
			}

			bit_set(bitmap, new_block);
			inode.ptrs[current_blocks + i] = new_block; // assign new block to inode
		}
	}

	size_t bytes_written = 0;
	while (bytes_written < len) 
	{
		int block_index = (offset + bytes_written) / FS_BLOCK_SIZE;
		int block_offset = (offset + bytes_written) % FS_BLOCK_SIZE;
		uint32_t block = inode.ptrs[block_index];
		char block_data[FS_BLOCK_SIZE];
		uint32_t block_len = MIN(len - bytes_written, FS_BLOCK_SIZE - block_offset); // minimum of remaining bytes to write and space remaining in current block

		if (block_offset > 0 || block_len < FS_BLOCK_SIZE) 
		{
			if (block_read(block_data, block, 1) != 0) 
			{
				return -EIO;
			}
		}

		memcpy(block_data + block_offset, buf + bytes_written, block_len); // copy data to the block
		if (block_write(block_data, inode.ptrs[block_index], 1) != 0) 
		{
			return -EIO;
		}

		bytes_written += block_len;
	}

	inode.size = MAX(inode.size, new_size);
	inode.mtime = time(NULL);
	char inode_block[FS_BLOCK_SIZE];
	memcpy(inode_block, &inode, sizeof(inode));
	if (block_write(inode_block, inum, 1) != 0) 
	{
		return -EIO;
	}

	if (new_blocks > current_blocks) {
		if (block_write(bitmap, 1, 1) != 0) return -EIO;
	}
	return bytes_written;
}

/* statfs - get file system statistics
 * see 'man 2 statfs' for description of 'struct statvfs'.
 * Errors - none. Needs to work.
 */
int fs_statfs(const char *path, struct statvfs *st)
{
	/* needs to return the following fields (set others to zero):
	 *   f_bsize = BLOCK_SIZE
	 *   f_blocks = total image - (superblock + block map)
	 *   f_bfree = f_blocks - blocks used
	 *   f_bavail = f_bfree
	 *   f_namemax = <whatever your max namelength is>
	 *
	 * it's OK to calculate this dynamically on the rare occasions
	 * when this function is called.
	 */
	memset(st, 0, sizeof(struct statvfs)); // To zero the structure's other values
	st->f_bsize = FS_BLOCK_SIZE;
	int total_blocks = superblock.disk_size;
	int metadata_blocks = 1 + 1; // 1 for superblock + 1 bitmap block
	st->f_blocks = total_blocks - metadata_blocks;

	int used_blocks = 0;
	for (int i = 0; i < superblock.disk_size; i++) 
	{
		if (bit_test(bitmap, i)) 
		{
			used_blocks++;
		}
	}
	st->f_bfree = st->f_blocks - used_blocks;
	st->f_bavail = st->f_bfree;
	st->f_namemax = MAX_NAME_LEN;
	return 0;
}

/* operations vector. Please don't rename it, or else you'll break things
 */
struct fuse_operations fs_ops = {
	.init = fs_init,            /* read-mostly operations */
	.getattr = fs_getattr,
	.readdir = fs_readdir,
	.rename = fs_rename,
	.chmod = fs_chmod,
	.read = fs_read,
	.statfs = fs_statfs,

	.create = fs_create,        /* write operations */
	.mkdir = fs_mkdir,
	.unlink = fs_unlink,
	.rmdir = fs_rmdir,
	.utime = fs_utime,
	.truncate = fs_truncate,
	.write = fs_write,
};

