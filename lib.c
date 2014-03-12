#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>  	/* S_IFREG, S_IFDIR */

#include "lib.h"
#include "disk.h" 		/* BLOCKSIZE */

static vcb_t vcb;
static inode_t root;
static bool vcb_initialized = false;
static bool root_initialized = false;


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
	entry_t entry;
	dirent_t root_dirent;
	free_t free_b;
	inode_t inode;

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
	vcb_initialized = true;

	insert_offset = 2;
	insert_block  = free_starter - 1;
	root_dirent_block = insert_block;
	if (clock_gettime(CLOCK_REALTIME, &now) == -1)
		return -1;
	inode.i_ino  = 1;
	inode.i_type = S_IFDIR;
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
	root = inode;
	if (write_struct(1, &root) < 0)			/* root inode */
		return -1;
	root_initialized = true;

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

vcb_t *
retrieve_vcb(){
	/* Returns pointer to vcb, Or NULL on error */
	if ( !vcb_initialized ){
		if ( read_struct(0, &vcb) < 0 )
			return NULL;
		vcb_initialized = true;
	}
	return &vcb;
}

inode_t *
retrieve_inode(int inode_num){
	/* Returns a pointer to specific inode, Or NULL on error */
	static inode_t inode;
	if ( read_struct(inode_num, &inode) < 0 )
		return NULL;
	return &inode;
}

inode_t *
retrieve_root(){
	/* Returns a pointer to root inode, Or NULL on error */
	if (!root_initialized){
		if ( read_struct(1, &root) < 0 )
			return NULL;
		root_initialized = true;
	}
	return &root;
}

dirent_t *
retrieve_dirent(int blocknum)
	/* Returns a pointer to struct dirent, Or NULL on error */
{
	int min_required_blocks;
	static dirent_t dirent;

	min_required_blocks = 1 			/* vcb */
						+ I_TABLE_SIZE  /* inode table */
						+ 1 			/* root dirent */;
	if (blocknum < min_required_blocks-1)
		return NULL;
	if ( read_struct(blocknum, &dirent) < 0 )
		return NULL;
	return &dirent;
}

entry_t *
step_dir(inode_t *dp)
		/* Returns 
				pointer to next entry in directory 
				NULL on no more entry
				NULL on error
		   Description: 
		   		<dp> must be provided on first call.
		   		pass <dp> as NULL on subsequent calls for the same directory.
		   		Returns the first entry in directory 	  if <dp> is given
		   		Returns the next  entry in same directory if <dp> is NULL */
{
	static int 			block_index;
	static int 			block;
	static int 			offset;
	static dirent_t 	*dirbuf;
	static entry_t 		entry;

	memset(&entry , 0, sizeof(entry_t ));
	if (dp) {
		memset(&dirbuf, 0, sizeof(dirent_t));
		block_index = 0;
		block 		= 0;
		offset 		= 0;
	}

	do {
		if (offset == 8 || dp){ 				/* cache new/next dirent_t data in dirbuf */
			if ( offset == 8 )
				block_index++;
			block = dp->i_direct[block_index];
			offset = 0;

			dirbuf = retrieve_dirent(block);
		}

		entry = dirbuf->d_entries[offset];
		offset++;
	}
	while (entry.et_ino == 0);

	return &entry;
}

int
find_ino(const char *path)
		/* Returns inode number Or -1 on error */
{
	debug("find_ino(): \n"
		  "       looking for ino of path: %s", path);

	char 		m_path[strlen(path)+1];   /* mutable string for path2tokens() */
	int 		tokenc;
	char 	    **tokens;
	char		**tokenp;				  /* for travese tokens */
	char		*token;
	inode_t  	*inodep;
	dirent_t 	dir;
	entry_t  	ent;
	// indirect_t 	indirect;
	insert_t 	insert;

	strcpy(m_path, path);

	inodep = retrieve_root();
	tokenc = path2tokens(m_path, &tokens);
	tokenp = tokens;

	int i;
	for (i=0; 
		( i<(tokenc-1) && I_ISDIR(inodep->i_type) ); 
		i++, tokenp++)
	{
		token = *tokenp;
		if ( ( insert=search_entry(token, inodep, &dir, &ent) ) < 0 )
			return -1;
		if (!(inodep = retrieve_inode(ent.et_ino)))
			return -1;
	}
	if (i != (tokenc-1))
		return -1;
	token = *tokenp;
	if ( (insert=search_entry(token, inodep, &dir, &ent)) < 0 )
		return -1;

	for (int i=0; i<tokenc; i++){		/* free tokens */
		free(*(tokens+i));
	}
	free(tokens);

	debug("find_ino() : \n"
		  "       ino : %d\n"
		  "       path: %s", ent.et_ino, path);

	return ent.et_ino;
}

