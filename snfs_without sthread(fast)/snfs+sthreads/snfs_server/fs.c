/* 
 * File System Layer
 * 
 * fs.c
 *
 * Implementation of the file system layer. Manages the internal 
 * organization of files and directories in a 'virtual memory disk'.
 * Implements the interface functions specified in fs.h.
 *
 */


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "fs.h"


#define dprintf if(1) printf

//CACHE STRUTURE
static cache_node* cache;

//#define BLOCK_SIZE 512

/*
 * Inode
 * - inode size = 64 bytes
 * - num of direct block refs = 10 blocks
 */

#define INODE_NUM_BLKS 10

#define EXT_INODE_NUM_BLKS (BLOCK_SIZE / sizeof(unsigned int))

typedef struct fs_inode {
   fs_itype_t type;
   unsigned int size;
   unsigned int blocks[INODE_NUM_BLKS];
   unsigned int reserved[4]; // reserved[0] -> extending table block number
} fs_inode_t;

typedef unsigned int fs_inode_ext_t;


/*
 * Directory entry
 * - directory entry size = 16 bytes
 * - filename max size - 14 bytes (13 chars + '\0') defined in fs.h
 */

#define DIR_PAGE_ENTRIES (BLOCK_SIZE / sizeof(fs_dentry_t))

typedef struct dentry {
   char name[FS_MAX_FNAME_SZ];
   inodeid_t inodeid;
} fs_dentry_t;


/*
 * File syste structure
 * - inode table size = 64 entries (8 blocks)
 * 
 * Internal organization 
 *   - block 0        - free block bitmap
 *   - block 1        - free inode bitmap
 *   - block 2-9      - inode table (8 blocks)
 *   - block 10-(N-1) - data blocks, where N is the number of blocks
 */

#define ITAB_NUM_BLKS 8

#define ITAB_SIZE (ITAB_NUM_BLKS*BLOCK_SIZE / sizeof(fs_inode_t))

struct fs_ {
   blocks_t* blocks;
   char inode_bmap [BLOCK_SIZE];
   char blk_bmap [BLOCK_SIZE];
   fs_inode_t inode_tab [ITAB_SIZE];
};

#define NOT_FS_INITIALIZER  1
                               
/*
 * Internal functions for loading/storing file system metadata do the blocks
 */
                                
                                
static void fsi_load_fsdata(fs_t* fs)
{
   blocks_t* bks = fs->blocks;
   
   // load free block bitmap from block 0
   block_read(bks,0,fs->blk_bmap);

   // load free inode bitmap from block 1
   block_read(bks,1,fs->inode_bmap);
   
   // load inode table from blocks 2-9
   for (int i = 0; i < ITAB_NUM_BLKS; i++) {
      block_read(bks,i+2,&((char*)fs->inode_tab)[i*BLOCK_SIZE]);
   }
#define NOT_FS_INITIALIZER  1  //file system is already initialized, subsequent block acess will be delayed using a sleep function.
}


static void fsi_store_fsdata(fs_t* fs)
{
   blocks_t* bks = fs->blocks;
 
   // store free block bitmap to block 0
   block_write(bks,0,fs->blk_bmap);

   // store free inode bitmap to block 1
   block_write(bks,1,fs->inode_bmap);
   
   // store inode table to blocks 2-9
   for (int i = 0; i < ITAB_NUM_BLKS; i++) {
      block_write(bks,i+2,&((char*)fs->inode_tab)[i*BLOCK_SIZE]);
   }
}


/*
 * Bitmap management macros and functions
 */

#define BMAP_SET(bmap,num) ((bmap)[(num)/8]|=(0x1<<((num)%8)))

#define BMAP_CLR(bmap,num) ((bmap)[(num)/8]&=~((0x1<<((num)%8))))

#define BMAP_ISSET(bmap,num) ((bmap)[(num)/8]&(0x1<<((num)%8)))


static int fsi_bmap_find_free(char* bmap, int size, unsigned* free)
{
   for (int i = 0; i < size; i++) {
      if (!BMAP_ISSET(bmap,i)) {
         *free = i;
         return 1;
      }
   }
   return 0;
}

static void fsi_dump_bmap(char* bmap,int size)
{	
	int i;
	for(i=0;i<size;++i){
		if(BMAP_ISSET(bmap,i))
			printf("1.");
		else
			printf("0.");
		if(i>0 && (i+1)%32==0)
			printf("\n");
	}
}

/*
 * Other internal file system macros and functions
 */

#define MIN(a,b) ((a)<=(b)?(a):(b))
                                
#define MAX(a,b) ((a)>=(b)?(a):(b))
                                
#define OFFSET_TO_BLOCKS(pos) ((pos)/BLOCK_SIZE+(((pos)%BLOCK_SIZE>0)?1:0))

                                
static void fsi_inode_init(fs_inode_t* inode, fs_itype_t type)
{
   int i;
   
   inode->type = type;
   inode->size = 0;
   for (i = 0; i < INODE_NUM_BLKS; i++) {
      inode->blocks[i] = 0;
   }
   
   for (i = 0; i < 4; i++) {
	   inode->reserved[i] = 0;
   }
}


static int fsi_dir_search(fs_t* fs, inodeid_t dir, char* file, 
   inodeid_t* fileid)
{
   fs_dentry_t page[DIR_PAGE_ENTRIES];
   fs_inode_t* idir = &fs->inode_tab[dir];
   int num = idir->size / sizeof(fs_dentry_t);
   int iblock = 0;

   while (num > 0) {
      readFrom_cache(fs,idir->blocks[iblock++],(char*)page);
      for (int i = 0; i < DIR_PAGE_ENTRIES && num > 0; i++, num--) {
         if (strcmp(page[i].name,file) == 0) {
            *fileid = page[i].inodeid;
            return 0;
         }
      }
   }
   return -1;
}


/*
 * File system interface functions
 */


void io_delay_on(int disk_delay);

fs_t* fs_new(unsigned num_blocks, int disk_delay)
{
   fs_t* fs = (fs_t*) malloc(sizeof(fs_t));
   fs->blocks = block_new(num_blocks,BLOCK_SIZE);
   fsi_load_fsdata(fs);
   io_delay_on(disk_delay);
   return fs;
}


