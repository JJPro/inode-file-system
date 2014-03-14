/*
 * CS3600, Spring 2014
 * Project 2 Starter Code
 * (c) 2013 Alan Mislove
 *
 * This file contains all of the basic functions that you will need 
 * to implement for this project.  Please see the project handout
 * for more details on any particular function, and ask on Piazza if
 * you get stuck.
 */

#define FUSE_USE_VERSION 26

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#define _POSIX_C_SOURCE 199309

#include <time.h>
#include <fuse.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include <sys/statfs.h>
#include <libgen.h> /* dirname(), 
                        basename() */

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include "3600fs.h"
#include "disk.h"
#include "lib.h"

static void vfs_unmount (void *private_data);

/*
 * Initialize filesystem. Read in file system metadata and initialize
 * memory structures. If there are inconsistencies, now would also be
 * a good time to deal with that. 
 *
 * HINT: You don't need to deal with the 'conn' parameter AND you may
 * just return NULL.
 *
 */
static void* vfs_mount(struct fuse_conn_info *conn) {
  fprintf(stderr, "vfs_mount called\n");
  (void) conn;
  // Do not touch or move this code; connects the disk
  dconnect();

  /* 3600: YOU SHOULD ADD CODE HERE TO CHECK THE CONSISTENCY OF YOUR DISK
           AND LOAD ANY DATA STRUCTURES INTO MEMORY */
  vcb_t *vcbp = retrieve_vcb();
  if (!vcbp) {
    err("Mount Failed\n"
      "Error retrieving vcb");
    vfs_unmount(NULL);
  }

  if (vcbp->vb_magic != MAGIC){
    err("Mount Failed\n"
      "This isn't your disk!");
    vfs_unmount(NULL);
  }

  if (!(vcbp->vb_clean)){
    err("Data damaged, last umount was not friendly");
    vfs_unmount(NULL);
  }
  vcbp->vb_clean = false;

  if (write_struct(0, vcbp) < 0)
    err("       ->updating vcb");

  return NULL;
}

/*
 * Called when your file system is unmounted.
 *
 */
static void vfs_unmount (void *private_data) {
  fprintf(stderr, "vfs_unmount called\n");
  (void) private_data;

  /* 3600: YOU SHOULD ADD CODE HERE TO MAKE SURE YOUR ON-DISK STRUCTURES
           ARE IN-SYNC BEFORE THE DISK IS UNMOUNTED (ONLY NECESSARY IF YOU
           KEEP DATA CACHED THAT'S NOT ON DISK */
  inode_t      *rootp      = retrieve_root();
  vcb_t        *vcbp       = retrieve_vcb();
  valid_t *validp = retrieve_valid();
  write_struct(1, rootp);
  write_struct(vcbp->vb_valid, validp);  /* flush valid cache */
  vcbp->vb_clean = true;                 /* all writes are successful, reset clean bit */
  write_struct(0, vcbp);                   /* flush vcb cache */

  // Do not touch or move this code; unconnects the disk
  dunconnect();
}

/* 
 *
 * Given an absolute path to a file/directory (i.e., /foo ---all
 * paths will start with the root directory of the CS3600 file
 * system, "/"), you need to return the file attributes that is
 * similar stat system call.
 *
 * HINT: You must implement stbuf->stmode, stbuf->st_size, and
 * stbuf->st_blocks correctly.
 *
 */
