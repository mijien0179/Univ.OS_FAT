#include <stdio.h>
#include <stdlib.h>
#include "disk.h"
#include "hw1.h"
#include <string.h>

void FileSysInit(void)
{
	char* blck = (char*)malloc(BLOCK_SIZE); // block Initialize
	memset(blck, 0, BLOCK_SIZE);

	DevCreateDisk();

	DevWriteBlock(FILESYS_INFO_BLOCK, blck); // FileSysInfomration
	DevWriteBlock(INODE_BYTEMAP_BLOCK_NUM, blck); // Inode Bytemap
	DevWriteBlock(BLOCK_BYTEMAP_BLOCK_NUM, blck); // Block Bytemap

	for (int i = INODELIST_BLOCK_FIRST; i < INODELIST_BLOCK_FIRST + INODELIST_BLOCKS; ++i) // inode list initialize
		DevWriteBlock(i, blck);

	for(int i = INODELIST_BLOCK_FIRST + INODELIST_BLOCKS; 
		i < (FS_DISK_CAPACITY / BLOCK_SIZE); ++i) // Data Region
	      DevWriteBlock(i, blck);

	free(blck);

}

void SetInodeBytemap(int inodeno)
{
	char* blck = (char*)malloc(BLOCK_SIZE);

	DevReadBlock(INODE_BYTEMAP_BLOCK_NUM, blck);

	blck[inodeno] = 1;

	DevWriteBlock(INODE_BYTEMAP_BLOCK_NUM, blck);

	free(blck);
}


void ResetInodeBytemap(int inodeno)
{
	char* blck = (char*)malloc(BLOCK_SIZE);

	DevReadBlock(INODE_BYTEMAP_BLOCK_NUM, blck);

	blck[inodeno] = 0;

	DevWriteBlock(INODE_BYTEMAP_BLOCK_NUM, blck);

	free(blck);
}


void SetBlockBytemap(int blkno)
{
	char* blck = (char*)malloc(BLOCK_SIZE);

	DevReadBlock(BLOCK_BYTEMAP_BLOCK_NUM, blck);

	blck[blkno] = 1;

	DevWriteBlock(BLOCK_BYTEMAP_BLOCK_NUM, blck);

	free(blck);

}


void ResetBlockBytemap(int blkno)
{
	char* blck = (char*)malloc(BLOCK_SIZE);
	DevReadBlock(BLOCK_BYTEMAP_BLOCK_NUM, blck);

	blck[blkno] = 0;
	DevWriteBlock(BLOCK_BYTEMAP_BLOCK_NUM, blck);
	free(blck);

}


void PutInode(int inodeno, Inode* pInode)
{
	Inode* node = (Inode*)malloc(BLOCK_SIZE);
	int blckCount = inodeno / (BLOCK_SIZE / sizeof(Inode));
	int _inodeno = inodeno % (BLOCK_SIZE / sizeof(Inode));

	DevReadBlock(INODELIST_BLOCK_FIRST + blckCount, (char*)node);

	memcpy(node + _inodeno, pInode, sizeof(Inode));

	DevWriteBlock(INODELIST_BLOCK_FIRST + blckCount, (char*)node);

	free(node);

}


void GetInode(int inodeno, Inode* pInode)
{
	Inode* node = (Inode*)malloc(BLOCK_SIZE);
	int blckCount = inodeno / (BLOCK_SIZE / sizeof(Inode));
	int _inodeno = inodeno % (BLOCK_SIZE / sizeof(Inode));

	DevReadBlock(INODELIST_BLOCK_FIRST + blckCount, (char*)node);

	memcpy(pInode, node + _inodeno, sizeof(Inode));

	free(node);

}


int GetFreeInodeNum(void)
{
	char* blck = (char*)malloc(BLOCK_SIZE);

	DevReadBlock(INODE_BYTEMAP_BLOCK_NUM, blck);

	int i;
	for (i = 0; i < BLOCK_SIZE && blck[i]; ++i);
	free(blck);
	return i;

}


int GetFreeBlockNum(void)
{
	char* blck = (char*)malloc(BLOCK_SIZE);

	DevReadBlock(BLOCK_BYTEMAP_BLOCK_NUM, blck);

	int i;
	for (i = INODELIST_BLOCK_FIRST + INODELIST_BLOCKS; i < BLOCK_SIZE && blck[i]; ++i);
	free(blck);
	return i;

}

void PutIndirectBlockEntry(int blkno, int index, int number)
{
	int* blck = (int*)malloc(BLOCK_SIZE);
	memset(blck, 0, BLOCK_SIZE);

	DevReadBlock(blkno, (char*)blck);
	blck[index] = number;

	DevWriteBlock(blkno, (char*)blck);

	free(blck);

}

int GetIndirectBlockEntry(int blkno, int index)
{
	int* blck = (int*)malloc(BLOCK_SIZE);
	memset(blck, 0, BLOCK_SIZE);

	DevReadBlock(blkno, (char*)blck);

	int result = blck[index];
	free(blck);

	return result;

}


void RemoveIndirectBlockEntry(int blkno, int index)
{
	int* blck = (int*)malloc(BLOCK_SIZE);

	DevReadBlock(blkno, (char*)blck);
	blck[index] = INVALID_ENTRY;

	DevWriteBlock(blkno, (char*)blck);

	free(blck);

}

void PutDirEntry(int blkno, int index, DirEntry* pEntry)
{
	DirEntry* entry = (DirEntry*)malloc(BLOCK_SIZE);

	DevReadBlock(blkno, (char*)entry);

	memcpy(entry + index, pEntry, sizeof(DirEntry));

	DevWriteBlock(blkno, (char*)entry);

	free(entry);

}

int GetDirEntry(int blkno, int index, DirEntry* pEntry)
{
	DirEntry* entry = (DirEntry*)malloc(BLOCK_SIZE);

	DevReadBlock(blkno, (char*)entry);

	memcpy(pEntry, entry + index, sizeof(DirEntry));

	if (entry[index].inodeNum == INVALID_ENTRY) {
		free(entry);
		return -1;
	}

	free(entry);
	return 1;

}

void RemoveDirEntry(int blkno, int index)
{
	DirEntry* entry = (DirEntry*)malloc(BLOCK_SIZE);

	DevReadBlock(blkno, (char*)entry);

	entry[index].inodeNum = INVALID_ENTRY;

	DevWriteBlock(blkno, (char*)entry);

	free(entry);

}