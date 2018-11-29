/*
 *
 *	FUSE: Filesystem in Userspace
 *
 *
 *	gcc -Wall `pkg-config fuse --cflags --libs` csc452fuse.c -o csc452
 *
 *
 * @author : Justin Benge <justinbng36@gmail.com>
 * Date    : 16 Novermber, 2018
 * 
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//The file representing the disk
#define DISK_FILE ".disk"

//Errors 
#define DISK_NFE {fprintf(stderr,"Disk File not found\n"); return(1);}
#define DISK_READ_ER {fprintf(stderr,"Error when reading from disk\n"); return(1);}
//The attribute packed means to not align these things
struct csc452_directory_entry
{
	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct csc452_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct csc452_file_directory) - sizeof(int)];
} ;

typedef struct csc452_root_directory csc452_root_directory;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct csc452_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct csc452_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct csc452_directory) - sizeof(int)];
} ;

typedef struct csc452_directory_entry csc452_directory_entry;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE)

struct csc452_disk_block
{
	//I am adding this to have a link to the next block of disk 
	//that will be needed to store a file. When nextBlock is -1 it will mean that
	//this is the last block of disk of a file. 
	long nextBlock;
	
	
	//All of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct csc452_disk_block csc452_disk_block;

//Lets just keep track of where we are in the file....
long int num_blocks = 0;

void check_errors(char *str)
{
    FILE *fp = fopen("/home/justin/errors.txt", "w");
    fprintf(fp, str);
    fclose(fp);
}
/**
 * let's just count up the number of slashes?
 */
int is_dir(const char *path) 
{
    int count = 0;
    char *temp;
    for(temp=path; *temp!='\0'; temp++)
    {
        if(*temp == '/')
            count++;
    }
//    fprintf(fp, "%d", count);
//    fclose(fp);
    return count >= 1;
}
int is_file(const char *path)
{
    return 0;
}

int loadRoot(csc452_root_directory*);

int dir_exists(const char *path) 
{
//    FILE *fp = fopen(".disk", "r");
//    if(fp == NULL)
//        return -1;
    csc452_root_directory root;
//    fread(&root, BLOCK_SIZE, 1, fp);
    loadRoot(&root);
    
    int i;
    for(i=0;i<root.nDirectories;i++)
    {
        char *temp = root.directories[i].dname;
        if(strcmp(temp, path))
            return 1;
       
        temp = NULL;
        free(temp);
    }
    //fclose(fp);

    //fp = NULL;
    //free(fp);

    return 0;
}

int file_exists(const char *path)
{
    /*
    char dir[] = strtok(path, "/");
    csc452_root_directory root;
    loadRoot(&root);
    
    long dir_start = findDirectory(root, dir);
    csc452_directory_entry dir;
    loadDir(&dir, dir_start);    

    int i;
    for(i=0;i<dir.nFiles;i++)
    {
        
    }
    */
    return 0;
}


/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not.
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int csc452_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;

	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else  {
		
		//If the path does exist and is a directory:
        if (is_dir(path) && dir_exists(path)) 
        {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
        }
		//If the path does exist and is a file:
        else if(is_file(path))
        {
            stbuf->st_mode = S_IFREG | 0666;
            stbuf->st_nlink = 2;
            stbuf->st_size = 512; //file size
        }	
		//Else return that path doesn't exist
        else
    		res = -ENOENT;

	}
	return res;
}

/*
 * This function is passed a string that contains a path
 * and pointers to strings where the different parts of 
 * the path will be stored. This function assumes that
 * the path provided is in the form /directory/file.ext.
 * This may be called with only /directory, in this case only
 * dir_name will be set
 */
