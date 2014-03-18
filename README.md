Team Victory: 
Lu Ji
Blakely Madden

###### Implementation Pattern : I-node

# Bug: 
Fails on very large write and read part in the test script. 
Grader has to turn off that test, otherwise the program crashes and the tests after that are not actually tested. 


# Features: (design structures at the bottom)
## 1. supports at most 200 files and directories, 
However, it can be easily adjusted by changing the constant I_TABLE_SIZE, along with JUNK_SIZE in lib.h.
Make sure I_TABLE_SIZE + JUNK_SIZE = 512, while changing those constants.
## 2. supports a maximum of directory depth of 10 level, 
However, it can be easily adjusted by changing the constant MAX_DIR_DEPTH in lib.h
## 3. supports file name of maximum length of 60.
This is not adjustable due to constraints from structures definitions

## 4. Caching: 
we cached vcb, root inode and i-node valid table(struct valid_t) from the mount of the drive, and saves it back on unmount. 
Within the running of the system, those three blocks are only written to disk when they are changed. 
When you do a read on them, the data actually comes from the local cache, so to reduce disk accesses. 

## 5. detects whether last umount was successful: 
we set the clean bit in vcb to false on mount, and restore it to true when the disk is umounted. 
So if the disk is unpluged accidently without calling umount, the next mount will notice from the vcb block. 

## 6. We defined Macros, instead of functions, to detect file types.

## 7. I-node: 
we eventually decided to drop the dnode structure, since it is generally the same as file inode. 
Instead we add a type field into inode struct. 
This avoids defining two sets of facilitate functions for them, which only makes the design more complex.

## 8. Valid inode table: 
We use the block right after the I-node table to store the validation information of each inode, and cached it the RAM. 
So that we don't have to go through all the inode blocks to find an available one when new inode is required. 
This features saves disk read access. 

## 9. The insert_t type: 
The insert_t is one value but contains two information, blocknum and offset. 
insert_t is typedef[ed] from type long int. 
	typedef long insert_t;
The last 3 bits of it contains the offset value. (offset is the offset of an dirent block, each dirent block contains 8 entires, thus it requires 3 bits to store the information)
The rest bits contains the block number. 
We use macros to pull off data from insert_t and combine values into insert_t. 

MACROs for insert_t are as follows: 
	#define I_BLOCK(x)  	( (int) ( (x) >> 3 ) )			/* insert entry block */
	#define I_OFFSET(x) 	( (int) ( (x) & (long)7 ) )		/* insert entry offset */
	#define I_INSERT(x, y)	(insert_t)( ( (long)(x) << 3 ) + (y) )	
													/* generate insert value */



# Test: 
1. we print debugging messages to stderr to provide debugging information. 
2. We also applied shell scripts and manually carrying out varies tests as more and as complicated as possible to ensure the reliability and robustness of the system. 
Examples are: 
we created a shell script to 
* generate 
	* 100 files
	* multi-level directories
* rename files/directories. 
* change file names while moving to other directories (eg. mv f1 dir/dir2/file1).
* touch files to update their timestamps
* calls chmod
* calls rmdir and rm (-r)
* echo "xxxx" > files
* echo "xxxx" >> files
3. GDB helps a loooot



# Difficulties and Resolutions: 
1. The inode FS is really not easy to archieve, there are too many pieces/modules have to be taken care of simutaneously. 
For each function writing, we draw complicated diagram to assist us keep track of the connections and dependences among each piece, and what part of a piece/module relies on the complition of the other(s).

2. We redesigned and rewrite the system a couple of times, and it takes a long looooooong time debugging. So we used up all our slip days to make the system reliable. 

3. we have a lib file which includes all kinds of helper functions to facilitate us with the repeated code. 
So to reduce code, thus reduce chances of error and time of debugging. 
We also use as more global constants and #define(s) as possible. So the code is easy to maintain if any changes are required. 

4. We accidentally made the dirent structure larger than size 512, which make the written data turns out wrong when we read them later. 
It took us a whole day debugging to discover this issue. We even rewrote a couple of functions(inlcuding read, write, mkdir, create and rename) thinking might caused by those function designs. WHooo! Wasting a lot of time. 




# Overall Structure: 
|vcb|-----I-node table------|valid|-------dirent/data--------|

+ vcb			: one block in size. 
+ I-node table: 200 blocks. 
			  contains 200 inodes, each one taking exactly one block. But the table size of adjustable as noted in Feature 1.
+ valid 		: 1 block. 
			  keeps track of validation information of each inode. 
+ dirent/data : the rest of the disk




