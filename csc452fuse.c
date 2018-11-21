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
	//All of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct csc452_disk_block csc452_disk_block;


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


int dir_exists(const char *path) 
{
    FILE *fp = fopen(".disk", "r");
    if(fp == NULL)
        return -1;
    csc452_root_directory root;
    fread(&root, 512, 1, fp);
    
    int i;
    for(i=0;i<root.nDirectories;i++)
    {
        char *temp = path;
        char *token = strtok(temp, "/");
        if(strcmp(token, path))
            return 1;
    temp = NULL;
    free(temp);
    }
    fclose(fp);

    fp = NULL;
    free(fp);

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
void extractFromPath(const char path[],char *file_name, char *file_ext,char *dir_name){
	//path will have form /directory/file.ext
	char *nav=path;
	char *writeOn;//this will be where I will write
	writeOn=dir_name;
	nav++;//skip the first slash
	//gets the directory
	while(*nav!='\0'){
		if(*nav!='/'){
			*writeOn=*nav;
			nav++;
			writeOn++;
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
		while(*nav!='\0'){
			if(*nav!='.'){
				*writeOn=*nav;
				nav++;
				writeOn++;
			}else{
				break;
			}
		}
		//add null char and start extracting file name
		*writeOn='\0';
		nav++;//skip period
		writeOn=file_ext;
		while(*nav!='\0'){
			*writeOn=*nav;
			nav++;
			writeOn++;
		}
		*writeOn='\0';
	}
	return;
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
	extractFromPath(path,&file_name[0],&file_ext[0],&dir_name[0]);
	
	//*****ensure path is a directory*****
	if(path[0]!='/' || file_name[0]!='\0'){//if a file was processed then, it is not a dir
		return -ENOENT;
	}
	/*****Load contents from the root*****/
	/* I need the root to either get all of its children or 
	   so I can add them, or I need it so I can search for
	   the target directory in its children
	 */
	csc452_root_directory root;
	int ret=loadRoot(&root);
	if(ret) return -EBADF; //file handle invalid
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
		if(ret) return -EBADF;
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

	return 0;
}

/*
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int csc452_mkdir(const char *path, mode_t mode)
{
	(void) path;
	(void) mode;
  
    //wait..... 
    csc452_directory_entry *dir = malloc(sizeof(csc452_directory_entry));
    FILE *disk = fopen(".disk", "r");
    csc452_root_directory root;
    fread(&root, 512, 1, disk);
    fclose(disk);

    //As of right now this is insuffiecient
    //I will need to create a 512b block as well in .disk
    //for my directory to store files....
    //Also I'm missing a bunch of error checks
    if(disk != NULL)
    {
        if(dir_exists(path))
            return -EEXIST;

        
        dir->nFiles = 0;
        fclose(disk);
        strcpy(root.directories[++root.nDirectories].dname, path);
        root.directories[root.nDirectories].nStartBlock = 512 * (root.nDirectories + 1);
        disk = fopen(".disk", "w");
        fwrite(&root, 512, 1, disk);
        fclose(disk);
    }
    disk = NULL;
    free(disk);
   	return 0;
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
	(void) path;
	(void) mode;
    (void) dev;
	
	return 0;
}

/*
 * Read size bytes from file into buf starting from offset
 *
 */
static int csc452_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//read in data
	//return success, or error

	return size;
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

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//write data
	//return success, or error

	return size;
}

/*
 * Removes a directory (must be empty)
 *
 */
static int csc452_rmdir(const char *path)
{
	  (void) path;

	  return 0;
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
