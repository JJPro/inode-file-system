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
  vcb_initialized = false;
  root_initialized = false;
  valid_initialized = false;

  vcb_t *vcbp = retrieve_vcb();
  if (!vcbp) {
    vfs_unmount(NULL); /* error reading vcb */
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
  fprintf(stderr, "vfs_getattr called\n");

  int ino;
  inode_t * inodep;

  // Do not mess with this code 
  stbuf->st_nlink = 1; // hard links
  stbuf->st_rdev  = 0;
  stbuf->st_blksize = BLOCKSIZE;

  /* 3600: YOU MUST UNCOMMENT BELOW AND IMPLEMENT THIS CORRECTLY */
  if (strcmp(path, "/") == 0 &&
      strcmp(path, "///") == 0){
    stbuf->st_mode  = (0777 & 0x0000ffff) | S_IFDIR;               /* is root */
    ino = 1;
  }
  else {
    if ((ino = find_ino(path)) < 0){
      return -ENOENT;
    }
  }
  if ( !(inodep = retrieve_inode(ino)) ){
    // err("       retrieve inode failed");
    return -1;
  }
  stbuf->st_ino   = ino;
  stbuf->st_mode  = I_ISREG(inodep->i_type) ? 
                    (inodep->i_mode & 0x0000ffff) | S_IFREG:
                    (inodep->i_mode & 0x0000ffff) | S_IFDIR;

  stbuf->st_uid     = inodep->i_uid;
  stbuf->st_gid     = inodep->i_gid;
  stbuf->st_atime   = inodep->i_atime.tv_sec;
  stbuf->st_mtime   = inodep->i_mtime.tv_sec;
  stbuf->st_ctime   = inodep->i_ctime.tv_sec;
  stbuf->st_size    = inodep->i_size;
  stbuf->st_blocks  = inodep->i_blocks;

  if (ino != 1)
      free(inodep);
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
    if (find_ino(path) > 0)
        return -EEXIST;

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
        return -ENOSPC;
    }
    int child_dirent_bnum = get_free_blocknum();
    if (child_dirent_bnum < 0)
        return -ENOSPC;
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
    valid_t *validp = retrieve_valid();
    validp->v_entries[child_ino] = V_USED;
    vcb_t *vcbp = retrieve_vcb();
    write_struct(vcbp->vb_valid, validp);
    
    int parent_ino = find_ino(parent);
    if (parent_ino < 0)
        return -ENOENT;
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
            return -ENOSPC;
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
    if (parent_ino != 1)
        free(parent_inodep);

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
    (void) fi;

    int     ino;
    inode_t *dp;
    entry_t *ep;

    if ( ( ino = find_ino(path) ) < 0 ) {
        return -1;
    }
    if ( !( dp = retrieve_inode(ino) ) ) {
        return -1;
    }
    if (dp->i_type != S_IFDIR)
        return -1;       /* not a directory */

    ep = step_dir(dp);
    if (offset == 0){
        offset++;
        if (filler(buf, ep->et_name, NULL, offset) != 0)
            return 0;
    } else {
        for(int i=1; i<offset; i++){
            step_dir(NULL);
        }        
    }
    while (dp->i_size > offset) /* still have more entries in the directory */
    {
        ep = step_dir(NULL);
        offset++;
        if (filler(buf, ep->et_name, NULL, offset) != 0)
            return 0;
    }
    if (ino != 1)
        free(dp);
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

    if (find_ino(path) > 0)
        return -EEXIST;                    /* file exists */
    
    int len = strlen(path)+1;
    char         m_path [len]; /* mutable path for dirname(), basename() */
    char         parent[len];   /* check */
    char         child[len];  /* check */
    strcpy(m_path, path); 
    strcpy(parent, dirname(m_path));                   /* dirname */
    strcpy(m_path, path);               /* reset m_path for next call */
    strcpy(child, basename(m_path));                 /* filename */

    struct timespec *now = get_time();

    int child_ino = get_new_ino();
    if (child_ino < 0){
        return -ENOSPC;
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
    valid_t *validp = retrieve_valid();
    validp->v_entries[child_ino] = V_USED;
    
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
            return -ENOSPC;
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
    if (parent_ino != 1)
        free(parent_inodep);
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

    int ino = find_ino(path);
    if (ino < 0)
        return -1;
    inode_t *inodep = retrieve_inode(ino);
    if (inodep->i_type != S_IFREG){
        if (ino != 1)
            free(inodep);
        return -1;
    }
    int f_size = inodep->i_size;
    if (offset > f_size)
        return -1;
    if (offset == f_size)
        return 0;
    int available = f_size - offset;

    char *buffer = (char *) malloc(BLOCKSIZE);
    int read = 0;
    int buffer_offset = -1;
    int blocknum;
    int i_direct_index = offset / BLOCKSIZE;

    bool within_block = ((offset % BLOCKSIZE) > 0);

    if (within_block){
        blocknum = get_data_blocknum(inodep, i_direct_index);
        dread(blocknum, buffer);
        buffer_offset = offset % BLOCKSIZE;
        int bytes_remaining_in_block = BLOCKSIZE - buffer_offset;
        if ((int)size <= bytes_remaining_in_block){
            memcpy(buf+read, buffer+buffer_offset, size);
            read += size;
            free(buffer);
            free(inodep);
            return read;
        } else {
            memcpy(buf+read, buffer+buffer_offset, bytes_remaining_in_block);
            available -= bytes_remaining_in_block;
            i_direct_index++;
            read += bytes_remaining_in_block;
            size -= bytes_remaining_in_block;
        }
    }
    while( (int)size >= BLOCKSIZE && available >= BLOCKSIZE){
        available -= BLOCKSIZE;
        size -= BLOCKSIZE;
        blocknum = get_data_blocknum(inodep, i_direct_index);
        dread(blocknum, buf+read);
        i_direct_index++;
        read += BLOCKSIZE;  
    }
    if ((int)size > 0){
        blocknum = get_data_blocknum(inodep, i_direct_index);
        /* read the rest bytes */
        if ((int)size <= available){
            dread(blocknum, buffer);
            memcpy(buf+read, buffer, size);
            read += size;
        } else {
            dread(blocknum, buffer);
            memcpy(buf+read, buffer, available);
            read += available;    
        }
    }

    if (ino != 1)
        free(inodep);
    free(buffer);
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

    int written = 0;
    int ino;
    inode_t *inodep;
    
    ino = find_ino(path);
    if (ino < 0)
        return -1;
    inodep = retrieve_inode(ino);
    if (!inodep)
        return -1;
    if (inodep->i_type != S_IFREG){
        if (ino != 1)
            free(inodep);
        return -1;
    }

    char *buffer = (char *) malloc(BLOCKSIZE);

    int ori_size = inodep->i_size;
    int trailings = offset - ori_size;
    bool append_to_buffer = false;
    int buffer_offset = 0;
    int i_direct_index = offset / BLOCKSIZE;
    int blocknum;
    int current_blocks = ceil((double)inodep->i_size / BLOCKSIZE);

    int final_size = (ori_size < ((int)offset + (int)size)) ? ((int)offset + (int)size) : ori_size;

    /* trailings part */
    if (trailings > 0){
        bool within_block = ((ori_size % BLOCKSIZE) > 0);

        if (within_block){
            i_direct_index = inodep->i_blocks -1;
            blocknum = inodep->i_direct[i_direct_index];
            dread(blocknum, buffer);
            buffer_offset = offset % BLOCKSIZE;
            int bytes_remaining_in_block = BLOCKSIZE - buffer_offset;
            if (trailings < bytes_remaining_in_block){
                memset(buffer+buffer_offset, 0, trailings);
                append_to_buffer = true;
                buffer_offset += trailings;
                trailings = 0;
            } else {
                memset(buffer+buffer_offset, 0, bytes_remaining_in_block);
                if (dwrite(blocknum, buffer)<0)
                    return -1;
                trailings -= bytes_remaining_in_block;
                i_direct_index++;
            }
        }
        while (trailings > BLOCKSIZE){
            blocknum = get_free_blocknum();
            if (blocknum<0)
                return -ENOSPC;
            current_blocks++;
            inodep->i_direct[i_direct_index] = blocknum;
            i_direct_index++;
            memset(buffer, 0, BLOCKSIZE);
            if (dwrite(blocknum, buffer)<0)
                return -1;
        }
        if (trailings > 0){
            blocknum = get_free_blocknum();
            if (blocknum<0)
                return -ENOSPC;
            current_blocks++;
            inodep->i_direct[i_direct_index] = blocknum;
            memset(buffer, 0, trailings);
            append_to_buffer = true;
            buffer_offset = trailings;
        }
    }
    /* true data part */
    bool within_block = ((offset % BLOCKSIZE) > 0);
    if (size == 0){
        written = 0;
        if (append_to_buffer){
            if (dwrite(blocknum, buffer)<0)
                return -1;
        }
        return 0;
    }
    if (within_block){
        if (append_to_buffer){
            int bytes_remaining_in_block = BLOCKSIZE - buffer_offset;
            if ((int)size <= bytes_remaining_in_block){
                memcpy(buffer+buffer_offset, buf, size);
                if (dwrite(blocknum, buffer)<0)
                    return -1;
                written += size;
            } else {
                memcpy(buffer+buffer_offset, buf, bytes_remaining_in_block);
                if (dwrite(blocknum, buffer) < 0)
                    return -1;
                written += BLOCKSIZE;
                i_direct_index++;
                size -= bytes_remaining_in_block;
            }
        } else {
            buffer_offset = offset % BLOCKSIZE;
            int bytes_remaining_in_block = BLOCKSIZE - buffer_offset;
            i_direct_index = offset / BLOCKSIZE;
            blocknum = get_data_blocknum(inodep, i_direct_index);
            dread(blocknum, buffer);
            if ((int)size <= bytes_remaining_in_block){ /* fill size bytes */
                memcpy(buffer+buffer_offset, buf, size);
                if(dwrite(blocknum, buffer)<0)
                    return -1;
                written += size;
            }else {                                     /* fill all rest of the block */
                memcpy(buffer+buffer_offset, buf, bytes_remaining_in_block);
                if (dwrite(blocknum, buffer) < 0)
                    return -1;
                written += bytes_remaining_in_block;
                i_direct_index++;
            }
        }
    }
    while ((int)size > BLOCKSIZE){
        if (written+offset >= BLOCKSIZE * current_blocks){
            blocknum = get_free_blocknum();
            if (blocknum<0)
                return -ENOSPC;
            current_blocks++;
            add_data_block(inodep, blocknum);
            if (dwrite(blocknum, (char*)buf+written)<0)
                return -1;
            i_direct_index++;
            written += BLOCKSIZE;
            size -= BLOCKSIZE;
        } else {
            blocknum = get_data_blocknum(inodep, i_direct_index);
            if (blocknum < 0)
                return -1;
            if (dwrite(blocknum, (char*)buf+written)<0)
                return -1;
            i_direct_index++;
            written += BLOCKSIZE;
            size -= BLOCKSIZE;
        }
    }
    if (size > 0){
        if (written+offset >= BLOCKSIZE * current_blocks){
            blocknum = get_free_blocknum();
            if (blocknum<0)
                return -ENOSPC;
            current_blocks++;
            add_data_block(inodep, blocknum);
            memset(buffer, 0, BLOCKSIZE);
            memcpy(buffer, buf+written, size);
            if (dwrite(blocknum, buffer)<0)
                return -1;
            written += size;
        } else {
            blocknum = get_data_blocknum(inodep, i_direct_index);
            dread(blocknum, buffer);
            memcpy(buffer, buf+written, size);
            if (dwrite(blocknum, buffer)<0)
                return -1;
            written += size;
        }
    }

    inodep->i_size = final_size;
    if (write_struct(ino, inodep) < 0)
        return -1;
    free(inodep);
    free(buffer);

    return written;
}

