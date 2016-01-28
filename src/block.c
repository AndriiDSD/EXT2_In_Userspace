/*
 * Author: Andrii Hlyvko
  Copyright (C) 2015 CS416/CS516

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.

*/

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
//#include <sys/stat.h>

#include "block.h"
#include <string.h>


void super_block_init(struct my_super_block *sb)
{
	sb->s_inodes_count = BLOCK_SIZE*8*GROUP_NUM;
	sb->s_blocks_count=GROUP_NUM*(1+1+INODE_BLOCKS+DATA_BLOCKS)+1+(GROUP_NUM*sizeof(struct my_group_desc)/BLOCK_SIZE);
	sb->s_free_inodes_count=BLOCK_SIZE*8*GROUP_NUM;
	sb->s_free_blocks_count=GROUP_NUM*(1+1+INODE_BLOCKS+DATA_BLOCKS)+1+(GROUP_NUM*sizeof(struct my_group_desc)/BLOCK_SIZE);
	//sb->s_free_blocks_count=GROUP_NUM*(4096);
	sb->s_first_data_block=1;	// the number of first useful block
	sb->s_blocks_per_group=1+1+INODE_BLOCKS+DATA_BLOCKS;
	sb->s_inodes_per_group=BLOCK_SIZE*8;
	sb->s_first_ino=0;
	sb->s_state=0;
	sb->s_inode_size=sizeof(struct my_inode);
	sb->s_block_group_nr=0;
	sb->s_word_padding=0;
	strncpy((sb->s_volume_name),"simple file system",strlen("simple file system"));
	//bzero((void *)sb->s_padding,32*sizeof(uint32_t));
}

void init_group(struct my_group_desc *gr, int n)
{
	//log_msg("num groups:%d\n",((GROUP_NUM*sizeof(struct my_group_desc))/BLOCK_SIZE));
	gr->bg_block_bitmap=n*(1+1+INODE_BLOCKS+DATA_BLOCKS)+1+(GROUP_NUM/2);
	gr->bg_inode_bitmap=n*(1+INODE_BLOCKS+DATA_BLOCKS+1)+1+1+(GROUP_NUM/2);
	gr->bg_inode_table=n*(INODE_BLOCKS+DATA_BLOCKS+1+1)+1+1+1+(GROUP_NUM/2);
	gr->bg_free_block_count=DATA_BLOCKS;
	gr->bg_free_inode_count=INODE_BLOCKS;
	gr->bg_used_dirs_count=0;
}


int diskfile = -1;

