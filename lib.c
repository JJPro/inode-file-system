#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>  	/* S_IFREG, S_IFDIR */

#include "disk.h" 				/* BLOCKSIZE */
#include "lib.h"

static vcb_t 	vcb;
static inode_t 	root;
static valid_t 	valid;
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
	int valid_block;
	int insert_block;
	struct timespec *now;
	entry_t entry;
	dirent_t root_dirent;
	free_t free_b;
	inode_t inode;
	valid_t valid;

	min_required_blocks = 1 			/* vcb */
						+ I_TABLE_SIZE  /* inode table */
						+ 1 			/* valid table */
						+ 1 			/* root dirent */;
	if (size < min_required_blocks){
		err("size must be at least %d", min_required_blocks);
		return -1;
	}

	free_starter = min_required_blocks;
	valid_block = free_starter -2;
	vcb.vb_magic = MAGIC;
	vcb.vb_blocksize = BLOCKSIZE;
	vcb.vb_root = 1;
	vcb.vb_free = free_starter;
	vcb.vb_itable_size = I_TABLE_SIZE;
	vcb.vb_clean = true;
	vcb.vb_valid = valid_block;
	if (write_struct(0, &vcb) < 0)				/* vcb */
		return -1;
	vcb_initialized = true;

	insert_block  = free_starter - 1;
	root_dirent_block = insert_block;
	if (!(now = get_time()))
		return -1;
	inode.i_ino  = 1;
	inode.i_type = S_IFDIR;
	inode.i_size = 2;
	inode.i_uid  = getuid();
	inode.i_gid  = getgid();
	inode.i_mode = 0777;
	inode.i_blocks = 1;
	inode.i_atime = *now;
	inode.i_mtime = *now;
	inode.i_ctime = *now;
	inode.i_direct[0] = root_dirent_block;
	for (int i=1; i<106; i++){
		inode.i_direct[i] = -1;
	}
	inode.i_single = -1;
	inode.i_double = -1;
	root = inode;
	if (write_struct(1, &root) < 0)				/* root inode */
		return -1;
	root_initialized = true;

	inode.i_ino = 0;
	inode.i_direct[0] = -1;
	for (int i=2; i<I_TABLE_SIZE+1; i++){
		if ( write_struct(i, &inode) < 0 )		/* the rest of I-node table */
			return -1;
	}

	valid.v_entries[1] = V_USED;
	for (int i=2; i<I_TABLE_SIZE+1; i++){
		valid.v_entries[i] = V_UNUSED;
	}
	if (write_struct(valid_block, &valid) < 0)  /* valid table */
		return -1;

	clear_dirent(&root_dirent);
	entry.et_ino = 1;
	strcpy(entry.et_name, ".");			/* .  entry */
	root_dirent.d_entries[0] = entry;	
	strcpy(entry.et_name, "..");			/* .. entry */
	root_dirent.d_entries[1] = entry;
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
retrieve_root(){
	/* Returns a pointer to root inode, Or NULL on error */
	if (!root_initialized){
		if ( read_struct(1, &root) < 0 )
			return NULL;
		root_initialized = true;
	}
	return &root;
}

valid_t  *
retrieve_valid(){
	static bool cached = false;
	if (!cached
	   && (read_struct(vcb.vb_valid, &valid) < 0))
		return NULL;
	cached = true;
	return &valid;
}

inode_t *
retrieve_inode(int inode_num){
	/* Returns a pointer to specific inode, Or NULL on error */
	inode_t *ip = calloc (1, sizeof(inode_t));
	read_struct(inode_num, ip);
	return ip;
}

dirent_t *
retrieve_dirent(int blocknum)
	/* Returns a pointer to struct dirent, Or NULL on error */
{
	int min_required_blocks;
 	dirent_t * direntp = calloc(1, sizeof(dirent_t));

	min_required_blocks = 1 			/* vcb */
						+ I_TABLE_SIZE  /* inode table */
						+ 1 			/* valid list  */
						+ 1 			/* root dirent */;
	if (blocknum < min_required_blocks-1)
		return NULL;
	if ( read_struct(blocknum, direntp) < 0 )
		return NULL;
	return direntp;
}