int extractFromPath(const char path[],char *file_name, char *file_ext,char *dir_name){
	//path will have form /directory/file.ext
	char *nav=path;
	char *writeOn;//this will be where I will write
	writeOn=dir_name;
	int length=1;
	nav++;//skip the first slash
	//gets the directory
	while(*nav!='\0'){
		if(*nav!='/' && length<=MAX_FILENAME){
			*writeOn=*nav;
			nav++;
			writeOn++;
			length++;
		}else if(length>MAX_FILENAME){
			return 1;
		}else{
			break;
		}
	}
	//add null char and start extracting file next
	*writeOn='\0';
	//if the path has a file name too, extract it and its ext
	if(*nav!='\0'){
		nav++;//skip next back slash
		writeOn=file_name;
		length=1;
		while(*nav!='\0'){
			if(*nav!='.' && length<=MAX_FILENAME){
				*writeOn=*nav;
				nav++;
				writeOn++;
				length++;
			}else if(length>MAX_FILENAME){
				return 2;
			}else{
				break;
			}
		}
		//add null char and start extracting file name
		*writeOn='\0';
		nav++;//skip period
		writeOn=file_ext;
		length=1;
		while(*nav!='\0'){
			if(length<=MAX_EXTENSION){
				*writeOn=*nav;
				nav++;
				writeOn++;
				length++;
			}else{
				return 3;
			}
		}
		*writeOn='\0';
	}
	return 0;
}

/*	This function will read from a file representing disk
 *	and load from it the root structure into a struct pointed by
 *	the input pointers
 */
int loadRoot(csc452_root_directory *root){
	 FILE* fp=fopen(DISK_FILE, "r");
	 if(fp==NULL) DISK_NFE;
	 //int fseek(FILE *stream, long int offset, int whence)
	 fseek(fp,0,SEEK_SET);
	 //size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
	 int ret =fread(root, BLOCK_SIZE, 1, fp);
	 fclose(fp);
	 if(ret!=1) DISK_READ_ER;
	 return 0;
 }

/*	This function will take in a pointer to a root struct and a string with a directory
 *	name and search in the root directories for this directory, if it is not found it 
 * 	will return -1 and if found it return a a number that represents where it is in disk
 */
long findDirectory(csc452_root_directory *root, char name[]){
	int i;
	int numDirs=root->nDirectories;
	for(i=0;i<numDirs;i++){
		if(!strcmp(root->directories[i].dname, name)){//strcmp returns zero if equal
			return root->directories[i].nStartBlock;
		}
	}
	return (long)(-1);
}

/*	This function will receive a pointer to a directory struct and a long that
 *	holds its location in .disk and then it will load the dir from .disk into the struct
 */
int loadDir(csc452_directory_entry *dir, long location){
	 FILE* fp=fopen(DISK_FILE, "r");
	 if(fp==NULL) DISK_NFE;
	 //int fseek(FILE *stream, long int offset, int whence)
	 long inFileLoc=location*BLOCK_SIZE;
	 fseek(fp,inFileLoc,SEEK_SET);
	 //size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
	 int ret =fread(dir, BLOCK_SIZE, 1, fp);
	 fclose(fp);
	 if(ret!=1) DISK_READ_ER;
	 return 0;
 }
/* This function will take in two strings one with a file name and one with an extension
 * and concat them with a period in between into a third string passed with pointer
 * This function assumes the string where the result will be stored has enough memory allocated
 * This function returns the number of characters loaded into fullName
 */