/**
 * This function deletes the last component of the path (e.g., /a/b/c you 
 * need to remove the file 'c' from the directory /a/b).
 */
static int vfs_delete(const char *path)
{
    fprintf(stderr, "vfs_delete is called\n");

    int len = strlen(path)+1;
    char m_path[len];
    char filename[len];
    char pathname[len];
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

    fileino = find_ino(path);
    if (fileino < 0)
        return -ENOENT;    /* file not exist */
    filep = retrieve_inode(fileino);
    if (!filep)
        return -1;
    if (filep->i_type == S_IFDIR)
        return -1;      /* path is a directory */
    vcbp = retrieve_vcb();
    validp = retrieve_valid();
    dirino = find_ino(pathname);
    if (dirino < 0)
        return -ENOENT;
    dirp = retrieve_inode(dirino);
    if (!dirp)
        return -1;

    /* free all data blocks taken by file */
    int blocks = filep->i_blocks;
    while (blocks > 0){
        int tofree = filep->i_direct[blocks-1];
        free_st.f_next = vcbp->vb_free;
        vcbp->vb_free = tofree;
        if (write_struct(tofree, &free_st) < 0)
            return -1;
        blocks--;
    }

    /* free file inode itself, set _ino field to 0, and mark as invalid in valid list */
    clear_inode(filep);
    write_struct(fileino, filep);

    /* clear the valid bit for valid list */
    validp->v_entries[fileino] = V_UNUSED;

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
        write_struct(dirent_bnum, dp);
    }

    /* parent size -1 */
    dirp->i_size--;
    write_struct(dirino, dirp);
    write_struct(0, vcbp);
    if (dirino != 1)
        free(dirp);
    free(filep);

    return 0;
}