int fs_format(fs_t* fs)
{
   if (fs == NULL) {
      printf("[fs] argument is null.\n");
      return -1;
   }

   // erase all blocks
   char null_block[BLOCK_SIZE];
   memset(null_block,0,sizeof(null_block));
   for (int i = 0; i < block_num_blocks(fs->blocks); i++) {
      block_write(fs->blocks,i,null_block);
      BMAP_CLR(fs->blk_bmap,i);
   }

	for(int i=0;i<ITAB_SIZE;++i)
		BMAP_CLR(fs->inode_bmap,i);
	
	
   // reserve file system meta data blocks
   BMAP_SET(fs->blk_bmap,0);
   BMAP_SET(fs->blk_bmap,1);
   for (int i = 0; i < ITAB_NUM_BLKS; i++) {
      BMAP_SET(fs->blk_bmap,i+2);
   }

   // reserve inodes 0 (will never be used) and 1 (the root)
   BMAP_SET(fs->inode_bmap,0);
   BMAP_SET(fs->inode_bmap,1);
   fsi_inode_init(&fs->inode_tab[1],FS_DIR);
	
   // save the file system metadata
   fsi_store_fsdata(fs);
   cache = fs_new_cache(fs);
   return 0;
}


int fs_get_attrs(fs_t* fs, inodeid_t file, fs_file_attrs_t* attrs)
{
   if (fs == NULL || file >= ITAB_SIZE || attrs == NULL) {
      dprintf("[fs_get_attrs] malformed arguments.\n");
      return -1;
   }

   if (!BMAP_ISSET(fs->inode_bmap,file)) {
      dprintf("[fs_get_attrs] inode is not being used.\n");
      return -1;
   }

   fs_inode_t* inode = &fs->inode_tab[file];
   attrs->inodeid = file;
   attrs->type = inode->type;
   attrs->size = inode->size;
   switch (inode->type) {
      case FS_DIR:
         attrs->num_entries = inode->size / sizeof(fs_dentry_t);
         break;
      case FS_FILE:
         attrs->num_entries = -1;
         break;
      default:
         dprintf("[fs_get_attrs] fatal error - invalid inode.\n");
         exit(-1);
   }
   return 0;
}


int fs_lookup(fs_t* fs, char* file, inodeid_t* fileid)
{

char *token;
char line[MAX_PATH_NAME_SIZE]; 
char *search = "/";
int i=0;
int dir=0;
   if (fs==NULL || file==NULL ) {
      dprintf("[fs_lookup] malformed arguments.\n");
      return -1;
   }


    if(file[0] != '/') {
        dprintf("[fs_lookup] malformed pathname.\n");
        return -1;
    }

    strcpy(line,file);
    token = strtok(line, search);
    
   while(token != NULL) {
     i++;
     if(i==1) dir=1;  //Root directory
     
     if (!BMAP_ISSET(fs->inode_bmap,dir)) {
	      dprintf("[fs_lookup] inode is not being used.\n");
	      return -1;
     }
     fs_inode_t* idir = &fs->inode_tab[dir];
     if (idir->type != FS_DIR) {
        dprintf("[fs_lookup] inode is not a directory.\n");
        return -1;
     }
     inodeid_t fid;
     if (fsi_dir_search(fs,dir,token,&fid) < 0) {
        dprintf("[fs_lookup] file does not exist.\n");
        return 0;
     }
     *fileid = fid;
     dir=fid;
     token = strtok(NULL, search);
   }

   return 1;
}


int fs_read(fs_t* fs, inodeid_t file, unsigned offset, unsigned count, 
   char* buffer, int* nread)
{
	if (fs==NULL || file >= ITAB_SIZE || buffer==NULL || nread==NULL) {
		dprintf("[fs_read] malformed arguments.\n");
		return -1;
	}

	if (!BMAP_ISSET(fs->inode_bmap,file)) {
		dprintf("[fs_read] inode is not being used.\n");
		return -1;
	}

	fs_inode_t* ifile = &fs->inode_tab[file];
	if (ifile->type != FS_FILE) {
		dprintf("[fs_read] inode is not a file.\n");
		return -1;
	}

	if (offset >= ifile->size) {
		*nread = 0;
		return 0;
	}
	
   	// read the specified range
	int pos = 0;
	int iblock = offset/BLOCK_SIZE;
	int blks_used = OFFSET_TO_BLOCKS(ifile->size);
	int max = MIN(count,ifile->size-offset);
	int tbl_pos;
	unsigned int *blk;
	char block[BLOCK_SIZE];
   
	while (pos < max && iblock < blks_used) {
		if(iblock < INODE_NUM_BLKS) {
			blk = ifile->blocks;
			tbl_pos = iblock;
		}
		
		readFrom_cache(fs, blk[tbl_pos], block);
		int start = ((pos == 0)?(offset % BLOCK_SIZE):0);
		int num = MIN(BLOCK_SIZE - start, max - pos);
		memcpy(&buffer[pos],&block[start],num);

		pos += num;
		iblock++;
	}
	*nread = pos;
	return 0;
}

int inode_search(fs_t* fs,inodeid_t file, inodeid_t* inodeid);

int copy_inode_write(fs_t* fs, inodeid_t dest, inodeid_t file);

