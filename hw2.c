#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "disk.h"
#include "hw1.h"
#include "hw2.h"
#include <limits.h>


FileDescTable* pFileDescTable;
extern FileSysInfo* pFileSysInfo;
FileTable* pFileTable;

static int pastInode;
static int count;


#define DATAREGION_BLOCK_FIRST	(INODELIST_BLOCK_FIRST + INODELIST_BLOCKS)
#define DATAREGION_BLOCKS		(FS_DISK_CAPACITY / BLOCK_SIZE - DATAREGION_BLOCK_FIRST)
#define NUM_OF_INDIRECT_BLOCK	(BLOCK_SIZE >> 2)

#define CURRENT_DIR "."
#define PARENT_DIR ".."

void BlockClear(int blkno) {
	char* clear = (char*)calloc(BLOCK_SIZE, sizeof(char));
	DevWriteBlock(blkno, clear);
	free(clear);
}

int FindParent(int* _parentInode, Inode* _iNode, char** _name) {
	int parentInode = pFileSysInfo->rootInodeNum;
	Inode iNode = *_iNode;
	int find = 1;
	char* name = (*_name) + 1;
	char* pNext = name;
	char* dirName;
	DirEntry dir;
	int i, k;

	GetInode(parentInode, &iNode);

	while (*pNext != '\0' && find) {
		if (*pNext == '/') {
			find = 0;
			dirName = (char*)calloc(pNext - name + 1, sizeof(char));
			strncpy(dirName, name, pNext - name);

			for (i = 0; i < iNode.allocBlocks && !find; ++i) {
				if (i < NUM_OF_DIRECT_BLOCK_PTR) {
					if (i == iNode.allocBlocks) break; // OUT OF RANGE
					for (k = 0; k < NUM_OF_DIRENT_PER_BLK && !find; ++k) {
						GetDirEntry(iNode.dirBlockPtr[i], k, &dir);
						if (dir.inodeNum < 1) continue;
						Inode temp;
						GetInode(dir.inodeNum, &temp);

						if (temp.type == FILE_TYPE_DIR && strcmp(dir.name, dirName) == 0) {
							parentInode = dir.inodeNum;
							GetInode(parentInode, &iNode);
							find = 1;
						}
					}
				}
				else {
					if (iNode.indirectBlockPtr > 0) {
						int blck = GetIndirectBlockEntry(iNode.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR);
						if (blck < 1) break; // OUT OF RANGE
						for (k = 0; k < NUM_OF_DIRECT_BLOCK_PTR; ++k) {
							GetDirEntry(blck, k, &dir);
							if (dir.inodeNum < 1)continue;
							Inode temp;
							GetInode(dir.inodeNum, &temp);

							if (temp.type == FILE_TYPE_DIR && strcmp(dir.name, dirName) == 0) {
								parentInode = dir.inodeNum;
								GetInode(parentInode, &iNode);
								find = 2;
							}
						}
					}
					else break;

				}
			}
			free(dirName);
			name = ++pNext;
		}
		pNext++;
	}

	*_name = name;
	*_iNode = iNode;
	*_parentInode = parentInode;
	return find;
}

