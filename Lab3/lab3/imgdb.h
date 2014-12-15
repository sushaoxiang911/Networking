/* 
 * Copyright (c) 2014 University of Michigan, Ann Arbor.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of Michigan, Ann Arbor. The name of the University 
 * may not be used to endorse or promote products derived from this 
 * software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Author: Sugih Jamin (jamin@eecs.umich.edu)
 *
*/
#ifndef __IMGDB_H__
#define __IMGDB_H__

#include <string>

#include "ltga.h"
#include "hash.h"

#define IMGDB_MAXFNAME   256  // including terminating NULL
#define IMGDB_FILELIST  "FILELIST.txt"
#define IMGDB_DIRSEP "/"
#define IMGDB_IDRBEG 0
#define IMGDB_IDREND 1
#define IMGDB_MAXDBSIZE 1024 // DB can only hold 1024 images max

typedef struct {
  unsigned char img_ID;
  char img_name[IMGDB_MAXFNAME];
} image_t;
   
class imgdb {
  unsigned char imgdb_IDrange[2];     // (start, end]
  unsigned long imgdb_bloomfilter;    // 64-bit bloom filter
  int imgdb_size;
  string imgdb_folder;  // image folder name
  image_t imgdb_db[IMGDB_MAXDBSIZE];
  LTGA imgdb_curimg;

public:
  imgdb(); // default constructor
  int cli(int argc, char *argv[]);
  void loaddb();
  int searchdb(char *imgname);
  double marshall_imsg(imsg_t *imsg);
  char *getimage() { return((char * ) imgdb_curimg.GetPixels()); }
#if 0
  void display();
#endif
};  

#endif /* __IMGDB_H__ */