static int vfs_getattr(const char *path, struct stat *stbuf) {
  fprintf(stdout, "readdir called\n");
  fprintf(stderr, "vfs_getattr called\n");

  int ino;
  inode_t * inodep;

  // Do not mess with this code 
  stbuf->st_nlink = 1; // hard links
  stbuf->st_rdev  = 0;
  stbuf->st_blksize = BLOCKSIZE;

  /* 3600: YOU MUST UNCOMMENT BELOW AND IMPLEMENT THIS CORRECTLY */
  // debug("       looking for path: %s", path);
  if (strcmp(path, "/") == 0 &&
      strcmp(path, "///") == 0){
    stbuf->st_mode  = 0777 | S_IFDIR;               /* is root */
    ino = 1;
  }
  else {
    if ((ino = find_ino(path)) < 0){
      // debug("       invalid path");
      return -1;
    }
  }
  if ( !(inodep = retrieve_inode(ino)) ){
    // err("       retrieve inode failed");
    return -1;
  }
  stbuf->st_mode  = I_ISREG(inodep->i_type) ? 
                    inodep->i_mode | S_IFREG:
                    inodep->i_mode | S_IFDIR;

  stbuf->st_uid     = inodep->i_uid;
  stbuf->st_gid     = inodep->i_gid;
  stbuf->st_atime   = inodep->i_atime.tv_sec;
  stbuf->st_mtime   = inodep->i_mtime.tv_sec;
  stbuf->st_ctime   = inodep->i_ctime.tv_sec;
  stbuf->st_size    = inodep->i_size;
  stbuf->st_blocks  = inodep->i_blocks;

  free(inodep);

  // debug("       ino for %s is %d", path, ino);
  // debug("       information about it: \n"
  //       "              uid: %d\n"
  //       "              gid: %d\n"
  //       "              atime: %s\n"
  //       "              size: %d\n"
  //       "              blocks: %d",
  //       (int)stbuf->st_uid, 
  //       (int)stbuf->st_gid,
  //       ctime(&(stbuf->st_atime)),
  //       (int)stbuf->st_size,
  //       (int)stbuf->st_blocks);                     
  fprintf(stdout, "getattr() return\n");
  fprintf(stderr, "getattr() return\n");
  return 0;
}

/*
 * Given an absolute path to a directory (which may or may not end in
 * '/'), vfs_mkdir will create a new directory named dirname in that
 * directory, and will create it with the specified initial mode.
 *
 * HINT: Don't forget to create . and .. while creating a
 * directory.
 */
static int vfs_mkdir(const char *path, mode_t mode) 
{
    // fprintf(stdout, "mkdir called\n");

    fprintf(stderr,"vfs_mkdir() called\n");
    char         m_path [strlen(path)+1]; /* mutable path for dirname(), basename() */
    char         parent[strlen(path)+1];   /* check */
    char         child[strlen(path)+1];  /* check */
    strcpy(m_path, path); 
    strcpy(parent, dirname(m_path));                   /* dirname */
    strcpy(m_path, path);               /* reset m_path for next call */
    strcpy(child, basename(m_path));                 /* filename */

    struct timespec *now = get_time();

    int child_ino = get_new_ino();
    if (child_ino < 0){
        // err("disk full");
        return -1;
    }
    int child_dirent_bnum = get_free_blocknum();
    inode_t child_inode;
    clear_inode(&child_inode);
    child_inode.i_ino = child_ino;
    child_inode.i_type = S_IFDIR;
    child_inode.i_size = 2;
    child_inode.i_uid = getuid();
    child_inode.i_gid = getgid();
    child_inode.i_mode = mode & 0x0000ffff;
    child_inode.i_blocks = 1;
    child_inode.i_atime = *now;
    child_inode.i_mtime = *now;
    child_inode.i_ctime = *now;
    child_inode.i_direct[0] = child_dirent_bnum;
    write_struct(child_ino, &child_inode);
                                            /* child inode done */
    int parent_ino = find_ino(parent);
    dirent_t child_dirent;
    clear_dirent(&child_dirent);
    entry_t entry;
    entry.et_ino = child_ino;
    strcpy(entry.et_name, ".");
    child_dirent.d_entries[0] = entry;
    entry.et_ino = parent_ino;
    strcpy(entry.et_name, "..");
    child_dirent.d_entries[1] = entry;
    write_struct(child_dirent_bnum, &child_dirent);
                                            /* child dirent done */
    inode_t *parent_inodep = retrieve_inode(parent_ino);
    dirent_t parent_dirent;
    entry.et_ino = child_ino;
    strcpy(entry.et_name, child);
    int insert = search_empty_slot(parent_inodep, &parent_dirent);
    if (insert > 0){
        parent_dirent.d_entries[I_OFFSET(insert)] = entry;
        write_struct(I_BLOCK(insert), &parent_dirent);
                                            /* parent dirent - case 1 */
    } else {
        int parent_dirent_bnum = get_free_blocknum();
        if (parent_dirent_bnum < 0){
            // err("Disk full");
            return -1;
        }    
        clear_dirent(&parent_dirent);
        parent_dirent.d_entries[0] = entry;
        write_struct(parent_dirent_bnum, &parent_dirent);
                                            /* parent dirent - case 2 */
        parent_inodep->i_blocks++;
        parent_inodep->i_direct[parent_inodep->i_blocks - 1] = parent_dirent_bnum;
    }
    parent_inodep->i_size++;
    parent_inodep->i_atime = *now;
    parent_inodep->i_mtime = *now;
    parent_inodep->i_ctime = *now;
    write_struct(parent_ino, parent_inodep);
    free(parent_inodep);

    // fprintf(stdout, "mkdir() return\n");
    fprintf(stderr, "mkdir() return\n");
    return 0;
} 