int OpenFile(const char* name, OpenFlag flag)
{
	if (pFileTable->numUsedFile == MAX_FILE_NUM) return -1;
	int result = -1;
	int parentInode, find;
	Inode iNode; DirEntry dir;
	char* path, * backup;
	path = (char*)malloc(strlen(name) + 1);
	strcpy(path, name);
	backup = path;
	find = FindParent(&parentInode, &iNode, &path);

	if (find) {
		find = 0;
		int lastBlck, lastEntry;
		int targetBlck, targetEntry;
		targetBlck = targetEntry = lastBlck = lastEntry = -1;
		int blck;
		// RETRIEVE TARGET FILE
		{
			for (int i = 0; i <= iNode.allocBlocks && !find; ++i) {
				if (i < NUM_OF_DIRECT_BLOCK_PTR) {
					if (iNode.dirBlockPtr[i] < 1 && lastBlck == -1) {
						blck = GetFreeBlockNum();
						iNode.dirBlockPtr[i] = blck;

						BlockClear(blck);
						SetBlockBytemap(blck);
						pFileSysInfo->numAllocBlocks++;
						pFileSysInfo->numFreeBlocks--;

						iNode.allocBlocks++;
						iNode.size += BLOCK_SIZE;
						PutInode(parentInode, &iNode);

					}
					blck = iNode.dirBlockPtr[i];
				}
				else {
					if (i == NUM_OF_DIRECT_BLOCK_PTR + NUM_OF_INDIRECT_BLOCK) break; // OUT OF RANGE
					if (iNode.indirectBlockPtr < 1) {
						blck = GetFreeBlockNum();
						iNode.indirectBlockPtr = blck;
						BlockClear(blck);
						SetBlockBytemap(blck);
						pFileSysInfo->numAllocBlocks++;
						pFileSysInfo->numFreeBlocks--;

						blck = GetFreeBlockNum();
						PutIndirectBlockEntry(iNode.indirectBlockPtr, 0, blck);
						BlockClear(blck);

						SetBlockBytemap(blck);
						pFileSysInfo->numAllocBlocks++;
						pFileSysInfo->numFreeBlocks--;

						iNode.allocBlocks++;
						iNode.size += BLOCK_SIZE;
						PutInode(parentInode, &iNode);

					}

					blck = GetIndirectBlockEntry(iNode.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR);
					if (blck < 1 && lastBlck == -1) {
						blck = GetFreeBlockNum();
						PutIndirectBlockEntry(iNode.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR, blck);
						BlockClear(blck);

						iNode.allocBlocks++;
						iNode.size += BLOCK_SIZE;
						PutInode(parentInode, &iNode);

						SetBlockBytemap(blck);
						pFileSysInfo->numAllocBlocks++;
						pFileSysInfo->numFreeBlocks--;
					}
				}

				if (blck < 1) break;
				for (int k = 0; k < NUM_OF_DIRENT_PER_BLK && !find; ++k) {
					GetDirEntry(blck, k, &dir);
					if (dir.inodeNum < 1) {
						if (lastBlck == -1) {
							lastBlck = blck;
							lastEntry = k;
						}
					}

					Inode temp;
					GetInode(dir.inodeNum, &temp);
					if (temp.type == FILE_TYPE_FILE && strcmp(dir.name, path) == 0) {
						targetBlck = blck;
						targetEntry = k;
						find = 1;
					}
				}
			}
		}

		switch (flag) {
		case OPEN_FLAG_TRUNCATE:
		{
			if (!find) break;
			GetDirEntry(targetBlck, targetEntry, &dir);
			Inode temp = {};
			GetInode(dir.inodeNum, &temp);
			// DELETE ALL DATA
			{
				for (int i = 0; i < temp.allocBlocks; ++i) {
					if (i < NUM_OF_DIRECT_BLOCK_PTR) {
						if (temp.dirBlockPtr[i] < 1) break;
						ResetBlockBytemap(temp.dirBlockPtr[i]);
						BlockClear(temp.dirBlockPtr[i]);
						temp.dirBlockPtr[i] = 0;
						pFileSysInfo->numAllocBlocks--;
						pFileSysInfo->numFreeBlocks++;
					}
					else {
						if (temp.indirectBlockPtr < 1) break;
						blck = GetIndirectBlockEntry(temp.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR);
						if (blck < 1) break;
						BlockClear(blck);
						ResetBlockBytemap(blck);
						pFileSysInfo->numAllocBlocks--;
						pFileSysInfo->numFreeBlocks++;
					}
				}
				if (temp.indirectBlockPtr > 0) {
					BlockClear(temp.indirectBlockPtr);
					ResetBlockBytemap(temp.indirectBlockPtr);
					pFileSysInfo->numAllocBlocks--;
					pFileSysInfo->numFreeBlocks++;
					temp.indirectBlockPtr = 0;
				}

				temp.size = 0;
				temp.allocBlocks = 0;
				PutInode(dir.inodeNum, &temp);
			}
			break;

		}
		break;
		case OPEN_FLAG_CREATE:
		{
			if (!find) {
				targetBlck = lastBlck;
				targetEntry = lastEntry;
				Inode temp = {};
				temp.type = FILE_TYPE_FILE;
				int freeInode = GetFreeInodeNum();
				PutInode(freeInode, &temp);

				SetInodeBytemap(freeInode);
				pFileSysInfo->numAllocInodes++;
				find = 1;

				strcpy(dir.name, path);
				dir.inodeNum = freeInode;
				PutDirEntry(targetBlck, targetEntry, &dir);
			}
		}
		break;
		}

		if (find) {
			GetDirEntry(targetBlck, targetEntry, &dir);

			for (int i = 0; i < MAX_FILE_NUM; ++i) {
				if (!pFileTable->pFile[i].bUsed) {
					pFileTable->numUsedFile++;
					pFileTable->pFile[i].bUsed = 1;
					pFileTable->pFile[i].inodeNum = dir.inodeNum;
					pFileTable->pFile[i].fileOffset = 0;
					result = i;
					break;
				}
			}

			for (int i = 0; i < DESC_ENTRY_NUM; ++i) {
				if (!pFileDescTable->pEntry[i].bUsed) {
					pFileDescTable->pEntry[i].bUsed = 1;
					pFileDescTable->pEntry[i].fileTableIndex = result;
					pFileDescTable->numUsedDescEntry++;
					result = i;
					break;
				}
			}

		}

	}

	free(backup);

	DevWriteBlock(FILESYS_INFO_BLOCK, (char*)pFileSysInfo);

	return result;
}