int fs_write(fs_t* fs, inodeid_t file, unsigned offset, unsigned count,
   char* buffer)
{
	if (fs == NULL || file >= ITAB_SIZE || buffer == NULL) {
		dprintf("[fs_write] malformed arguments.\n");
		return -1;
	}

	if (!BMAP_ISSET(fs->inode_bmap,file)) {
		dprintf("[fs_write] inode is not being used.\n");
		return -1;
	}

	fs_inode_t* ifile = &fs->inode_tab[file];
	if (ifile->type != FS_FILE) {
		dprintf("[fs_write] inode is not a file.\n");
		return -1;
	}
	
	inodeid_t aux;
	if(inode_search(fs,file, &aux)){
		if(copy_inode_write(fs, file , aux))
			dprintf("[fs_write] inode is not a file.\n");
	}

	if (offset > ifile->size) {
		offset = ifile->size;
	}

	unsigned *blk;

	int blks_used = OFFSET_TO_BLOCKS(ifile->size);
	int blks_req = MAX(OFFSET_TO_BLOCKS(offset+count),blks_used)-blks_used;

	dprintf("[fs_write] count=%d, offset=%d, fsize=%d, bused=%d, breq=%d\n",
		count,offset,ifile->size,blks_used,blks_req);
	
	if (blks_req > 0) {
		if(blks_req > INODE_NUM_BLKS-blks_used) {
			dprintf("[fs_write] no free block entries in inode.\n");
			return -1;
		}

		dprintf("[fs_write] required %d blocks, used %d\n", blks_req, blks_used);

      		// check and reserve if there are free blocks
		for (int i = blks_used; i < blks_used + blks_req; i++) {

			if(i < INODE_NUM_BLKS)
				blk = &ifile->blocks[i];
	 
			if (!fsi_bmap_find_free(fs->blk_bmap,block_num_blocks(fs->blocks),blk)) { // ITAB_SIZE
				dprintf("[fs_write] there are no free blocks.\n");
				return -1;
			}
			BMAP_SET(fs->blk_bmap, *blk);
			dprintf("[fs_write] block %d allocated.\n", *blk);
		}
	}
   
	char block[BLOCK_SIZE];
	int num = 0, pos;
	int iblock = offset/BLOCK_SIZE;

   	// write within the existent blocks
	while (num < count && iblock < blks_used) {
		if(iblock < INODE_NUM_BLKS) {
			blk = ifile->blocks;
			pos = iblock;
		}
		readFrom_cache(fs, blk[pos], block);

		int start = ((num == 0)?(offset % BLOCK_SIZE):0);
		for (int i = start; i < BLOCK_SIZE && num < count; i++, num++) {
			block[i] = buffer[num];
		}
		writeIn_cache(fs, blk[pos], block);
		iblock++;
	}

	dprintf("[fs_write] written %d bytes within.\n", num);

  	// write within the allocated blocks
	while (num < count && iblock < blks_used + blks_req) {
		if(iblock < INODE_NUM_BLKS) {
			blk = ifile->blocks;
			pos = iblock;
		}
      
		for (int i = 0; i < BLOCK_SIZE && num < count; i++, num++) {
			block[i] = buffer[num];
		}
		
		writeIn_cache(fs, blk[pos], block);
		iblock++;
	}

	if (num != count) {
		printf("[fs_write] severe error: num=%d != count=%d!\n", num, count);
		exit(-1);
	}

	ifile->size = MAX(offset + count, ifile->size);

   	// update the inode in disk
	fsi_store_fsdata(fs);

	dprintf("[fs_write] written %d bytes, file size %d.\n", count, ifile->size);
	return 0;
}


int fs_create(fs_t* fs, inodeid_t dir, char* file, inodeid_t* fileid)
{
   if (fs == NULL || dir >= ITAB_SIZE || file == NULL || fileid == NULL) {
      printf("[fs_create] malformed arguments.\n");
      return -1;
   }

   if (strlen(file) == 0 || strlen(file)+1 > FS_MAX_FNAME_SZ){
      dprintf("[fs_create] file name size error.\n");
      return -1;
   }

   if (!BMAP_ISSET(fs->inode_bmap,dir)) {
      dprintf("[fs_create] inode is not being used.\n");
      return -1;
   }

   fs_inode_t* idir = &fs->inode_tab[dir];
   if (idir->type != FS_DIR) {
      dprintf("[fs_create] inode is not a directory.\n");
      return -1;
   }

   if (fsi_dir_search(fs,dir,file,fileid) == 0) {
      dprintf("[fs_create] file already exists.\n");
      return -1;
   }
   
   // check if there are free inodes
   unsigned finode;
   if (!fsi_bmap_find_free(fs->inode_bmap,ITAB_SIZE,&finode)) {
      dprintf("[fs_create] there are no free inodes.\n");
      return -1;
   }

   // add a new block to the directory if necessary
   if (idir->size % BLOCK_SIZE == 0) {
      unsigned fblock;
      if (!fsi_bmap_find_free(fs->blk_bmap,block_num_blocks(fs->blocks),&fblock)) {
         dprintf("[fs_create] no free blocks to augment directory.\n");
         return -1;
      }
      BMAP_SET(fs->blk_bmap,fblock);
      idir->blocks[idir->size / BLOCK_SIZE] = fblock;
   }

   // add the entry to the directory
   fs_dentry_t page[DIR_PAGE_ENTRIES];
   readFrom_cache(fs, idir->blocks[idir->size/BLOCK_SIZE],(char*)page);
   fs_dentry_t* entry = &page[idir->size % BLOCK_SIZE / sizeof(fs_dentry_t)];
   strcpy(entry->name,file);
   entry->inodeid = finode;
   writeIn_cache(fs, idir->blocks[idir->size/BLOCK_SIZE],(char*)page);
   idir->size += sizeof(fs_dentry_t);

   // reserve and init the new file inode
   BMAP_SET(fs->inode_bmap,finode);
   fsi_inode_init(&fs->inode_tab[finode],FS_FILE);

   // save the file system metadata
   fsi_store_fsdata(fs);

   *fileid = finode;
   return 0;
}


int fs_mkdir(fs_t* fs, inodeid_t dir, char* newdir, inodeid_t* newdirid)
{
	if (fs==NULL || dir>=ITAB_SIZE || newdir==NULL || newdirid==NULL) {
		printf("[fs_mkdir] malformed arguments.\n");
		return -1;
	}

	if (strlen(newdir) == 0 || strlen(newdir)+1 > FS_MAX_FNAME_SZ){
		dprintf("[fs_mkdir] directory size error.\n");
		return -1;
	}

	if (!BMAP_ISSET(fs->inode_bmap,dir)) {
		dprintf("[fs_mkdir] inode is not being used.\n");
		return -1;
	}

	fs_inode_t* idir = &fs->inode_tab[dir];
	if (idir->type != FS_DIR) {
		dprintf("[fs_mkdir] inode is not a directory.\n");
		return -1;
	}

	if (fsi_dir_search(fs,dir,newdir,newdirid) == 0) {
		dprintf("[fs_mkdir] directory already exists.\n");
		return -1;
	}
   
   	// check if there are free inodes
	unsigned finode;
	if (!fsi_bmap_find_free(fs->inode_bmap,ITAB_SIZE,&finode)) {
		dprintf("[fs_mkdir] there are no free inodes.\n");
		return -1;
	}

   	// add a new block to the directory if necessary
	if (idir->size % BLOCK_SIZE == 0) {
		unsigned fblock;
		if (!fsi_bmap_find_free(fs->blk_bmap,block_num_blocks(fs->blocks),&fblock)) {
			dprintf("[fs_mkdir] no free blocks to augment directory.\n");
			return -1;
		}
		BMAP_SET(fs->blk_bmap,fblock);
		idir->blocks[idir->size / BLOCK_SIZE] = fblock;
	}

   	// add the entry to the directory
	fs_dentry_t page[DIR_PAGE_ENTRIES];
	readFrom_cache(fs, idir->blocks[idir->size/BLOCK_SIZE],(char*)page);
	fs_dentry_t* entry = &page[idir->size % BLOCK_SIZE / sizeof(fs_dentry_t)];
	strcpy(entry->name,newdir);
	entry->inodeid = finode;
	writeIn_cache(fs, idir->blocks[idir->size/BLOCK_SIZE],(char*)page);
	idir->size += sizeof(fs_dentry_t);

   	// reserve and init the new file inode
	BMAP_SET(fs->inode_bmap,finode);
	fsi_inode_init(&fs->inode_tab[finode],FS_DIR);

   	// save the file system metadata
	fsi_store_fsdata(fs);

	*newdirid = finode;
	return 0;
}