/** Read directory
 *
 * Given an absolute path to a directory, vfs_readdir will return 
 * all the files and directories in that directory.
 *
 * HINT:
 * Use the filler parameter to fill in, look at fusexmp.c to see an example
 * Prototype below
 *
 * Function to add an entry in a readdir() operation
 *
 * @param buf the buffer passed to the readdir() operation
 * @param name the file name of the directory entry
 * @param stat file attributes, can be NULL
 * @param off offset of the next entry or zero
 * @return 1 if buffer is full, zero otherwise
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *                                 const struct stat *stbuf, off_t off);
 *			   
 * Your solution should not need to touch fi
 *
 */
static int vfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    // fprintf(stdout, "readdir called\n");
    // debug("vfs_readdir()");
    (void) fi; (void) offset;

    int     ino;
    inode_t *dp;
    inode_t *inodep;
    entry_t *ep;

    struct stat st;

    if ( ( ino = find_ino(path) ) < 0 ) {
        // debug("       inode not exist for path %s", path);
        return -1;
    }
    if ( !( dp = retrieve_inode(ino) ) ) {
        // debug("       retrieve inode failed");
        return -1;
    }
    ep = step_dir(dp);
    if ( !(inodep = retrieve_inode(ep->et_ino)) ) return -1;
    st.st_ino = inodep->i_ino;
    st.st_mode = inodep->i_type | inodep->i_mode;
    if (filler(buf, ep->et_name, &st, 0) != 0)
        return -1;

    while ((ep = step_dir(NULL))) {
        // debug("       current entry inode: %d", ep->et_ino);
        memset(&st, 0, sizeof(st));
        if ( !(inodep = retrieve_inode(ep->et_ino)) ) return -1;

        st.st_ino = inodep->i_ino;
        st.st_mode = inodep->i_type | inodep->i_mode;
        if (filler(buf, ep->et_name, &st, 0) != 0)
            return -1;
    }

    free(dp);
    // fprintf(stdout, "readdir() return\n");
    // fprintf(stderr, "readdir() return\n");
    return 0;
}

/*
 * Given an absolute path to a file (for example /a/b/myFile), vfs_create 
 * will create a new file named myFile in the /a/b directory.
 *
 */