void disk_open(const char* diskfile_path)
{
    if(diskfile >= 0){
	return;
    }
    
    diskfile = open(diskfile_path, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
    if (diskfile < 0) {
	perror("disk_open failed");
	exit(EXIT_FAILURE);
    }
}

void disk_close()
{
    if(diskfile >= 0){
	close(diskfile);
    }
}

/** Read a block from an open file
 *
 * Read should return (1) exactly @BLOCK_SIZE when succeeded, or (2) 0 when the requested block has never been touched before, or (3) a negtive value when failed. 
 * In cases of error or return value equals to 0, the content of the @buf is set to 0.
 */
int block_read(const int block_num, void *buf)
{
    int retstat = 0;
    retstat = pread(diskfile, buf, BLOCK_SIZE, block_num*BLOCK_SIZE);
    if (retstat <= 0){
	memset(buf, 0, BLOCK_SIZE);
	if(retstat<0)
	perror("block_read failed");
    }

    return retstat;
}

/** Write a block to an open file
 *
 * Write should return exactly @BLOCK_SIZE except on error. 
 */
int block_write(const int block_num, const void *buf)
{
    int retstat = 0;
    retstat = pwrite(diskfile, buf, BLOCK_SIZE, block_num*BLOCK_SIZE);
    if (retstat < 0)
	perror("block_write failed");
    
    return retstat;
}


int name_for_inode(char *tok, uint8_t *block)
{
	
	struct my_dir_entry de;
	int inode_num=0;
	size_t pos=0;

	while(pos<=BLOCK_SIZE)
	{
		if((pos+2*sizeof(uint32_t))>=BLOCK_SIZE)
			break;
		memcpy(&de.inode,block+pos, sizeof(uint32_t));
		pos+=sizeof(uint32_t);
	
		if(de.inode == 0)
			break;
		
		memcpy(&de.rec_len, block+pos, sizeof(uint16_t));
		pos+=sizeof(uint16_t);
	
		memcpy(&de.name_len, block+pos, sizeof(uint8_t));
		pos+=sizeof(uint8_t);
	
		memcpy(&de.file_type, block+pos, sizeof(uint8_t));
		pos+=sizeof(uint8_t);
	
		strcpy(de.name, block+pos);
		//log_msg("current dir entry:%s",de.name);
		//log_dir_entry(&de);
		if(strcmp(de.name, tok)==0)
			return de.inode;		
		
		

		pos = (pos - 2*sizeof(uint32_t)) + de.rec_len;
	}
	log_msg("name for inode: %s number is %d\n",tok,inode_num);
	return inode_num;
}

void read_inode(int i_num, struct my_group_desc *gr_cache, struct my_inode * ino)
{

	//struct my_inode *i=(struct my_inode*)malloc(sizeof(struct my_inode));
	struct my_inode cache[4];

	int group_number =0, logical_number=0, block_offset=0;
	int block_num =0;

	// determine the group of the inode 
	group_number = i_num / INODE_TABLE_SIZE;
	logical_number = i_num % INODE_TABLE_SIZE;
	block_offset = logical_number/4;


	//log_msg("reading inode number %d from group %d\n",i_num, group_number);	
	//log_msg("logical numebr %d\n",logical_number);
	//log_msg("block offset %d\n",block_offset);
	//log_msg("the inode will be in block %d\n",gr_cache[group_number].bg_inode_table);
	//log_msg("in the cache it will be %d\n",(i_num%4));
	// read block
	block_read((gr_cache[group_number].bg_inode_table+block_offset), cache);
	
	//copy data to inode
	memcpy(ino,(const void *)&cache[i_num%4], sizeof(struct my_inode));

	//log_msg("read inode number %d\n",i_num);
	//log_inode(ino);
}

void read_inode_path(char * path, int max_path, struct my_group_desc *gr_cache, struct my_inode * ino, struct my_inode *root,struct my_inode*current)
{
	int path_type=0;
	char fpath[max_path];
	
	int i=0, next_inode_num=0;
	uint8_t data_cache[BLOCK_SIZE];

	struct my_inode *tmp =(struct my_inode *)malloc(sizeof(struct my_inode));

	strcpy(fpath, path);	

	log_msg("get inode from path %s\n",fpath);

	if(fpath[0] == '/')
		path_type=0;
	else
		path_type=1;

	char *tok = strtok(fpath, "/");

	if(path_type == 0)
		memcpy(tmp, root, sizeof(struct my_inode));
		//tmp = root;
	else
		memcpy(tmp, current, sizeof(struct my_inode));

	while(tok != NULL)
	{
		log_msg("token:%s\n",tok);

		for(i=0; i < (SFS_N_DATA_BLOCKS-1); i++)
		{
			if(tmp->i_block[i] <= 0)
				continue;
			block_read(tmp->i_block[i], data_cache);
			log_msg("going to look for %s in block %d\n",tok, tmp->i_block[i]);
			next_inode_num = name_for_inode(tok, data_cache);
			if(next_inode_num > 0)
				break;
		}
		log_msg("next inode number is:%d\n",next_inode_num);
		if(next_inode_num <=0)
		{
			free(tmp);
			return;
		}


		tok = strtok(NULL, "/");
		
		//read in the next inode into tmp. if it is not a directory break
		read_inode(next_inode_num, gr_cache, tmp);
		//log_inode(tmp);		

		if(!(tmp->i_mode && SFS_DIRECTORY))
		{
			free(tmp);
			return;
		}
	}
	memcpy(ino, tmp, sizeof(struct my_inode));
	log_inode(tmp);
	free(tmp);
	log_msg("get inode from path end\n");
}

void write_inode(struct my_group_desc *gr_cache, struct my_inode *ino)
{
	if(gr_cache == NULL || ino == NULL)
		return;
	
	int i_num = ino->i_ino;
	struct my_inode cache[4];

	int group_number =0, logical_number=0, block_offset=0;
	int block_num =0;

	// determine the group of the inode 
	group_number = i_num / INODE_TABLE_SIZE;
	logical_number = i_num % INODE_TABLE_SIZE;
	block_offset = logical_number/4;


	block_read((gr_cache[group_number].bg_inode_table+block_offset), cache);

	// repalce inode in cache
	memcpy((const void *)&cache[i_num%4],ino, sizeof(struct my_inode));

	// write cache back
	block_write((gr_cache[group_number].bg_inode_table+block_offset), cache);
}


int add_dir_to_block(struct my_dir_entry *dir, uint8_t *block)
{
	int retval= -1;
	
	size_t pos = 0;

	struct my_dir_entry temp;

	while(pos<BLOCK_SIZE)
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

		log_dir_entry(&temp);

		pos = (pos - 2*sizeof(uint32_t)) + temp.rec_len;
	}
	if(pos>=BLOCK_SIZE)
		return -1;

	pos-=sizeof(uint32_t);
	
	memcpy(block+pos, dir, dir->rec_len);

	
	
	return 0;
}
int add_entry_to_dir(struct my_inode *parent, struct my_group_desc *gr_cache, struct my_dir_entry *dir)
{
	int i=0,j=0;
	int s=-1;
	uint8_t cache[BLOCK_SIZE];

	for (i=0; i<SFS_N_DATA_BLOCKS-1; i++)
	{
		if((parent->i_block)[i] == 0)
		{
			// allocate a new data block to directory
			int free_block_num =0;
			int gr_b =0;
			for(j=0; j< GROUP_NUM; j++)
			{
				block_read(gr_cache[j].bg_block_bitmap, cache);
				free_block_num = find_free_bit(cache, BLOCK_SIZE);

				// need to update everything
				
				
	
				if(free_block_num > 0)
				{
					free_block_num = (1+GROUP_NUM/2)+(j*INODE_TABLE_SIZE + j*DATA_BLOCKS)+free_block_num;
					gr_b = j;
					parent-> i_block[i] = free_block_num;
					break;
				}	
			}
		}

		block_read(parent->i_block[i], (void *) cache);

		s = add_dir_to_block(dir, cache);
		
		if(s == 0)
		{
			block_write(parent->i_block[i], (void *) cache);
			return s;
		}
	}

	return s;
}