entry_t *
fetch_entry(inode_t *dp, int offset)
{
	static entry_t entry;
	dirent_t *direntp;
	int index = offset / 8;
	int entry_offset = offset % 8;
	direntp = retrieve_dirent(dp->i_direct[index]);
	entry = direntp->d_entries[entry_offset];

	free(direntp);
	return &entry;
}

inode_t *
clear_inode(inode_t *dp)
{
	dp->i_ino = 0;
	dp->i_type = -1;
	dp->i_size = 0;
	dp->i_uid = 0;
	dp->i_gid = 0;
	dp->i_mode = 0;
	dp->i_blocks = 0;
	for (int i=0; i<106; i++){
		dp->i_direct[i] = -1;
	}
	dp->i_single = -1;
	dp->i_double = -1;

	return dp;
}

dirent_t * 
clear_dirent(dirent_t *dirp)
{
	entry_t entry;
	entry.et_ino = 0;
	
	for (int i=0; i<8; i++){
		dirp->d_entries[i] = entry;
	}

	return dirp;
}

// entry_t *
// step_dir(inode_t *dp)
// 		/* Returns 
// 				pointer to next entry in directory 
// 				NULL on no more entry
// 				NULL on error
// 		   Description: 
// 		   		<dp> must be provided on first call.
// 		   		pass <dp> as NULL on subsequent calls for the same directory.
// 		   		Returns the first entry in directory 	  if <dp> is given
// 		   		Returns the next  entry in same directory if <dp> is NULL */
// {
// 	static int 			offset;
// 	static int 			block_index;
// 	static inode_t		inode;
// 	static dirent_t 	dirent;
// 	static entry_t 		entry;
// 	static bool 	    block_finish = false;

// 	/* clear entry each step */
// 	memset(&entry , 0, sizeof(entry_t ));
// 	/* store inode on first call */
// 	if (dp) {
// 		memcpy(&inode, dp, sizeof(inode_t));
// 		offset 		= 0;
// 		block_index = 0;
// 		/* reload dirent */
// 		read_struct(inode.i_direct[block_index], &dirent);
// 	}
// 	/* keep reading until it reaches a valid entry */
// 	for (; block_index < inode.i_blocks; block_index++){
// 		/* reload dirent */
// 		if (block_finish)
// 			read_struct(inode.i_direct[block_index], &dirent);
// 		for (; offset < 8; offset++){
// 			entry = dirent.d_entries[offset];
// 			if (entry.et_ino > 0){
// 				offset++;
// 				return &entry;
// 			}
// 			block_finish = false;
// 		}
// 		offset = 0;
// 		block_finish = true;
// 	}
// 	return NULL;
// }

free_t *
get_free()
	/* Returns pointer to the free block, 
		Or NULL on no space or error */
{
	static free_t freeb;
	static int 	  last_free;
	vcb_t         *vbp;
	int 		  block;

	vbp = retrieve_vcb();
	if (!vbp)
		return NULL;
	block = vbp->vb_free;
	if (block < 0)
		return NULL;
	if (last_free == block)
		return &freeb; 					/* return cached when same as last request */
	if ( read_struct(block, &freeb) < 0 )
		return NULL;
	return &freeb;
}

int
get_new_ino()
{
	valid_t *vap = retrieve_valid();
	int i;
	bool found = false;
	for (i=1; i<I_TABLE_SIZE+1; i++){
		if(vap->v_entries[i] == V_UNUSED)
		{
			found = true;
			break;
		} 
	}
	if (found)
		return i;
	else 
		return -1; /* no inode available */
}