static int vfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) 
{
    // fprintf(stdout, "create called\n");
    fprintf(stderr,"vfs_create() called\n");
    (void) fi;
    
    char         m_path [strlen(path)+1]; /* mutable path for dirname(), basename() */
    char         parent[strlen(path)+1];   /* check */
    char         child[strlen(path)+1];  /* check */
    strcpy(m_path, path); 
    strcpy(parent, dirname(m_path));                   /* dirname */
    strcpy(m_path, path);               /* reset m_path for next call */
    strcpy(child, basename(m_path));                 /* filename */

    struct timespec *now = get_time();

    int child_ino = get_new_ino();
    if (child_ino < 0){
        // err("disk full");
        return -1;
    }
    inode_t child_inode;
    clear_inode(&child_inode);
    child_inode.i_ino = child_ino;
    child_inode.i_type = S_IFREG;
    child_inode.i_size = 0;
    child_inode.i_uid = getuid();
    child_inode.i_gid = getgid();
    child_inode.i_mode = mode & 0x0000ffff;
    child_inode.i_atime = *now;
    child_inode.i_mtime = *now;
    child_inode.i_ctime = *now;
    write_struct(child_ino, &child_inode);
                                            /* child inode done */
    int parent_ino = find_ino(parent);
    inode_t *parent_inodep = retrieve_inode(parent_ino);
    dirent_t parent_dirent;
    entry_t entry;
    entry.et_ino = child_ino;
    strcpy(entry.et_name, child);
    int insert = search_empty_slot(parent_inodep, &parent_dirent);
    if (insert > 0){
        parent_dirent.d_entries[I_OFFSET(insert)] = entry;
        write_struct(I_BLOCK(insert), &parent_dirent);
                                            /* parent dirent - case 1 */
    } else {
        int parent_dirent_bnum = get_free_blocknum();
        if (parent_dirent_bnum < 0){
            // err("Disk full");
            return -1;
        }    
        clear_dirent(&parent_dirent);
        parent_dirent.d_entries[0] = entry;
        write_struct(parent_dirent_bnum, &parent_dirent);
                                            /* parent dirent - case 2 */
        parent_inodep->i_blocks++;
        parent_inodep->i_direct[parent_inodep->i_blocks - 1] = parent_dirent_bnum;
    }
    parent_inodep->i_size++;
    parent_inodep->i_atime = *now;
    parent_inodep->i_mtime = *now;
    parent_inodep->i_ctime = *now;
    write_struct(parent_ino, parent_inodep);
    free(parent_inodep);

    
    // fprintf(stdout, "create() return\n");
    // fprintf(stderr, "create() return\n");
    return 0;
}

/*
 * The function vfs_read provides the ability to read data from 
 * an absolute path 'path,' which should specify an existing file.
 * It will attempt to read 'size' bytes starting at the specified
 * offset (offset) from the specified file (path)
 * on your filesystem into the memory address 'buf'. The return 
 * value is the amount of bytes actually read; if the file is 
 * smaller than size, vfs_read will simply return the most amount
 * of bytes it could read. 
 *
 * HINT: You should be able to ignore 'fi'
 *
 */
static int vfs_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    fprintf(stderr, "vfs_read called\n"); (void) fi;
    (void) fi;

    int ino;
    inode_t *inodep;
    int targetblock_index;

    int targetblock_offset;
    char buffer[BLOCKSIZE];
    int read;
    int maximum_read;
    int buffer_index;

    ino = find_ino(path);
    inodep = retrieve_inode(ino);
    targetblock_index = (int)offset / BLOCKSIZE;
    targetblock_offset = (int)offset % BLOCKSIZE;
    read = 0;
    buffer_index = targetblock_offset;
    maximum_read = inodep->i_size - offset;

    if (offset > inodep->i_size)
        return 0;

    if (dread(inodep->i_direct[targetblock_index], buffer) < 0) 
        return 0;

    while (read < (int)size){

        if (buffer_index > (BLOCKSIZE-1)){
            /* reload the buffer with next block data */
            targetblock_index++;
            if (dread(inodep->i_direct[targetblock_index], buffer) < 0)
                return 0;
            buffer_index = 0;
        }

        *buf = buffer[targetblock_offset + read];

        read++;
        if (read >= maximum_read)
            break;
        
        buf++;
        buffer_index++;
    }

    free(inodep);

    return read;
}

