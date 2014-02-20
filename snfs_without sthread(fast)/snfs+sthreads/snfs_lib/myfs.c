/* 
 * File System Interface
 * 
 * myfs.c
 *
 * Implementation of the SNFS programming interface simulating the 
 * standard Unix I/O interface. This interface uses the SNFS API to
 * invoke the SNFS services in a remote server.
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <myfs.h>
#include <snfs_api.h>
#include <snfs_proto.h>
#include <unistd.h>
#include "queue.h"


#ifndef SERVER_SOCK
#define SERVER_SOCK "/tmp/server.socket"
#endif

#define MAX_OPEN_FILES 10	// how many files can be open at the same time


static queue_t *Open_files_list;	// Open files list
static int Lib_initted = 0;	// Flag to test if library was initiated
static int Open_files = 0;	// How many files are currently open


int mkstemp(char *template);

int myparse(char *pathname);

int my_init_lib(){
	char CLIENT_SOCK[]="/tmp/clientXXXXXX";
	if(mkstemp(CLIENT_SOCK)<0){
		printf("[my_init_lib] Unable to create client socket.\n");
		return -1;
	}
	if(snfs_init(CLIENT_SOCK,SERVER_SOCK)<0){
		printf("[my_init_lib] Unable to initialize SNFS API.\n");
		return -1;
	}
	Open_files_list=queue_create();
	Lib_initted=1;
	return 0;
}

int my_open(char* name,int flags){
	if(!Lib_initted){
		printf("[my_open] Library is not initialized.\n");
		return -1;
	}
	if(Open_files>=MAX_OPEN_FILES){
		printf("[my_open] All slots filled.\n");
		return -1;
	}
	if ( myparse(name) != 0 ) {
		printf("[my_open] Malformed pathname.\n");
		return -1;
	}
	snfs_fhandle_t dir, file_fh;
	unsigned fsize = 0;
	char newfilename[MAX_FILE_NAME_SIZE];
	char newdirname[MAX_PATH_NAME_SIZE];
	char fulldirname[MAX_PATH_NAME_SIZE];
	char *token;
	char *search="/";
	int i=0;
	memset(&newfilename,0,MAX_FILE_NAME_SIZE);
	memset(&newdirname,0,MAX_PATH_NAME_SIZE);
	memset(&fulldirname,0,MAX_PATH_NAME_SIZE);
	strcpy(fulldirname,name);
	token = strtok(fulldirname, search);

	while(token != NULL) {
		i++;
		strcpy(newfilename,token);
		token = strtok(NULL, search);
	} 
	if ( i > 1){
		strncpy(newdirname, name, strlen(name)-(strlen(newfilename)+1));
		newdirname[strlen(newdirname)]='\0';
	}
	else {  
		strncpy(newfilename, &name[1], (strlen(name)-1));
		newfilename[strlen(newfilename)]='\0';
		strcpy(newdirname, name);   
	}
	if(newfilename == NULL) {
		printf("[my_open] Error looking for directory in server.\n");
		return -1;
	}
	snfs_call_status_t status = snfs_lookup(name,&file_fh,&fsize);
	if (status != STAT_OK) {
		snfs_lookup(newdirname,&dir,&fsize);        
   	if (i==1)  //Create a file in Root directory
			dir = ( snfs_fhandle_t)  1;
	}
	if(flags == O_CREATE && status != STAT_OK) {
		if (snfs_create(dir,newfilename,&file_fh) != STAT_OK) {
			printf("[my_open] Error creating a file in server.\n");
			return -1;
		}
	}
	else
		if (status != STAT_OK) {
			printf("[my_open] Error opening up file. %d \n",file_fh);
			return -1;
		}
	fd_t fdesc = (fd_t) malloc(sizeof(struct _file_desc));
	fdesc->fileId = file_fh;
	fdesc->size = fsize;
	fdesc->write_offset = 0;
	fdesc->read_offset = 0;
	queue_enqueue(Open_files_list, fdesc);
	Open_files++;
	return file_fh;
}

int my_read(int fileId, char* buffer, unsigned numBytes)
{
	if (!Lib_initted) {
		printf("[my_read] Library is not initialized.\n");
		return -1;
	}
	
	fd_t fdesc = queue_node_get(Open_files_list, fileId);
	if(fdesc == NULL) {
		printf("[my_read] File isn't in use. Open it first.\n");
		return -1;
	}
	
	// EoF ?
	if(fdesc->read_offset == fdesc->size)
		return 0;
	
	int nread;
	unsigned rBytes;
	int counter = 0;
	
	// If bytes to be read are greater than file size
	if(fdesc->size < ((unsigned)fdesc->read_offset) + numBytes)
		numBytes = fdesc->size - (unsigned)(fdesc->read_offset);
	
	//if data must be read using blocks
	if(numBytes > MAX_READ_DATA)
		rBytes = MAX_READ_DATA;
	else
		rBytes = numBytes;
	
	while(numBytes) {
		if(rBytes > numBytes)
			rBytes = numBytes;
		
		if (snfs_read(fileId,(unsigned)fdesc->read_offset,rBytes,&buffer[counter],&nread) != STAT_OK) {
			printf("[my_read] Error reading from file.\n");
			return -1;
		}
		fdesc->read_offset += nread;
		numBytes -= (unsigned)nread;
		counter += nread;
	}
	
	return counter;
}

int my_write(int fileId, char* buffer, unsigned numBytes)
{
	if (!Lib_initted) {
		printf("[my_write] Library is not initialized.\n");
		return -1;
	}
	
	fd_t fdesc = queue_node_get(Open_files_list, fileId);
	if(fdesc == NULL) {
		printf("[my_write] File isn't in use. Open it first.\n");
		return -1;
	}
	
	unsigned fsize;
	unsigned wBytes;
	int counter = 0;
	
	if(numBytes > MAX_WRITE_DATA)
		wBytes = MAX_WRITE_DATA;
	else
		wBytes = numBytes;
	
	while(numBytes) {
		if(wBytes > numBytes)
			wBytes = numBytes;
		
		if (snfs_write(fileId,(unsigned)fdesc->write_offset,wBytes,&buffer[counter],&fsize) != STAT_OK) {
			printf("[my_write] Error writing to file.\n");
			return -1;
		}
		fdesc->size = fsize;
		fdesc->write_offset += (int)wBytes;
		numBytes -= (unsigned)wBytes;
		counter += (int)wBytes;
	}
	
	return counter;
}

int my_close(int fileId)
{
	if (!Lib_initted) {
		printf("[my_close] Library is not initialized.\n");
		return -1;
	}
	
	fd_t temp = queue_node_remove(Open_files_list, fileId);
	if(temp == NULL) {
		printf("[my_close] File isn't in use. Open it first.\n");
		return -1;
	}
	
	free(temp);
	Open_files--;
	
	return 0;
}


int my_listdir(char* path, char **filenames, int* numFiles)
{
	if (!Lib_initted) {
		printf("[my_listdir] Library is not initialized.\n");
		return -1;
	}
	
	snfs_fhandle_t dir;
	unsigned fsize;	
	
	if ( myparse(path) != 0 ) {
		printf("[my_listdir] Error looking for folder in server.\n");
		return -1;
	}   
     		
	if ((strlen(path)==1) && (path[0]== '/') ) 
		dir = ( snfs_fhandle_t)  1;
	else
		if(snfs_lookup(path, &dir, &fsize) != STAT_OK) {
	     printf("[my_listdir] Error looking for folder in server.\n");
	     return -1;
	   }
	
	
	snfs_dir_entry_t list[MAX_READDIR_ENTRIES];
	unsigned nFiles;
	char* fnames;
	
	if (snfs_readdir(dir, MAX_READDIR_ENTRIES, list, &nFiles) != STAT_OK) {
		printf("[my_listdir] Error reading directory in server.\n");
		return -1;
	}
	
	*numFiles = (int)nFiles;
	
	*filenames = fnames = (char*) malloc(sizeof(char)*((MAX_FILE_NAME_SIZE+1)*(*numFiles)));
	for (int i = 0; i < *numFiles; i++) {
		strcpy(fnames, list[i].name);
		fnames += strlen(fnames)+1;
	}
	
	return 0;
}

int my_mkdir(char* dirname){
	if (!Lib_initted) {
		printf("[my_mkdir] Library is not initialized.\n");
		return -1;
	}

	if ( myparse(dirname) != 0 ) {
		printf("[my_mkdir] Malformed pathname.\n");
		return -1;
	}
	
	
	
	snfs_fhandle_t dir, newdir;
	unsigned fsize;
	char newfilename[MAX_FILE_NAME_SIZE];
	char newdirname[MAX_PATH_NAME_SIZE];
	char fulldirname[MAX_PATH_NAME_SIZE];
	char *token;
	char *search="/";
	int i=0;
	
	memset(&newfilename,0,MAX_FILE_NAME_SIZE);
	memset(&newdirname,0,MAX_PATH_NAME_SIZE);
	memset(&fulldirname,0,MAX_PATH_NAME_SIZE);
	
	if(snfs_lookup(dirname, &dir, &fsize) == STAT_OK) {
		printf("[my_mkdir] Error creating a  subdirectory that already exists.\n");
		return -1;
	}
	

	strcpy(fulldirname,dirname);
	token = strtok(fulldirname, search);


	while(token != NULL) {
		i++;
		strcpy(newfilename,token);
		token = strtok(NULL, search);
	} 
	if ( i > 1){
		strncpy(newdirname, dirname, strlen(dirname)-(strlen(newfilename)+1));
		newdirname[strlen(newdirname)]='\0';//CORRECCAO
	}
	else {  
		strncpy(newfilename, &dirname[1], (strlen(dirname)-1));
		newfilename[strlen(newfilename)]='\0';//CORRECCAO
		strcpy(newdirname, dirname);   
	}    
	

	if(newdirname == NULL) {
		printf("[my_mkdir] Error looking for directory in server.\n");
		return -1;
	}
	
	
	if (i==1)  //Create a directory in Root
		dir = ( snfs_fhandle_t)  1;
	else   //Create a directory elsewhere
		if(snfs_lookup(newdirname, &dir, &fsize) != STAT_OK) {
			printf("[my_mkdir] Error creating a  subdirectory which has a wrong pathname.\n");
			return -1;
		}


	if(snfs_mkdir(dir, newfilename, &newdir) != STAT_OK) {
		printf("[my_mkdir] Error creating new directory in server.\n");
		return -1;
	}
	
	return 0;
}

int myparse(char* pathname) {

	char line[MAX_PATH_NAME_SIZE]; 
	char *token;
	char *search = "/";
	int i=0;

	strcpy(line,pathname); 
	if(strlen(line) >= MAX_PATH_NAME_SIZE || (strlen(line) < 1) ) {
		return -1; 
	}
	if (strchr(line, ' ') != NULL || strstr( (const char *) line, "//") != NULL || line[0] != '/' ) {
		return -1; 
	}


	if ((i=strlen(pathname)) && line[i]=='/') {
		return -1; 
	}
	   	i=0;
	token = strtok(line, search);

	while(token != NULL) {
		if ( strlen(token) > MAX_FILE_NAME_SIZE -1) { 
			return -1; 
		}
		i++;

		token = strtok(NULL, search);
	}

	return 0;
}

void removeLastName(char* path,char* file,char* newpath){
	int i;
	char temp[MAX_PATH_NAME_SIZE];
	strcpy(temp,path);
	for(i=0;temp[i]!='\0';++i);
	for(;temp[i-1]!='/';--i);
	temp[i-1]='\0';
	char* tempPtr=&temp[i];
	strcpy(file,tempPtr);
	if(temp[0]=='\0'){
		temp[0]='/';
		temp[1]='\0';
	}
	strcpy(newpath,temp);
}

int my_remove(char* name){
	char dirPathName[MAX_PATH_NAME_SIZE];
	char fileName[MAX_FILE_NAME_SIZE];
	snfs_fhandle_t file;
	snfs_fhandle_t dir;
	unsigned fileSize;
	unsigned dirSize;
	if(snfs_lookup(name,&file,&fileSize)!=STAT_OK){
		printf("[my_remove] Error no file/directory found with that pathname.\n");
		return -1;
	}
	removeLastName(name,fileName,dirPathName);
	snfs_lookup(dirPathName,&dir,&dirSize);
	if(strcmp(dirPathName,"/")==0)
		dir=1;
	
	if(snfs_remove(dir,fileName,&file)!=STAT_OK){
		printf("[my_remove] Error removing file/directory.\n");
		return -1;
	}
	return 0;
}

int my_copy(char* name1,char* name2){
	snfs_fhandle_t file1;
	unsigned file1size;
	char fileName1[MAX_FILE_NAME_SIZE];
	char dirPathName1[MAX_PATH_NAME_SIZE];
	snfs_fhandle_t dir1;
	unsigned dirSize1;
	snfs_fhandle_t file2;
	char fileName2[MAX_FILE_NAME_SIZE];
	char dirPathName2[MAX_PATH_NAME_SIZE];
	snfs_fhandle_t dir2;
	unsigned dirSize2;
	if(snfs_lookup(name1,&file1,&file1size)!=STAT_OK){
		printf("[my_copy] Error no file/directory found with that pathname.\n");
		return -1;
	}
	removeLastName(name1,fileName1,dirPathName1);
	removeLastName(name2,fileName2,dirPathName2);
	snfs_lookup(dirPathName1,&dir1,&dirSize1);
	snfs_lookup(dirPathName2,&dir2,&dirSize2);
	if(strcmp(dirPathName1,"/")==0)
		dir1=1;
	if(strcmp(dirPathName2,"/")==0)
		dir2=1;	
	
	if(snfs_copy(dir1,fileName1,dir2,fileName2,&file2)!=STAT_OK){
		printf("[my_copy] Error copying file/directory.\n");
		return -1;
	}
	return 0;
}

int my_append(char* name1,char* name2){
	snfs_fhandle_t file1;
	unsigned file1size;
	char fileName1[MAX_FILE_NAME_SIZE];
	char dirPathName1[MAX_PATH_NAME_SIZE];
	snfs_fhandle_t dir1;
	unsigned dirSize1;
	snfs_fhandle_t file2;
	unsigned file2size;
	char fileName2[MAX_FILE_NAME_SIZE];
	char dirPathName2[MAX_PATH_NAME_SIZE];
	snfs_fhandle_t dir2;
	unsigned dirSize2;
	if(snfs_lookup(name1,&file1,&file1size)!=STAT_OK || snfs_lookup(name2,&file2,&file2size)!=STAT_OK){
		printf("[my_append] Error no file/directory found with that pathname.\n");
		return -1;
	}
	
	removeLastName(name1,fileName1,dirPathName1);
	removeLastName(name2,fileName2,dirPathName2);
	snfs_lookup(dirPathName1,&dir1,&dirSize1);
	snfs_lookup(dirPathName2,&dir2,&dirSize2);
	if(strcmp(dirPathName1,"/")==0)
		dir1=1;
	if(strcmp(dirPathName2,"/")==0)
		dir2=1;	
	
	if(snfs_append(dir1,fileName1,dir2,fileName2,&file1size)!=STAT_OK){
		printf("[my_append] Error appending files.\n");
		return -1;
	}
	return 0;
}

int my_defrag(){
	if(snfs_defrag()!=STAT_OK){
		printf("[my_defrag] Error defragging\n");
		return -1;
	}
	return 0;
}

int my_diskusage(){
	if(snfs_diskusage()!=STAT_OK){
		printf("[my_diskusage] Error defragging\n");
		return -1;
	}
	return 0;
}
int my_dumpcache(){
	if(snfs_dumpcache()!=STAT_OK){
		printf("[my_dumpcache] Error defragging\n");
		return -1;
	}
	return 0;
}