int 	 
get_free_blocknum()
{
	vcb_t *vbp = retrieve_vcb();
	int res = vbp->vb_free;
	free_t freeb;
	if (res > 0)
		read_struct(res, &freeb);
	vbp->vb_free = freeb.f_next;
	return res;
}

struct timespec *
get_time(){
	static struct timespec now;
	if (clock_gettime(CLOCK_REALTIME, &now) == -1)
		return NULL;
	return &now;
}

int
find_ino(const char *path)
		/* Returns inode number Or -1 on error */
{
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

	if ((strcmp(path, "/") == 0) ||
		(strcmp(path, "///") == 0))
		return 1;

	inodep = retrieve_root();
	tokenc = path2tokens(m_path, &tokens);
	if (tokenc < 0)
		return -1;
	tokenp = tokens;

	int i;
	int type = S_IFDIR; /* starts from root dir */
	for (i=0; 
		( i<(tokenc-1) && I_ISDIR(type) ); 
		i++, tokenp++)
	{
		token = *tokenp;
		if ( ( insert=search_entry(token, inodep, &dir, &ent) ) < 0 )
			return -1;
		if (!(inodep = retrieve_inode(ent.et_ino)))
			return -1;
		type = inodep->i_type;
		free(inodep);
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
search_entry(const char *et_name, inode_t *inodep, dirent_t *dirp, entry_t *entp)
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
			entry = direntp->d_entries[j];
			if ( entry.et_ino > 0
				&& strcmp(entry.et_name, et_name) == 0 )
			{
				offset = j;
				block = inodep->i_direct[i];
				insert = I_INSERT(block, offset);
			
				*dirp = *direntp;
				*entp = entry;
				free(direntp);
				return insert;
			}
		}
		free(direntp);
	}
	return -1;
}

insert_t 
search_empty_slot(inode_t *parentp, dirent_t *direntp)
{
	if (parentp->i_size == parentp->i_blocks * 8)
		return -1;

	insert_t res = -1;
	dirent_t dirent;
	for (int i=0; i<parentp->i_blocks; i++){
		read_struct(parentp->i_direct[i], &dirent);
		for(int j=0; j<8; j++){
			if (dirent.d_entries[j].et_ino == 0){
				*direntp = dirent;
				res = I_INSERT(parentp->i_direct[i], j);
				return res;
			}
		}
	}
	return res;
}


int 
write_struct(int blocknum, void *structp){
	return dwrite(blocknum, structp);
}

int 
read_struct (int blocknum, void *structp){
	int bytes_read = dread(blocknum, (char *)structp);
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
		  "            free: %d\n"
		  "           valid: %d"
		  , vcb.vb_magic, vcb.vb_root, vcb.vb_free, vcb.vb_valid);
}

void 
print_root(){
 	debug("print_root:");
	inode_t *rootp;
 	
 	rootp = retrieve_root();
 	print_inode(rootp);
}

void
print_inode(inode_t *ip){
 	char *username  = getpwuid(ip->i_uid)->pw_name;
 	char *groupname = getgrgid(ip->i_gid)->gr_name;

 	dirent_t *direntp;
 	entry_t  e1, e2;

 	direntp = retrieve_dirent(ip->i_direct[0]);
 	if (!direntp){
 		err("failed to retrieve dirent @ %d", ip->i_direct[0]);
 		return;
 	}
 	e1 = direntp->d_entries[0];
 	e2 = direntp->d_entries[1];

 	debug("    inode: %d\n"
 		  "            size: %d\n"
 		  "            user: %s\n"
 		  "           group: %s\n"
 		  "            mode: 0%o\n"
 		  "       direct[0]: %d\n"
 		  "           file1: %s %d\n"
 		  "           file2: %s %d"
 		  , ip->i_ino, ip->i_size, username, groupname, (int)ip->i_mode, 
 		  ip->i_direct[0], e1.et_name, e1.et_ino, e2.et_name, e2.et_ino);
}