int fs_readdir(fs_t* fs, inodeid_t dir, fs_file_name_t* entries, int maxentries,
   int* numentries)
{
   if (fs == NULL || dir >= ITAB_SIZE || entries == NULL ||
      numentries == NULL || maxentries < 0) {
      dprintf("[fs_readdir] malformed arguments.\n");
      return -1;
   }

   if (!BMAP_ISSET(fs->inode_bmap,dir)) {
      dprintf("[fs_readdir] inode is not being used.\n");
      return -1;
   }

   fs_inode_t* idir = &fs->inode_tab[dir];
   if (idir->type != FS_DIR) {
      dprintf("[fs_readdir] inode is not a directory.\n");
      return -1;
   }

   // fill in the entries with the directory content
   fs_dentry_t page[DIR_PAGE_ENTRIES];
   int num = MIN(idir->size / sizeof(fs_dentry_t), maxentries);
   int iblock = 0, ientry = 0;

   while (num > 0) {
   	  readFrom_cache(fs, idir->blocks[iblock++],(char*)page);
      for (int i = 0; i < DIR_PAGE_ENTRIES && num > 0; i++, num--) {
         strcpy(entries[ientry].name, page[i].name);
         entries[ientry].type = fs->inode_tab[page[i].inodeid].type;
         ientry++;
      }
   }
   *numentries = ientry;
   return 0;
}


void fs_dump(fs_t* fs)
{
   printf("Free block bitmap:\n");
   fsi_dump_bmap(fs->blk_bmap,BLOCK_SIZE);
   printf("\n");
   
   printf("Free inode table bitmap:\n");
   fsi_dump_bmap(fs->inode_bmap,BLOCK_SIZE);
   printf("\n");
}

int fs_remove_aux(fs_t* fs, inodeid_t file);

int fsi_dir_search_file(fs_t* fs,inodeid_t dir,int i,inodeid_t* fileid);

void swap_entry(fs_t* fs,inodeid_t mother, inodeid_t file){
	fs_inode_t* imother = &fs->inode_tab[mother];
	int num = imother->size/ sizeof(fs_dentry_t);
	int i, j, k;
	int file_be, file_b, last_be, last_b;
	fs_dentry_t page1[DIR_PAGE_ENTRIES];
	fs_dentry_t page2[DIR_PAGE_ENTRIES];
	if(num > 32){
		last_b=1;
		last_be= (num%32)-1;
	}
	else{
		last_b=0;
		last_be= num-1;
	}
	for(i=0, j=0; imother->blocks[i]!=0 && i<2;i++){
		readFrom_cache(fs,imother->blocks[i],(char*)page1);
		for(k=0; k<DIR_PAGE_ENTRIES && j < num; j++, k++){
			if( page1[k].inodeid == file){
				file_b=i;
				file_be=k;
				break;
			}
		}
	}
	readFrom_cache( fs,imother->blocks[0],(char*)page1);
	readFrom_cache(fs,imother->blocks[1],(char*)page2);
	unsigned temp_inode;
	char temp_name[FS_MAX_FNAME_SZ];
	if(file_b==1 && last_b==1){
		temp_inode=page2[file_be].inodeid;
		strcpy(temp_name,page2[file_be].name);
		page2[file_be].inodeid = page2[last_be].inodeid;
		strcpy(page2[file_be].name,page2[last_be].name);
		page2[last_be].inodeid = temp_inode;
		strcpy(page2[last_be].name,temp_name);
	}
	if(file_b==1 && last_b==0){
		temp_inode=page2[file_be].inodeid;
		strcpy(temp_name,page2[file_be].name);
		page2[file_be].inodeid = page1[last_be].inodeid;
		strcpy(page2[file_be].name,page1[last_be].name);
		page1[last_be].inodeid = temp_inode;
		strcpy(page1[last_be].name,temp_name);
	}
	if(file_b==0 && last_b==1){
		temp_inode=page1[file_be].inodeid;
		strcpy(temp_name,page1[file_be].name);
		page1[file_be].inodeid = page2[last_be].inodeid;
		strcpy(page1[file_be].name,page2[last_be].name);
		page2[last_be].inodeid = temp_inode;
		strcpy(page2[last_be].name,temp_name);
	}
	if(file_b==0 && last_b==0){
		temp_inode=page1[file_be].inodeid;
		strcpy(temp_name,page1[file_be].name);
		page1[file_be].inodeid = page1[last_be].inodeid;
		strcpy(page1[file_be].name,page1[last_be].name);
		page1[last_be].inodeid = temp_inode;
		strcpy(page1[last_be].name,temp_name);
	}
	writeIn_cache(fs, imother->blocks[0],(char*)page1);
	writeIn_cache(fs, imother->blocks[1],(char*)page2);
}

int fs_remove(fs_t* fs, inodeid_t dir,char* name, inodeid_t* fileid)
{
	if (fs == NULL || dir >= ITAB_SIZE) {
		dprintf("[fs_remove] malformed arguments.\n");
		return -1;
	}

	if (!BMAP_ISSET(fs->inode_bmap,dir)) {
		dprintf("[fs_remove] inode is not being used.\n");
		return -1;
	}
	inodeid_t file;
	if(fsi_dir_search(fs, dir, name, &file)){
		return -1;
	}

	
	swap_entry(fs, dir, file);
	
	
	if(fs_remove_aux(fs, file)){
		return -1;
	}
	fs_inode_t*  idir= &fs->inode_tab[dir];
	idir->size -= sizeof(fs_dentry_t);
	fsi_store_fsdata(fs);
	*fileid= file;
	return 0;
}

