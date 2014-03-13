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

static bool vcb_needs_update  = false;
static bool root_needs_update = false;
static bool validation_needs_update = false;

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
  vcb_needs_update  = false;
  root_needs_update = false;
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

  /* 3600: YOU SHOULD ADD CODE HERE TO MAKE SURE YOUR ON-DISK STRUCTURES
           ARE IN-SYNC BEFORE THE DISK IS UNMOUNTED (ONLY NECESSARY IF YOU
           KEEP DATA CACHED THAT'S NOT ON DISK */
  inode_t      *rootp      = retrieve_root();
  vcb_t        *vcbp       = retrieve_vcb();
  valid_t *validp = retrieve_valid();
  if (root_needs_update 
    && ( write_struct(1, rootp) < 0 ) ) {               /* flush root cache */
    err("       ->updating root");
    dunconnect();
  }
  if (validation_needs_update 
    && ( write_struct(vcbp->vb_valid, validp) < 0 ) ){  /* flush valid cache */
    err("       ->updating valid table");
    dunconnect();
  }
  if (!retrieve_inode(0, R_FLUSH)){                     /* flush inode cache */
    err("       ->flusing cached inode"); 
    dunconnect();
  }
  if (!retrieve_dirent(0, R_FLUSH)){                    /* flush dirent cache */
    err("       ->flushing cached dirent");
    dunconnect();
  }
  vcbp->vb_clean = true;              /* all writes are successful, reset clean bit */
  if ( write_struct(0, vcbp) < 0 ) {                    /* flush vcb cache */
    err("       ->updating vcb"); 
    dunconnect();
  }

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
  debug("       looking for path: %s", path);
  if (strcmp(path, "/") == 0 &&
      strcmp(path, "///") == 0){
    stbuf->st_mode  = 0777 | S_IFDIR;               /* is root */
    ino = 1;
  }
  else {
    if ((ino = find_ino(path)) < 0){
      debug("       invalid path");
      return -errno;
    }
  }
  if ( !(inodep = retrieve_inode(ino, R_RD)) ){
    err("       retrieve inode failed");
    return -errno;
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

  debug("       ino for %s is %d", path, ino);
  debug("       information about it: \n"
        "              uid: %d\n"
        "              gid: %d\n"
        "              atime: %s\n"
        "              size: %d\n"
        "              blocks: %d",
        (int)stbuf->st_uid, 
        (int)stbuf->st_gid,
        ctime(&(stbuf->st_atime)),
        (int)stbuf->st_size,
        (int)stbuf->st_blocks);                     

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
        /* dependences: 
                child_dirent             <-   child_ino, parent_ino
                
                child_inode.i_ino        <-   child_ino
                child_inode.i_insert     <-   child_dirent_bnum
                child_inode.i_direct[0]  <-   child_dirent_bnum

                valid.v_entries[]        <-   child_ino

                parent.i_blocks          <-   parent_insert
                parent.i_insert          <-   parent_insert, parent_dirent_bnum, parent_direntp
                parent.i_direct[]        <-   parent_insert, parent_dirent_bnum

                parent_dirent            <-   parent_insert, child_ino, filename
     
                vcb.vb_free              <-   freep

           required elements: 
                + child inode 
                    - i_ino                         *child_ino
                    - i_type                        S_IFDIR
                    - i_size                        2
                    - i_uid                         user
                    - i_gid                         group
                    - i_mode                        mode
                    - i_blocks                      1
                    - i_insert                      *I_INSERT(child_dirent_bnum, 2)
                    - i_atime, i_mtime, i_ctime     now
                    - i_direct[0]                   *child_dirent_bnum
                + child dirent
                    - .                             *child_ino
                    - ..                            *parent_ino
                + valid
                    - v_entries[]                   *v_entries[child_ino] = V_VALID
                + parent inode (update)
                    - i_size                                +1
                    - i_blocks (untouched OR +1,         update in the end:
                                depends on insert val)      *= parent_insert ? i_blocks : i_blocks+1;
                    - i_insert                              
                                             < 0            *I_INSERT(parent_dirent_bnum(get new from freep), 1), init all other 7 entries inserts bellow in parent dirent creation
                                             > 0         2. *pass in store next insert value as new insert: parent_direntp->entries[I_OFFSET(parent_insert)].et_insert
                    - i_atime, i_mtime                      now
                    - i_direct[]                            *parent_insert ? nothing : i_direct[i_blocks(updated)-1] = parent_dirent_bnum
                        (update or assign new block, 
                                    depends on insert val)
                + parent dirent (update OR new) (depens on parent_insert val)
                                    parent_insert < 0       create new parent_dirent, init all other 7 entries' inserts
                                    parent_insert > 0       *update parent_dirent: 
                                                         1. parent_direntp = retrieve_dirent(I_BLOCK(parent_insert), R_WR), 
                                                         3. & add child entry -- (need filename, child_ino)
                + vcb
                    - vb_free update along the procedure 

                + last step:
                    write child_inode to disk
                    write child_dirent to disk
                    write parent_dirent to disk, if parent_insert < 0   */
{
    fprintf(stderr,"vfs_mkdir() called\n");
    char         m_path [strlen(path)+1]; /* mutable path for dirname(), basename() */

    vcb_t       *vcbp;                      /* check */
    free_t      *freep;                     /* check */
    valid_t     *validp;                    /* check */
    
    char         dirpath[strlen(path)+1];   /* check */
    char         filename[strlen(path)+1];  /* check */
    entry_t      entry;                     /* check */

    int          parent_ino;                /* check */
    inode_t     *parent_inodep;             /* check */
    insert_t     parent_insert;             /* check */
    int          parent_dirent_bnum;        /* check */
    dirent_t     parent_dirent;             /* check */
    dirent_t    *parent_direntp;            /* check */

    int          child_ino;                 /* check */
    inode_t      child_inode;               /* check */
    int          child_dirent_bnum;         /* check */
    dirent_t     child_dirent;              /* check */

    struct timespec *now;

    vcbp = retrieve_vcb();                              /* vcbp */
    freep = get_free();                                 /* freep */
    validp = retrieve_valid(vcbp->vb_valid);            /* validp */

    strcpy(m_path, path); 
    strcpy(dirpath, dirname(m_path));                   /* dirname */
    strcpy(m_path, path);               /* reset m_path for next call */
    strcpy(filename, basename(m_path));                 /* filename */

    parent_ino = find_ino(dirpath);                     /* parent_ino */
    parent_inodep = retrieve_inode(parent_ino, R_WR);   /* parent_inodep */
    parent_insert = parent_inodep->i_insert;            /* parent_insert */
    if (!parent_insert){
        parent_dirent_bnum = vcbp->vb_free;
        if (parent_dirent_bnum < 0)
            return -errno;                              /* parent_dirent_bnum */
        freep = get_free();
        vcbp->vb_free = freep->f_next;  /* update vcb free */

        if (!clear_dirent(&parent_dirent, I_INSERT(parent_dirent_bnum, 0)))
            return -errno;
        entry.et_ino = child_ino;      
        strcpy(entry.et_name, filename);
        parent_dirent.d_entries[0] = entry;             /* parent_dirent */
        parent_direntp = &parent_dirent;                /* parent_direntp */
    } else {
        parent_direntp = retrieve_dirent(I_BLOCK(parent_insert), R_WR);
                                                        /* parent_direntp */
    }

    int i;
    for (i=1; i<VALID_TABLE_SIZE; i++){
        if (validp->v_entries[i] == V_INVALID){
            child_ino = i;                              
            break;
        }
    }
    if (i >= VALID_TABLE_SIZE)                          /* child_ino */
        return -errno;

    child_dirent_bnum = vcbp->vb_free;
    if (child_dirent_bnum < 0)
        return -errno;                                  /* child_dirent_bnum */
    freep = get_free();
    vcbp->vb_free = freep->f_next;  /* update vcb free */

    if (!clear_inode(&child_inode)) return -errno;
    if (!(now = get_time()))        return -errno;
    child_inode.i_ino  = child_ino;
    child_inode.i_type = S_IFREG;
    child_inode.i_size = 2;
    child_inode.i_uid  = getuid();
    child_inode.i_gid  = getgid();
    child_inode.i_mode = mode;
    child_inode.i_blocks = 1;
    child_inode.i_insert = I_INSERT(child_dirent_bnum, 2);
    child_inode.i_atime = *now;
    child_inode.i_mtime = *now;
    child_inode.i_ctime = *now;
    child_inode.i_direct[0] = child_dirent_bnum;        /* child_inode */
                                                                /* + child_inode */

    if (!clear_dirent(&child_dirent, I_INSERT(child_dirent_bnum, 0)))
        return -errno;                                  
    entry.et_ino = child_ino;
    strcpy(entry.et_name, ".");
    child_dirent.d_entries[0] = entry;
    entry.et_ino = parent_ino;
    strcpy(entry.et_name, "..");
    child_dirent.d_entries[1] = entry;                  /* child_dirent */
                                                                /* + child_dirent */

    validp->v_entries[child_ino] = V_VALID;                     /* + valid */

    (parent_inodep->i_size)++;              /* i_size */
    if (!parent_insert)
        parent_inodep->i_blocks++;          /* i_blocks */
    parent_inodep->i_insert = (!parent_insert)
                            ? I_INSERT(parent_dirent_bnum, 1)
                            : parent_direntp->d_entries[I_OFFSET(parent_insert)].et_insert;
                                            /* i_insert */
    parent_inodep->i_atime = *now;          
    parent_inodep->i_mtime = *now;          /* i_atime, i_mtime */
    if (!parent_insert)
        parent_inodep->i_direct[parent_inodep->i_blocks - 1] = parent_dirent_bnum;
                                            /* i_direct[] */    /* + parent_inode */

    entry.et_ino = child_ino;      
    strcpy(entry.et_name, filename);
    if (parent_insert)
        parent_direntp->d_entries[I_OFFSET(parent_insert)] = entry;
                                                                /* + parent_dirent */

    if (write_struct(child_ino, &child_inode) < 0) return -errno;
    if (write_struct(child_dirent_bnum, &child_dirent) < 0) return -errno;
    if (write_struct(parent_dirent_bnum, parent_direntp) < 0) return -errno;
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
    debug("vfs_readdir()");
    int     ino;
    inode_t *dp;
    inode_t *inodep;
    entry_t *ep;

    struct stat st;

    if ( ( ino = find_ino(path) ) < 0 ) {
        debug("       inode not exist for path %s", path);
        return -errno;
    }
    if ( !( dp = retrieve_inode(ino, R_RD) ) ) {
        debug("       retrieve inode failed");
        return -errno;
    }
    ep = step_dir(dp);
    if ( !(inodep = retrieve_inode(ep->et_ino, R_RD)) ) return -errno;
    st.st_ino = inodep->i_ino;
    st.st_mode = inodep->i_type | inodep->i_mode;
    if (filler(buf, ep->et_name, &st, 0))
        return 0;

    while ((ep = step_dir(NULL))) {
        debug("       current entry inode: %d", ep->et_ino);
        memset(&st, 0, sizeof(st));
        if ( !(inodep = retrieve_inode(ep->et_ino, R_RD)) ) return -errno;

        st.st_ino = inodep->i_ino;
        st.st_mode = inodep->i_type | inodep->i_mode;
        if (filler(buf, ep->et_name, &st, 0))
            break;
    }
    return 0;
}

/*
 * Given an absolute path to a file (for example /a/b/myFile), vfs_create 
 * will create a new file named myFile in the /a/b directory.
 *
 */
static int vfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) 
        /* dependences: 
                valid.v_entries[]     <-   child_ino

                parent.i_blocks       <-   parent_insert
                parent.i_insert       <-   parent_dirent_bnum; parent_direntp, parent_insert
                parent.i_direct[]     <-   parent_dirent_bnum

                parent_dirent         <-   parent_insert, child_ino, filename

                vcb.vb_free           <-   freep

           required elements: 
                + child inode (create new)
                    - i_ino                         *child_ino
                    - i_type                        S_IFREG
                    - i_size                        0
                    - i_uid                         user
                    - i_gid                         group
                    - i_mode                        mode
                    - i_blocks                      0
                    - i_insert                      -1
                    - i_atime, i_mtime, i_ctime     now
                + valid        (update)
                    - v_entries[]                   *v_entries[child_ino] = V_VALID
                + parent inode (update)
                    - i_size                                +1
                    - i_blocks (untouched OR +1,         update in the end:
                                depends on insert val)      *= parent_insert ? i_blocks : i_blocks+1;
                    - i_insert                              
                                             < 0            *I_INSERT(parent_dirent_bnum(get new from freep), 1), init all other 7 entries inserts bellow in parent dirent creation
                                             > 0         2. *pass in store next insert value as new insert: parent_direntp->entries[I_OFFSET(parent_insert)].et_insert
                    - i_atime, i_mtime                      now
                    - i_direct[]                            *parent_insert ? nothing : i_direct[i_blocks(updated)-1] = parent_dirent_bnum
                        (update or assign new block, 
                                    depends on insert val)
                + parent dirent (update OR new) (depens on parent_insert val)
                                    parent_insert < 0       create new parent_dirent, init all other 7 entries' inserts
                                    parent_insert > 0       *update parent_dirent: 
                                                         1. parent_direntp = retrieve_dirent(I_BLOCK(parent_insert), R_WR), 
                                                         3. & add child entry -- (need filename, child_ino)
                + vcb
                    - vb_free update along the procedure 

                + last step: 
                    write child_inode to disk
                    write parent_dirent to disk, if parent_insert < 0    */
{

    fprintf(stderr,"vfs_create() called\n");
    char         m_path [strlen(path)+1]; /* mutable path for dirname(), basename() */

    vcb_t       *vcbp;                      /* check */
    free_t      *freep;                     /* check */
    valid_t     *validp;                    /* check */
    
    char         dirpath[strlen(path)+1];   /* check */
    char         filename[strlen(path)+1];  /* check */
    entry_t      entry;                     /* check */

    int          parent_ino;                /* check */
    inode_t     *parent_inodep;             /* check */
    insert_t     parent_insert;             /* check */
    int          parent_dirent_bnum;        /* check */
    dirent_t     parent_dirent;             /* check */
    dirent_t    *parent_direntp;            /* check */

    int          child_ino;                 /* check */
    inode_t      child_inode;               /* check */

    struct timespec *now;

    vcbp = retrieve_vcb();                              /* vcbp */
    freep = get_free();                                 /* freep */
    validp = retrieve_valid(vcbp->vb_valid);            /* validp */

    strcpy(m_path, path); 
    strcpy(dirpath, dirname(m_path));                   /* dirname */
    strcpy(m_path, path);               /* reset m_path for next call */
    strcpy(filename, basename(m_path));                 /* filename */
    entry.et_ino = child_ino;      
    strcpy(entry.et_name, filename);                    /* entry */

    parent_ino = find_ino(dirpath);                     /* parent_ino */
    parent_inodep = retrieve_inode(parent_ino, R_WR);   /* parent_inodep */
    parent_insert = parent_inodep->i_insert;            /* parent_insert */
    if (!parent_insert){
        parent_dirent_bnum = vcbp->vb_free;
        if (parent_dirent_bnum < 0)
            return -errno;                              /* parent_dirent_bnum */
        freep = get_free();
        vcbp->vb_free = freep->f_next;  /* update vcb free */

        if (!clear_dirent(&parent_dirent, I_INSERT(parent_dirent_bnum, 0)))
            return -errno;
        parent_dirent.d_entries[0] = entry;             /* parent_dirent */
        parent_direntp = &parent_dirent;                /* parent_direntp */
    } else {
        parent_direntp = retrieve_dirent(I_BLOCK(parent_insert), R_WR);
                                                        /* parent_direntp */
    }

    int i;
    for (i=1; i<VALID_TABLE_SIZE; i++){
        if (validp->v_entries[i] == V_INVALID){
            child_ino = i;                              
            break;
        }
    }
    if (i >= VALID_TABLE_SIZE)                          /* child_ino */
        return -errno;
    if (!clear_inode(&child_inode)) return -errno;
    if (!(now = get_time()))        return -errno;
    child_inode.i_ino  = child_ino;
    child_inode.i_type = S_IFREG;
    child_inode.i_size = 0;
    child_inode.i_uid  = getuid();
    child_inode.i_gid  = getgid();
    child_inode.i_mode = mode;
    child_inode.i_blocks = 0;
    child_inode.i_insert = -1;
    child_inode.i_atime = *now;
    child_inode.i_mtime = *now;
    child_inode.i_ctime = *now;                         /* child_inode */
                                                                /* + child_inode */
    validp->v_entries[child_ino] = V_VALID;                     /* + valid */

    (parent_inodep->i_size)++;              /* i_size */
    if (!parent_insert)
        parent_inodep->i_blocks++;          /* i_blocks */
    parent_inodep->i_insert = (!parent_insert)
                            ? I_INSERT(parent_dirent_bnum, 1)
                            : parent_direntp->d_entries[I_OFFSET(parent_insert)].et_insert;
                                            /* i_insert */
    parent_inodep->i_atime = *now;          
    parent_inodep->i_mtime = *now;          /* i_atime, i_mtime */
    if (!parent_insert)
        parent_inodep->i_direct[parent_inodep->i_blocks - 1] = parent_dirent_bnum;
                                            /* i_direct[] */    /* + parent_inode */

    if (parent_insert)
        parent_direntp->d_entries[I_OFFSET(parent_insert)] = entry;
                                                                /* + parent_dirent */

    if (write_struct(child_ino, &child_inode) < 0) return -errno;
    if (write_struct(parent_dirent_bnum, parent_direntp) < 0) return -errno;
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

    return 0;
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

  return 0;
}