# Variables and Structures: 
	#define MAGIC 8876318
	#define I_TABLE_SIZE 200

	#define MAX_FILENAME_LENGTH 60	/* see declaration of entry_t for why it must be this value */
	#define MAX_DIR_DEPTH		10

	#define R_RD	1				/* retrieve functions mode: retrieve for read */
	#define R_WR	2				/* retrieve functions mode: retrieve for write */
	#define R_FLUSH 3				/* retrieve functions mode: flush cached objects */

	#define VALID_TABLE_SIZE 201 	/* I_TABLE_SIZE + 1 */
	#define JUNK_SIZE 		 311 	/* BLOCKSIZE - VALID_TABLE_SIZE */
	#define V_USED 		 	 1		/* inode is valid */
	#define V_UNUSED 		 0 		/* inode is invalid */

	// MACROs:
	#define I_BLOCK(x)  	( (int) ( (x) >> 3 ) )			/* insert entry block */
	#define I_OFFSET(x) 	( (int) ( (x) & (long)7 ) )		/* insert entry offset */
	#define I_INSERT(x, y)	(insert_t)( ( (long)(x) << 3 ) + (y) )	
															/* generate insert value */

	#define I_ISREG(t) 		( (t) == S_IFREG )	/* is file? */
	#define I_ISDIR(t)		( (t) == S_IFDIR )	/* is directory? */

	bool vcb_initialized;
	bool root_initialized;
	bool valid_initialized;

	typedef long insert_t;

	typedef struct {
		int  vb_magic;
		int  vb_blocksize;		/* size of logical block */
		int  vb_root; 			/* root inode */
		int  vb_valid; 			/* inode table validation block */
		int  vb_free;			/* next free block */
		int  vb_itable_size;	/* size of inode table */
		bool vb_clean;			/* clean bit */
		char name[487];
	} vcb_t;

	typedef struct {
		int 			i_ino; 			/* I-node number of file */
		int 		 	i_type;			/* file type */
		int 			i_size;			/* Either: number of entries in directory
							   					OR total file size (bytes) */
		uid_t 		 	i_uid;
		gid_t 		 	i_gid;
		mode_t 		 	i_mode;			/* permissions, represented in octal */
		int 			i_blocks; 		/* total number of blocks in file */
		struct timespec i_atime;
		struct timespec i_mtime;
		struct timespec i_ctime;

		int 			i_direct[106];
		int 			i_single;
		int 			i_double;
	} inode_t;

	typedef struct {
		int index[128];
	} indirect_t;

	typedef struct {
		int  	  et_ino;
		char 	  et_name[60];
	} entry_t;

	typedef struct {
		entry_t d_entries[8];
	} dirent_t;

	typedef struct {
		int  f_next;		/* next free block */
		char f_junk[508];
	} free_t;

	typedef struct {
		char v_entries[VALID_TABLE_SIZE];
		char v_junk[JUNK_SIZE];
	} valid_t;



Facilitate functions: 
	int format_disk(int size);

	vcb_t    *retrieve_vcb(); /* return vcb is statically alloced */
	inode_t  *retrieve_root();
	valid_t  *retrieve_valid();
	inode_t  *retrieve_inode(int inode_num);
	dirent_t *retrieve_dirent(int blocknum);

	// entry_t *fetch_entry(inode_t *dp, int offset);
	// 			 /* fetch the entry at offset position from given directory  */

	inode_t  *clear_inode(inode_t *inodep); 
			 	/* Returns pointer or NULL on error */
	dirent_t *clear_dirent(dirent_t *dirp);		
				/* each entry get inode_num 0, which indicates unused.
				   Returns pointer or NULL on error */

	entry_t  *step_dir(inode_t *dp);
	free_t 	 *get_free();
	int      get_new_ino(); /* valid list is updated */
	int 	 get_free_blocknum(); /* vcb is updated */
	int 	free_blocknum(int blocknum); /* vcb is updated */

	struct timespec *get_time();

	int      find_ino   (const char* path);
	int      path2tokens(char* path, char *** tokens); 
				/* caller is responsible for freeing memory dynamically allocated to tokens */
	insert_t search_entry(const char* et_name, inode_t *parentp, dirent_t *direntp, entry_t *entp);
	insert_t search_empty_slot(inode_t *parentp, dirent_t *direntp);

	int write_struct(int blocknum, void *structp);
	int read_struct (int blocknum, void *structp);

	/*** debugging */
	void debug(const char *format, ...);
	void err  (const char *format, ...);
	void print_vcb();
	void print_root();
	void print_inode(inode_t *inodep);