int fs_remove_aux(fs_t* fs, inodeid_t file)
{
	fs_inode_t* ifile = &fs->inode_tab[file];
	if (ifile->type == FS_DIR){
		inodeid_t inodeId;
		int num = ifile->size / sizeof(fs_dentry_t);
 		for(int i=0;i<num;++i){
 			if(fsi_dir_search_file(fs,file,i,&inodeId))
 				return -1;
 			if(fs_remove_aux(fs, inodeId))
 				return -1;
 		}
	}
	inodeid_t aux;

	if(!inode_search(fs,file, &aux)){
		//erase blocks and update block bmap
		char null_block[BLOCK_SIZE];
		memset(null_block,0,sizeof(null_block));
		for(int i = 0; ifile->blocks[i] != 0; i++){ 
 		   	block_write(fs->blocks,ifile->blocks[i],null_block);
	    	BMAP_CLR(fs->blk_bmap, ifile->blocks[i]);
	    	cache_clean(ifile->blocks[i]);
	  }
  }
  
	//update inode bmapnode� may be used uninitialized in this fu
	BMAP_CLR(fs->inode_bmap,file);
	fsi_inode_init(ifile, FS_FILE);
    
    
  // save the file system metadata
	fsi_store_fsdata(fs);
	return 0;
}

int fsi_dir_get_path_name(fs_t* fs,inodeid_t dirId,inodeid_t fileId,char* name);

int fsi_get_path_name(fs_t* fs,inodeid_t fileId,char* name)
{
	if(fs==NULL || fileId>=ITAB_SIZE){
		dprintf("[fsi_dir_get_file_name] malformed arguments.\n");
		return -1;
	}
	
	if(!BMAP_ISSET(fs->inode_bmap,fileId)){
		dprintf("[fs_dir_get_file_name] inode is not being used.\n");
		return -1;
	}
	
	inodeid_t rootInodeId=1;
	char filePathName[MAX_PATH_NAME_SIZE];
	filePathName[0]='\0';
	int output=fsi_dir_get_path_name(fs,rootInodeId,fileId,filePathName);
	if(output)
		return -1;
	strcpy(name,filePathName);
	return 0;
}			


int fsi_dir_get_path_name(fs_t* fs,inodeid_t dirId,inodeid_t fileId,char* name)
{	
	if(fs==NULL || dirId>=ITAB_SIZE || fileId>=ITAB_SIZE){
		dprintf("[fsi_dir_get_path_name] malformed arguments.\n");
		return -1;
	}
  
	if(!BMAP_ISSET(fs->inode_bmap,dirId) || !BMAP_ISSET(fs->inode_bmap,fileId)){
		dprintf("[fsi_dir_get_path_name] inode is not being used.\n");
		return -1;
	}
  
	fs_inode_t* dirInode=&fs->inode_tab[dirId];
  
	if(dirInode->type!=FS_DIR){
		dprintf("[fsi_dir_get_path_name] inode is not a directory.\n");
		return -1;
	}
  
	if(fileId==1){
		strcat(name,"/");
		return 0;
	}
  
	char tempName[MAX_PATH_NAME_SIZE];
	tempName[0]='\0';
  
	//check directory entries
	int num_entries=dirInode->size/sizeof(fs_dentry_t);
	fs_dentry_t page[DIR_PAGE_ENTRIES];
	for(int i=0;dirInode->blocks[i]!=0;i++){
		readFrom_cache(fs,dirInode->blocks[i],(char*)page);
		for(int j=0;j<DIR_PAGE_ENTRIES && num_entries>0;++j){
			--num_entries;
			fs_dentry_t* entry=&page[j];
			if(entry->inodeid==fileId){
				strcat(name,"/");
				strcat(name,entry->name);
				return 0;
			}
			if(fs->inode_tab[entry->inodeid].type==FS_DIR){
				int nameSize=strlen(tempName);
				int tempSize=strlen(entry->name);
				strcat(tempName,"/");
				strcat(tempName,entry->name);
				char* tempPtr=&tempName[nameSize];
				if(fsi_dir_get_path_name(fs,entry->inodeid,fileId,tempName)==0){
					strcat(name,tempName);
					return 0;
				}
				else
					memset(tempPtr,'\0',tempSize+1);
			}
		}
	}
  
  return -1;
}

int fsi_num_blocks_used(fs_t* fs)
{
	int blocks=0;
	for(int i=0;i<block_num_blocks(fs->blocks);++i){
		if(BMAP_ISSET(fs->blk_bmap,i))
			++blocks;
	}
	return blocks;
}


int fsi_dir_search_file(fs_t* fs,inodeid_t dir,int i,inodeid_t* fileid)
{
   fs_dentry_t page[DIR_PAGE_ENTRIES];
   fs_inode_t* idir = &fs->inode_tab[dir];
   
   readFrom_cache(fs,idir->blocks[i/DIR_PAGE_ENTRIES],(char*)page);
   if((i/DIR_PAGE_ENTRIES) <DIR_PAGE_ENTRIES){
      *fileid=page[i%DIR_PAGE_ENTRIES].inodeid;
      return 0;
   }
   
   return -1;
}

int inode_search(fs_t* fs,inodeid_t file, inodeid_t* inodeid)
{
	fs_inode_t* ifile = &fs->inode_tab[file];
	int i=0;
	for(i=1; i<ITAB_SIZE; i++){
		if(i!=file){
			if(BMAP_ISSET(fs->inode_bmap,i)){
				if(ifile->blocks[0] == fs->inode_tab[i].blocks[0] && ifile->blocks[0]!=0){
					printf("blocos partilhados ficheiros:%d & %d\n",file,i);
					*inodeid=i;
					return 1;
				}
			}
		}
	}
	return 0;
}

int get_name(fs_t* fs, inodeid_t mother,inodeid_t file, char* name){
	
	fs_dentry_t page[DIR_PAGE_ENTRIES];
	fs_inode_t* imot = &fs->inode_tab[mother];
   	if (imot->type != FS_DIR) {
		dprintf("[fs_copy] error mother isn't a directory\n");
		return -1;
	}
	int num = imot->size / sizeof(fs_dentry_t);
	int i, j, k;
	for(i=0, j=0; imot->blocks[i]!=0 && i<2;i++){
		readFrom_cache(fs,imot->blocks[i],(char*)page);
		for(k=0; k<DIR_PAGE_ENTRIES && j < num; j++, k++){
			if( file == page[k].inodeid){
				strcpy(name,page[k].name);
				return 0;
			}
		}
	}
	return -1;
}