int WriteFile(int fileDesc, char* pBuffer, int length)
{
	if (!pFileDescTable->pEntry[fileDesc].bUsed) return -1;
	int fd = pFileDescTable->pEntry[fileDesc].fileTableIndex;
	if (!pFileTable->pFile[fd].bUsed) return -1;
	int inodNum = pFileTable->pFile[fd].inodeNum;

	int result = 0;

	Inode iNode;
	GetInode(inodNum, &iNode);

	int offset, blck, entry;
	char* storage = (char*)calloc(BLOCK_SIZE, sizeof(char));
	for (int i = 0; i < length; ++i) {
		offset = pFileTable->pFile[fd].fileOffset + i;
		blck = offset >> 9;				   // offset / 512
		entry = offset & (BLOCK_SIZE - 1); // offset % 512

		if (blck < NUM_OF_DIRECT_BLOCK_PTR) {
			if (iNode.dirBlockPtr[blck] < 1) {
				int newBlck = GetFreeBlockNum();
				BlockClear(newBlck);
				iNode.dirBlockPtr[blck] = newBlck;
				iNode.size += BLOCK_SIZE;
				iNode.allocBlocks++;

				PutInode(inodNum, &iNode);

				SetBlockBytemap(newBlck);
				pFileSysInfo->numAllocBlocks++;
				pFileSysInfo->numFreeBlocks--;
			}
			blck = iNode.dirBlockPtr[blck];
		}
		else {
			if (iNode.indirectBlockPtr < 1) {
				int newBlck = GetFreeBlockNum();
				BlockClear(newBlck);
				iNode.indirectBlockPtr = newBlck;

				SetBlockBytemap(newBlck);
				pFileSysInfo->numAllocBlocks++;
				pFileSysInfo->numFreeBlocks--;

				newBlck = GetFreeBlockNum();
				BlockClear(newBlck);
				PutIndirectBlockEntry(iNode.indirectBlockPtr, 0, newBlck);

				iNode.allocBlocks++;
				iNode.size += BLOCK_SIZE;
				PutInode(inodNum, &iNode);

				SetBlockBytemap(newBlck);
				pFileSysInfo->numAllocBlocks++;
				pFileSysInfo->numFreeBlocks--;
			}

			blck -= NUM_OF_DIRECT_BLOCK_PTR; // directblck;
			int checker = GetIndirectBlockEntry(iNode.indirectBlockPtr, blck);
			if (checker < 1) {
				checker = GetFreeBlockNum();
				PutIndirectBlockEntry(iNode.indirectBlockPtr, blck, checker);
				BlockClear(checker);

				iNode.allocBlocks++;
				iNode.size += BLOCK_SIZE;
				PutInode(inodNum, &iNode);

				SetBlockBytemap(checker);
				pFileSysInfo->numAllocBlocks++;
				pFileSysInfo->numFreeBlocks--;
			}
			blck = checker;
		}

		int swap = BLOCK_SIZE - entry;
		int restI = length - i;
		int value = swap < restI ? swap : restI;
		DevReadBlock(blck, storage);
		memcpy(storage + entry, pBuffer + i, value);
		i += value - 1;
		
		DevWriteBlock(blck, storage);
		result+= value;
	}
	pFileTable->pFile[fd].fileOffset += result;
	free(storage);
	DevWriteBlock(FILESYS_INFO_BLOCK, (char*)pFileSysInfo);
	return result;
}

int ReadFile(int fileDesc, char* pBuffer, int length)
{
	if (!pFileDescTable->pEntry[fileDesc].bUsed) return -1;
	int fd = pFileDescTable->pEntry[fileDesc].fileTableIndex;
	if (!pFileTable->pFile[fd].bUsed) return -1;
	int inodNum = pFileTable->pFile[fd].inodeNum;
	Inode iNode;
	GetInode(inodNum, &iNode);
	int result = 0;
	int offset, blck, entry;
	char* storage = (char*)calloc(BLOCK_SIZE, sizeof(char));
	for (int i = 0; i < length; ++i) {
		offset = pFileTable->pFile[fd].fileOffset + i;
		blck = offset >> 9;				   // offset / 512
		entry = offset & (BLOCK_SIZE - 1); // offset % 512

		if (blck < NUM_OF_DIRECT_BLOCK_PTR) {
			blck = iNode.dirBlockPtr[blck];
		}
		else {
			if (iNode.indirectBlockPtr < 1) break;
			blck -= NUM_OF_DIRECT_BLOCK_PTR;
			blck = GetIndirectBlockEntry(iNode.indirectBlockPtr, blck);
		}

		if (blck < 1) break;

		int swap = BLOCK_SIZE - entry;
		int restI = length - i;
		int value = swap < restI ? swap : restI;
		DevReadBlock(blck, storage);
		memcpy(pBuffer + i, storage + entry, value);
		
		i += value -1;
		result+= value;
	}
	free(storage);
	pFileTable->pFile[fd].fileOffset += result;
	return result;
}