/*
 * The function vfs_write will attempt to write 'size' bytes from 
 * memory address 'buf' into a file specified by an absolute 'path'.
 * It should do so starting at the specified offset 'offset'.  If
 * offset is beyond the current size of the file, you should pad the
 * file with 0s until you reach the appropriate length.
 *
 * You should return the number of bytes written.
 *
 * HINT: Ignore 'fi'
 */
static int vfs_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{

  /* 3600: NOTE THAT IF THE OFFSET+SIZE GOES OFF THE END OF THE FILE, YOU
           MAY HAVE TO EXTEND THE FILE (ALLOCATE MORE BLOCKS TO IT). */

    fprintf(stderr, "vfs_write called\n"); (void) fi;
    (void) fi;

    char buffer[BLOCKSIZE];
    int written;
    int ino;
    inode_t *inodep;
    int current_writing_block_index;        /* ****/
    int in_block_offset;
    int number_of_paddings;
    int final_file_size;
    int final_blocks_in_file; 
    vcb_t *vcbp;

    ino = find_ino(path);
    if (ino < 0)
        return -1;
    inodep = retrieve_inode(ino);
    written = 0;

    final_file_size      = (int)(offset+size) > inodep->i_size ? 
                            (int)offset+(int)size:
                            inodep->i_size;
    final_blocks_in_file = (ceil(final_file_size / BLOCKSIZE));
    number_of_paddings   = offset - inodep->i_size;
    if (number_of_paddings > 0){
        /* current_writing_block_index is last block  or new block */
        current_writing_block_index = (inodep->i_size % BLOCKSIZE == 0) ?
                                        inodep->i_blocks : 
                                        inodep->i_blocks - 1;
    } else {
        /* no paddings, offset is within the file data */
        current_writing_block_index = offset / BLOCKSIZE;           /* current writing block index */
        in_block_offset = offset % BLOCKSIZE;                       /* in_block_offset */
        dread(inodep->i_direct[current_writing_block_index], buffer); /* prepare buffer to write to */
    }

    int blocknum;
    while (number_of_paddings > 0){
        if (in_block_offset == BLOCKSIZE){
            current_writing_block_index++;
            in_block_offset = 0;

            /* flush current buffer to disk */
            dwrite(blocknum, buffer);

            /* assign a new data block to write to */
            vcbp = retrieve_vcb();
            blocknum = vcbp->vb_free;
            free_t freeb;
            read_struct(blocknum, &freeb);
            vcbp->vb_free = freeb.f_next;
        }
        /* writing in last block */ 
        if (current_writing_block_index == (inodep->i_blocks -1)){
            if (dread(inodep->i_direct[current_writing_block_index], buffer)<0)
                return written;
            in_block_offset = inodep->i_size % BLOCKSIZE;           /* buffer, in_block_offset */
        }

        /* writing to buffer */
        buffer[in_block_offset] = '\0';
    
        in_block_offset++;
        number_of_paddings--;
    }

    /* writing real data */
    while (size > 0){
        if (in_block_offset == BLOCKSIZE){
            in_block_offset = 0;
            current_writing_block_index++;

            /* flush buffer to disk */
            dwrite(blocknum, buffer);

            /* assign new block to continue writing */
            vcbp = retrieve_vcb();
            blocknum = vcbp->vb_free;
            free_t freeb;
            read_struct(blocknum, &freeb);
            vcbp->vb_free = freeb.f_next;
        }

        /* writing to buffer */
        buffer[in_block_offset] = *buf;

        buf++;
        written++;
        in_block_offset++;
        size--;
    }

    /* update inode */
    inodep->i_size = final_file_size;
    inodep->i_blocks = final_blocks_in_file;
    write_struct(ino, inodep);
    free(inodep);

    return written;
}

