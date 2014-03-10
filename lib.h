// #define _POSIX_C_SOURCE 199309 	/* clock_gettime() */
#include <time.h>				/* struct timespec */
#include <unistd.h>				/* gid_t, uid_t, mode_t */
#include <sys/types.h>
#include <stdbool.h>

#define MAGIC 8876318
#define I_TABLE_SIZE 200
#define I_TF 0 		/* file type */
#define I_TD 1		/* directory type */

// MACROs:
#define I_BLOCK(x)  	( (int) ( (x) >> 3 ) )			/* insert entry block */
#define I_OFFSET(x) 	( (int) ( (x) & (long)7 ) )		/* insert entry offset */
#define I_INSERT(x, y)	( ( (long)(x) << 3 ) + (y) )	/* generate insert value */

typedef struct {
	int  vb_magic;
	int  vb_blocksize;		/* size of logical block */
	int  vb_root; 			/* root inode */
	int  vb_free;			/* first free block */
	int  vb_itable_size;	/* size of inode table */
	bool vb_clean;			/* clean bit */
	char name[491];
} vcb_st;

typedef struct {
	int 			i_ino; 			/* I-node number of file */
	int 		 	i_type;			/* file type */
	int 			i_size;			/* Either: number of entries in directory
						   					OR total file size (bytes) */
	uid_t 		 	i_uid;
	gid_t 		 	i_gid;
	mode_t 		 	i_mode;			/* permissions, represented in octal */
	int 			i_blocks; 		/* total number of blocks in file */
	long 		 	i_insert;		/* Only for directory inode: 
									   next dirent insertion location
							   	   	   call get_insert_block() to retrieve 
							   	   			the block loc
							   	   	   call get_insert_offset() to retrieve
							   	   	   		empty entry offset */
	struct timespec i_atime;
	struct timespec i_mtime;
	struct timespec i_ctime;

	int 			i_direct[106];
	int 			i_single;
	int 			i_double;
} inode_st;

typedef struct {
	int index[128];
} indirect_st;

typedef struct {
	int  et_ino;
	char et_name[60];
} entry_st;

typedef struct {
	entry_st d_entries[8];
} dirent_st;

typedef struct {
	int  f_next;		/* next free block */
	char f_junk[508];
} free_st;


int format_disk(int size);

vcb_st   *retrieve_vcb(); /* return vcb is statically allocated */
inode_st *retrieve_inode(int inode_num);

int write_struct(int blocknum, void *structp);
int read_struct (int blocknum, void *structp);

/*** debugging */
void debug(const char *format, ...);
void err(const char *format, ...);
void print_vcb();
void print_root();
void print_inode(inode_st *inodep);