int CloseFile(int fileDesc)
{
	int result = 0;
	if (pFileDescTable->pEntry[fileDesc].bUsed) {
		int fd = pFileDescTable->pEntry[fileDesc].fileTableIndex;
		if (pFileTable->pFile[fd].bUsed) {
			pFileTable->pFile[fd].bUsed = 0;
			pFileTable->numUsedFile--;
		}
		pFileDescTable->pEntry[fileDesc].bUsed = 0;
		pFileDescTable->numUsedDescEntry--;
	}
	return result;
}

int RemoveFile(char* name)
{
	char* path, * backup;
	path = (char*)malloc(strlen(name) + 1);
	strcpy(path, name);
	backup = path;

	int result = -1;

	int find, parentInode; Inode iNode; DirEntry dir;
	find = FindParent(&parentInode, &iNode, &path);

	if (find) {
		find = 0;
		int targetBlck, targetEntry;
		int lastBlck, lastEntry;
		targetBlck = targetEntry = lastBlck = lastEntry = -1;
		int indirect, targetIndirect;
		indirect = targetIndirect = 0;

		// SEARCH ERASE TARGET
		{
			for (int i = 0; i < iNode.allocBlocks; ++i) {
				int blck;
				if (i < NUM_OF_DIRECT_BLOCK_PTR) {
					blck = iNode.dirBlockPtr[i];
					if (blck < 1) break;
				}
				else {
					if (iNode.indirectBlockPtr < 1) break;
					blck = GetIndirectBlockEntry(iNode.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR);
					if (blck < 1) break;
					if (targetBlck == -1) targetIndirect = 1;
					indirect = 1;
				}


				for (int k = 0; k < NUM_OF_DIRENT_PER_BLK; ++k) {
					GetDirEntry(blck, k, &dir);
					if (dir.inodeNum < 1) {
						if (strcmp(dir.name, CURRENT_DIR) == 0) continue;
						if (strcmp(dir.name, PARENT_DIR) == 0) continue;

						break;
					}

					Inode temp;
					GetInode(dir.inodeNum, &temp);
					lastBlck = i; lastEntry = k;
					if (temp.type == FILE_TYPE_FILE && strcmp(dir.name, path) == 0) {
						targetBlck = i;
						targetEntry = k;
						find = 1;
					}
				}
			}

		}

		if (find) {
			int relLBlck;
			if (indirect) relLBlck = GetIndirectBlockEntry(iNode.indirectBlockPtr, lastBlck - NUM_OF_DIRECT_BLOCK_PTR);
			else relLBlck = iNode.dirBlockPtr[lastBlck];

			int relTBlck;
			if (targetIndirect) relTBlck= GetIndirectBlockEntry(iNode.indirectBlockPtr, targetBlck - NUM_OF_DIRECT_BLOCK_PTR);
			else relTBlck = iNode.dirBlockPtr[targetBlck]; 


			Inode dNode; DirEntry dDir;
			GetDirEntry(relTBlck, targetEntry, &dDir);
			GetInode(dDir.inodeNum, &dNode);

			// REMOVE DATA
			{
				for (int i = 0; i < dNode.allocBlocks; ++i) {
					if (i < NUM_OF_DIRECT_BLOCK_PTR) {
						if (dNode.dirBlockPtr[i] < 1) break;
						ResetBlockBytemap(dNode.dirBlockPtr[i]);
						pFileSysInfo->numAllocBlocks--;
						pFileSysInfo->numFreeBlocks++;
					
						dNode.dirBlockPtr[i] = 0;
						dNode.size -= BLOCK_SIZE;
					}
					else {
						if (dNode.indirectBlockPtr < 1) break;
						int blck = GetIndirectBlockEntry(dNode.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR);
						if (blck < 1) break;
						RemoveIndirectBlockEntry(dNode.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR);
						ResetBlockBytemap(blck);
						pFileSysInfo->numFreeBlocks++;
						pFileSysInfo->numAllocBlocks--;
						dNode.size -= BLOCK_SIZE;
					}

					PutInode(dDir.inodeNum, &dNode);
				}
				if (dNode.indirectBlockPtr > 0) {
					ResetBlockBytemap(dNode.indirectBlockPtr);
					pFileSysInfo->numFreeBlocks++;
					pFileSysInfo->numAllocBlocks--;
					dNode.indirectBlockPtr = 0;
				}

				dNode.allocBlocks = 0;
				PutInode(dDir.inodeNum, &dNode);

			}


			// ERASE FILE FROM DIRECTORY
			{
				ResetInodeBytemap(dDir.inodeNum);
				pFileSysInfo->numAllocInodes--;

				if (targetBlck != lastBlck || targetEntry != lastEntry) {
					GetDirEntry(relLBlck, lastEntry, &dir);
					PutDirEntry(relTBlck, targetEntry, &dir);
				}
				RemoveDirEntry(relLBlck, lastEntry);
			}

			// SPACE MANAGEMENT
			{

				if (lastEntry == 0) {
					ResetBlockBytemap(relLBlck);
					iNode.allocBlocks--;
					iNode.size -= BLOCK_SIZE;

					pFileSysInfo->numAllocBlocks--;
					pFileSysInfo->numFreeBlocks++;

					// DIRECT BLOCK
					if (indirect == 0) {
						iNode.dirBlockPtr[lastBlck] = 0;
					}
					// IN-DIRECT BLOCK
					else {
						RemoveIndirectBlockEntry(iNode.indirectBlockPtr, lastBlck - NUM_OF_DIRECT_BLOCK_PTR);
						if (lastBlck - NUM_OF_DIRECT_BLOCK_PTR == 0) {
							ResetBlockBytemap(iNode.indirectBlockPtr);
							iNode.indirectBlockPtr = 0;
						}
					}
					PutInode(parentInode, &iNode);
				}

			}

			result = 0;

		}

	}

	free(backup);

	DevWriteBlock(FILESYS_INFO_BLOCK, (char*)pFileSysInfo);
	return result;

}