int 
path2tokens(char* path, char *** tokens)
		/* e.g : /var/log/syslog -> {"var", "log", "syslog"} 
		   Warning: the caller is responsible for 
		            freeing the memory allocated to tokens
	            							(pointer to strings)
		   Returns number of tokens Or -1 on error 
		   path must be absolute */
{
	int 	count;
	int 	depth;
	char 	*delimiter = "/";
	char	*word;
	char	*word_container;
	char	**tokens_container;

	tokens_container = (char **)calloc(MAX_DIR_DEPTH, sizeof(char **));

	for (word = strtok(path, delimiter), count=0, depth = count;
		 word && (depth < MAX_DIR_DEPTH);
		 word = strtok(NULL, delimiter), count++, depth = count)
	{
		word_container = (char *)calloc(1, MAX_FILENAME_LENGTH);
		strcpy(word_container, word);
		*(tokens_container+count) = word_container;
	}
	if (depth >= MAX_DIR_DEPTH)
		return -1;
	*tokens = tokens_container;
	return count;
}

insert_t
search_entry(char *et_name, inode_t *inodep, dirent_t *dirp, entry_t *entp)
		/* Returns 
				insert value of the entry position
				-1 not found
				-2 on error 

		   et_name is the file/dir name to be search for
		   inodep  is the pointer to the inode, which is the directory to be searched
		   			  the inode_t inodep refers to must be a directory

		   pointer to dirent_t, where the entry resides, is stored in dirp
		   pointer to entry_t,  search result, is stored in entp */
{
	dirent_t *direntp;
	entry_t  entry;
	int total_blocks;

	int insert;
	int block;
	int offset;

	total_blocks = inodep->i_blocks;
	for (int i=0; i<total_blocks; i++){
		if ( !(direntp = retrieve_dirent(inodep->i_direct[i])) )
			return -2;
		for (int j=0; j<8; j++){
			entry = direntp->d_entries[i];
			if ( entry.et_ino
				&& strcmp(entry.et_name, et_name) == 0 )
			{
				offset = j;
				block = inodep->i_direct[i];
				insert = I_INSERT(block, offset);
			
				*dirp = *direntp;
				*entp = entry;
				return insert;
			}
		}
	}
	return -1;
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
	if ( read_struct(0, &vcb) < 0) {
		err("print_vcb()->read_struct");
		return;
	}
	vcb_initialized = true;
	debug("print_vcb:\n"
		  "           magic: %d\n"
		  "            root: %d\n"
		  "            free: %d"
		  , vcb.vb_magic, vcb.vb_root, vcb.vb_free);
}

void 
print_root(){
 	debug("print_root:");
	inode_t root;
 	if ( read_struct(1, &root) < 0 ) {
 		err("print_root()->read_struct");
 		return;
 	}
 	root_initialized = true;
 	print_inode(&root);
}

void
print_inode(inode_t *ip){
 	char *username  = getpwuid(ip->i_uid)->pw_name;
 	char *groupname = getgrgid(ip->i_gid)->gr_name;

 	debug("    inode: %d\n"
 		  "            size: %d\n"
 		  "            user: %s\n"
 		  "           group: %s\n"
 		  "            mode: 0%o"
 		  , ip->i_ino, ip->i_size, username, groupname, (int)ip->i_mode);
}