void getFullFileName(char *fileName, char *extension, char *fullName){
	char *source=fileName;
	char *destination=fullName;
	while(*source!='\0'){
		*destination=*source;
		source++;
		destination++;
	}
	//add period
	*destination='.';
	destination++;
	//add extension
	source=extension;
	while(*source!='\0'){
		*destination=*source;
		source++;
		destination++;

	}
	return;
}
/*
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int csc452_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	//satisfy the compiler
	(void) offset;
	(void) fi;
    //There will be a root structure that contains 
	//info to directory structures that contain files structs
	//each directory can be identified by name as well as the files
	char file_name[MAX_FILENAME+1]="\0";
	char file_ext[MAX_EXTENSION+1]="\0";
	char dir_name[MAX_FILENAME+1]="\0";//assume dir name max is same as file FOR NOW
	
	//extract these from path
	int retVal=extractFromPath(path,&file_name[0],&file_ext[0],&dir_name[0]);
	
	//*****ensure path is a directory*****
	if(file_name[0]!='\0'){//if a file was processed then, it is not a dir
		return -ENOTDIR;
	}
	//if retVal was nonzero then there was an error with the length of a section of path
	if(retVal || path[0]!='/') return -ENOENT;
	/*****Load contents from the root*****/
	/* I need the root to either get all of its children or 
	   so I can add them, or I need it so I can search for
	   the target directory in its children
	 */
	csc452_root_directory root;
	int ret=loadRoot(&root);
	if(ret) return -EIO; //file handle invalid
	if (strcmp(path, "/") != 0) {
		//this is not the root so it must be a directory in the root 
		/*filler(buf, ".", NULL,0);
		filler(buf, "..", NULL, 0);*/
		//filler format 
		//filler(void *buff, char *name, struct stat *stbuff,off_t offf)
		//list all files in this directory find this directory in the root
		long location=findDirectory(&root, dir_name);
		if (location==-1)return -ENOENT;
		//load the directory
		csc452_directory_entry thisDir;
		ret=loadDir(&thisDir, location);
		if(ret) return -EIO;
		//get all files from disk struct
		int i,numFiles=thisDir.nFiles;
		
		for(i=0; i<numFiles;i++){
			//assume within a directory are all next to each other and thay nfile is updated to current num of files
			char fullName[MAX_FILENAME+MAX_EXTENSION+2];
			getFullFileName(thisDir.files[i].fname,thisDir.files[i].fext,&fullName[0]);
			filler(buf, fullName, NULL, 0);
		}
	}
	else {
		//For the root, all of its child directories should be added
		//there is an array of directory structs inside the root struct
		int i=0;
		int numOfDirs=root.nDirectories;
		for(i=0;i<numOfDirs;i++){
			struct csc452_directory currDir=root.directories[i];
			//add all directories to the buff
			//assume all valid directories exists sequentially in array
			filler(buf, currDir.dname,NULL, 0);//Can I assume all directories are in seq in array or should i search among all ????
		}
			
	}
	//???On success, 1 is returned. On end of directory, 0 is returned
	return 0;
}

/*
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int csc452_mkdir(const char *path, mode_t mode)
{
	(void) mode;
  
    //wait..... 
    csc452_root_directory root;
    loadRoot(&root);
    //As of right now this is insuffiecient
    //I will need to create a 512b block as well in .disk
    //for my directory to store files....
    //Also I'm missing a bunch of error checks

    if(&root != NULL)
    {
        if(dir_exists(path))
            return -EEXIST;
        
        csc452_directory_entry *dir = malloc(sizeof(csc452_directory_entry));
        dir->nFiles = 0;
        

        strcpy(root.directories[root.nDirectories++].dname, path);
        root.directories[root.nDirectories].nStartBlock = BLOCK_SIZE * (++num_blocks);

        FILE *disk_write_fp = fopen(".disk", "w");
        fseek(disk_write_fp, 0, SEEK_SET);
        fwrite(&root, BLOCK_SIZE, 1, disk_write_fp);
        
        fseek(disk_write_fp, num_blocks * BLOCK_SIZE, SEEK_SET);
        fwrite(dir, BLOCK_SIZE, 1, disk_write_fp);
        fclose(disk_write_fp);
        disk_write_fp = NULL;
        free(disk_write_fp);
    }
   	return 0;
}


void shift_directories(csc452_root_directory *root)
{
}
/*
 * Removes a directory (must be empty)
 *
 */