int MakeDirectory(char* name)
{
	int result = -1;
	int parentInode; Inode iNode;
	DirEntry dir;
	int find = 1;
	int targetBlck, targetIndex, isIndirect;
	int i, k;

	char* path = (char*)malloc(strlen(name) + 1);
	strcpy(path, name);
	char* backup = path;

	find = FindParent(&parentInode, &iNode, &path);
	int blck;
	// FIND PARERNT
	if (find) {
		find = 0;
		int freeBlck = GetFreeBlockNum();

		int freeInode = GetFreeInodeNum();

		for (i = 0; i <= iNode.allocBlocks && !find; ++i) {
			if (i < NUM_OF_DIRECT_BLOCK_PTR) {
				if (iNode.dirBlockPtr[i] < 1) {
					iNode.dirBlockPtr[i] = freeBlck;

					SetBlockBytemap(freeBlck);
					pFileSysInfo->numAllocBlocks++;
					pFileSysInfo->numFreeBlocks--;


					iNode.size += BLOCK_SIZE;
					iNode.allocBlocks++;


					PutInode(parentInode, &iNode);

					BlockClear(freeBlck);
					freeBlck = GetFreeBlockNum();
				}
				blck = iNode.dirBlockPtr[i];

			}
			else {
				if (i == NUM_OF_DIRECT_BLOCK_PTR + NUM_OF_INDIRECT_BLOCK) break; // OUT OUF RANGE

				if (iNode.indirectBlockPtr < 1) {
					iNode.indirectBlockPtr = freeBlck;
					BlockClear(iNode.indirectBlockPtr);
					SetBlockBytemap(freeBlck);
					pFileSysInfo->numAllocBlocks++;
					pFileSysInfo->numFreeBlocks--;

					freeBlck = GetFreeBlockNum();
					PutIndirectBlockEntry(iNode.indirectBlockPtr, 0, freeBlck);
					BlockClear(freeBlck);

					SetBlockBytemap(freeBlck);
					pFileSysInfo->numAllocBlocks++;
					pFileSysInfo->numFreeBlocks--;

					freeBlck = GetFreeBlockNum();

					iNode.size += BLOCK_SIZE;
					iNode.allocBlocks++;

					PutInode(parentInode, &iNode);
				}

				blck = GetIndirectBlockEntry(iNode.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR);
				if (blck < 1) {
					PutIndirectBlockEntry(iNode.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR, freeBlck);
					SetBlockBytemap(freeBlck);
					pFileSysInfo->numAllocBlocks++;
					pFileSysInfo->numFreeBlocks--;
					BlockClear(freeBlck);

					iNode.size += BLOCK_SIZE;
					iNode.allocBlocks++;
					PutInode(parentInode, &iNode);
					freeBlck = GetFreeBlockNum();
				}
				blck = GetIndirectBlockEntry(iNode.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR);
			}

			for (k = 0; k < NUM_OF_DIRENT_PER_BLK && !find; ++k) {
				GetDirEntry(blck, k, &dir);
				if (dir.inodeNum < 1 && strcmp(dir.name, CURRENT_DIR) && strcmp(dir.name, PARENT_DIR)) {
					targetBlck = blck;
					targetIndex = k;
					find = 1;
					break;
				}
			}

		}

		if (find) {
			Inode maked = {};
			maked.type = FILE_TYPE_DIR;
			maked.size = BLOCK_SIZE;
			maked.allocBlocks = 1;
			maked.dirBlockPtr[0] = freeBlck;
			PutInode(freeInode, &maked);

			SetBlockBytemap(freeBlck);
			pFileSysInfo->numAllocBlocks++;
			pFileSysInfo->numFreeBlocks--;

			SetInodeBytemap(freeInode);
			pFileSysInfo->numAllocInodes++;

			dir.inodeNum = freeInode;
			strcpy(dir.name, path);
			PutDirEntry(targetBlck, targetIndex, &dir);

			memset(dir.name, 0, MAX_NAME_LEN);

			dir.inodeNum = freeInode;
			strcpy(dir.name, CURRENT_DIR);
			PutDirEntry(freeBlck, 0, &dir);

			dir.inodeNum = parentInode;
			strcpy(dir.name, PARENT_DIR);
			PutDirEntry(freeBlck, 1, &dir);

			result = 0;
		}
	}



	DevWriteBlock(FILESYS_INFO_BLOCK, (char*)pFileSysInfo);

	free(backup);

	return result;
}

