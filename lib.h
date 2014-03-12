// #define _POSIX_C_SOURCE 199309 	/* clock_gettime() */
#include <time.h>				/* struct timespec */
#include <unistd.h>				/* gid_t, uid_t, mode_t */
#include <sys/types.h>
#include <stdbool.h>
#include "disk.h" 		/* BLOCKSIZE */

#define MAGIC 8876318
#define I_TABLE_SIZE 200

#define MAX_FILENAME_LENGTH 56	/* see declaration of entry_t for why it must be this value */
#define MAX_DIR_DEPTH		10

// MACROs:
#define I_BLOCK(x)  	( (int) ( (x) >> 3 ) )			/* insert entry block */
#define I_OFFSET(x) 	( (int) ( (x) & (long)7 ) )		/* insert entry offset */
#define I_INSERT(x, y)	(insert_t)( ( (long)(x) << 3 ) + (y) )	/* generate insert value */

#define I_ISREG(t) 		( (t) == S_IFREG )	/* is file? */
#define I_ISDIR(t)		( (t) == S_IFDIR )	/* is directory? */

#define VALID_TABLE_SIZE 201 /* I_TABLE_SIZE + 1 */
#define JUNK_SIZE 311		 /* BLOCKSIZE - VALID_TABLE_SIZE */

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
	insert_t	 	i_insert;		/* Only for directory inode: 
									   next dirent insertion location
							   	   	   call get_insert_block() to retrieve 
							   	   			the block loc
							   	   	   call get_insert_offset() to retrieve
							   	   	   		empty entry offset */
	struct timespec i_atime;
	struct timespec i_mtime;
	struct timespec i_ctime;

	int 			i_direct[105];
	int 			i_single;
	int 			i_double;
} inode_t;

typedef struct {
	int index[128];
} indirect_t;

typedef struct {
	int  	  et_ino;
	char 	  et_name[56];
	insert_t  et_insert;
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


int format_disk(int size);

vcb_t    *retrieve_vcb(); /* return vcb is statically alloced */
inode_t  *retrieve_inode(int inode_num);
inode_t  *retrieve_root();
dirent_t *retrieve_dirent(int blocknum);
free_t 	 *retrieve_free();
valid_t  *retrieve_valid();

entry_t  *step_dir(inode_t *dp);

int      find_ino   (const char* path);
int      path2tokens(char* path, char *** tokens); 
			/* caller is responsible for freeing memory allocated to tokens */
insert_t search_entry(char* et_name, inode_t *inodep, dirent_t *dirp, entry_t *entp);
			/* dirp and entp is statically allocated */

int write_struct(int blocknum, void *structp);
int read_struct (int blocknum, void *structp);

/*** debugging */
void debug(const char *format, ...);
void err  (const char *format, ...);
void print_vcb();
void print_root();
void print_inode(inode_t *inodep);