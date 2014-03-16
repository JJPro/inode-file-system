/*
 * CS3600, Spring 2014
 * Project 2 Starter Code
 * (c) 2013 Alan Mislove
 *
 * This program is intended to format your disk file, and should be executed
 * BEFORE any attempt is made to mount your file system.  It will not, however
 * be called before every mount (you will call it manually when you format 
 * your disk file).
 */

#include <math.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "3600fs.h"
#include "disk.h"
#include "lib.h"

// creates and formats the disk of size <size> in term of blocks. 
void myformat(int size) {
  // Do not touch or move this function
  dcreate_connect();

//  /**** Zero-out the disk *****/
//  // first, create a zero-ed out array of memory  
//  char *tmp = (char *) malloc(BLOCKSIZE);
//  memset(tmp, 0, BLOCKSIZE);
//
//  // now, write that to every block
//  for (int i=0; i<size; i++) 
//    if (dwrite(i, tmp) < 0) 
//      perror("Error while writing to disk");
//
//  // voila! we now have a disk containing all zeros
  
  /**** Format the disk to inode FS layout *****/
  if (format_disk(size) < 0)
    exit(EXIT_FAILURE);
  
  // Do not touch or move this function
  dunconnect();
}

// This creates and formats the disk
// Then closes the disk file after done with format
int main(int argc, char** argv) {
  // Do not touch this function
  if (argc != 2) {
    printf("Invalid number of arguments \n");
    printf("usage: %s diskSizeInBlockSize\n", argv[0]);
    return 1;
  }

  unsigned long size = atoi(argv[1]);
  printf("Formatting the disk with size %lu \n", size);
  myformat(size);
}