int RemoveDirectory(char* name)
{
	char* dirName, * pNext;
	Inode iNode; DirEntry dir;
	int i, k, parentInode, find;
	parentInode = pFileSysInfo->rootInodeNum;
	GetInode(parentInode, &iNode);
	find = 1;

	char* path, * backup;
	path = (char*)malloc(strlen(name) + 1);
	strcpy(path, name);
	backup = path;

	find = FindParent(&parentInode, &iNode, &path);


	if (find) {
		find = 0;
		int targetIndirect, targetBlck, targetEntry;
		int lastIndirect, lastBlck, lastEntry;
		targetIndirect = lastIndirect = 0;
		targetBlck = targetEntry = lastBlck = lastEntry = -1;

		// SEARCH ERASE TARGET
		{
			for (i = 0; i < iNode.allocBlocks; ++i) {
				int blck;
				if (i < NUM_OF_DIRECT_BLOCK_PTR) {
					blck = iNode.dirBlockPtr[i];
					if (blck < 1) break;
				}
				else {
					if (iNode.indirectBlockPtr < 1) break;
					blck = GetIndirectBlockEntry(iNode.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR);
					if (blck < 1) break;
					if (targetBlck == -1) targetIndirect = 1;
					lastIndirect = 1;
				}

				for (k = 0; k < NUM_OF_DIRENT_PER_BLK; ++k) {
					GetDirEntry(blck, k, &dir);
					if (dir.inodeNum < 1) {
						if (strcmp(dir.name, CURRENT_DIR) == 0) continue;
						if (strcmp(dir.name, PARENT_DIR) == 0) continue;

						break;
					}

					Inode temp;
					GetInode(dir.inodeNum, &temp);
					lastBlck = i; lastEntry = k;
					if (temp.type == FILE_TYPE_DIR && strcmp(dir.name, path) == 0) {
						targetBlck = i;
						targetEntry = k;
						find = 1;
					}
				}

			}

		}

		if (find) {

			int relTBlck; int relLBlck;
			if (targetIndirect == 0) relTBlck = iNode.dirBlockPtr[targetBlck];
			else  relTBlck = GetIndirectBlockEntry(iNode.indirectBlockPtr, targetBlck - NUM_OF_DIRECT_BLOCK_PTR);

			if (lastIndirect == 0) relLBlck = iNode.dirBlockPtr[lastBlck];
			else relLBlck = GetIndirectBlockEntry(iNode.indirectBlockPtr, lastBlck - NUM_OF_DIRECT_BLOCK_PTR);

			Inode dNode; DirEntry dDir;
			GetDirEntry(relTBlck, targetEntry, &dDir);
			GetInode(dDir.inodeNum, &dNode);

			int exists = 0;
			{
				int eBlck; DirEntry eDir;
				for (int s = 0; s < dNode.allocBlocks && !exists; ++s) {
					if (s < NUM_OF_DIRECT_BLOCK_PTR) {
						eBlck = dNode.dirBlockPtr[s];
					}
					else {
						if (dNode.indirectBlockPtr < 1) break;
						eBlck = GetIndirectBlockEntry(dNode.indirectBlockPtr, s - NUM_OF_DIRECT_BLOCK_PTR);
					}

					if (eBlck < 1) break;
					for (int w = 0; w < NUM_OF_DIRENT_PER_BLK; ++w) {
						GetDirEntry(eBlck, w, &eDir);
						if (eDir.inodeNum > 0 && strcmp(eDir.name, CURRENT_DIR) && strcmp(eDir.name, PARENT_DIR)) {
							exists = 1;
							break;
						}
					}
				}
			}

			// ERASE TARGET IS EMTPY
			if (!exists) {
				// TARGET ERASE
				{
					ResetInodeBytemap(dDir.inodeNum);
					pFileSysInfo->numAllocInodes--;

					ResetBlockBytemap(dNode.dirBlockPtr[0]);
					pFileSysInfo->numAllocBlocks--;
					pFileSysInfo->numFreeBlocks++;

					RemoveDirEntry(dNode.dirBlockPtr[0], 0); // CURRERNT_DIR
					RemoveDirEntry(dNode.dirBlockPtr[0], 1); // PARENT_DIR

				}

				// ERASE TARGET FROM PARENT
				{
					if (lastBlck != targetBlck || lastEntry != targetEntry) {
						GetDirEntry(relLBlck, lastEntry, &dir);
						PutDirEntry(relTBlck, targetEntry, &dir);
					}
					RemoveDirEntry(relLBlck, lastEntry);
				}

				// SPACE MANAGEMENT
				{
					if (lastEntry == 0) {
						ResetBlockBytemap(relLBlck);
						iNode.allocBlocks--;
						iNode.size -= BLOCK_SIZE;

						pFileSysInfo->numAllocBlocks--;
						pFileSysInfo->numFreeBlocks++;

						// DIRECT-BLOCK
						if (lastIndirect == 0) {
							iNode.dirBlockPtr[lastBlck] = 0;
						}
						// IN_DIRECT BLOCK
						else {
							RemoveIndirectBlockEntry(iNode.indirectBlockPtr, lastBlck - NUM_OF_DIRECT_BLOCK_PTR);

							if (lastBlck - NUM_OF_DIRECT_BLOCK_PTR == 0) {
								ResetBlockBytemap(iNode.indirectBlockPtr);
								iNode.indirectBlockPtr = 0;
							}
						}
						PutInode(parentInode, &iNode);
					}

				}

			}

		}

	}

	free(backup);

	DevWriteBlock(FILESYS_INFO_BLOCK, (char*)pFileSysInfo);

	return -1;
}

