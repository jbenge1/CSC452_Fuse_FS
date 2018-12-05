/*
 *
 *	FUSE: Filesystem in Userspace
 *
 *
 *	gcc -Wall `pkg-config fuse --cflags --libs` csc452fuse.c -o csc452
 *
 *
 * @author : Justin Benge <justinbng36@gmail.com>
 *         	 Cristal Castellanos net cristalc
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
//5MB 5*2^20
#define DISK_SIZE 5242880 

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

#define MAX_NUM_BLOCKS ((DISK_SIZE/BLOCK_SIZE)-40)
//The size of this spans 40 blocks of disk, By Cristal C.
typedef struct FileAllocationTable{
	short numOfAllocations;//current number of allocations
	short lastAllocated;
	short FAT[MAX_NUM_BLOCKS];//#max blocks in file, one for root 
    char  padding[BLOCK_SIZE * 40];
}FAT;

/*This function will load the FAT from the disk file
 * into fatMem, assumes that it its the right size to store it
 * assumes no file will be allowed to overwrite it 
 * It returns 0 on success
 * By Cristal C.
 */
int loadFAT(FAT *fatMem){
	/*The disk size is 5 MB, thus is holds up to 5*2^11 blocks, which would require the same number of 
	 *entries which in a table their index can be stores in a short, resulting in a table that requires 40
	 *blocks of memory. Therefore the Max number of blocks that can be used will be 5*2^11-40, making the the size of
	 *the fat =to 39.87 blocks, which gives me just enough space to add a short containing the number of blocks currently allocated
	 */
	 FILE* fp=fopen(DISK_FILE, "r");
	 if(fp==NULL) DISK_NFE;
	 //int fseek(FILE *stream, long int offset, int whence)
	 long location=MAX_NUM_BLOCKS;//the one after the last file 
	// long inFileLoc=location*BLOCK_SIZE;
	 fseek(fp,location,SEEK_SET);
	 int ret =fread(fatMem, BLOCK_SIZE*40, 1, fp);
	 fclose(fp);
	 if(ret!=1) DISK_READ_ER;
	 return 0;
}
/* load FAT after updates, By Cristal C. */
int writeFAT(FAT *fatStruct){
	FILE* fp=fopen(DISK_FILE, "r+");
	 if(fp==NULL) DISK_NFE;
	 //int fseek(FILE *stream, long int offset, int whence)
	 long location=MAX_NUM_BLOCKS;//the one after the last file 
	 long inFileLoc=location*BLOCK_SIZE;
	 fseek(fp,inFileLoc,SEEK_SET);
	 fwrite(fatStruct, BLOCK_SIZE*40, 1, fp);
	 fclose(fp);
	 return 0;
}

//Lets just keep track of where we are in the file....
long int num_blocks = 0;

void check_errors(char *str)
{
    FILE *fp = fopen("/home/justin/errors.txt", "w");
    fprintf(fp,"%s\n", str);
    fclose(fp);
}
/**
 * let's just count up the number of slashes?
 *
 */
int is_dir(const char *path, int num) 
{
	//check_errors(path);
    int count = 0;
    char *temp;
    for(temp=(char *)path; *temp!='\0'; temp++)
    {
        if(*temp == '/')
            count++;
    }
    return count >= num;
}

/*int is_file(const char *path)
{
    return 0;
}*/

int loadRoot(csc452_root_directory*);

int dir_exists(const char *path) 
{
    csc452_root_directory root;
    loadRoot(&root);
    int res = 0; 
    int i;
    for(i=0;i<MAX_DIRS_IN_ROOT;i++)
    {
		if(root.directories[i].nStartBlock==0) continue;
        char *temp = root.directories[i].dname;
        //check_errors(path);
        if(strcmp(temp, path) == 0)
            res = 1;
       
        temp = NULL;
        free(temp);
    }
//    FILE *fp = fopen("/home/justin/errors.txt", "w");
//    fprintf(fp, "%d", res);
//    fclose(fp); 
    return res;
}

long findDirectory(csc452_root_directory*, char* );
int  loadDir      (csc452_directory_entry*, long );

