/*
 *  Copyright (C) 2024 CS416/CS518 Rutgers CS
 *  Tiny File System
 *  File: block.h
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#define BLOCK_SIZE 4096

void dev_init(const char* diskfile_path);
int dev_open(const char* diskfile_path);
void dev_close();
int bio_read(const int block_num, void *buf);
int bio_write(const int block_num, const void *buf);

#ifdef __cplusplus
} 
#endif

#endif