void CreateFileSystem(void)
{
	FileSysInit();

	int freeBlock = GetFreeBlockNum();
	int freeInode = GetFreeInodeNum();

	DirEntry* rBlck = (DirEntry*)malloc(BLOCK_SIZE);

	rBlck[0].inodeNum = freeInode;
	strcpy(rBlck[0].name, CURRENT_DIR);
	DevWriteBlock(freeBlock, (char*)rBlck);

	if (pFileSysInfo == NULL)
		pFileSysInfo = (FileSysInfo*)malloc(BLOCK_SIZE); // free to CloseFileSystem
	if (pFileDescTable == NULL)
		pFileDescTable = (FileDescTable*)malloc(sizeof(*pFileDescTable));
	if (pFileTable == NULL)
		pFileTable = (FileTable*)malloc(sizeof(*pFileTable));

	pFileDescTable->numUsedDescEntry = 0;
	pFileTable->numUsedFile = 0;

	// FILE SYS INITIALIZE
	{
		pFileSysInfo->blocks = 1;
		pFileSysInfo->rootInodeNum = freeInode;
		pFileSysInfo->diskCapacity = FS_DISK_CAPACITY;

		pFileSysInfo->numAllocBlocks = 0;
		pFileSysInfo->numFreeBlocks = FS_DISK_CAPACITY / BLOCK_SIZE - (DATAREGION_BLOCK_FIRST); // 505 = 512 - 7
		pFileSysInfo->numAllocInodes = 0;

		pFileSysInfo->blockBytemapBlock = BLOCK_BYTEMAP_BLOCK_NUM;
		pFileSysInfo->inodeBytemapBlock = INODE_BYTEMAP_BLOCK_NUM;
		pFileSysInfo->inodeListBlock = INODELIST_BLOCK_FIRST;
		pFileSysInfo->dataRegionBlock = DATAREGION_BLOCK_FIRST;
	}

	pFileSysInfo->numAllocBlocks++;
	pFileSysInfo->numFreeBlocks--;
	pFileSysInfo->numAllocInodes++;

	DevWriteBlock(FILESYS_INFO_BLOCK, (char*)pFileSysInfo);

	SetBlockBytemap(freeBlock);
	SetInodeBytemap(freeInode);

	// ----------------------------------------------------------------------------------

	Inode iNode = {};
	GetInode(freeInode, &iNode);

	iNode.dirBlockPtr[0] = freeBlock;
	iNode.allocBlocks = 1;
	iNode.type = FILE_TYPE_DIR;
	iNode.size = BLOCK_SIZE;

	PutInode(freeInode, &iNode);

	free(rBlck);

	pFileDescTable = (FileDescTable*)calloc(1, sizeof(*pFileDescTable));

}