/**
 * This function deletes the last component of the path (e.g., /a/b/c you 
 * need to remove the file 'c' from the directory /a/b).
 */
static int vfs_delete(const char *path)
{

  /* 3600: NOTE THAT THE BLOCKS CORRESPONDING TO THE FILE SHOULD BE MARKED
           AS FREE, AND YOU SHOULD MAKE THEM AVAILABLE TO BE USED WITH OTHER FILES */

    return 0;
}

static int vfs_rmdir(const char *path)
{
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

    return 0;
}

/*
 * This function will change the user and group of the file
 * to be uid and gid.  This should only update the file's owner
 * and group.
 */
static int vfs_chown(const char *file, uid_t uid, gid_t gid)
{

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
    if (file_ino < 0) return -errno;

    fp = retrieve_inode(file_ino, R_WR);
    if (!fp) return -errno;

    fp->i_atime = ts[0];
    fp->i_mtime = ts[1];

    return 0;
}

/*
 * This function will truncate the file at the given offset
 * (essentially, it should shorten the file to only be offset
 * bytes long).
 */
static int vfs_truncate(const char *file, off_t offset)
{

  /* 3600: NOTE THAT ANY BLOCKS FREED BY THIS OPERATION SHOULD
           BE AVAILABLE FOR OTHER FILES TO USE. */

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