static int vfs_rmdir(const char *path)
{
    fprintf(stderr, "vfs_rmdir called\n");

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

    fileino = find_ino(path);
    if (fileino < 0)
        return -ENOENT;    /* file not exist */
    filep = retrieve_inode(fileino);
    if (filep->i_type != S_IFDIR)
        return -ENOENT;      /* path is not a directory */
    vcbp = retrieve_vcb();
    validp = retrieve_valid();
    dirino = find_ino(pathname);
    dirp = retrieve_inode(dirino);

    /* free all data blocks taken by file */
    int blocks = filep->i_blocks;
    while (blocks > 0){
        int tofree = filep->i_direct[blocks-1];
        free_st.f_next = vcbp->vb_free;
        vcbp->vb_free = tofree;
        if (write_struct(tofree, &free_st) < 0)
            return -1;
        blocks--;
    }

    /* free file inode itself, set _ino field to 0, and mark as invalid in valid list */
    clear_inode(filep);
    write_struct(fileino, filep);

    /* clear the valid bit for valid list */
    validp->v_entries[fileino] = V_UNUSED;

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
        write_struct(dirent_bnum, dp);
    }

    /* parent size -1 */
    dirp->i_size--;
    write_struct(dirino, dirp);
    write_struct(0, vcbp);

    if (dirino != 1)
        free(dirp);
    free(filep);

    return 0;
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
    fprintf(stderr, "vfs_rename called\n");

    int len = strlen(from)+1;
    char m_from[len];
    char dir_from[len];
    char base_from[len];
    char m_to[len];
    char dir_to[len];
    char base_to[len];

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
    if (dir_from_ino < 0)
        return -1;
    inode_t *dir_from_p = retrieve_inode(dir_from_ino);
    if (!dir_from_p)
        return -1;

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
        if (dir_to_ino < 0){
            if (dir_from_ino != 1)
                free(dir_from_p);
            return -1;
        }
        inode_t *dir_to_p = retrieve_inode(dir_to_ino);
        if (!dir_to_p)
            return -1;
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
            free_blocknum(tofree);
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
            write_struct(I_BLOCK(insert), &dirent);
        }

        /* size -1 */
        dir_from_p->i_size--;
        write_struct(dir_from_ino, dir_from_p);
        if(dir_from_ino != 1)
            free(dir_from_p);


        /* now add to new dir */
        strcpy(entry.et_name, base_to);
        /* find a free slot */
        insert = search_empty_slot(dir_to_p, &dirent);
        offset = I_OFFSET(insert);
        dirent_bnum = I_BLOCK(insert);
        if (insert > 0){
            /* add to dirent */
            dirent.d_entries[offset] = entry;
            write_struct(dirent_bnum, &dirent);
        } else {
            /* get new dirent block */
            dirent_bnum = get_free_blocknum();
            if (dirent_bnum < 0)
                return -ENOSPC;
            /* init dirent */
            dirent_t dirent;
            clear_dirent(&dirent);
            dirent.d_entries[0] = entry;
            write_struct(dirent_bnum, &dirent);
            int i_direct_index = dir_to_p->i_blocks;
            dir_to_p->i_direct[i_direct_index] = dirent_bnum;

            dir_to_p->i_blocks++;
        }

        dir_to_p->i_size++;
        write_struct(dir_to_ino, dir_to_p);
        if (dir_to_ino != 1)
            free(dir_to_p);
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
    fprintf(stderr, "vfs_chmod called\n");
    int ino = find_ino(file);
    if (ino < 0)
        return -ENOENT;
    inode_t *filep = retrieve_inode(ino);
    if (!filep)
        return -1;
    filep->i_mode = (mode & 0x0000ffff);
    write_struct(ino, filep);
    if (ino != 1)
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
    fprintf(stderr, "vfs_chown called\n");
    int ino = find_ino(file);
    if (ino < 0)
        return -ENOENT;
    inode_t *filep = retrieve_inode(ino);
    if (!filep)
        return -1;
    filep->i_uid = uid;
    filep->i_gid = gid;
    write_struct(ino, filep);
    if (ino != 1)
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
    if (file_ino < 0) return -ENOENT;

    fp = retrieve_inode(file_ino);
    if (!fp) return -1;

    fp->i_atime = ts[0];
    fp->i_mtime = ts[1];

    write_struct(file_ino, fp);
    if (file_ino != 1)
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
    if (ino < 0)
        return -ENOENT;
    inode_t *fp = retrieve_inode(ino);
    if (fp->i_type != S_IFREG)
    {
        if (ino != 1)
            free(fp);
        return -1;
    }
    if (offset > fp->i_size)
        return -1;
    if (offset == fp->i_size)
        return 0;

    int new_size = (int) offset;
    int new_blocks = (int) ceil((double)new_size / BLOCKSIZE);
    int num_of_blocks_to_be_removed = fp->i_blocks - new_blocks;

    /* free data blocks */
    while(num_of_blocks_to_be_removed > 0){
        int index = num_of_blocks_to_be_removed + new_blocks - 1;
        int bnum = fp->i_direct[index];
        fp->i_direct[index] = -1;
        
        free_blocknum(bnum);
        num_of_blocks_to_be_removed--;        
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