int countcopies(fs_t* fs, inodeid_t file){
		fs_inode_t* ifile = &fs->inode_tab[file];
	int count=0;
	if (ifile->type == FS_FILE){
   	return 1;
  	}
  else {
    inodeid_t inodeId;
		int num = ifile->size / sizeof(fs_dentry_t);
 		for(int i=0;i<num;i++){
 			if(fsi_dir_search_file(fs,file,i, &inodeId))
 				return -1;
 			count += countcopies(fs, inodeId);
 		}
	}
	return count+1;
}


int copy_inode_write(fs_t* fs, inodeid_t dest, inodeid_t file)
{	
	fs_inode_t* ifile = &fs->inode_tab[file];
	fs_inode_t* idest = &fs->inode_tab[dest];
	int i;
	for( i = 0; ifile->blocks[i] != 0; i++);
	unsigned temp=0;
	for(int j=0; i>0; i--, j++){
			if (!fsi_bmap_find_free(fs->blk_bmap,block_num_blocks(fs->blocks),&temp)) { // ITAB_SIZE
				dprintf("[fs_write] there are no free blocks.\n");
				return -1;
			}
			BMAP_SET(fs->blk_bmap, temp);
			dprintf("[fs_write] block %d allocated.\n", temp);
			idest->blocks[j]=temp;
			
		char block_aux[BLOCK_SIZE];	
		readFrom_cache(fs, ifile->blocks[j], block_aux);
		writeIn_cache(fs, idest->blocks[j], block_aux);
 	}
 	 	
  	// save the file system metadata
	fsi_store_fsdata(fs);
  	return 0;
}

void copy_inode(fs_t* fs, inodeid_t dest, inodeid_t file)
{
	fs_inode_t* ifile = &fs->inode_tab[file];
	fs_inode_t* idest = &fs->inode_tab[dest];
	int i;
	for( i = 0; ifile->blocks[i] != 0; i++)
		idest->blocks[i]=ifile->blocks[i];
	idest->size=ifile->size;
}

int descendsFrom(fs_t* fs,inodeid_t des,inodeid_t parent,inodeid_t* initParent)
{
	if(parent==des && parent!=(*initParent))
		return 1;
	fs_inode_t* parentInode=&fs->inode_tab[parent];
	int numEntries=parentInode->size/sizeof(fs_dentry_t);
	int numPages=parentInode->size/(sizeof(fs_dentry_t)*32)+1;
	for(int i=0;i<numPages;++i){
		fs_dentry_t page[DIR_PAGE_ENTRIES];
		readFrom_cache(fs,parentInode->blocks[i],(char*)page);
		for(int j=0;j<numEntries;++j){
			fs_dentry_t* entry=&page[j];
			fs_inode_t* temp=&fs->inode_tab[entry->inodeid];
			if(temp->type==FS_DIR){
				if(descendsFrom(fs,des,entry->inodeid,initParent))
					return 1;
			}
		}
	numEntries-=32;
	}
	return 0;
}

int fs_copy_first(fs_t* fs, inodeid_t file, inodeid_t dest, inodeid_t mother, int* count);

int fs_copy_aux(fs_t* fs, inodeid_t file, inodeid_t dest, inodeid_t mother, int* count);

int fs_copy(fs_t* fs, inodeid_t file, char * file_name, inodeid_t dest, char* dest_name, inodeid_t* fileid)
{
	if (fs == NULL || file >= ITAB_SIZE || dest >= ITAB_SIZE) {
		dprintf("[fs_copy] malformed arguments.\n");
		return -1;
	}
	
	if (!BMAP_ISSET(fs->inode_bmap,file)) {
		dprintf("[fs_copy] file/dir inode is not being used.\n");
		return -1;
	}
	
	if (!BMAP_ISSET(fs->inode_bmap, dest )) {
		dprintf("[fs_copy] destination inode is not being used.\n");
		return -1;
	}
	
	fs_inode_t* idest = &fs->inode_tab[dest];
	if (idest->type != FS_DIR) {
		dprintf("[fs_copy] inode1 is not a directory.\n");
		return -1;
	}
	
	fs_inode_t* ifile = &fs->inode_tab[file];
	if (ifile->type != FS_DIR) {
		dprintf("[fs_copy] inode2 is not a directory.\n");
		return -1;
	}
	
	inodeid_t src;
	if(fsi_dir_search(fs, file, file_name, &src)){
   	dprintf("[fs_copy] theres no file with that name\n");
		return -1;
	}
	
	if(file==dest && strcmp(file_name,dest_name)==0){
		dprintf("[fs_copy] there is already a file/directory with that name\n");
		return -1;
	}
	
	fs_inode_t* isrc = &fs->inode_tab[src];
	inodeid_t temp=file;
	if(isrc->type==FS_DIR && descendsFrom(fs,dest,file,&temp)){
		dprintf("[fs_copy] you cant copy a directory into one of its subdirectories\n");
		return -1;
	}
	
	int count = countcopies(fs, src);
	inodeid_t dst;
	if(!fsi_dir_search(fs, dest, dest_name, &dst)){
		fs_inode_t* idst = &fs->inode_tab[dst];
		if(isrc->type != idst->type){
   		dprintf("[fs_copy] the files are not of the same type\n");
			return -1;
		}
		*fileid= dst;
		count--;
		return fs_copy_first(fs, src, dst, dest, &count);
	}
	inodeid_t new;
	if(isrc->type == FS_DIR){
		if(fs_mkdir(fs, dest, dest_name, &new)){
		  dprintf("[fs_copy] 1 error creating new directory.\n");
  		return -1;
		}
	} else {
		if(fs_create(fs, dest, dest_name, &new)){
		  dprintf("[fs_copy] 2 error creating new file.\n");
  		  return -1;
		}
	}
	count--;
	*fileid= new;
	return fs_copy_first(fs, src, new, src, &count);
}

int fs_copy_first(fs_t* fs, inodeid_t file, inodeid_t dest, inodeid_t mother, int* count)
{
	fs_inode_t* ifile = &fs->inode_tab[file];
	if (ifile->type == FS_FILE){
   		copy_inode(fs, dest, file);
	}else {
		inodeid_t inodeId;
		int num = ifile->size / sizeof(fs_dentry_t);
 		for(int i=0;i<num;++i){
 			if(fsi_dir_search_file(fs,file,i,&inodeId))
 				return -1;
 			if(fs_copy_aux(fs, inodeId, dest, mother, count))
 				return -1;
 		}
	}
	// save the file system metadata
 	fsi_store_fsdata(fs);
	return 0;
}