int file_exists(const char dname[], const char *fname)
{
    
    csc452_root_directory root;
    loadRoot(&root);
    long dir_start = findDirectory(&root, (char *)dname);
    csc452_directory_entry dir;
    loadDir(&dir, dir_start);    

    int i;
    for(i=0;i<MAX_FILES_IN_DIR;i++)
    {
		if(dir.files[i].nStartBlock==0)continue;//non active spot
       if(strcmp(fname, dir.files[i].fname) == 0)
           return 1; 
    }
    
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
	char filename[MAX_FILENAME+1]="\0";
	char extension[MAX_EXTENSION+1]="\0";
	char directory[MAX_FILENAME+1]="\0";
    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
    
    check_errors(path);
    char temp_dir[strlen(directory)];
    if(filename[0] != '\0')
    {
        check_errors(temp_dir);
        strcpy(temp_dir, directory);
        directory[0] = '\0';
    }


	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else  {
		
		//If the path does exist and is a directory:
        if (is_dir(path,1) && dir_exists(directory)) 
        {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
        }
		//If the path does exist and is a file:
        else if(filename[0] != '\0' && file_exists(temp_dir, filename))
        {
            check_errors("HERE2");
            stbuf->st_mode = S_IFREG | 0666;
            stbuf->st_nlink = 2;
            stbuf->st_size = 512; //file size
        }	
		//Else return that path doesn't exist
        else
        {   
            check_errors("HERE3");
    		res = -ENOENT;
        }

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
 * By Cristal C.
 */
int extractFromPath(const char path[],char *file_name, char *file_ext,char *dir_name){
	//path will have form /directory/file.ext
	char *nav=(char *)path;
	char *writeOn;//this will be where I will write
	writeOn=dir_name;
	int length=1;
	nav++;//skip the first slash
	//gets the directory
	if(!strcmp(path, "")) return -1;
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
 *	By Cristal C.
 */
int loadRoot(csc452_root_directory *root){
	 FILE* fp=fopen(DISK_FILE, "r+");
	 if(fp==NULL) DISK_NFE;
	 //int fseek(FILE *stream, long int offset, int whence)
	 fseek(fp,0,SEEK_SET);
	 //size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
	 int ret =fread(root, BLOCK_SIZE, 1, fp);
	 fclose(fp);
	 if(ret!=1) DISK_READ_ER;
	 return 0;
 }
int writeRoot(csc452_root_directory *root){
	 FILE* fp=fopen(DISK_FILE, "r+");
	 if(fp==NULL) DISK_NFE;
	 //int fseek(FILE *stream, long int offset, int whence)
	 fseek(fp,0,SEEK_SET);
	 //size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
	 fwrite(root, BLOCK_SIZE, 1, fp);
	 fclose(fp);
	 return 0;
 }

/*	This function will take in a pointer to a root struct and a string with a directory
 *	name and search in the root directories for this directory, if it is not found it 
 * 	will return -1 and if found it return a a number that represents where it is in disk
 *	By Cristal C.
 */
long findDirectory(csc452_root_directory *root, char *name){
	int i;
	int numDirs=MAX_DIRS_IN_ROOT;
	for(i=0;i<numDirs;i++){
		//some of these may be uninitialized, so special case it
		if(root->directories[i].nStartBlock==0) continue;
		if(!strcmp((char *)(root->directories[i].dname), name)){//strcmp returns zero if equal
			return root->directories[i].nStartBlock;
		}
	}
	return (long)(-1);
}

/* This function will write a directory at a particular block 
   that was modified back to the disk file
   By Cristal C.*/
int writeDirectory(csc452_directory_entry *dir, long location){
	FILE* fp=fopen(DISK_FILE, "r+");
	 if(fp==NULL) DISK_NFE;
	 //int fseek(FILE *stream, long int offset, int whence)
	 long inFileLoc=location*BLOCK_SIZE;
	 fseek(fp,inFileLoc,SEEK_SET);
	 //size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
	fwrite(dir, BLOCK_SIZE, 1, fp);
	 fclose(fp);
	 return 0;
 }
/*	This function will receive a pointer to a directory struct and a long that
 *	holds its location in .disk and then it will load the dir from .disk into the struct
 *	By Cristal C.
 */
int loadDir(csc452_directory_entry *dir, long location){
	 FILE* fp=fopen(DISK_FILE, "r+");
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
/* This function assumes path is valid
	By Cristal C.
*/
int removeFileFromDirectory(const char path[]){
	char file_name[MAX_FILENAME+1]="\0";
	char file_ext[MAX_EXTENSION+1]="\0";
	char dir_name[MAX_FILENAME+1]="\0";
	sscanf(path, "/%[^/]/%[^.].%s", dir_name, file_name, file_ext);
	csc452_root_directory root;
	int ret=loadRoot(&root);
	if(ret) return -EIO; 
	long location=findDirectory(&root, dir_name);
	if (location==-1)return -ENOENT;
	csc452_directory_entry thisDir;
	ret=loadDir(&thisDir, location);
	if(ret) return -EIO;
	int i;
	int numFiles=MAX_FILES_IN_DIR;
	for(i=0;i<numFiles;i++){
		if(!strcmp(thisDir.files[i].fname, file_name) && !strcmp(thisDir.files[i].fext,file_ext)){//strcmp returns zero if equal
			thisDir.files[i].fsize=0;
			thisDir.files[i].fname[0]='\0';	//filename (plus space for nul)
		    thisDir.files[i].fext[0]='\0';	//extension (plus space for nul)
		    thisDir.files[i].nStartBlock=0;
		}
	}
	thisDir.nFiles=thisDir.nFiles-1;
	writeDirectory(&thisDir, location);
	return 0;
}	
 /* This function will take in two strings one with a file name and one with an extension
 * and concat them with a period in between into a third string passed with pointer
 * This function assumes the string where the result will be stored has enough memory allocated
 * This function returns the number of characters loaded into fullName
 * By Cristal C.
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
 * By Cristal C.
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
		//filler(void *buff, char *name, struct stat *stbuff,off_t offf)
		//list all files in this directory find this directory in the root
		long location=findDirectory(&root, dir_name);
		if (location==-1)return -ENOENT;
		//load the directory
		csc452_directory_entry thisDir;
		ret=loadDir(&thisDir, location);
		if(ret) return -EIO;
		//get all files from disk struct
		int i,numFiles=MAX_FILES_IN_DIR;
		
		for(i=0; i<numFiles;i++){
			//directories are not necessarly next to each other
			if(thisDir.files[i].nStartBlock==0) continue;//zero is reserved for the root
			char fullName[MAX_FILENAME+MAX_EXTENSION+2];
			getFullFileName(thisDir.files[i].fname,thisDir.files[i].fext,&fullName[0]);
			filler(buf, fullName, NULL, 0);
		}
	}
	else {
		//For the root, all of its child directories should be added
		//there is an array of directory structs inside the root struct
		int i=0;
		int numOfDirs=MAX_DIRS_IN_ROOT;
		for(i=0;i<numOfDirs;i++){
			if(root.directories[i].nStartBlock==0) continue;//0 is free or root, but root should not be a directory
			struct csc452_directory currDir=root.directories[i];
			//add all directories to the buff
			//assume all valid directories exists sequentially in array
			filler(buf, currDir.dname,NULL, 0);//Can I assume all directories are in seq in array or should i search among all ????
		}
			
	}
	//???On success, 1 is returned. On end of directory, 0 is returned
	return 0;
}

short getDisk(FAT *);
/*
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int csc452_mkdir(const char *path, mode_t mode)
{
	(void) mode;
  
    if(is_dir(path, 2))
        return -EPERM;
    char *token = strtok((char *)path, "/");
    if(strlen(token) > 8)
        return -ENAMETOOLONG;
    

    csc452_root_directory root;
    loadRoot(&root);

    if(dir_exists(path))
        return -EEXIST;
    
    csc452_directory_entry *dir = malloc(sizeof(csc452_directory_entry));
    dir->nFiles = 0;
    
	//Wee need to add the name of the directory and not the path on name, it will only hold 9 chars
	char file_name[MAX_FILENAME+1]="\0";
	char file_ext[MAX_EXTENSION+1]="\0";
	char dir_name[MAX_FILENAME+1];
    
	sscanf(path, "/%[^/]/%[^.].%s", dir_name, file_name, file_ext);
	dir_name[strlen(dir_name)] = '\0';
	//wee need to add this dir to the root so wee must find out if the root can have one more in it
	if(root.nDirectories>=MAX_DIRS_IN_ROOT) return -ENOSPC;
	
	int i;
	int available_dir=0;
	for(i=0;i<MAX_DIRS_IN_ROOT;i++){
		//look for an available space
		if(root.directories[i].nStartBlock==0){//0 should only be for root
			available_dir=i;
			break;
		}
	}
//	if(available_dir==0)
//		return -ENOSPC;
	/* root changes */
	strcpy(root.directories[available_dir].dname, dir_name);
    //strcpy(root.directories[root.nDirectories++].dname, path);
	//get a disk for it to live at 
	FAT fat;
    int ret = loadFAT(&fat);
    if(ret)
        return -EIO;
    short avail = getDisk(&fat);
    if(avail == -1)
    {
        writeFAT(&fat);
        return EDQUOT;
    }
    root.directories[available_dir].nStartBlock = (long)avail;//this will be the num of block and not the position
    root.nDirectories++;
	//write changes to the root
	writeRoot(&root);
	//now I need to add the new directory's block to the file
	writeDirectory(dir, avail);
	
	
//    FILE *disk_write_fp = fopen(".disk", "r+");
//    fseek(disk_write_fp, 0, SEEK_SET);
//    fwrite(&root, BLOCK_SIZE, 1, disk_write_fp);
    
//    fseek(disk_write_fp, num_blocks * BLOCK_SIZE, SEEK_SET);
//    fwrite(dir, BLOCK_SIZE, 1, disk_write_fp);
//    fclose(disk_write_fp);
//    disk_write_fp = NULL;
//    free(disk_write_fp);
	
   
    fat.FAT[avail] = -1;//end of file, num of allocations increased in getDisk
    // write new FAT
	writeFAT(&fat);
   	return 0;
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
    if(!is_dir(path, 1))
        return -ENOTDIR;
    //now lets actually find the directory
	char file_name[MAX_FILENAME+1]="\0";
	char file_ext[MAX_EXTENSION+1]="\0";
	char dir_name[MAX_FILENAME+1]="\0";
	sscanf(path, "/%[^/]/%[^.].%s", dir_name, file_name, file_ext);
    int i;
    for(i=0;i<MAX_DIRS_IN_ROOT;i++)
    {
		if(root.directories[i].nStartBlock==0) continue;//skip over deallocated blocks
        if(strcmp(dir_name, root.directories[i].dname) == 0)
        {
            long start = root.directories[i].nStartBlock;
            csc452_directory_entry dir;
            loadDir(&dir, start);
			
			//is dir empty?
			if(dir.nFiles!=0) return -ENOTEMPTY;
			//proceed with removal
			
			//need to remove directory from file, from FATand from root
			
			//remove directory from root
			root.directories[i].nStartBlock=0;
			root.nDirectories--;
            //remove from FAT
			FAT fat;
			int ret = loadFAT(&fat);
			if(ret) return -EIO;
			//a directory is a single block of mem and does not follow another
			fat.FAT[start]=-2;
			fat.numOfAllocations=fat.numOfAllocations-1;
			//write this back
			writeFAT(&fat);
			writeRoot(&root);
			return 0;
			
          /*  if(dir.nFiles-1 == 0)// what is this?//shifting code
            {
                int j;
                for(j=i;j<root.nDirectories-1;j++)
                {
                    root.directories[j] = root.directories[j+1];
                }
                root.nDirectories -= 1;
                return 0;
            }
            else
                return -ENOTEMPTY;*/
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

//    check_errors(path);    
    
    csc452_root_directory root;
    loadRoot(&root);
    char filename[MAX_FILENAME+1]="\0";
    char extension[MAX_EXTENSION+1]="\0";
    char directory[MAX_FILENAME+1]="\0";
    sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

    if(!dir_exists(directory))
        return -ENOENT;
    if(!is_dir(path, 2))
        return -EPERM; 
    
    char *dname = strtok((char*)path, "/");
    char *temp  = strtok(NULL, "/");
    char *fname = strtok(temp, "."); 
    char *ext   = strtok(NULL, ".");
    
    char dir_name[9] = "/";
    strcat(dir_name, dname);
    if(file_exists(dir_name, fname))
        return -EEXIST;
    if(strlen(fname) > 8)
        return -ENAMETOOLONG;


    csc452_disk_block *nod = malloc(sizeof(csc452_disk_block));
    long dir_start = findDirectory(&root, dir_name);
    csc452_directory_entry dir;
    loadDir(&dir, dir_start);    
    if(dir.nFiles >= MAXF_FILES_IN_DIR)
       return -ENOSPC; 
    strcpy(dir.files[dir.nFiles].fname, fname);
    strcpy(dir.files[dir.nFiles++].fext, ext);
   
    
    FILE *disk_write_fp = fopen(".disk", "r+");
    fseek(disk_write_fp, 0, SEEK_SET);
    fwrite(&root, BLOCK_SIZE, 1, disk_write_fp);

    fseek(disk_write_fp, (++num_blocks) * BLOCK_SIZE, SEEK_SET);
    fwrite(nod, BLOCK_SIZE, 1, disk_write_fp);
    fclose(disk_write_fp);
    disk_write_fp = NULL;
    free(disk_write_fp);
    
    FAT fat;
    int ret = loadFAT(&fat);
    if(ret)
        return -EIO;
    short avail = getDisk(&fat);
    if(avail == -1)
    {
        writeFAT(&fat);
        return EDQUOT;
    }
    fat.FAT[avail] = -1;
    ++fat.numOfAllocations;

    return 0;
}
/* 
 * This function will search in the files of a directory pointed by 
 * *directory and will return the long with the location to the file starting pointed
 * when found, or -1 otherwise. 
 * When found the memory pointed by fsize gets set to the file's size.
 * There is an assumption that all valid files are
 * adjacent to each other (no invalid file in between.
 * By Cristal C.
 */
long findFile(csc452_directory_entry *directory, char fname[], char fext[], size_t *fsize,int write, size_t newSize){
	int i;
	int numFiles=MAX_FILES_IN_DIR;
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
 * By Cristal C.
 */
int loadFile(csc452_disk_block *block, long location){
	 FILE* fp=fopen(DISK_FILE, "r+");
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
 * This function will write a block of disk into the disk file
 * By Cristal C.
 */
int writeFile(csc452_disk_block *block, long location){
	 FILE* fp=fopen(DISK_FILE, "r+");
	 if(fp==NULL) DISK_NFE;
	 //int fseek(FILE *stream, long int offset, int whence)
	 long inFileLoc=location*BLOCK_SIZE;
	 fseek(fp,inFileLoc,SEEK_SET);
	 //size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
	 fwrite(block, BLOCK_SIZE, 1, fp);
	 fclose(fp);
	 return 0;
 }
 /*
 * This function will determine if a path is valid 
 * and if valid it will determine if exists. It will return 
 * an error code if there was an issue with the path or 0 if the path was good,
 * and if good it initialized the fields pointed by the passed pointers
 * to the file's location and size
 * By Cristal C.
 */
int fileDoesntExists(const char *path, long *fileLoc, size_t *fileSize,int onWrite, size_t newSize){
	//check if the format is good
	char file_name[MAX_FILENAME+1]="\0";
	char file_ext[MAX_EXTENSION+1]="\0";
	char dir_name[MAX_FILENAME+1]="\0";//assume dir name max is same as file FOR NOW
	
	//extract these from path
	int invalid=extractFromPath(path,&file_name[0],&file_ext[0],&dir_name[0]);
	if(invalid) return -ENOENT;//path was not valid due to length of names or null path
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
 * By Cristal C.
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
	//load FAT
	FAT fat;
	ret=loadFAT(&fat);
	if(ret) return -EIO;
	while(skipBlocks>0){
		if(fat.FAT[fileLoc]<1){
			//next block was not defined
			return -EIO;
		}
		//load next block of disk
		fileLoc=fat.FAT[fileLoc];
		skipBlocks--;
	}
	ret=loadFile(&data, fileLoc);//update struct
	if(ret) return -EIO;
	//I now have the first block I need to read from, offset affects this one
	//starting point 
	off_t ofInBlock=offset%BLOCK_SIZE;
	thisRead=BLOCK_SIZE-ofInBlock;
	if(size<thisRead){
		thisRead=size;
	}
	memcpy(buf, data.data+ofInBlock,thisRead);//read the remaining until the end of the first block
	allReads=allReads+thisRead;
	//Any following reads will not be affected by offset on their data starting point
	while(allReads<size){
		//I want to read the rest of the data from the next disks
		if(fat.FAT[fileLoc]<1){
			//we reached the last block
			break;
		}
		fileLoc=fat.FAT[fileLoc];
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

/* This function takes in a pointer to a file allocation table,
 * and a file location and it deallocates all data disks that were 
 * after the file location provided for the file from the file allocation table
 * by setting them to 0 in the table
 * By Cristal C.
 */
void trimFrom(FAT* fat, short fileLoc){
	if(fileLoc==-1) return;
	short next=fat->FAT[fileLoc];
	fat->FAT[fileLoc]=-2;//setting it available
	//decrement allocation
	fat->numOfAllocations=fat->numOfAllocations-1;
	trimFrom(fat, next);
}
/* This function has a job of finding the next available disk in the FAT
 * we need to come up with a search algorithm
 * By Cristal C.
 */
short findADisk(FAT *fat){
	//serach through the FAT to find an available space
	short i;
	short last=fat->lastAllocated;
	short maxBound=MAX_NUM_BLOCKS+last+1;
	for(i=last+1;i<maxBound;i++){
		short actualInd=i%MAX_NUM_BLOCKS;
		if(actualInd==0)continue;//skip over root space
		if(fat->FAT[actualInd]==-2 || fat->FAT[actualInd]==0){
			//no node should have its next element be the root, so this works for the uninitialized FAT
			fat->lastAllocated=actualInd;
			fat->numOfAllocations=fat->numOfAllocations+1;
			return actualInd;//wrap around, available memory is likely next other available
		}
	}
	return -1;
}
/*
 * This function receives a pointer to a file allocation table struct
 * and it searches in its elements for an available disk of memory
 * it returns an idex to this block or -1 if none is available
 * By Cristal C.
 */
short getDisk(FAT *fat){
	if(fat->numOfAllocations < MAX_NUM_BLOCKS-1){//-1 because the first is root
		//can get a new one
		return findADisk(fat);
	}
	return -1;
}

/* This function takes in the location of the first disk of memory for a file, 
 * and goes through each updating the file size*/
/*
 * Write size bytes from buf into file starting from offset
 *By Cristal C.
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
	long fileLocOr;
	//get the new size of file after writing to it
	size_t newSize=offset+size;
	//dont update the size in case there is an error
	int doesnt=fileDoesntExists(path, &fileLocOr, &fileSize,0,newSize);//haven't written anything
	//what happens if overwritten is smaller than what it used to be???
	//if larger add more blocks if the same just dont add any 
	//but if smaller should disk blocks be deallocated?
	long fileLoc= fileLocOr;
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
				from buff is written, then everything else is deleted.
	*/
	//get the file allocation table 
	FAT fat;
	ret=loadFAT(&fat);
	if(ret) return -EIO;
	off_t skipBlocks=offset/BLOCK_SIZE;
	while(skipBlocks>0){
		short nextLoc=fat.FAT[fileLoc];
		if(nextLoc<1){//-1 sentinel for last block, 0 uninitialized
			return -EIO;
		}
		//load next block of disk
		fileLoc=nextLoc;
		skipBlocks--;
	}
	ret=loadFile(&data, fileLoc);//update struct
	if(ret) return -EIO;
	off_t ofInBlock=offset%BLOCK_SIZE; //offset within first block
	size_t thisWrite=0;
	size_t allWrites=0;
	thisWrite=BLOCK_SIZE-ofInBlock;
	//how much am I going to write 
	if(size<thisWrite){
		//I am only writing a portion of the first block
		thisWrite=size;
	}
	memcpy(data.data+offset,buf,thisWrite);
	allWrites=thisWrite;
	if((ofInBlock+thisWrite)!=BLOCK_SIZE){//write shorter than the first block
		//I need to clear up from here
		int i;
		for(i=(ofInBlock+thisWrite); i<BLOCK_SIZE;i++){
			data.data[i]=0;
		}
		//need to deallocate all next files from before
		trimFrom(&fat, fileLoc);
		//keep the current as EOF
		fat.FAT[fileLoc]=-1;
		fat.numOfAllocations=fat.numOfAllocations+1;
	}
	writeFile(&data, fileLoc);//update first block in file
	//now need to determine where to write next
	while(size<allWrites){
		//write to next allocated data block unless there are no more
		short newfileLoc=fat.FAT[fileLoc];
		if(newfileLoc==-1){
			//we reached the last block, but we are not done writing so we need to get a new block
			short available=getDisk(&fat);
			if(available==-1){
				doesnt=fileDoesntExists(path, &fileLocOr, &fileSize,1,offset+allWrites);//update the potion of fils so far
				writeFAT(&fat);
				return EDQUOT;
			}
			fat.FAT[fileLoc]=available;
			fat.FAT[available]=-1;//this is the current last disk
			newfileLoc=available;
		}
		fileLoc=newfileLoc;
		ret=loadFile(&data, fileLoc);//update struct to next disk
		if(ret){
			//update partial rewrite
			doesnt=fileDoesntExists(path, &fileLocOr, &fileSize,1,offset+allWrites);
			writeFAT(&fat);
			return -EIO;	
		}			
		if((size-allWrites)<=BLOCK_SIZE){
			//this is the last block that needs to be written into
			memcpy(data.data+offset+allWrites,buf+allWrites, size-allWrites);
			//set the rest of this block to zero
			size_t nextEl=size-allWrites+1;
			size_t i;
			for(i=nextEl;i<BLOCK_SIZE;i++){
				data.data[i]=0;
			}
			allWrites=size;
			//write this disk to disk o update it
			//if there were more blocks allocated after this one, 
			//they should be removed from the fat
			trimFrom(&fat, fileLoc);
			//set this to end of file and keep it
			fat.FAT[fileLoc]=-1;
			fat.numOfAllocations=fat.numOfAllocations+1;
		}else{
			//the whole block needs to be written into
			memcpy(data.data+offset+allWrites, buf+allWrites,BLOCK_SIZE);
			allWrites=allWrites+BLOCK_SIZE;
		}
		writeFile(&data, fileLoc);
	}	
	//after done update all the files new sizes
	doesnt=fileDoesntExists(path, &fileLocOr, &fileSize,1,offset+allWrites);
	writeFAT(&fat);//update FAT
	//return success, or error
	return allWrites;
}

/*
 * Removes a file.
 * By Cristal C.
 */
static int csc452_unlink(const char *path)
{
	//-EISDIR if the path is a directory
	//-ENOENT if the file is not found
        (void) path;
		//verify that path is valid
		size_t fileSize;
		long fileLocOr;
		int doesnt=fileDoesntExists(path, &fileLocOr, &fileSize,0,0);
		if(doesnt){
			return doesnt;//it already includes the reason
		}
		//load file allocation table
		FAT fat;
		int ret=loadFAT(&fat);
		if(ret) return -EIO;
		//now remove all the blocks from this file after itself from the FAT
		trimFrom(&fat, fileLocOr);
		
		ret=removeFileFromDirectory(path);//update Dir
		if(ret) return -EIO;
		ret=writeFAT(&fat);//update FAT
		if(ret) return -EIO;
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