void free_data_block(int data_block, struct my_group_desc *gr_cache)
{
	int group_number = 0;
	int gr_offset = 0;
	

	group_number = data_block / DATA_BLOCKS;
	
	gr_offset = data_block % DATA_BLOCKS;

	
	gr_cache[group_number].bg_free_block_count++;
	int k=1+(group_number/2);
	if(group_number % 2 == 0)
		block_write(k, (void *)&gr_cache[group_number]);
	else
		block_write(k, (void *)&gr_cache[group_number-1]);

	
	// change the bitmap

	uint8_t cache[BLOCK_SIZE];

	block_read(gr_cache[group_number].bg_block_bitmap, cache);
	bitmap_clear(cache, gr_offset, BLOCK_SIZE);
	block_write(gr_cache[group_number].bg_block_bitmap, cache);
}

unsigned char * create_bitmap(int size)
{
	int n = (size/8);
	if(size%8>0)
		n++;
	unsigned char *m=(unsigned char *)malloc(n*sizeof(unsigned char));
	bzero(m,n*sizeof(unsigned char));
	return m;
}

void free_bitmap(unsigned char * map)
{
	free(map);
}


void bitmap_set(unsigned char *map, int n, int size)
{
	if(n>=size)
		return;
	int s= 0x80;
	map[n/8] |= s >> (n%8);
}
void bitmap_clear(unsigned char *map, int n, int size)
{
	if(n>=size)
		return;
	int s=0x80;
	map[n/8] &= ~(s >> (n%8));
}
int get_bitmap(unsigned char *map, int n, int size)
{
	if(n>=size)
		return -1;
	int s=0x80;
	if(map[n/8] & (s >> (n%8)))
		return 1;
	else 
		return 0;	
}
void or_bitmap(unsigned char *map1, unsigned char *map2, int size)
{
	int i=0;
	for(i=0; i<size;i++)
	{
		if(get_bitmap(map1, i, size) || get_bitmap(map2, i, size))
			bitmap_set(map1, i, size);
	}

}

int find_free_bit(unsigned char *map, int size)
{
	int index=0;
	int testBit = 1;
	for(index=0; index<=size; index++)
	{
		testBit = get_bitmap(map, index, size);
		if(testBit == 0)
			break;
	}
	if(testBit == -1)
		return testBit;
	else
		return index;
}