int fs_copy_aux(fs_t* fs, inodeid_t file, inodeid_t dest, inodeid_t mother,int* count )
{
	fs_inode_t* ifile = &fs->inode_tab[file];
	if(*count >=0){
		if (ifile->type == FS_FILE){
			char dest_name[FS_MAX_FNAME_SZ];
			inodeid_t new;
			if(get_name(fs, mother,  file, dest_name)){
				dprintf("[fs_copy] 8 error getting directory name.\n");
					return -1;
				}
			if(fs_create(fs, dest, dest_name, &new)){
				dprintf("[fs_copy] 4 error creating new directory.\n");
				return -1;
			}
		 	copy_inode(fs, new, file);
		 	(*count)--;
		 	fsi_store_fsdata(fs);
		}
		else {
			inodeid_t new;
			inodeid_t inodeId;
			char dest_name[FS_MAX_FNAME_SZ];
			if(get_name(fs, mother,  file, dest_name)){
				dprintf("[fs_copy] 8 error getting directory name.\n");
					return -1;
				}
			if(fs_mkdir(fs, dest, dest_name, &new)){
				dprintf("[fs_copy] 4 error creating new directory.\n");
				return -1;
			}
			int num = ifile->size / sizeof(fs_dentry_t);
	 		for(int i=0;i<num;i++){
	 			if(fsi_dir_search_file(fs,file,i, &inodeId))
	 				return -1;
	 			(*count)--;
	 			if(fs_copy_aux(fs, inodeId, new, file, count))
	 				return -1;
	 		}
		}
	}
	return 0;
}
 
int fs_append(fs_t* fs, inodeid_t dest, char * dest_name, inodeid_t file, char* file_name)
{
	if (fs == NULL || file >= ITAB_SIZE || dest >= ITAB_SIZE) {
		dprintf("[fs_append] malformed arguments.\n");
		return -1;
	}
	
	if (!BMAP_ISSET(fs->inode_bmap,file)) {
		dprintf("[fs_append] file/dir inode is not being used.\n");
		return -1;
	}
	
	if (!BMAP_ISSET(fs->inode_bmap, dest )) {
		dprintf("[fs_append] destination inode is not being used.\n");
		return -1;
	}
	
	fs_inode_t* idest = &fs->inode_tab[dest];
	if (idest->type != FS_DIR) {
		dprintf("[fs_append] inode1 is not a directory.\n");
		return -1;
	}
	
	fs_inode_t* ifile = &fs->inode_tab[file];
	if (ifile->type != FS_DIR) {
		dprintf("[fs_append] inode2 is not a directory.\n");
		return -1;
	}
	
	inodeid_t src;
	if(fsi_dir_search(fs, file, file_name, &src)){
   	dprintf("[fs_append] theres no file with that name\n");
		return -1;
	}
	
	inodeid_t dst;
	if(fsi_dir_search(fs, dest, dest_name, &dst)){
   	dprintf("[fs_append] theres no file with that name\n");
		return -1;
	}
	
	idest = &fs->inode_tab[dst];
	if (idest->type != FS_FILE) {
		dprintf("[fs_append] inode1 is not a file.\n");
		return -1;
	}
	
	ifile = &fs->inode_tab[src];
	if (ifile->type != FS_FILE) {
		dprintf("[fs_append] inode2 is not a file.\n");
		return -1;
	}
	
	unsigned offset = idest->size;
	unsigned size = ifile->size;
	int test=0;
	char buffer[size];
	if(fs_read(fs, src, 0, size, buffer,&test))
 		return -1;
 	if(fs_write(fs, dst, offset, test, buffer))
 		return -1;
 	return 0;
}

int fs_diskusage(fs_t* fs)
{
	printf("===== Dump: FileSystem Blocks =======================\n");
	int num_blocks=fsi_num_blocks_used(fs)-10;
	for(int i=0,j=10;i<num_blocks;++i,++j){
		while(!BMAP_ISSET(fs->blk_bmap,j))
			++j;
		if(j>num_blocks+10)
			return 0;	
		dprintf("blk_id: %d\n",j);
		for(int k=1,n=0;k<ITAB_SIZE;++k){
			for(int l=0;l<INODE_NUM_BLKS;++l){
				if(fs->inode_tab[k].blocks[l]==j){
					char pathname[MAX_PATH_NAME_SIZE];
					if(fsi_get_path_name(fs,k,pathname)==0){
						printf("file_name%d: %s\n",n++,pathname);
						break;
					}
					else
						return -1;
				}
			}
		}
		printf("*******************************************************\n");
	}
	return 0;
}

int getOwner(fs_t* fs,int block_number){
	for(int i=1;i<ITAB_SIZE;++i){
		fs_inode_t* inode=&fs->inode_tab[i];
		for(int j=0;inode->blocks[j]!=0;++j){
			if(inode->blocks[j]==block_number)
				return i;
		}
	}
	return -1;
}

void swap(fs_t* fs,inodeid_t s_owner,int src,int dst){
	int d_owner=getOwner(fs,dst);
	if(d_owner==s_owner){
		char buffer0[BLOCK_SIZE];
		char buffer1[BLOCK_SIZE];
		block_read(fs->blocks,src,buffer0);
		block_read(fs->blocks,dst,buffer1);
		block_write(fs->blocks,dst,buffer0);
		block_write(fs->blocks,src,buffer1);
		fs_inode_t* ownerInode=&fs->inode_tab[s_owner];
		int i,j;
		for(i=0;ownerInode->blocks[i]!=src;++i);
		for(j=0;ownerInode->blocks[j]!=dst;++j);
		ownerInode->blocks[i]=dst;
		ownerInode->blocks[j]=src;
	}
	else{
		if(d_owner==-1){
			char buffer[BLOCK_SIZE];
			block_read(fs->blocks,src,buffer);
			block_write(fs->blocks,dst,buffer);
			fs_inode_t* ownerInode=&fs->inode_tab[s_owner];
			int i;
			for(i=0;ownerInode->blocks[i]!=src;++i);
			ownerInode->blocks[i]=dst;
			BMAP_CLR(fs->blk_bmap,src);
			BMAP_SET(fs->blk_bmap,dst);
			char null_block[BLOCK_SIZE];
			memset(null_block,0,sizeof(null_block));
			block_write(fs->blocks,src,null_block);
		}
		else{
			char buffer0[BLOCK_SIZE];
			char buffer1[BLOCK_SIZE];
			block_read(fs->blocks,src,buffer0);
			block_read(fs->blocks,dst,buffer1);
			block_write(fs->blocks,dst,buffer0);
			block_write(fs->blocks,src,buffer1);
			fs_inode_t* sInode=&fs->inode_tab[s_owner];
			fs_inode_t* dInode=&fs->inode_tab[s_owner];
			int i,j;
			for(i=0;sInode->blocks[i]!=src;++i);
			for(j=0;dInode->blocks[j]!=dst;++j);
			sInode->blocks[i]=dst;
			dInode->blocks[j]=src;
		}
	}
			
	fsi_store_fsdata(fs);
}

