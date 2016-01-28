/*
  Simple File System

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.

*/
/*
Andrii Hlyvko
tested on kill.cs.rutgers.edu
*/
#include "params.h"
//#include "block.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"
//#include "bitmap.h"
#include <time.h>

unsigned int openedFd[1024];


struct fuse_context *fc;
struct my_super_block *superblock;
struct my_inode *root;
struct my_inode *current_dir;
struct my_group_desc groups[GROUP_NUM];

struct my_inode i_cache[4]; // inodes in one block
struct my_group_desc gr_cache[GROUP_NUM]; 
uint8_t data_cache[BLOCK_SIZE];

unsigned char *bitmap_cache; 

struct my_file fds[SFS_MAX_OPEN_FILES];
unsigned char *f_bitmap;

///////////////////////////////////////////////////////////
//
// Prototypes for all these functions, and the C-style comments,
// come indirectly from /usr/include/fuse.h
//

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
void *sfs_init(struct fuse_conn_info *conn)
{
	int i=0;
	fprintf(stderr, "in bb-init\n");
	log_msg("\nsfs_init()\n");    
	log_conn(conn);
	log_fuse_context(fuse_get_context());

	disk_open(SFS_DATA->diskfile);  // open the disk file
	
	log_msg("Opened the disk file: %s\n",SFS_DATA->diskfile);
	fc = fuse_get_context();
	f_bitmap=create_bitmap(BLOCK_SIZE);
	bitmap_cache  = create_bitmap(BLOCK_SIZE);
	superblock = (struct my_super_block *)malloc(sizeof(struct my_super_block));
	root = (struct my_inode *)malloc(sizeof(struct my_inode));
	current_dir=root;
	int readn=block_read(0,(void *)superblock);
	if(readn==0)
	{
		//log_msg("Could not read superblock.Have to create sfs\n");
		//printf("Could not read superblock.Have to create sfs\n");
		super_block_init(superblock);
		// change superblock
		superblock->s_free_inodes_count--;
		superblock->s_free_blocks_count-=(1+2+1+1+1+1);//superblock+group blocks + inode bitmap + data bitmap + inode talbe block + one data block
		superblock->s_free_blocks_count-=(GROUP_NUM-1)*(2); // minus all the bitmaps
		//superblock->s_free_blocks_count--;
		superblock->s_first_ino=2;
		strncpy((superblock->s_last_mounted),SFS_DATA->diskfile,strlen(SFS_DATA->diskfile));
		block_write(0, (void *)superblock);
		bzero(superblock, sizeof(struct my_super_block));
		block_read(0, (void*)superblock);
		log_msg("Newly created superblock:\n");
		log_superblock(superblock);

		
		// create group descriptors
		for(i=0;i<GROUP_NUM;i++)
		{
			init_group(&gr_cache[i], i);
			//log_group_descriptor(&gr_cache[i]);
		}
		gr_cache[0].bg_free_block_count--;
		gr_cache[0].bg_free_inode_count--;
		gr_cache[0].bg_used_dirs_count=1;
	
		int k=1;
		for(i=0; i < GROUP_NUM;i+=2)
		{
			block_write(k, (void *)&gr_cache[i]);
			k++;
		}
		
		//k=1;
		//for(i=0; i < GROUP_NUM;i+=2)
		//{
		//	block_read(k, (void *)&gr_cache[i]);
		//	k++;
		//}

		//for(i=0;i<GROUP_NUM;i++)
		//{
		//	init_group(&gr_cache[i], i);
		//	log_group_descriptor(&gr_cache[i]);
		//}

		// create all bitmaps
		unsigned char *bitmap = create_bitmap(BITMAP_BLOCK_SIZE);
		bitmap_set(bitmap, 1, BITMAP_BLOCK_SIZE);
		bitmap_set(bitmap, 0 , BITMAP_BLOCK_SIZE);
		//bitmap_set(bitmap,10, BITMAP_BLOCK_SIZE);
		block_write(gr_cache[0].bg_inode_bitmap, (void*)bitmap);
		bitmap_clear(bitmap,1, BITMAP_BLOCK_SIZE);
		bitmap_set(bitmap,0,BITMAP_BLOCK_SIZE);
		block_write(gr_cache[0].bg_block_bitmap,(void *) bitmap);
		bitmap_clear(bitmap, 0, BITMAP_BLOCK_SIZE);
		for(i=1;i<(GROUP_NUM);i++)
		{
			block_write(gr_cache[i].bg_block_bitmap,(void *) bitmap);
			block_write(gr_cache[i].bg_inode_bitmap, (void*)bitmap);
		}

		//create root directory
		root->i_ino = 1;
		root->i_mode = DIRECTORY_TYPE | fc->umask;
		root->i_uid = fc->uid;
		root->i_size = 0;
		root->i_gid = fc->gid;
		root->i_links_count = 2;
		root->i_blocks = 1;
		bzero(root->i_block,SFS_N_DATA_BLOCKS*sizeof(uint32_t));
		root->i_block[0] = 1+(GROUP_NUM/2)+1+1+INODE_BLOCKS; // first data block
		root->i_atime=time(NULL);
		root->i_ctime=time(NULL);
		root->i_mtime=time(NULL);
		memcpy(&i_cache[1], root, sizeof(struct my_inode));

		// write root inode
		memcpy(&i_cache[1], root, sizeof(struct my_inode));
		block_write(gr_cache[0].bg_inode_table, (void *)&i_cache[0]);
		bzero(i_cache, 4*sizeof(struct my_inode));
		block_read(gr_cache[0].bg_inode_table, (void*)&i_cache[0]);
		//log_msg("newly created root inode at block %d\n",gr_cache[0].bg_inode_table);
		//log_inode(&i_cache[1]);

		//create dir entry for root
		bzero(data_cache, BLOCK_SIZE*sizeof(uint8_t));
		struct my_dir_entry temp;
		bzero((void *)&temp, sizeof(struct my_dir_entry));
		temp.inode=1;
		temp.rec_len=12;
		temp.name_len =1;
		temp.file_type=2;
		temp.name[0] = '.';
		memcpy((void *)data_cache, (const void *)&temp, temp.rec_len*sizeof(uint8_t));

		temp.name_len = 2;
		temp.name[1] = '.';
		memcpy((void *)(data_cache+temp.rec_len), (const void *)&temp, temp.rec_len*sizeof(uint8_t));	
		block_write(root->i_block[0], data_cache);
		//log_block(data_cache);
	}
	else if(readn>0)
	{
		log_msg("System was already created\n");
		//printf("System was already created\n");
		//log_msg("old superblock:\n");
		//log_superblock(superblock);
		//log_msg("old group descriptors\n");
		int i=0,k=1;
		for(i=0; i < GROUP_NUM;i+=2)
		{
			block_read(k, (void *)&gr_cache[i]);
			k++;
		}
		//for(i=0;i<GROUP_NUM;i++)
		//{
		//	log_group_descriptor(&gr_cache[i]);
		//}

		// read root inode
		
		bzero(i_cache, 4*sizeof(struct my_inode));
		block_read(gr_cache[0].bg_inode_table, (void*)&i_cache[0]);
		//log_msg("old root inode from block %d\n",gr_cache[0].bg_inode_table);
		//if(root==NULL)
		//	log_msg("root was null\n");
		root = (struct my_inode *)malloc(sizeof(struct my_inode));
		memcpy(root,(const void *)&i_cache[1], sizeof(struct my_inode));
		//log_msg("got here\n");
		//log_inode(&i_cache[0]);
		//log_inode(root);
		
		//bcopy(i_cache, root, sizeof(struct my_inode));
		//log_msg("The root block data %d\n",root->i_block[0]);
		//log_inode(&i_cache[0]);
		//log_inode(&i_cache[1]);
		//log_inode(&i_cache[2]);
		//log_inode(&i_cache[3]);
		current_dir=root;

		// check group 0 bitmap
		block_read(gr_cache[0].bg_inode_bitmap, data_cache);
		log_bitmap(data_cache, BLOCK_SIZE);

		// check the root directory		
		//bzero(data_cache, BLOCK_SIZE*sizeof(uint8_t));
		//block_read(root->i_block[0], data_cache);
		//log_dir_entry_block(data_cache);

		//log_msg("create mkdir test\n");
		//log_msg("mkdir\n");
		//mode_t m=777;

		//struct fuse_file_info fi;
		//char *p="/new";
		//char *nf="/file.txt";
		//sfs_create(nf,m,&fi);
		//sfs_create(p, m, &fi);
		//sfs_mkdir("/directory",m);
		//sfs_mkdir("/directory/test",m);
		//char *n="/nFile";
		//sfs_create(p, n, &fi);

		//log_msg("check after create:\n");
		//log_msg("---------------------------------------------\n");
	
		//struct my_inode *n=(struct my_inode*)malloc(sizeof(struct my_inode));
		
		//log_msg("parent inode\n");
		//log_inode(root);		
		
		//read_inode(2, gr_cache, n);
		//log_msg("new inode:\n");
		//log_inode(n);
		//read_inode(3, gr_cache, n);
		//log_inode(n);
		//block_read(n->i_block[0], data_cache);
		//log_dir_entry_block(data_cache);
		
		//log_msg("group 0:\n");
		//log_group_descriptor(&gr_cache[0]);
		
		//log_msg("group 0 inode bitmap\n");
		//block_read(gr_cache[0].bg_inode_bitmap, data_cache);
		//log_bitmap(data_cache, BLOCK_SIZE);
		//log_msg("dir entry of parent\n");
		//bzero(data_cache, BLOCK_SIZE*sizeof(uint8_t));
		//block_read(root->i_block[0], data_cache);
		//log_dir_entry_block(data_cache);
		//log_block(data_cache);

		

		//log_msg("bitmap test\n");
		//bzero(data_cache, BLOCK_SIZE*sizeof(uint8_t));	
		//bitmap_set(0, data_cache, BLOCK_SIZE);
		//log_bitmap(data_cache, BLOCK_SIZE);

		//bitmap_set(2, data_cache,BLOCK_SIZE);
		//log_bitmap(data_cache, BLOCK_SIZE);

		//free(n);
		
	}
	else
	{
		log_msg("Error: Could not read the superblock");
		//perror("Could not read the superblock\n");
		exit(EXIT_FAILURE);
	}			

	log_msg("sfs_init end\n");

	return SFS_DATA;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void sfs_destroy(void *userdata)
{
	printf("sfs_destroy called\n");
	log_msg("\nsfs_destroy(userdata=0x%08x)\n", userdata);
	block_write(0, superblock);
	// write group descriptors
	int k=1,i=0;
	for(i=0; i < GROUP_NUM;i+=2)
	{
		block_write(k, (void *)&gr_cache[i]);
		k++;
	}

	// close the disk
	free(superblock);
	free_bitmap(f_bitmap);
	free_bitmap(bitmap_cache);
	if(current_dir!=root)
		free(current_dir);
	free(root);
	disk_close();
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int sfs_getattr(const char *path, struct stat *statbuf)
{
	int retstat = 0;
	char fpath[PATH_MAX];
    
	log_msg("\nsfs_getattr(path=\"%s\", statbuf=0x%08x)\n",
	  path, statbuf);

	strcpy(fpath, path);

	struct my_inode *tmp=(struct my_inode*)malloc(sizeof(struct my_inode));
	bzero(tmp, sizeof(struct my_inode));
	
	read_inode_path(fpath, PATH_MAX, gr_cache, tmp, root, current_dir);

	if(tmp->i_ino <= 0)
		return -1;


	// fill out the stat
	statbuf->st_ino = tmp->i_ino;
	statbuf->st_mode = tmp->i_mode;
	statbuf->st_nlink = tmp->i_links_count;
	statbuf->st_uid=tmp->i_uid;
	statbuf->st_gid=tmp->i_gid;
	statbuf->st_size=tmp->i_size;
	statbuf->st_blksize=BLOCK_SIZE;
	statbuf->st_blocks=tmp->i_blocks;
	statbuf->st_atime=tmp->i_atime;
	statbuf->st_mtime=tmp->i_mtime;
	statbuf->st_ctime=tmp->i_ctime;

    	free(tmp);

	log_stat(statbuf);
	log_msg("sfs_stat end\n");
	return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int sfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int retstat = 0;
	log_msg("\nsfs_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n", path, mode, fi);
    
	int inode_number =0;

	if(superblock->s_free_inodes_count -1 == 0)
		return -1;

	struct my_inode *tmp = (struct my_inode*)malloc(sizeof(struct my_inode));
	struct my_inode *i_parent = (struct my_inode *)malloc(sizeof(struct my_inode));
	
	bzero(tmp, sizeof(struct my_inode));
	
	read_inode_path((char *)path, PATH_MAX, gr_cache, tmp, root, current_dir);
	if(tmp->i_ino > 0)
	{
		//log_msg("inode already exists");
		//log_inode(tmp);
		// inode already exists. If it is a file open it 
		if(tmp->i_mode && SFS_FILE)
		{
			sfs_open(path, fi);
		}
		free(tmp);
		free(i_parent);
		return 0;
	}
	char path_parent[PATH_MAX];
	char file_name[SFS_NAME_LEN];
	if(path[0]!='/' && path[0]!='.')
	{
		path_parent[0]='.';
		path_parent[1]='/';
		strcpy(path_parent+2, path);
	}
	else
		strcpy(path_parent, path);
	char *current;
	int pos=-1;
	int i=0;
	for(current = path_parent; *current !='\0'; current++,i++)
	{
		if(*current == '/')
			pos=i;
	}

	strcpy(file_name, path_parent+pos+1);
	path_parent[pos+1]='\0';
	//log_msg("path of parent:%s\n",path_parent);
	//log_msg("new file name:%s\n",file_name);
	read_inode_path(path_parent, PATH_MAX, gr_cache, i_parent, root, current_dir);
	//log_msg("parent inode\n");
	//log_inode(i_parent);

	//log_msg("parent block:\n");
	block_read(i_parent->i_block[0],(void*) data_cache);
	//log_dir_entry_block(data_cache);
	if(i_parent->i_ino <=0 || !(i_parent-> i_mode && SFS_DIRECTORY))
	{	
		free(tmp);
		free(i_parent);
		return -1;
	}

	int free_inode_num =0;
	int gr_i =0;
	for(i=0; i< GROUP_NUM; i++)
	{
		block_read(gr_cache[i].bg_inode_bitmap, data_cache);
		free_inode_num = find_free_bit(data_cache, BLOCK_SIZE);

		if(free_inode_num > 0)
		{
			gr_i = i;
		}
		

		if(free_inode_num >0)
			break;
	}

	if(free_inode_num <= 0 )
	{
		free(tmp);
		free(i_parent);
		return -1;
	}
	
	superblock->s_free_inodes_count--;
	block_write(0, (void *)superblock);

	// group descriptors
	gr_cache[gr_i].bg_free_inode_count--;
	int k=1+(gr_i/2);
	if(gr_i % 2 == 0)
		block_write(k, (void *)&gr_cache[gr_i]);
	else
		block_write(k, (void *)&gr_cache[gr_i-1]);
	//if(gr_i % 2 == 0)
	//	block_read(k, (void *)&gr_cache[gr_i]);
	//else
	//	block_read(k, (void *)&gr_cache[gr_i-1]);

	// set bitmap in the appropriate group
	//log_msg("setting bit %d in inode bitmap from group %d. block %d\n",free_inode_num, gr_i, gr_cache[gr_i].bg_inode_bitmap);
	block_read(gr_cache[gr_i].bg_inode_bitmap, (void *)data_cache);
	bitmap_set(data_cache,free_inode_num, BLOCK_SIZE);
	block_write(gr_cache[gr_i].bg_inode_bitmap, (void*)data_cache);
	
	//unsigned char *bm=create_bitmap(BLOCK_SIZE);
	//block_read(gr_cache[gr_i].bg_inode_bitmap, (void *)data_cache);
	//bitmap_
	//or_bitmap(bm, data_cache, BLOCK_SIZE);
	//memcpy(bm, data_cache, BLOCK_SIZE);
	//bitmap_set(free_inode_num, bm, BLOCK_SIZE);
	//log_bitmap(bm, BLOCK_SIZE);
	//memcpy(data_cache, bm, BLOCK_SIZE);
	//block_write(gr_cache[gr_i].bg_inode_bitmap, (void *)bm);
	//free_bitmap(bm);
	
	//log_msg("got here\n");

	tmp->i_ino= (gr_i * INODE_TABLE_SIZE) + free_inode_num;
	tmp->i_mode= mode | SFS_FILE;
	tmp->i_uid=fc->uid;
	tmp->i_gid=fc->gid;
	tmp->i_links_count=1;
	tmp->i_size=0;
	tmp->i_atime=time(NULL);
	tmp->i_mtime=time(NULL);
	tmp->i_ctime=time(NULL);
	tmp->i_blocks=0;
	tmp->i_flags=fi->flags;
	bzero(tmp->i_block,SFS_N_DATA_BLOCKS*sizeof(uint32_t));
	

	write_inode(gr_cache, tmp);
	
	struct my_dir_entry * new_entry=(struct my_dir_entry*)malloc(sizeof(struct my_dir_entry));
	new_entry->inode= tmp->i_ino;

	int rlen=0;

	
	new_entry->name_len=strlen(file_name);
	bzero(new_entry->name,SFS_NAME_LEN*sizeof(char));

	rlen = 2*sizeof(uint32_t) +strlen(file_name) +1;
	if(rlen % 4 != 0)
	{
		if(rlen % 4 == 1)
			rlen+=3;
		else if(rlen % 4 == 2)
			rlen+=2;
		else 
			rlen+=1;
	}
	
	new_entry->rec_len=rlen;
	new_entry->file_type=FILE_TYPE;//SFS_FILE;
	strcpy(&(new_entry->name), file_name);

	//log_msg("dir ent to be added:\n");
	//log_dir_entry(new_entry);
	//log_msg("add file to dir. file type:%d\n",new_entry->file_type);

	add_entry_to_dir(i_parent,gr_cache, new_entry);
	
	free(new_entry);

	fi->fh=tmp->i_ino;
	fi->fh_old = 0; 
 
	log_msg("sfs_create end\n");
	free(tmp);
	free(i_parent);
	return retstat;
}

/** Remove a file */
int sfs_unlink(const char *path)
{
	int retstat = 0;
	log_msg("sfs_unlink(path=\"%s\")\n", path);

	
	// first get the file inode

	struct my_inode *file = (struct my_inode *)malloc(sizeof(struct my_inode));

	read_inode_path(path, PATH_MAX, gr_cache, file, root, current_dir);

	int i=0;
	for(i=0;i<SFS_N_DATA_BLOCKS-1;i++)
	{
		if(file->i_block[i]!=0)
			continue;

		superblock->s_free_blocks_count++;
		// update bitmap 
		free_data_block(file->i_block, gr_cache);

		//update group descriptor
		
	}	

	// release the file inode
	superblock->s_free_inodes_count++;

	block_write(0, superblock);

	int gr_i = file->i_ino / INODE_TABLE_SIZE;
	int i_offset = file->i_ino % INODE_TABLE_SIZE;

	block_read(gr_cache[gr_i].bg_inode_bitmap, data_cache);
	bitmap_clear(data_cache, i_offset, BLOCK_SIZE);
	block_write(gr_cache[gr_i].bg_inode_bitmap, data_cache);

	// inode gr desc
	gr_cache[gr_i].bg_free_inode_count++;
	int k=1+(gr_i/2);
	if(gr_i % 2 == 0)
		block_write(k, (void *)&gr_cache[gr_i]);
	else
		block_write(k, (void *)&gr_cache[gr_i-1]);

	
	//  update the directory of parent

	
	

    	free(file);
	return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int sfs_open(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_open(path\"%s\", fi=0x%08x)\n",
	    path, fi);

	
	int flags = fi->flags;

	
	struct my_inode ino;

	read_inode_path(path, PATH_MAX, gr_cache, &ino, root, current_dir);

	if(ino.i_ino <=0)
		return -1;

	fi->fh_old=0;
	
	fi->fh=ino.i_ino;
	retstat=ino.i_ino;
    
    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int sfs_release(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_release(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
	fi->fh = 0;
	fi->fh_old=0;	

    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
int sfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);
	
	struct my_inode *tmp=(struct my_inode*)malloc(sizeof(struct my_inode));
   	 read_inode(fi->fh, gr_cache, tmp);
    
    int blocknum = offset/BLOCK_SIZE;
    int nset = offset % BLOCK_SIZE;
  	
  	char* bufptr = buf;
  	int avail = size; 
  	
  	while (avail > 0)
  	{
  		if (nset >0)
  		{
			block_read(tmp->i_block[blocknum], data_cache);
			memcpy(bufptr, data_cache + nset, BLOCK_SIZE-nset);
			nset = 0;
			blocknum++;
			avail -= (BLOCK_SIZE-nset);
  		}
  		else 
  		{
  			block_read(tmp->i_block[blocknum], data_cache);
  			memcpy(bufptr, data_cache, BLOCK_SIZE);
  			blocknum++;
  			avail -= BLOCK_SIZE;
  		}
  	}
  	
   
    free(tmp);
    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int sfs_write(const char *path, const char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);
    
    struct my_inode *tmp=(struct my_inode*)malloc(sizeof(struct my_inode));
    read_inode(fi->fh, gr_cache, tmp);
    
    int blocknum = offset / BLOCK_SIZE;
    int nset = offset % BLOCK_SIZE;
    
    if (blocknum == 0) 
    	return -1;
    	//cannot overwrite super block
    	
    char* bufptr = buf;
    size_t avail = size;
    
    while (avail > 0)
    {
   			//find a new free block if the current pointer is NULL
    		if (tmp->i_block[blocknum] == 0)
    		{
    			int index = find_free_bit(f_bitmap, BLOCK_SIZE);
    			
    			//tmp->i_block[index]=;
    			tmp->i_blocks++;
    			block_write(index, bufptr);
    			
    			//updates
    			//superblock->s_block_count++;
    			superblock->s_free_blocks_count--;
    			
    			log_msg("added a new block");
    			//updating group descriptors
    			
    		}	
    		//if overwriting the existing file
    		block_write(blocknum, bufptr);	
    		
    		tmp->i_size += BLOCK_SIZE;
    		
    		bufptr += BLOCK_SIZE;
    		avail -= BLOCK_SIZE;
    		blocknum++;
    }
    free(tmp);
    return retstat;
}


/** Create a directory */
int sfs_mkdir(const char *path, mode_t mode)
{
	int retstat = 0;
	log_msg("\nsfs_mkdir(path=\"%s\", mode=0%3o)\n",
	    path, mode);

	int inode_number =0;

	if(superblock->s_free_inodes_count -1 == 0)
		return -1;

	struct my_inode *tmp = (struct my_inode*)malloc(sizeof(struct my_inode));
	struct my_inode *i_parent = (struct my_inode *)malloc(sizeof(struct my_inode));
	
	bzero(tmp, sizeof(struct my_inode));
	
	read_inode_path((char *)path, PATH_MAX, gr_cache, tmp, root, current_dir);
	if(tmp->i_ino > 0)
	{
		log_msg("inode already exists");
		log_inode(tmp);
		// inode already exists. If it is a file open it 
		free(tmp);
		free(i_parent);
		return -1;
	}
	char path_parent[PATH_MAX];
	char file_name[SFS_NAME_LEN];
	if(path[0]!='/' && path[0]!='.')
	{
		path_parent[0]='.';
		path_parent[1]='/';
		strcpy(path_parent+2, path);
	}
	else
		strcpy(path_parent, path);
	char *current;
	int pos=-1;
	int i=0;
	for(current = path_parent; *current !='\0'; current++,i++)
	{
		if(*current == '/')
			pos=i;
	}

	strcpy(file_name, path_parent+pos+1);
	path_parent[pos+1]='\0';
	log_msg("path of parent:%s\n",path_parent);
	log_msg("new file name:%s\n",file_name);
	read_inode_path(path_parent, PATH_MAX, gr_cache, i_parent, root, current_dir);
	log_msg("parent inode\n");
	log_inode(i_parent);

	log_msg("parent block:\n");
	block_read(i_parent->i_block[0],(void*) data_cache);
	log_dir_entry_block(data_cache);
	if(i_parent->i_ino <=0 || !(i_parent-> i_mode && SFS_DIRECTORY))
	{	
		free(tmp);
		free(i_parent);
		return -1;
	}

	int free_inode_num =0;
	int gr_i =0;
	for(i=0; i< GROUP_NUM; i++)
	{
		block_read(gr_cache[i].bg_inode_bitmap, data_cache);
		free_inode_num = find_free_bit(data_cache, BLOCK_SIZE);

		if(free_inode_num > 0)
		{
			gr_i = i;
		}
		

		if(free_inode_num >0)
			break;
	}

	if(free_inode_num <= 0 )
	{
		free(tmp);
		free(i_parent);
		return -1;
	}
	
	superblock->s_free_inodes_count--;
	block_write(0, (void *)superblock);

	// group descriptors
	gr_cache[gr_i].bg_free_inode_count--;
	int k=1+(gr_i/2);
	if(gr_i % 2 == 0)
		block_write(k, (void *)&gr_cache[gr_i]);
	else
		block_write(k, (void *)&gr_cache[gr_i-1]);
	//if(gr_i % 2 == 0)
	//	block_read(k, (void *)&gr_cache[gr_i]);
	//else
	//	block_read(k, (void *)&gr_cache[gr_i-1]);

	// set bitmap in the appropriate group
	log_msg("setting bit %d in inode bitmap from group %d. block %d\n",free_inode_num, gr_i, gr_cache[gr_i].bg_inode_bitmap);
	block_read(gr_cache[gr_i].bg_inode_bitmap, (void *)data_cache);
	bitmap_set(data_cache,free_inode_num, BLOCK_SIZE);
	block_write(gr_cache[gr_i].bg_inode_bitmap, (void*)data_cache);
	
	//unsigned char *bm=create_bitmap(BLOCK_SIZE);
	//block_read(gr_cache[gr_i].bg_inode_bitmap, (void *)data_cache);
	//bitmap_
	//or_bitmap(bm, data_cache, BLOCK_SIZE);
	//memcpy(bm, data_cache, BLOCK_SIZE);
	//bitmap_set(free_inode_num, bm, BLOCK_SIZE);
	//log_bitmap(bm, BLOCK_SIZE);
	//memcpy(data_cache, bm, BLOCK_SIZE);
	//block_write(gr_cache[gr_i].bg_inode_bitmap, (void *)bm);
	//free_bitmap(bm);
	
	log_msg("got here\n");

	tmp->i_ino= (gr_i * INODE_TABLE_SIZE) + free_inode_num;
	tmp->i_mode= mode | SFS_DIRECTORY;
	tmp->i_uid=fc->uid;
	tmp->i_gid=fc->gid;
	tmp->i_links_count=1;
	tmp->i_size=0;
	tmp->i_atime=time(NULL);
	tmp->i_mtime=time(NULL);
	tmp->i_ctime=time(NULL);
	tmp->i_blocks=1;
	//tmp->i_flags=fi->flags;
	bzero(tmp->i_block,SFS_N_DATA_BLOCKS*sizeof(uint32_t));
	
	// allocate a new block for the directory structure
	int free_block_num =0;
	int gr_b =0,j=0;
	for(j=0; j< GROUP_NUM; j++)
	{
		block_read(gr_cache[j].bg_block_bitmap, data_cache);
		free_block_num = find_free_bit(data_cache, BLOCK_SIZE);

				// need to update everything
				
		
	
		if(free_block_num > 0)
		{
			free_block_num = (1+GROUP_NUM/2)+(j*INODE_TABLE_SIZE + j*DATA_BLOCKS)+free_block_num;
			gr_b = j;
			tmp-> i_block[0] = free_block_num;

			//update superblock

			//update group descriptos

			//update bitmap	
			block_read(gr_cache[gr_b].bg_inode_bitmap, (void *)data_cache);
			bitmap_set(data_cache,free_block_num, BLOCK_SIZE);
			block_write(gr_cache[gr_b].bg_inode_bitmap, (void*)data_cache);	

			break;
		}	
	}
	struct my_dir_entry temp;
	temp.inode=tmp->i_ino;
	temp.rec_len=12;
	temp.name_len =1;
	temp.file_type=2;
	temp.name[0] = '.';
	memcpy((void *)data_cache, (const void *)&temp, temp.rec_len*sizeof(uint8_t));

	temp.name_len = 2;
	temp.name[1] = '.';
	memcpy((void *)(data_cache+temp.rec_len), (const void *)&temp, temp.rec_len*sizeof(uint8_t));	
	block_write(tmp->i_block[0], data_cache);
	//log_block(data_cache);



	write_inode(gr_cache, tmp);
	
	struct my_dir_entry * new_entry=(struct my_dir_entry*)malloc(sizeof(struct my_dir_entry));
	bzero(new_entry, sizeof(struct my_dir_entry));
	//new_entry->inode=tmp->i_ino;
	
	//bzero(new_enrty, sizeof(struct my_dir_entry));



	new_entry->inode= tmp->i_ino;

	int rlen=0;

	
	new_entry->name_len=strlen(file_name);
	bzero(new_entry->name,SFS_NAME_LEN*sizeof(char));

	rlen = 2*sizeof(uint32_t) +strlen(file_name) +1;
	if(rlen % 4 != 0)
	{
		if(rlen % 4 == 1)
			rlen+=3;
		else if(rlen % 4 == 2)
			rlen+=2;
		else 
			rlen+=1;
	}
	
	new_entry->rec_len=rlen;
	new_entry->file_type=DIRECTORY_TYPE;//SFS_FILE;
	strcpy(&(new_entry->name), file_name);

	log_msg("dir ent to be added:\n");
	log_dir_entry(new_entry);
	//log_msg("add file to dir. file type:%d\n",new_entry->file_type);

	add_entry_to_dir(i_parent,gr_cache, new_entry);
	
	free(new_entry);

	//fi->fh=tmp->i_ino;
	//fi->fh_old = 0; 
 
	log_msg("sfs_mkdir end\n");
	free(tmp);
	free(i_parent);
	
	return retstat;
}


/** Remove a directory */
int sfs_rmdir(const char *path)
{
    int retstat = 0;
    log_msg("sfs_rmdir(path=\"%s\")\n",
	    path);
    
    
    return retstat;
}


/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int sfs_opendir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nsfs_opendir(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
    
    
    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int sfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
	       struct fuse_file_info *fi)
{
    int retstat = 0;
     log_msg("\nbb_readdir(path=\"%s\", buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)\n",
	    path, buf, filler, offset, fi);


	struct my_inode *ino = (struct my_inode *)malloc(sizeof(struct my_inode));
	if(fi->fh <= 0)
	{
		read_inode_path(path, PATH_MAX, gr_cache, ino, root, current_dir);
		fi->fh = ino->i_ino;
	}
	else
	{
		read_inode(fi->fh, gr_cache, ino);
	}	

	if(ino->i_ino <=0)
	{
		free(ino);
		return -1;
	}

	int full =0;
	char *current_entry;
	off_t next_entry=0;
	int finished = 0;
	int current_block = 0;
	struct stat stbuf;
	
	struct my_dir_entry temp;//=(struct my_dir_entry *)malloc(sizeof(struct my_dir_entry));
	

	int i=0;
	size_t pos =0;
	uint8_t block[BLOCK_SIZE];
	for(i=0;i<SFS_N_DATA_BLOCKS-1;i++)
	{
		if(ino->i_block[i]==0)
			break;
		block_read(ino->i_block[i], block);
		

		while(pos<=BLOCK_SIZE)
		{
			memcpy(&temp.inode,block+pos, sizeof(uint32_t));
			pos+=sizeof(uint32_t);
	
			if(temp.inode == 0)
				break;
		
			memcpy(&temp.rec_len, block+pos, sizeof(uint16_t));
			pos+=sizeof(uint16_t);
	
			memcpy(&temp.name_len, block+pos, sizeof(uint8_t));
			pos+=sizeof(uint8_t);
	
			memcpy(&temp.file_type, block+pos, sizeof(uint8_t));
			pos+=sizeof(uint8_t);
	
			strncpy(temp.name, block+pos, temp.name_len+1);

			//log_dir_entry(&temp);

			pos = (pos - 2*sizeof(uint32_t)) + temp.rec_len;

			filler(buf, temp.name, NULL, 0);
		}

		pos = 0;
	}

	//free(dir);
    	free(ino);
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int sfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
	log_msg("\nsfs_releasedir(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
    
    return retstat;
}

struct fuse_operations sfs_oper = {
  .init = sfs_init,
  .destroy = sfs_destroy,

  .getattr = sfs_getattr,
  .create = sfs_create,
  .unlink = sfs_unlink,
  .open = sfs_open,
  .release = sfs_release,
  .read = sfs_read,
  .write = sfs_write,

  .rmdir = sfs_rmdir,
  .mkdir = sfs_mkdir,

  .opendir = sfs_opendir,
  .readdir = sfs_readdir,
  .releasedir = sfs_releasedir
};

void sfs_usage()
{
    fprintf(stderr, "usage:  sfs [FUSE and mount options] diskFile mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct sfs_state *sfs_data;
    
    // sanity checking on the command line
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
	sfs_usage();

    sfs_data = malloc(sizeof(struct sfs_state));
    if (sfs_data == NULL) {
	perror("main calloc");
	abort();
    }

    // Pull the diskfile and save it in internal data
    sfs_data->diskfile = argv[argc-2];
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    
    sfs_data->logfile = log_open();
    
    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main, %s \n", sfs_data->diskfile);
    fuse_stat = fuse_main(argc, argv, &sfs_oper, sfs_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}