static int csc452_rmdir(const char *path)
{
    csc452_root_directory root;
    loadRoot(&root);

    //first the errors
    if(!dir_exists(path))
        return -ENOENT;
    if(!is_dir(path))
        return -ENOTDIR;
    
    //now lets actually find the directory
    int i;
    for(i=0;i<root.nDirectories;i++)
    {
        if(strcmp(path, root.directories[i].dname))
        {
            long start = findDirectory(&root, path);
            csc452_directory_entry dir;
            loadDir(&dir, start);
            if(dir.nFiles == 0)
            {
                int j;
                for(int j=i;j<root.nDirectories-1;j++)
                {
                    root.directories[j] = root.directories[j+1];
                }
                root.nDirectories -= 1;
                shift_directories(&root);
                return 0;
            }
            else
                return -ENOTEMPTY;
        }
    }
    
	return -ENOENT;
}

/*
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 * Note that the mknod shell command is not the one to test this.
 * mknod at the shell is used to create "special" files and we are
 * only supporting regular files.
 *
 */
static int csc452_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
    (void) dev;
	
    csc452_root_directory root;
    loadRoot(&root);

    if(file_exists(path))
        return -EEXIST;

    csc452_disk_block *nod = malloc(sizeof(csc452_disk_block));
    char *dir_name = strtok(path, "/"); 
    long dir_start = findDirectory(&root, dir_name[0]);
    csc452_directory_entry dir;
    loadDir(&dir, dir_start);    
   
    char *token = strtok(path, ".");
    strcpy(dir.files[dir.nFiles].fname, token);
    token = strtok(NULL, ".");
    strcpy(dir.files[dir.nFiles++].fext, token);
    check_errors(dir.files[dir.nFiles - 1].fname);
//    char *fname = strtok(token[1], ".");  
//    check_errors(token);
//    dir.files[dir.nFiles].fname = path; 

	return 0;
}
/* 
 * This function will search in the files of a directory pointed by 
 * *directory and will return the long with the location to the file starting pointed
 * when found, or -1 otherwise. 
 * When found the memory pointed by fsize gets set to the file's size.
 * There is an assumption that all valid files are
 * adjacent to each other (no invalid file in between.
 */
long findFile(csc452_directory_entry *directory, char fname[], char fext[], size_t *fsize,int write, size_t newSize){
	int i;
	int numFiles=directory->nFiles;
	for(i=0;i<numFiles;i++){
		if(!strcmp(directory->files[i].fname, fname) && !strcmp(directory->files[i].fext,fext)){//strcmp returns zero if equal
			*fsize=directory->files[i].fsize;//pass old size
			if(write){
				directory->files[i].fsize=newSize;
			}
			return directory->files[i].nStartBlock;
		}
	}
	return (long)(-1);
}

/*
 * This function will take in a pointer to a disk block and a long with the location 
 * of that block in memory and then load the block into the struct. 
 * It returns zero if successful or other if there were errors.
 */
int loadFile(csc452_disk_block *block, long location){
	 FILE* fp=fopen(DISK_FILE, "r");
	 if(fp==NULL) DISK_NFE;
	 //int fseek(FILE *stream, long int offset, int whence)
	 long inFileLoc=location*BLOCK_SIZE;
	 fseek(fp,inFileLoc,SEEK_SET);
	 //size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
	 int ret =fread(block, BLOCK_SIZE, 1, fp);
	 fclose(fp);
	 if(ret!=1) DISK_READ_ER;
	 return 0;
 }
 /*
 * This function will determine if a path is valid 
 * and if valid it will determine if exists. It will return 
 * an error code if there was an issue with the path or 0 if the path was good,
 * and if good it initialized the fields pointed by the passed pointers
 * to the file's location and size
 */