/**
 * This function deletes the last component of the path (e.g., /a/b/c you 
 * need to remove the file 'c' from the directory /a/b).
 */
static int vfs_delete(const char *path)
{
    fprintf(stderr, "vfs_delete is called\n");

    char m_path[strlen(path)+1];
    char filename[strlen(path)+1];
    char pathname[strlen(path)+1];
    strcpy(m_path, path);
    strcpy(filename, basename(m_path));
    strcpy(m_path, path);
    strcpy(pathname, dirname(m_path));
    free_t free_st;

    int fileino;
    int dirino;
    inode_t *filep;
    inode_t *dirp;
    dirent_t dirent;
    vcb_t * vcbp;
    valid_t *validp;
    entry_t entry;

    vcbp = retrieve_vcb();
    validp = retrieve_valid();
    dirino = find_ino(pathname);
    dirp = retrieve_inode(dirino);
    fileino = find_ino(path);
    filep = retrieve_inode(fileino);

    /* free all data blocks taken by file */
    int blocks = filep->i_blocks;
    while (blocks > 0){
        int tofree = filep->i_direct[blocks-1];
        free_st.f_next = vcbp->vb_free;
        vcbp->vb_free = tofree;
        write_struct(tofree, &free_st);
    }

    /* free file inode itself */
    clear_inode(filep);

    /* clear the valid bit for valid list */
    validp->v_entries[fileino] = V_INVALID;

    /***** in directory ***/
    insert_t insert = search_entry(filename, dirp, &dirent, &entry);
    int dirent_bnum = I_BLOCK(insert);
    int offset = I_OFFSET(insert);
    dirent_t *dp = retrieve_dirent(dirent_bnum);

    bool allclear = true;
    for (int i=0; i<8; i++){
        if (i == offset && (offset != 7))
            continue;
        if (dp->d_entries[i].et_ino != 0){
            allclear = false;
            break;
        }
    }
    if (allclear){
        int tofree = dirent_bnum;
        free_st.f_next = vcbp->vb_free;
        vcbp->vb_free = tofree;
        write_struct(tofree, &free_st);
        /* parent direct[] adjust, movements */
        if (dirp->i_direct[(dirp->i_blocks - 1)] != dirent_bnum){
            int index;
            for (index=0; index<dirp->i_blocks; index++){
                if (dirp->i_direct[index] == dirent_bnum)
                    break;
            }
            /* move last direct[] to index position */
            dirp->i_direct[index] = dirp->i_direct[dirp->i_blocks-1];
        }
        /* i_direct [last] = -1 */
        dirp->i_direct[dirp->i_blocks-1] = -1;
        /* parent blocks -1 */
        dirp->i_blocks--;
    } else {
        dp->d_entries[offset].et_ino = 0;
    }

    /* parent size -1 */
    dirp->i_size--;



    return 0;
}

static int vfs_rmdir(const char *path)
{
    return vfs_delete(path);
}

/*
 * The function rename will rename a file or directory named by the
 * string 'oldpath' and rename it to the file name specified by 'newpath'.
 *
 * HINT: Renaming could also be moving in disguise
 *
 */
