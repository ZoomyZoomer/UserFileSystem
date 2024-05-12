/*
 *  Copyright (C) 2024 CS416/CS518 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

struct superblock sb;

char block[BLOCK_SIZE];

// Declare your in-memory data structures here

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	char block[BLOCK_SIZE];

	bio_read(1, block);
	
	// Step 2: Traverse inode bitmap to find an available slot
	int inodeNum = -1;

	for (int i = 0; i < MAX_INUM; i++){

		if (get_bitmap((bitmap_t)block, i) == 0){

			inodeNum = i;
			printf("Found available inode slot: %d\n", i);

			// Set the bit as used
			set_bitmap((bitmap_t)block, i);

			// Write the updated bitmap to the disk
			bio_write(1, block);

			break;
		}

	}
 
	if (inodeNum == -1){
		printf("Failed to locate available Inode!\n");
		exit(1);
	}

	return inodeNum;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	char block[BLOCK_SIZE];

	bio_read(2, block);

	
	// Step 2: Traverse data block bitmap to find an available slot
	int blockNum = -1;

	for (int i = 0; i < MAX_DNUM; i++){

		if (get_bitmap((bitmap_t)block, i) == 0){

			blockNum = i;
			printf("Found available datablock slot: %d\n", i);

			// Set bit
			set_bitmap((bitmap_t)block, i);

			// Write updated bitmap to the disk
			bio_write(2, block);

			break;

		}

	}

	if (blockNum == -1){
		printf("Failed to locate available DataBlock!\n");
		exit(1);
	}

	return blockNum;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

  // Step 1: Get the inode's on-disk block number
  int inode_block_index = ino / (BLOCK_SIZE / sizeof(struct inode));

  // Step 2: Get offset of the inode in the inode on-disk block
  int inode_offset = ino % (BLOCK_SIZE / sizeof(struct inode));

  // Step 3: Read the block from disk and then copy into inode structure
  char block[BLOCK_SIZE];

  // Read superblock
  struct superblock sb;

  bio_read(0, &sb);

  bio_read(sb.i_start_blk + inode_block_index, block);

  memcpy(inode, block + (inode_offset * sizeof(struct inode)), sizeof(struct inode));

  return 0;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
    int inode_block_index = ino / (BLOCK_SIZE / sizeof(struct inode));

    // Step 2: Get the offset in the block where this inode resides on disk
    int inode_offset = ino % (BLOCK_SIZE / sizeof(struct inode));

	// Step 3: Write inode to disk 
	char block[BLOCK_SIZE];

	// Read superblock
	struct superblock sb;

	bio_read(0, &sb);

	// Read existing block to preserve other inodes
	bio_read(sb.i_start_blk + inode_block_index, block);

	memcpy(block + (inode_offset * sizeof(struct inode)), inode, sizeof(struct inode));

	bio_write(sb.i_start_blk + inode_block_index, block);

	// Update the inode bitmap
	if (inode->valid == 1){

		memset(block, 0, BLOCK_SIZE);

		// Read the inode bitmap from the disk
		bio_read(sb.i_bitmap_blk, block);

		// Set corresonpding bit
		set_bitmap((bitmap_t)block, ino);

		// Write the updated bitmap to the disk
		bio_write(sb.i_bitmap_blk, block);
	}

	

	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)
  struct inode inode;
  readi(ino, &inode);

  // Step 2: Get data block of current directory from inode
  int numBlocks = (inode.size + BLOCK_SIZE - 1) / BLOCK_SIZE;

  // If there exits datablocks already..
  bio_read(0, &sb);

  for (int i = 0; i < numBlocks; i++){

	bio_read(sb.d_start_blk + inode.direct_ptr[i], block);

	// Iterate through the dirents of the block
	struct dirent *entry = (struct dirent *)block;
	int numEntries = BLOCK_SIZE / sizeof(struct dirent);

	for (int j = 0; j < numEntries; j++){

		// If the name matches
		if (entry[j].valid == 1 && strncmp(entry[j].name, fname, name_len) == 0){

			memcpy(dirent, &entry[j], sizeof(struct dirent));
			return 0;

		}

	}

  }




  printf("ERORR: dir_find() could not find the specified directory!\n");
	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Read this inode in
	struct inode dirInode;
	readi(dir_inode.ino, &dirInode);

	int blockNum = -1;
	int dirFound = 0;

	int numDirs = BLOCK_SIZE / sizeof(struct dirent);

	bio_read(0, &sb);

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	int numBlocks = (dirInode.size + BLOCK_SIZE - 1) / BLOCK_SIZE;

	// If there exists atleast 1 datablock
	for (int i = 0; i < numBlocks; i++){

		memset(block, 0, BLOCK_SIZE);
		bio_read(sb.d_start_blk + dirInode.direct_ptr[i], block);

		struct dirent *entry = (struct dirent *)block;

		// Check for a duplicate directory name
		for (int j = 0; j < numDirs; j++){

			if (entry[j].valid == 1 && strncmp(entry[j].name, fname, name_len) == 0){

				printf("ERROR: Duplicate directory name!\n");
				return -1;
				exit(1);

			}

			// Find free datablock space to put a dirent in for later
			if (entry[j].valid == 0 && dirFound == 0){

				blockNum = dirInode.direct_ptr[i];
				dirFound = 1;

			}

		}

	}

	// If an open spot for a new dirent has not been found
	if (dirFound == 0){

		// Create a datablock
		blockNum = get_avail_blkno();

		memset(block, 0, BLOCK_SIZE);

		struct dirent invalidDirent = {0};
		
		// Just to make sure
		invalidDirent.valid = 0;

		int numDirents = BLOCK_SIZE / sizeof(struct dirent);

		// Fill the datablock with invalid dirents
		for (int i = 0; i < numDirents; i++){

			memcpy(block + (i * sizeof(struct dirent)), &invalidDirent, sizeof(struct dirent));

		}

		// Write the new datablock to the disk
		bio_write(sb.d_start_blk + blockNum, block);

		// Update direct pointers of the inode and increase its size
		int numPointers = 16;

		for (int i = 0; i < numPointers; i++){

			// If the pointer has not been initialized yet
			if (dirInode.direct_ptr[i] == -1) {

				dirInode.direct_ptr[i] = blockNum;
				dirInode.size += BLOCK_SIZE;
				time(&(dirInode.vstat.st_mtime));

				break;

			}

		}

		// Write the updated inode back to the disk
		writei(dirInode.ino, &dirInode);
	

	}

	// Now lets add a dirent to the datablock

		// Read the datablock
		memset(block, 0, BLOCK_SIZE);
		bio_read(sb.d_start_blk + blockNum, block);

		// Create the dirent we want to add
		struct dirent newEntry;
		newEntry.ino = f_ino;
		newEntry.valid = 1;
		strncpy(newEntry.name, fname, name_len);
		newEntry.len = name_len;

		struct dirent *entry = (struct dirent *)block;

		// Find an invalid dirent to replace with the new dirent entry
		for (int i = 0; i < numDirs; i++){

			// If the dirent is invalid
			if (entry[i].valid == 0){

				memcpy(block + (i * sizeof(struct dirent)), &newEntry, sizeof(struct dirent));
				break;

			}

		}	

		// Write the datablock back to the disk
		bio_write(sb.d_start_blk + blockNum, block);

	return 0;
}

// Required for 518
int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	char *pathCopy = strdup(path);

	// Start with the root inode
	uint16_t currentIno = ino;

	// Split the path;
	char *saveptr;
	char *section = strtok_r(pathCopy, "/", &saveptr);

	printf("SECTION: %s\n", section);

	struct inode currentInode;

	// If the parent path is just the root path, ('/')
	if (section == NULL){

		printf("ROOT IS /\n");

		struct inode inodexD;
		readi(0, &inodexD);

		memcpy(inode, &inodexD, sizeof(struct inode));

		return 0;

	}

	while (section != NULL){

		struct dirent dirent;

		// Find the inode for the section
		if (dir_find(currentIno, section, strlen(section), &dirent) < 0){
			printf("ERROR: Could not find Inode for section: %s", section);
			return -1;
		}

		currentIno = dirent.ino;

		// Save most recent Inode
		readi(currentIno, &currentInode);

		// Next section
		section = strtok_r(NULL, "/", &saveptr);

	}

	// After the while loop is finished, memcpy the final inode into the struct
	memcpy(inode, &currentInode, sizeof(struct inode));


	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);



	// Write superblock information
	struct superblock sb;
	sb.magic_num = MAGIC_NUM;
	sb.max_inum = MAX_INUM;
	sb.max_dnum = MAX_DNUM;
	sb.i_bitmap_blk = 1;
	sb.d_bitmap_blk = 2;
	sb.i_start_blk = 3;
	sb.d_start_blk = sb.i_start_blk + ceil((MAX_INUM * sizeof(struct inode)) / BLOCK_SIZE);

	char block[BLOCK_SIZE];

	// Copy superblock info to the block
	memcpy(block, &sb, sizeof(struct superblock));

	// Write the block into the disk
	bio_write(0, block);


	// initialize inode bitmap
	bitmap_t i_bitmap = calloc((MAX_INUM + 7) / 8, sizeof(char));

	//Set root inode as taken
	set_bitmap(i_bitmap, 0);

	memset(block, 0, BLOCK_SIZE);
	memcpy(block, i_bitmap, (MAX_INUM + 7) / 8);

	// Write it to the disk
	bio_write(sb.i_bitmap_blk, block);

	free(i_bitmap);

	// initialize data block bitmap
	bitmap_t d_bitmap = calloc((MAX_DNUM + 7) / 8, sizeof(char));

	memset(block, 0, BLOCK_SIZE);
    memcpy(block, d_bitmap, (MAX_DNUM + 7) / 8);

	// Write it to the disk
	bio_write(sb.d_bitmap_blk, block);

	free(d_bitmap);



	// Update inode for root directory
	struct inode root_inode;
	root_inode.ino = 0;
	root_inode.valid = 1;
	root_inode.size = 0;
	root_inode.type = S_IFDIR;
	root_inode.link = 1;
	time(&(root_inode.vstat.st_mtime));
	memset(root_inode.direct_ptr, -1, sizeof(root_inode.direct_ptr));
    memset(root_inode.indirect_ptr, -1, sizeof(root_inode.indirect_ptr));

	// Write it to the disk
	memset(block, 0, BLOCK_SIZE);
	memcpy(block, &root_inode, sizeof(struct inode));

	bio_write(sb.i_start_blk, block);


	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
	if (dev_open(diskfile_path) == -1){
		rufs_mkfs();
	}

  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk

  	bio_read(0, &sb);

	char block[BLOCK_SIZE];

	if (bio_read(0, block) < 0) {
        perror("Failed to read superblock");
        exit(EXIT_FAILURE);
    }
	return NULL;
}

static void rufs_destroy(void *userdata) {

	// No in-memory data structures
	
	// Step 2: Close diskfile
	dev_close();

}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path
	struct inode inode;
	if (get_node_by_path(path, 0, &inode) == -1){
		return -ENOENT;
	}

	// Step 2: fill attribute of file into stbuf from inode

		stbuf->st_size = inode.size;
		stbuf->st_uid = getuid();
		stbuf->st_gid = getgid();

		if (inode.type == S_IFREG){
			stbuf->st_mode   = S_IFREG;
		} else {
			stbuf->st_mode   = S_IFDIR;
		}

		stbuf->st_nlink  = inode.link;
		time(&stbuf->st_mtime);

	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
    struct inode dir_inode;

    // Step 1: Get the inode for the given path
    if (get_node_by_path(path, 0, &dir_inode) < 0) {
        // If the path does not exist or an error occurred, return -ENOENT (No such file or directory)
        return -ENOENT;
    }
	
	// if it's a file and NOT a directory
	if (dir_inode.type == S_IFREG){

		return -ENOTDIR;

	}

    // If everything checks out, return 0 indicating success
    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    struct inode dir_inode;
    
    // Step 1: Get the inode for the given path

    if (get_node_by_path(path, 0, &dir_inode) < 0) {
        return -ENOENT; // Path does not exist
    }

    // Step 2: Read the data blocks for the directory and use filler to add the entries to the FUSE context

    int num_blocks = (dir_inode.size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    for (int i = 0; i < num_blocks; i++) {

        bio_read(sb.d_start_blk + dir_inode.direct_ptr[i], block);

        struct dirent *entry = (struct dirent *)block;

        for (int j = 0; j < 16; j++) {
            if (entry[j].valid) {
                if (filler(buffer, entry[j].name, NULL, 0) != 0) {
                    return -ENOMEM;
                }
            }
        }
    }

    return 0;
}

static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char *pathCopy = strdup(path);
    char *pathCopy2 = strdup(path);

	char* myDirName = dirname(pathCopy);
	char* myBaseName = basename(pathCopy2);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode parentInode;

	printf("root: %s\n", myDirName);
	printf("target: %s\n", myBaseName);

	get_node_by_path(myDirName, 0, &parentInode);


	// Step 3: Call get_avail_ino() to get an available inode number
	uint16_t newIno = get_avail_ino();

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	dir_add(parentInode, newIno, myBaseName, strlen(myBaseName));

	// Step 5: Update inode for target directory
	struct inode newInode = {0};
	newInode.valid = 1;
	newInode.ino = newIno;
	newInode.type = S_IFDIR;
	time(&(newInode.vstat.st_mtime));
	
	memset(newInode.direct_ptr, -1, sizeof(newInode.direct_ptr));

	// Step 6: Call writei() to write inode to disk
	writei(newIno, &newInode);

	

	return 0;
}

// Required for 518
static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char *pathCopy = strdup(path);
    char *pathCopy2 = strdup(path);

	char* myDirName = dirname(pathCopy);
	char* myBaseName = basename(pathCopy2);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode parentInode;
	get_node_by_path(myDirName, 0, &parentInode);

	// Step 3: Call get_avail_ino() to get an available inode number
	uint16_t newIno = get_avail_ino();

	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	dir_add(parentInode, newIno, myBaseName, strlen(myBaseName));

	// Step 5: Update inode for target file
	struct inode newInode = {0};
	memset(&newInode, 0, sizeof(struct inode));
	newInode.ino = newIno;
	newInode.valid = 1;
	newInode.type = S_IFREG;
	time(&(newInode.vstat.st_mtime));

	memset(newInode.direct_ptr, -1, sizeof(newInode.direct_ptr));

	int newBlockNum = get_avail_blkno();

	newInode.direct_ptr[0] = newBlockNum;

	// Step 6: Call writei() to write inode to disk
	writei(newIno, &newInode);

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	struct inode inode;

	// Step 1: Call get_node_by_path() to get inode from path
	if (get_node_by_path(path, 0, &inode) == -1){
		// Step 2: If not find, return -1
		return -1;
	}

	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode fileNode;

	get_node_by_path(path, 0, &fileNode);

	// Step 2: Based on size and offset, read its data blocks from disk
	size_t bytesToRead = size;

	if (offset + size > fileNode.size) {
		bytesToRead = fileNode.size - offset;
	}

	size_t blockOffset = offset / BLOCK_SIZE;
	size_t byteOffset = offset % BLOCK_SIZE;
	size_t bytesRead = 0;

	// Read from the datablock(s)
	while (bytesToRead > 0){

		memset(block, 0, BLOCK_SIZE);
		bio_read(sb.d_start_blk + fileNode.direct_ptr[blockOffset], block);

		// Calculate how much data we can copy
		size_t bytesFromBlock = BLOCK_SIZE - byteOffset;

		if (bytesFromBlock > bytesToRead){

			// Only read the remaining bytes
			bytesFromBlock = bytesToRead;

		}

		// Copy data from the block to the buffer
		memcpy(buffer + bytesRead, block + byteOffset, bytesFromBlock);

		bytesRead += bytesFromBlock;
		bytesToRead -= bytesFromBlock;

		blockOffset++;

		// Since only the first block read will use the offset
		byteOffset = 0;

	}
	
	return bytesRead;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode fileNode;

	get_node_by_path(path, 0, &fileNode);

	// Step 2: Based on size and offset, read its data blocks from disk
	if (offset > fileNode.size){
		fileNode.size = offset;
	}

	size_t bytesWritten = 0;
	size_t remainingSize = size;
	size_t currentOffset = offset;

	while (remainingSize > 0){

		size_t blockIndex = currentOffset / BLOCK_SIZE;
		size_t blockOffset = currentOffset % BLOCK_SIZE;

		// Check if this datablock is already allocated
		if (fileNode.direct_ptr[blockIndex] == -1){

			// If not, allocate one
			int newBlock = get_avail_blkno();
			fileNode.direct_ptr[blockIndex] = newBlock;

		}		

		int diskBlockNum = fileNode.direct_ptr[blockIndex];

		bio_read(sb.d_start_blk + diskBlockNum, block);

		size_t bytesToWrite = BLOCK_SIZE - blockOffset;

		if (bytesToWrite > remainingSize) {

			bytesToWrite = remainingSize;

		}

		memcpy(block + blockOffset, buffer + bytesWritten, bytesToWrite);
		
		bio_write(sb.d_start_blk + diskBlockNum, block);

		bytesWritten += bytesToWrite;
		remainingSize -= bytesToWrite;
		currentOffset += bytesToWrite;

	}

	// Update the inode info and write it to disk
	fileNode.size = currentOffset;
	time(&(fileNode.vstat.st_mtime));

	writei(fileNode.ino, &fileNode);


	return bytesWritten;
}

// Required for 518

static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