int fileDoesntExists(const char *path, long *fileLoc, size_t *fileSize,int onWrite, size_t newSize){
	//check if the format is good
	char file_name[MAX_FILENAME+1]="\0";
	char file_ext[MAX_EXTENSION+1]="\0";
	char dir_name[MAX_FILENAME+1]="\0";//assume dir name max is same as file FOR NOW
	
	//extract these from path
	int invalid=extractFromPath(path,&file_name[0],&file_ext[0],&dir_name[0]);
	if(invalid) return -ENOENT;//path was not valid due to length of names
	if(file_name=='\0'){//if no file was read then it would the root or a directory so they cant be read
		return -EISDIR;
	}
	// Need to determine if file actually exists
	csc452_root_directory root;
	int ret=loadRoot(&root);
	if(ret) return -EIO; //could not load from .disk
	//search for directory in root struct
	long location=findDirectory(&root, dir_name);
	if (location==-1)return -ENOENT; //the directory was not found in root
	//load the directory
	csc452_directory_entry thisDir;
	ret=loadDir(&thisDir, location); 
	if(ret) return -EIO;//problem reading from .disk
	//search for file in directory
	*fileLoc=findFile(&thisDir, file_name,file_ext,fileSize, onWrite, newSize);
	if(*fileLoc==-1) return -ENOENT; //this file was not found 
	//return true
	return 0;
}
/*
 * Read size bytes from file into buf starting from offset
 *
 */
static int csc452_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	(void) fi;
	/******    check to make sure path exists    *************/
	size_t fileSize;
	long fileLoc;
	int doesnt=fileDoesntExists(path, &fileLoc, &fileSize,0,0);
	if(doesnt){//anything other than 0 is true in c
		return doesnt;
	}
	//check that size is > 0 ???
	if(size<=0) return 0;
	/******** check that offset is <= to the file size ********/
	if(offset>fileSize){
		return 0;
	}
	/*******              read in data       *****/
	//find out if it will fit in a block
	csc452_disk_block data;
	//the offset changes how much data can be read from the first data block
	//from it 
	int ret=loadFile(&data, fileLoc);
	if(ret) return -EIO;
	//variables to keep track about how much data still needs to be read
	size_t thisRead=0;
	size_t allReads=0;
	if(fileSize<size){
		//cannot read more than what the file has so update size
		size=fileSize;
	}
	//There could be blocks of data that were placed before offset so I need to skip over these
	off_t skipBlocks=offset/BLOCK_SIZE;
	while(skipBlocks>0){
		if(data.nextBlock==-1){
			//next block was not defined
			return -EIO;
		}
		//load next block of disk
		fileLoc=data.nextBlock;
		ret=loadFile(&data, fileLoc);//update struct
		if(ret) return -EIO;
		skipBlocks--;
	}
	//I now have the first block I need to read from, offset affects this one
	//starting point 
	off_t ofInBlock=offset%BLOCK_SIZE;
	thisRead=BLOCK_SIZE-ofInBlock;
	memcpy(buf, data.data+ofInBlock,thisRead);//read the remaining until the end of the first block
	allReads=allReads+thisRead;
	//Any following reads will not be affected by offset on their data starting point
	while(allReads<size){
		//I want to read the rest of the data from the next disks
		fileLoc=data.nextBlock;
		if(fileLoc==-1){
			//we reached the last block
			break;
		}
		ret=loadFile(&data, fileLoc);//update struct
		if(ret) return -EIO;
		
		if((size-allReads)<=BLOCK_SIZE){
			//this is the last block that needs to be read
			memcpy(buf, data.data, size-allReads);
			allReads=size;
		}else{
			//the whole block needs to be read
			memcpy(buf, data.data,BLOCK_SIZE);
			allReads=allReads+BLOCK_SIZE;
		}
	}
	return allReads;
}
	
/*
 * Write size bytes from buf into file starting from offset
 *
 */