static int vfs_rename(const char *from, const char *to)
{
    char m_from[strlen(from)+1];
    char dir_from[strlen(from)+1];
    char base_from[strlen(from)+1];
    char m_to[strlen(to)+1];
    char dir_to[strlen(to)+1];
    char base_to[strlen(to)+1];

    strcpy(m_from, from);
    strcpy(dir_from, dirname(m_from));
    strcpy(m_from, from);
    strcpy(base_from, basename(m_from));

    strcpy(m_to, to);
    strcpy(dir_to, dirname(m_to));
    strcpy(m_to, to);
    strcpy(base_to, basename(m_to));

    bool samedir = (strcmp(dir_from, dir_to) == 0) ? true : false;

    int      dir_from_ino = find_ino(dir_from);
    inode_t *dir_from_p = retrieve_inode(dir_from_ino);

    int dirent_bnum;

    if (samedir){
        dirent_t dirent;
        entry_t entry;
        insert_t insert = search_entry(base_from, dir_from_p, &dirent, &entry);
        strcpy(entry.et_name, base_to);
        dirent.d_entries[I_OFFSET(insert)] = entry;
        write_struct(I_BLOCK(insert), &dirent);
    } else {
        int      dir_to_ino = find_ino(dir_to);
        inode_t *dir_to_p = retrieve_inode(dir_to_ino);
        
        /* remove from old dir */
        dirent_t dirent;
        entry_t entry;
        insert_t insert = search_entry(base_from, dir_from_p, &dirent, &entry);
        /*  - check if the rest entries are all clear */
        int offset = I_OFFSET(insert);
        bool allclear = true;
        for (int i=0; i<8; i++){
            if (i == offset)
                continue;
            if (dirent.d_entries[i].et_ino != 0){
                allclear = false;
                break;
            }
        }
        if (allclear){
            /* remove dirent block, free it */
            int tofree = I_BLOCK(insert);
            free_t free_st;
            vcb_t *vcbp = retrieve_vcb();
            free_st.f_next = vcbp->vb_free;
            vcbp->vb_free = tofree;
            write_struct(tofree, &free_st);
            /* direct[] adjust, movements */
            dirent_bnum = tofree;
            if (dir_from_p->i_direct[dir_from_p->i_blocks-1] != dirent_bnum){
                int index;
                for (index=0; index < dir_from_p->i_blocks; index++){
                    if (dir_from_p->i_direct[index] == dirent_bnum)
                        break;
                }
                /* move last direct[] to position of index */
                dir_from_p->i_direct[index] = dir_from_p->i_direct[dir_from_p->i_blocks-1];
            }
            /* i_drect[last] = -1 */
            dir_from_p->i_direct[dir_from_p->i_blocks-1] = -1;
            /* blocks-- */
            dir_from_p->i_blocks--;
        } else {
            /* set entry ino to 0 */
            dirent.d_entries[offset].et_ino = 0;
        }

        /* size -1 */
        dir_from_p->i_size--;



        /* now add to new dir */
        strcpy(entry.et_name, base_to);
        /* find a free slot */
        for (int i=0; i<dir_to_p->i_blocks; i++){
            dirent_t *direntp = retrieve_dirent(dir_to_p->i_direct[i]);
            int index;
            bool found = false;
            for (index=0; index<8; index++){
                if (direntp->d_entries[index].et_ino == 0){
                    found = true;
                    break;
                }
            }
            if (found){
                /* add to dirent */
                dirent.d_entries[index] = entry;
            } else {
                /* get new dirent block */
                vcb_t *vcbp = retrieve_vcb();
                int blocknum = vcbp->vb_free;
                free_t freeb;
                read_struct(blocknum, &freeb);
                vcbp->vb_free = freeb.f_next;

                /* init dirent */
                dirent_t dirent;
                clear_dirent(&dirent);
                dirent.d_entries[0] = entry;
                write_struct(blocknum, &dirent);

                dir_to_p->i_blocks++;
            }
        }
        dir_to_p->i_size++;
    }



    return 0;
}


/*
 * This function will change the permissions on the file
 * to be mode.  This should only update the file's mode.  
 * Only the permission bits of mode should be examined 
 * (basically, the last 16 bits).  You should do something like
 * 
 * fcb->mode = (mode & 0x0000ffff);
 *
 */
static int vfs_chmod(const char *file, mode_t mode)
{
    int ino = find_ino(file);
    inode_t *filep = retrieve_inode(ino);
    filep->i_mode = (mode & 0x0000ffff);
    write_struct(ino, filep);
    free(filep);
    return 0;
}

/*
 * This function will change the user and group of the file
 * to be uid and gid.  This should only update the file's owner
 * and group.
 */
