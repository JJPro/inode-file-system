#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h> /* dirname(), 
					   basename() */
#include <stdarg.h>
#include <pwd.h>
#include <grp.h>

#include "lib.h"
#include "disk.h" 	/* BLOCKSIZE */

static vcb_st vcb;
static inode_st inode;


int 
format_disk(int size) /* size: total number of blocks on disk */
{
	/* format disk following rules: 
		+ vcb
		+ inode table
			- root
			- others
		+ root dirent
		+ free blocks 

	 Returns 0 on success, or -1 on error */

	int min_required_blocks;
	int free_starter;
	int root_dirent_block;
	int insert_block, insert_offset;
	struct timespec now;
	entry_st entry;
	dirent_st root_dirent;
	free_st free_b;

	min_required_blocks = 1 			/* vcb */
						+ I_TABLE_SIZE  /* inode table */
						+ 1 			/* root dirent */;
	if (size < min_required_blocks){
		err("size must be at least %d", min_required_blocks);
		return -1;
	}

	free_starter = min_required_blocks - 1;
	vcb.vb_magic = MAGIC;
	vcb.vb_blocksize = BLOCKSIZE;
	vcb.vb_root = 1;
	vcb.vb_free = free_starter;
	vcb.vb_itable_size = I_TABLE_SIZE;
	vcb.vb_clean = true;
	if (write_struct(0, &vcb) < 0)			/* vcb */
		return -1;

	insert_offset = 2;
	insert_block  = free_starter - 1;
	root_dirent_block = insert_block;
	if (clock_gettime(CLOCK_REALTIME, &now) == -1)
		return -1;
	inode.i_ino  = 1;
	inode.i_type = I_TD;
	inode.i_size = 2;
	inode.i_uid  = getuid();
	inode.i_gid  = getgid();
	inode.i_mode = 0777;
	inode.i_blocks = 1;
	inode.i_insert = I_INSERT(insert_block, insert_offset);
	inode.i_atime = now;
	inode.i_mtime = now;
	inode.i_ctime = now;
	inode.i_direct[0] = root_dirent_block;
	for (int i=1; i<106; i++){
		inode.i_direct[i] = -1;
	}
	inode.i_single = -1;
	inode.i_double = -1;
	if (write_struct(1, &inode) < 0)			/* root inode */
		return -1;

	inode.i_ino = 0;
	inode.i_direct[0] = -1;
	for (int i=2; i<I_TABLE_SIZE; i++){
		if ( write_struct(i, &inode) < 0 )		/* the rest of I-node table */
			return -1;
	}

	entry.et_ino = 1;
	strcpy(entry.et_name, ".");			/* .  entry */
	root_dirent.d_entries[0] = entry;	
	strcpy(entry.et_name, "..");			/* .. entry */
	root_dirent.d_entries[1] = entry;
	entry.et_ino = 0;
	for (int i=2; i<8; i++){
		root_dirent.d_entries[i] = entry;	/* other entries in that data block */
	}
	if ( write_struct(root_dirent_block, &root_dirent) < 0 ) /* root dirent */
		return -1;

	for (int i=free_starter; i<size-1; i++){
		free_b.f_next = i + 1;
		if ( write_struct(i, &free_b) < 0 )
			return -1;
	}
	free_b.f_next = -1;
	if ( write_struct(size-1, &free_b) < 0 )		/* free blocks */
		return -1;

	return 0;
}

vcb_st *
retrieve_vcb(){
	return &vcb;
}

inode_st *
retrieve_inode(int inode_num){
	read_struct(inode_num, &inode);
	return &inode;
}

int 
write_struct(int blocknum, void *structp){
	char buffer[BLOCKSIZE];
	memcpy(buffer, structp, BLOCKSIZE);
	return dwrite(blocknum, buffer);
}

int 
read_struct (int blocknum, void *structp){
	char buffer[BLOCKSIZE];
	int bytes_read = dread(blocknum, buffer);
	memcpy(structp, buffer, BLOCKSIZE);
	return bytes_read;
}


/*** debug */
void 
debug(const char* format, ...){
  va_list args;
  va_start(args, format);
  char * new_format = calloc(1, strlen(format) + 7 + 1 + 1); // 7 for "DEBUG: ", 1 for \n, 1 for null char
  strcat(new_format, "DEBUG: ");
  strcat(new_format, format);
  strcat(new_format, "\n");
  vfprintf(stderr, new_format, args);
  va_end(args);
}

void 
err(const char* format, ...){
	/* output error message to stderr */
  va_list args;
  va_start(args, format);
  char * new_format = calloc(1, strlen(format) + 7 + 1 + 1); // 7 for "DEBUG: ", 1 for \n, 1 for null char
  strcat(new_format, "ERROR: ");
  strcat(new_format, format);
  strcat(new_format, "\n");
  vfprintf(stderr, new_format, args);
  va_end(args);
}

void
print_vcb(){
	read_struct(0, &vcb);
	debug("print_vcb:");
	debug("    magic: %d", vcb.vb_magic);
	debug("     root: %d", vcb.vb_root);
	debug("     free: %d", vcb.vb_free);
}

void 
print_root(){
 	debug("print_root:");
	inode_st root;
 	read_struct(1, &root);
 	print_inode(&root);
}

void
print_inode(inode_st *ip){
 	char *username  = getpwuid(ip->i_uid)->pw_name;
 	char *groupname = getgrgid(ip->i_gid)->gr_name;

 	debug("    inode: %d", ip->i_ino);
 	debug("     size: %d", ip->i_size);
 	debug("     user: %s", username);
 	debug("    group: %s", groupname);
 	debug("     mode: 0%o", (int)ip->i_mode);
}