void OpenFileSystem(void)
{
	if (pFileSysInfo == NULL)
		pFileSysInfo = (FileSysInfo*)malloc(BLOCK_SIZE);

	if (pFileDescTable == NULL) {
		pFileDescTable = (FileDescTable*)malloc(sizeof(*pFileDescTable));
		pFileDescTable->numUsedDescEntry = 0;

	}

	if (pFileTable == NULL) {
		pFileTable = (FileTable*)malloc(sizeof(*pFileTable));
		pFileTable->numUsedFile = 0;
	}

	DevOpenDisk();

	// get superblock
	DevReadBlock(FILESYS_INFO_BLOCK, (char*)pFileSysInfo);

}

void CloseFileSystem(void)
{
	DevCloseDisk();

	free(pFileDescTable); pFileDescTable = NULL;
	free(pFileSysInfo); pFileSysInfo = NULL;
	free(pFileTable); pFileTable = NULL;

}

Directory* OpenDirectory(char* name)
{
	int parentInode;
	Inode iNode; DirEntry dir;
	int find;
	char* path, * backup;
	path = (char*)malloc(strlen(name) + 1);
	strcpy(path, name);
	backup = path;
	Directory* result = NULL;

	find = FindParent(&parentInode, &iNode, &path);

	if (find) {
		if (strcmp(backup, "/") == 0) {
			result = (Directory*)malloc(sizeof(*result));
			result->inodeNum = pFileSysInfo->rootInodeNum;
		}
		else {
			find = 0;
			for (int i = 0; i < iNode.allocBlocks && !find; ++i) {
				int blck;
				if (i < NUM_OF_DIRECT_BLOCK_PTR) {
					blck = iNode.dirBlockPtr[i];
				}
				else {
					if (iNode.indirectBlockPtr < 1) break;
					blck = GetIndirectBlockEntry(iNode.indirectBlockPtr, i - NUM_OF_DIRECT_BLOCK_PTR);
					if (blck < 1) break;
				}
				for (int k = 0; k < NUM_OF_DIRENT_PER_BLK && !find; ++k) {
					GetDirEntry(blck, k, &dir);
					if (dir.inodeNum < 1) continue;

					Inode temp;
					GetInode(dir.inodeNum, &temp);
					if (temp.type == FILE_TYPE_DIR && strcmp(dir.name, path) == 0) {
						result = (Directory*)malloc(sizeof(*result));
						result->inodeNum = dir.inodeNum;
						find = 1;
					}

				}
			}
		}

		if (result) pastInode = -1;
	}

	free(backup);

	return result;
}

FileInfo* ReadDirectory(Directory* pDir)
{
	if (!pDir) return NULL;

	if (pDir->inodeNum != pastInode) {
		pastInode = pDir->inodeNum;
		count = 0;
	}

	Inode iNode;
	GetInode(pDir->inodeNum, &iNode);
	FileInfo* result = NULL;

	int readCnt = count++;
	int blck = readCnt >> 3;							// readCnt / 8
	int ent = readCnt & (NUM_OF_DIRENT_PER_BLK - 1);	// readCnt % 8
	if (blck < iNode.allocBlocks) {
		if (blck < NUM_OF_DIRECT_BLOCK_PTR) {
			blck = iNode.dirBlockPtr[blck];
		}
		else {
			if (iNode.indirectBlockPtr > 0) {
				blck = GetIndirectBlockEntry(iNode.indirectBlockPtr, blck - NUM_OF_DIRECT_BLOCK_PTR);
			}
		}

		if (blck > 0) {
			DirEntry dir;
			GetDirEntry(blck, ent, &dir);
			GetInode(dir.inodeNum, &iNode);
			if (dir.inodeNum != INVALID_ENTRY) {
				if (dir.inodeNum > 0 || (strcmp(dir.name, CURRENT_DIR) == 0 || strcmp(dir.name, PARENT_DIR) == 0)) {
					result = (FileInfo*)malloc(sizeof(FileInfo));
					result->filetype = iNode.type;
					result->inodeNum = dir.inodeNum;
					strcpy(result->name, dir.name);
					result->numBlocks = iNode.allocBlocks;
					result->size = iNode.size;
				}
			}
		}

	}

	return result;
}

int CloseDirectory(Directory* pDir)
{
	if (pDir) free(pDir);
	else return -1;

	return 0;
}