static int vfs_chown(const char *file, uid_t uid, gid_t gid)
{
    int ino = find_ino(file);
    inode_t *filep = retrieve_inode(ino);
    filep->i_uid = uid;
    filep->i_gid = gid;
    write_struct(ino, filep);
    free(filep);

    return 0;
}

/*
 * This function will update the file's last accessed time to
 * be ts[0] and will update the file's last modified time to be ts[1].
 */
static int vfs_utimens(const char *file, const struct timespec ts[2])
{
    fprintf(stderr,"vfs_utimens() called\n");
    int file_ino;
    inode_t *fp;

    file_ino = find_ino(file);
    if (file_ino < 0) return -1;

    fp = retrieve_inode(file_ino);
    if (!fp) return -1;

    fp->i_atime = ts[0];
    fp->i_mtime = ts[1];

    write_struct(file_ino, fp);
    free(fp);

    return 0;
}

/*
 * This function will truncate the file at the given offset
 * (essentially, it should shorten the file to only be offset
 * bytes long).
 */
static int vfs_truncate(const char *file, off_t offset)
{
    fprintf(stderr, "vfs_truncate is called\n");

    int ino = find_ino(file);
    inode_t *fp = retrieve_inode(ino);
    vcb_t *vcbp = retrieve_vcb();
    free_t free_st;

    int new_size = (int) offset;
    int new_blocks = (int) ceil(offset / BLOCKSIZE);
    int num_of_blocks_removed = fp->i_blocks - new_blocks;
    if (num_of_blocks_removed <= 0)
        return 0;

    /* free data blocks */
    int _start = new_blocks;
    int _end = fp->i_blocks;
    for (; _start < _end; _start++){
        int tofree = fp->i_direct[_start];
        free_st.f_next = vcbp->vb_free;
        vcbp->vb_free = tofree;
        write_struct(tofree, &free_st);
        /* set direct[]s to -1 */
        fp->i_direct[_start] = -1;
    }

    /* add trailing 0s to last block if neccesary */
    int d_offset = new_size % BLOCKSIZE + 1;
    int block_bnum = fp->i_direct[new_blocks-1];
    if (d_offset != 0){
        char buffer[BLOCKSIZE];
        if (dread(block_bnum, buffer) < 0)
            return -1;
        for (; d_offset<BLOCKSIZE; d_offset++){
            buffer[d_offset] = 0;
        }
        if (dwrite(block_bnum, buffer) < 0)
            return -1;
    }

    /* update inode */
    fp->i_size = new_size;
    fp->i_blocks = new_blocks;
    write_struct(ino, fp);
    free(fp);
    return 0;
}


/*
 * You shouldn't mess with this; it sets up FUSE
 *
 * NOTE: If you're supporting multiple directories for extra credit,
 * you should add 
 *
 *     .mkdir	 = vfs_mkdir,
 */
static struct fuse_operations vfs_oper = {
    .init    = vfs_mount,
    .destroy = vfs_unmount,
    .getattr = vfs_getattr,
    .readdir = vfs_readdir,
    .create	 = vfs_create,
    .read	 = vfs_read,
    .write	 = vfs_write,
    .unlink	 = vfs_delete,
    .rename	 = vfs_rename,
    .chmod	 = vfs_chmod,
    .chown	 = vfs_chown,
    .utimens	 = vfs_utimens,
    .truncate	 = vfs_truncate,
    .mkdir       = vfs_mkdir, 
    .rmdir       = vfs_rmdir,
};

int main(int argc, char *argv[]) {
    /* Do not modify this function */
    umask(0);
    if ((argc < 4) || (strcmp("-s", argv[1])) || (strcmp("-d", argv[2]))) {
      printf("Usage: ./3600fs -s -d <dir>\n");
      exit(-1);
    }
    return fuse_main(argc, argv, &vfs_oper, NULL);
}

