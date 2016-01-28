/*
 * Author: Andrii Hlyvko
  Copyright (C) 2015 CS416/CS516

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.
*/
#include <stdint.h>
#include <sys/stat.h>

#define SFS_N_DATA_BLOCKS	21	// single indirection number of blocks
#define SFS_NAME_LEN		248	// name length of directories and files
#define SFS_MAX_OPEN_FILES	128
#define BITMAP_BLOCK_SIZE	512

#define GROUP_NUM		16	// total number of groups
//#define GROUP_BLOCKS		2	// number of group blocks
#define INODE_TABLE_SIZE	(512*8)	// number of inodes in one group
#define INODE_BLOCKS		1024 
#define DATA_BLOCKS		(512*8)
#define ROOT_INODE_NUM		1

// access rights
#define RUSR	0x0100
#define WUSR	0x0080
#define XUSR	0x0040
#define RGRP	0x0020
#define WGRP	0x0010
#define XGRP	0x0008
#define ROTH	0x0004
#define WOTH	0x0002
#define XOTH	0x0001

// filetypes
#define SFS_SOCKET		0xC000
#define SFS_SYMBOLIC_LINK	0xA000
#define SFS_FILE		0x8000
#define SFS_BLOCK_DEVICE	0x6000
#define SFS_DIRECTORY		0x4000
#define SFS_CHAR_DEVICE		0x2000
#define SFS_FIFO		0x1000

#define SOCKET_TYPE		6
#define SYMBOLIC_TYPE		7
#define FILE_TYPE		1
#define BLOCK_DEVICE_TYPE	4
#define DIRECTORY_TYPE		2
#define CHAR_DEVICE_TYPE	3
#define UNKNOWN_TYPE		0
#define	PIPE_TYPE		5	

#ifndef _BLOCK_H_
#define _BLOCK_H_

#define BLOCK_SIZE 512

void disk_open(const char* diskfile_path);
void disk_close();
int block_read(const int block_num, void *buf);
int block_write(const int block_num, const void *buf);

#endif

// should be padded to 512 bytes
/*
* s_state
* 0:	filesystem is mounted 
* 1:	filesystem was cleanly unmounted
* 2:	filesystem has errors
*/
struct my_super_block {
uint32_t	s_inodes_count;		// total number of inodes
uint32_t	s_blocks_count;		// size of the filesystem in blocks = BLOCK_SIZE bytes * 8bits/byte *BLOCK_SIZE= 
uint32_t	s_free_inodes_count;	//number of free inodes
uint32_t	s_free_blocks_count;	// number of free blocks
uint32_t	s_first_data_block;	// number of first data block
uint32_t	s_blocks_per_group;	// number of blocks per group
uint32_t	s_inodes_per_group;	// number of inodes per group
uint32_t	s_first_ino;		// number of first non reserved inode
uint16_t	s_state;		// status flag
uint16_t	s_inode_size;		// size of the inode structure
uint16_t	s_block_group_nr;	// block group number of this superblock
uint16_t	s_word_padding;		// word padding
char		s_volume_name[16];	// volume name
char		s_last_mounted[64];	// last mounting point
uint32_t	s_padding[98];		// padding to BLOCK_SIZE
};

// data blocks of directory store file names with corresponding inode numbers 
// should be 128 bytes
/*
*	0	Unknown
*	1	Regular file
*	2	Directory
*	3	Character Device
*	4	Block Device
*	5	Named Pipe
*	6	Socket
*	7	Symbolic Link
*/
struct my_inode {
uint32_t i_ino;			// inode number 
uint8_t i_mode;		// file type plus access rights
uint16_t i_uid;			// owner id
uint16_t i_gid;			// user group id
uint16_t i_links_count;		// number of hard links
uint32_t i_size;		// file length in bytes
uint32_t i_atime;		// time of last access
uint32_t i_mtime;		// time of last modification
uint32_t i_ctime;		// time of last status change
uint32_t i_blocks;		// number of data blocks
uint32_t i_flags;
uint32_t i_file_acl;		// file access control list
uint32_t i_dir_acl;		// directory acces control list
uint32_t i_block[SFS_N_DATA_BLOCKS];	// array of data block pointers	
};

struct my_group_desc {
uint32_t bg_block_bitmap;	// block number of block bitmap
uint32_t bg_inode_bitmap;	// block number of inode bitmap
uint32_t bg_inode_table;	// block number of first inode table block
uint16_t bg_free_block_count;	// number of free blocks in group
uint16_t bg_free_inode_count;	// number of free inodes in group
uint32_t bg_used_dirs_count;	// numebr of directories in the group
uint32_t bg_padding[3];		// padding
};

// Data blocks of a directory contain dir_entry objects
// is of variable length and should be a multiple of 4 with max size of 256
struct my_dir_entry {
uint32_t inode;			// inode number
uint16_t rec_len;		// directory entry length
uint8_t	name_len;		// filename length
uint8_t	file_type;		// file type
char name[SFS_NAME_LEN];	// file name	
};

struct my_file {
int f_count;
unsigned int f_flags;
mode_t f_mode;
off_t f_pos;
unsigned int f_uid;
unsigned int f_gid;
int f_inode;
};

void super_block_init(struct my_super_block *sb);

unsigned char * create_bitmap(int size);

void free_bitmap(unsigned char * map);

void bitmap_set(unsigned char *map, int n, int size);

void bitmap_clear(unsigned char *map, int n, int size);

void or_bitmap(unsigned char *map1, unsigned char *map2, int size);

int get_bitmap(unsigned char *map, int n, int size);

int find_free_bit(unsigned char *map, int size);


/*
this function gets the directory entry block of a directory. it will 
look if there is a directory with a given name and output the inode number 
*/
int name_for_inode(char *tok, uint8_t *block);


/*
read the inode into memory given by the inode_number. The function fills out the *ino
*/
void read_inode(int inode_num, struct my_group_desc *cache, struct my_inode *ino);

void write_inode(struct my_group_desc *gr_cache, struct my_inode *ino);

//void free_inode(int inode_num, struct my_group_desc *cache, struct my_);
void free_data_block(int data_block, struct my_group_desc *gr_cache);
/*
read the inode into memory using a path. fills out the struct given by *ino
the last entry of the path can be a file or a directory.
*/
void read_inode_path(char * path,int max_path, struct my_group_desc *gr_cache, struct my_inode * ino, struct my_inode *root, struct my_inode *current);

int add_entry_to_dir(struct my_inode *parent, struct my_group_desc *gr_cache, struct my_dir_entry *dir);

int add_dir_to_block(struct my_dir_entry *dir, uint8_t *block);