#define MAX_NUM_BLKS ITAB_SIZE-1*INODE_NUM_BLKS

int fs_defrag(fs_t* fs)
{
	cache_flush(fs);
	
	int j=10;
	for(int i=10;i<MAX_NUM_BLKS;){
		if(BMAP_ISSET(fs->blk_bmap,i)){
			int owner=getOwner(fs,i);
			if(owner==-1)
				return -1;
			fs_inode_t* ownerInode=&fs->inode_tab[owner];
			for(int k=0;ownerInode->blocks[k]!=0;++k){
				if(ownerInode->blocks[k]!=j)
					swap(fs,owner,ownerInode->blocks[k],j);
				++j;
				++i;
			}
		}
		else
			++i;
	}
	return 0;
}

/***************************************************************************************************
*
*
*
*											CACHE
*
*
\***************************************************************************************************/

#include "sthread.h"


cache_node* fs_new_cache(fs_t* fs){
	cache_node* cache=(cache_node*)malloc(sizeof(cache_node)*CACHE_SIZE);
	for(int i=0;i<CACHE_SIZE;++i){
		cache[i].V=0;
		cache[i].R=0;
		cache[i].M=0;
		cache[i].counter=0;
		cache[i].block_number=-1;
		memset(cache[i].block,0,sizeof(cache[i].block));
	}
	if (sthread_create(thread_cache_function, (void*)fs, 1) == NULL) {
    printf("sthread_create failed\n");
    exit(1);
  }
	return cache;
}

void printBlock(char* block)
{
	for(int i=0;i<BLOCK_SIZE;++i){
		if(BMAP_ISSET(block,i))
			printf("1.");
		else
			printf("0.");
		if(i>0 && (i+1)%32==0)
			printf("\n");
	}
}

int fs_dumpcache()
{
	dprintf("===== Dump: Cache of Blocks Entries =======================\n");
	for(int i=0;i<CACHE_SIZE;++i){
		printf("Entry: %d\n",i);
		printf("V: %d M: %d R: %d\n",cache[i].V,cache[i].M,cache[i].R);
		if(cache[i].V==1){
			printf("Blk_Num: %d\n", cache[i].block_number);
			printf("Blk_Cnt:\n");
			printBlock(cache[i].block);
		}
		printf("************************************************************\n");
	}
	return 0;
}

void fs_write_back(fs_t* fs,int block_number);

void cache_excg(fs_t* fs,int block_number)
{
	for(int i=0;i<CACHE_SIZE;++i){
		if(cache[i].V==0){
			block_read(fs->blocks,block_number,cache[i].block);
			cache[i].R=1;
			cache[i].V=1;
			cache[i].block_number=block_number;
			return;
		}
	}
	for(int i=0;i<CACHE_SIZE;++i){
		if(cache[i].R==0 && cache[i].M==0){
			fs_write_back(fs,i);
			block_read(fs->blocks,block_number,cache[i].block);
			cache[i].R=1;
			cache[i].block_number=block_number;
			return;
		}
	}
	for(int i=0;i<CACHE_SIZE;++i){
		if(cache[i].R==0){
			fs_write_back(fs,i);
			block_read(fs->blocks,block_number,cache[i].block);
			cache[i].R=1;
			cache[i].block_number=block_number;
			return;
		}
	}
	for(int i=0;i<CACHE_SIZE;++i){
		if(cache[i].M==0){
			fs_write_back(fs,i);
			block_read(fs->blocks,block_number,cache[i].block);
			cache[i].R=1;
			cache[i].block_number=block_number;
			return;
		}
	}
	fs_write_back(fs,0);
	block_read(fs->blocks,block_number,cache[0].block);
	cache[0].R=1;
	cache[0].block_number=block_number;
}

int cache_read(int block_number,char* block_read)
{
	for(int i=0;i<CACHE_SIZE;++i){
		if(cache[i].block_number==block_number){
			memcpy(block_read,cache[i].block,BLOCK_SIZE);
			return 1;
		}
	}
	return 0;
}

int cache_write(int block_number,char* block)
{
	for(int i=0;i<CACHE_SIZE;++i){
		if(cache[i].block_number==block_number){
			memcpy(cache[i].block,block,BLOCK_SIZE);
			cache[i].R=1;
			cache[i].M=1;
			return 1;
		}
	}
	return 0;
}

void writeIn_cache(fs_t* fs, int block_number,char* block){
	if(!cache_write( block_number , block)){
		cache_excg(fs, block_number);
		if(!cache_write( block_number, block))
			dprintf("[cache_write] error writing from cache");
    }
}

void readFrom_cache(fs_t* fs, int block_number,char* block){
	if(!cache_read( block_number , block)){
		cache_excg(fs, block_number);
		if(!cache_read( block_number, block))
			dprintf("[cache_read] error reading from cache");
    }
}

void cache_clean(int block_number){
	for(int i=0;i<CACHE_SIZE;++i){
		if(cache[i].block_number==block_number)
			cache[i].V=0;
	}
}

void not_rec_used(int block_number)
{
	cache[block_number].R=0;
}

void fs_write_back(fs_t* fs,int block_number)
{
	block_write(fs->blocks,cache[block_number].block_number,cache[block_number].block);
	cache[block_number].M=0;
}

void cache_flush(fs_t*fs){
	for(int i=0; i<CACHE_SIZE; i++){
		fs_write_back(fs,i);
		cache[i].V=0;
	}
}


void * thread_cache_function(void* ptr)
{
	while(1){
		fs_t* fs=(fs_t*) ptr;
		sleep(1000);
		for(int i=0;i<CACHE_SIZE;++i){
			(cache[i].counter)++;
			if(cache[i].counter%4==0)
				not_rec_used(i);
			if(cache[i].counter%10==0)
				fs_write_back(fs,i);
			if(cache[i].counter==20)
				cache[i].counter=0;
		}
	}
	return 0;
}