static int csc452_write(const char *path, const char *buf, size_t size,
			  off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	/*         check to make sure path exists        */
	size_t fileSize;
	long fileLoc;
	//get the new size of file after writing to it
	size_t newSize=offset+size;
	int doesnt=fileDoesntExists(path, &fileLoc, &fileSize,1,newSize);
	//what happens if overwritten is smaller than what it used to be???
	//if larger add more blocks if the same just dont add any 
	//but if smaller should disk blocks be deallocated?
	if(doesnt){//anything other than 0 is true in c
		return doesnt;
	}
	/*              check that size is > 0            */
	if(size<=0) return 0;
	/*    check that offset is <= to the file size    */
	if(offset>fileSize){
		return -EFBIG;
	}
	/*                  write data                    */
	//I need to update size of the file struct, so I will need access to it
	//Load first disk of data
	csc452_disk_block data; 
	int ret=loadFile(&data, fileLoc);
	if(ret) return -EIO;
	/* There are possibly three blocks of interest when writing.
		1.	The first block after the offset:
				To reach this one we need to skip over blocks covered by offset
				then, we need to find the offset within it.
		2.	Blocks that are written into fully:
				All of their data slots are written into.
		3.	The last block that you write into:
				This block gets partially written into when the last of the data 
				from buff is written.
	*/
	//1.	Skip the blocks passed with offset, the file must be at least as big as the 
	//offset then, there must be enough disk blocks that exist that will cover the offset
	
	off_t skipBlocks=offset/BLOCK_SIZE;
	while(skipBlocks>0){
		if(data.nextBlock==-1){
			//next block was not defined
			return -EIO;
		}
		//load next block of disk
		fileLoc=data.nextBlock;
		ret=loadFile(&data, fileLoc);//update struct
		if(ret) return -EIO;
		skipBlocks--;
	}
	off_t ofInBlock=offset%BLOCK_SIZE; //offset within first block
	size_t thisWrite=0;
	size_t allWrites=0;
	thisWrite=BLOCK_SIZE-ofInBlock;
	memcpy(data.data+offset,buf,thisWrite);
	allWrites=thisWrite;
	//now need to determine where to write next
	while(size<allWrites){
		//write to next allocated data block unless there are no more
		fileLoc=data.nextBlock;
		if(fileLoc==-1){
			//we reached the last block, but we are not done writing so we need to get a new block
			/***We have not created blocks and added them to FAT yet so this is incomplete***/
			
		}
		//assuming that a new disk was created and that file lock containes the number to it
		ret=loadFile(&data, fileLoc);//update struct to next disk
		if(ret) return -EIO;		
		if((size-allWrites)<=BLOCK_SIZE){
			//this is the last block that needs to be written into
			memcpy(data.data,buf+allWrites, size-allWrites);
			//set the rest of this block to zero
			size_t nextEl=size-allWrites+1;
			size_t i;
			for(i=nextEl;i<BLOCK_SIZE;i++){
				data.data[i]=0;
			}
			allWrites=size;
		}else{
			//the whole block needs to be written into
			memcpy(buf+allWrites, data.data,BLOCK_SIZE);
			allWrites=allWrites+BLOCK_SIZE;
		}
	}
	//when everything has been written there could be a possibility that the overwrite made
	//the file smaller so the remaining disks from the previous write need to be deallocated
	if(newSize<=fileSize){
		//deallocate all consecutive blocks
		long nextB;
		while((nextB=data.nextBlock)!=-1){
			//dealocate the block at nextB
		}
	}		
	//return success, or error
	return size;
}

/*
 * Removes a file.
 *
 */
static int csc452_unlink(const char *path)
{
        (void) path;
        return 0;
}


/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int csc452_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}

/*
 * Called when we open a file
 *
 */
static int csc452_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int csc452_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations csc452_oper = {
    .getattr	= csc452_getattr,
    .readdir	= csc452_readdir,
    .mkdir		= csc452_mkdir,
    .read		= csc452_read,
    .write		= csc452_write,
    .mknod		= csc452_mknod,
    .truncate	= csc452_truncate,
    .flush		= csc452_flush,
    .open		= csc452_open,
    .unlink		= csc452_unlink,
    .rmdir		= csc452_rmdir
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &csc452_oper, NULL);
}
