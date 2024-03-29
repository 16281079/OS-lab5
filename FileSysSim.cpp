#include "stdafx.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <Windows.h>
#include <time.h>

#pragma pack(1)	//消除结构体的自动对齐，保证fread可以将所有的内容读入BPB
//整体的思路，使用文件指针来读取数据，而在读取完毕后在根据具体的位置进行修改

#define	C 2
#define	H 80
#define	B 18
#define DISK_SIZE 1474560
#define MY_FILE_BEGIN 0
#define MY_FILE_CURRENT 1
#define MY_FILE_END 2

typedef unsigned char u8;   //1字节  
typedef unsigned short u16; //2字节  
typedef unsigned int u32;   //4字节 

u8* setzero = (u8*)calloc(512, sizeof(u8)); // 用于创建目录时清0

struct BPB {
	u16  BPB_BytsPerSec;    //每扇区字节数  
	u8   BPB_SecPerClus;    //每簇扇区数  
	u16  BPB_RsvdSecCnt;    //Boot记录占用的扇区数  
	u8   BPB_NumFATs;   //FAT表个数  
	u16  BPB_RootEntCnt;    //根目录最大文件数  
	u16  BPB_TotSec16;  // 扇区总数
	u8   BPB_Media;  // 介质描述符
	u16  BPB_FATSz16;   // 每FAT扇区数  
	u16  BPB_SecPerTrk; // 每磁道扇区数
	u16  BPB_NumHeads; // 磁头数
	u32  BPB_HiddSec; // 隐藏扇区数
	u32  BPB_TotSec32;  //如果BPB_TotSec16为0，该值为FAT扇区数  
};

struct RootEntry {
	char DIR_Name[11]; // 文件名8字节，扩展名3字节
	u8   DIR_Attr;      //文件属性  
	char reserved[10]; //保留字段
	u16  DIR_WrtTime; //修改时间
	u16  DIR_WrtDate; // 修改日期
	u16  DIR_FstClus;   //开始簇号  
	u32  DIR_FileSize;
};

typedef struct Block {
	int address;
	long block_pos;
	char sector[512];
}*Disk, Block;

struct FileHandle {
	RootEntry fileInfo;
	LONG offset; // 当前偏移
	u16 parentClus; //所在目录簇号，0为根目录
}; // 文件句柄内部结构

struct Expression {
	const char* ls = "ls";
	const char* mkfle = "mkfle";
	const char* rmfle = "rmfle";
	const char* mkdir = "mkdir";
	const char* rmdir = "rmdir";
	const char* edit = "edit";
	const char* disp = "disp";
	const char* help = "help";
	const char* exit = "exit";
};

Disk ldisk[C][H][B];
Disk ldiskr[C][H][B];
struct BPB bpb;
struct BPB *bpb_ptr = &bpb;
FileHandle* dwHandles[1024] = { NULL };
struct Expression expression;

int BytsPerSec;    //每扇区字节数  
int SecPerClus;    //每簇扇区数  
int RsvdSecCnt;    //Boot记录占用的扇区数  
int NumFATs;   //FAT表个数  
int RootEntCnt;    //根目录最大文件数  
int TotSec; //扇区总数
int FATSz; // FAT扇区数
int BPB_Read_Success = 0;

FILE *fp;

//=============================
int BPB_Read();
DWORD MyCreateFile(char *pszFolderPath, char *pszFileName);
BOOL MyDeleteFile(char *pszFolderPath, char *pszFileName);
DWORD MyOpenFile(char *pszFolderPath, char *pszFileName);
void MyCloseFile(DWORD dwHandle);
BOOL MyCreateDirectory(char *pszFolderPath, char *pszFolderName);
BOOL MyDeleteDirectory(char *pszFolderPath, char *pszFolderName);
DWORD MyWriteFile(DWORD dwHandle, char* pBuffer, DWORD dwBytesToWrite);
DWORD MyReadFile(DWORD dwHandle, LPVOID pBuffer, DWORD dwBytesToRead);
BOOL MyDisplay();
DWORD createHandle(RootEntry* FileInfo, u16 parentClus);
int isPathExist(char *pszFolderPath);
int isFileExist(char *pszFileName, u16 FstClus);
u16 isDirectoryExist(char* FolderName, u16 FstClus);
void initFileInfo(RootEntry* FileInfo_ptr, char* FileName, u8 FileAttr, u32 FileSize);
u16 setFATValue(int clusNum);
BOOL recoverClus(u16 fileClus);
int WriteToDisk_root(RootEntry* lpbuffer, int NumberOfBytesToWrite, int offset);
int WriteToDisk_u8(u8* lpbuffer, int NumberOfBytesToWrite, int offset);
int WriteToDisk_u16(u16* lpbuffer, int NumberOfBytesToWrite, int offset);
int WriteToDisk_char(char* lpbuffer, int NumberOfBytesToWrite, int offset);
int WriteToDisk_clus(u16* lpbuffer, u16 bytes, u16 FstClus, int offset);
BOOL writeEmptyClus(u16 FstClus, RootEntry* FileInfo);
void syncFat12();
u16 getFATValue(u16 FstClus);
int getDOSDate(time_t ts);
int getDOSTime(time_t ts);
BOOL MySetFilePointer(DWORD dwFileHandle, int nOffset, DWORD dwMoveMethod);
void Disk_Open();
void Disk_Shutdown();
void recursiveDeleteDirectory(u16 fClus);
void HelpDisplay();
//=============================

int BPB_Read() {
	int temp;
	temp = fseek(fp, 11, SEEK_SET);
	if (temp == -1) {
		printf("Seek Failed Please Check Your Input File!\n");
		return 0;
	}
	temp = fread(bpb_ptr, 1, 25, fp);
	if (temp == -1) {
		printf("Read Failed Please Check Your Input File!\n");
		return 0;
	}
	BytsPerSec = bpb_ptr->BPB_BytsPerSec;
	SecPerClus = bpb_ptr->BPB_SecPerClus;
	RsvdSecCnt = bpb_ptr->BPB_RsvdSecCnt;
	NumFATs = bpb_ptr->BPB_NumFATs;
	RootEntCnt = bpb_ptr->BPB_RootEntCnt;
	if (bpb_ptr->BPB_TotSec16 != 0) {
		TotSec = bpb_ptr->BPB_TotSec16;
	}
	else {
		TotSec = bpb_ptr->BPB_TotSec32;
	}
	FATSz = bpb_ptr->BPB_FATSz16;
	return 1;
}

DWORD MyCreateFile(char *pszFolderPath, char *pszFileName) {
	DWORD FileHandle = 0;
	if (strlen(pszFileName) > 12 || strlen(pszFileName) < 3) return 0;
	u16 FstClus;
	u32 FileSize = 0;
	RootEntry FileInfo;
	RootEntry* FileInfo_ptr = &FileInfo;
	memset(FileInfo_ptr, 0, sizeof(RootEntry));
	if (BPB_Read_Success == 1) {
		if ((FstClus = isPathExist(pszFolderPath)) || strlen(pszFolderPath) <= 3) {
			if (isFileExist(pszFileName, FstClus)) {
				printf("%s \\ %s has existed in the memory, please try again!\n", pszFolderPath, pszFileName);
				return 0;
			}
			else {
				initFileInfo(FileInfo_ptr, pszFileName, 0x20, FileSize);
				if (FileInfo_ptr->DIR_FstClus == 0) return 0;
				if (writeEmptyClus(FstClus, FileInfo_ptr) == TRUE) {
					FileHandle = createHandle(FileInfo_ptr, FstClus);
				}
			}
		}
	}
	if (FileHandle != 0) syncFat12();
	return FileHandle;
}

BOOL MyDeleteFile(char *pszFolderPath, char *pszFileName) {
	BOOL result = FALSE;
	if (strlen(pszFileName) > 12 || strlen(pszFileName) < 3) return FALSE; // 8+3+'.'
	u16 FstClus;
	char filename[13];
	RootEntry FileInfo;
	RootEntry* FileInfo_ptr = &FileInfo;
	if (BPB_Read_Success == 1) {
		if ((FstClus = isPathExist(pszFolderPath)) || strlen(pszFolderPath) <= 3) {
			if (isFileExist(pszFileName, FstClus)) {
				int dataBase;
				do {
					int loop;
					if (FstClus == 0) {
						// 根目录区偏移
						dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec;
						loop = RootEntCnt;
					}
					else {
						// 数据区文件首址偏移
						dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (FstClus - 2) * BytsPerSec;
						loop = BytsPerSec / 32;
					}
					for (int i = 0; i < loop; i++) {
						fseek(fp, dataBase, SEEK_SET);
						if (fread(FileInfo_ptr,1 , 32, fp) != -1) {
							// 目录0x10，文件0x20，卷标0x28
							if (FileInfo_ptr->DIR_Name[0] != (char)0xE5 && FileInfo_ptr->DIR_Name[0] != (char)0 && FileInfo_ptr->DIR_Name[0] != (char)0x2E) {
								int len_of_filename = 0;
								if (FileInfo_ptr->DIR_Attr == 0x20) {
									for (int j = 0; j < 11; j++) {
										if (FileInfo_ptr->DIR_Name[j] != ' ') {
											filename[len_of_filename++] = FileInfo_ptr->DIR_Name[j];
										}
										else {
											filename[len_of_filename++] = '.';
											while (FileInfo_ptr->DIR_Name[j] == ' ') j++;
											j--;
										}
									}
									filename[len_of_filename] = '\0';
									// 忽略大小写比较
									if (_stricmp(filename, pszFileName) == 0) {
										// 上面读取了32字节，复位一下，第一字节写入0xe5
										fseek(fp, dataBase, SEEK_SET);
										u8 del = 0xE5;
										if (WriteToDisk_u8(&del, 1, dataBase) != 0) {
											// 回收簇
											u16 fileClus = FileInfo_ptr->DIR_FstClus; // 首簇
											u16 bytes;
											u16* bytes_ptr = &bytes;
											// 下一簇为末尾簇退出循环
											while (fileClus != 0xFFF) {
												int clusBase = RsvdSecCnt * BytsPerSec + fileClus * 3 / 2;
												u16 tempClus = getFATValue(fileClus); // 暂存下一簇，当前簇内容刷新成0
												fseek(fp, clusBase, SEEK_SET);
												if (fread(bytes_ptr, 1, 2, fp) != -1) {
													if (fileClus % 2 == 0) {
														bytes = bytes >> 12;
														bytes = bytes << 12; // 低12位置0
													}
													else {
														bytes = bytes << 12;
														bytes = bytes >> 12; // 高12位置0
													}
													fseek(fp, clusBase, SEEK_SET);
													WriteToDisk_u16(bytes_ptr, 2, clusBase); // 写回，回收该簇
												}
												fileClus = tempClus; // 更新偏移量
											}
											result = TRUE;
											break;
										}
									}
								}
							}
						}
						dataBase += 32;
					}
					if (result) break;
				} while ((FstClus = getFATValue(FstClus)) != 0xFFF && FstClus != 0);
			}
		}
	}
	if (result) syncFat12();
	return result;
}

DWORD MyOpenFile(char *pszFolderPath, char *pszFileName) {
	DWORD FileHandle = 0;
	if (strlen(pszFileName) > 12 || strlen(pszFileName) < 3) return 0; // 8+3+'.'
	u16 FstClus = 0;
	BOOL isExist = FALSE;
	char filename[13];
	RootEntry FileInfo;
	RootEntry* FileInfo_ptr = &FileInfo;
	if (BPB_Read_Success == 1) {
		// fix bug:优先级，赋值记得加括号
		if ((FstClus = isPathExist(pszFolderPath)) || strlen(pszFolderPath) <= 3) {
			u16 parentClus = FstClus;
			if (isFileExist(pszFileName, FstClus)) {
				int dataBase;
				do {
					int loop;
					if (FstClus == 0) {
						// 根目录区偏移
						dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec;
						loop = RootEntCnt;
					}
					else {
						// 数据区文件首址偏移
						dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (FstClus - 2) * BytsPerSec;
						loop = BytsPerSec / 32;
					}
					for (int i = 0; i < loop; i++) {
						fseek(fp, dataBase, SEEK_SET);
						if (fread(FileInfo_ptr, 1, 32, fp) != -1) {
							// 目录0x10，文件0x20，卷标0x28
							if (FileInfo_ptr->DIR_Name[0] != (char)0xE5 && FileInfo_ptr->DIR_Name[0] != (char)0 && FileInfo_ptr->DIR_Name[0] != (char)0x2E) {
								int len_of_filename = 0;
								if (FileInfo_ptr->DIR_Attr == 0x20) {
									for (int j = 0; j < 11; j++) {
										if (FileInfo_ptr->DIR_Name[j] != ' ') {
											filename[len_of_filename++] = FileInfo_ptr->DIR_Name[j];
										}
										else {
											filename[len_of_filename++] = '.';
											while (FileInfo_ptr->DIR_Name[j] == ' ') j++;
											j--;
										}
									}
									filename[len_of_filename] = '\0';
									// 忽略大小写比较
									if (_stricmp(filename, pszFileName) == 0) {
										isExist = TRUE;
										break;
									}
								}
							}
						}
						dataBase += 32;
					}
					if (isExist) {
						FileHandle = createHandle(FileInfo_ptr, parentClus);
						break;
					}
				} while ((FstClus = getFATValue(FstClus)) != 0xFFF && FstClus != 0);
			}
		}
	}
	return FileHandle;
}

void MyCloseFile(DWORD dwHandle) {
	if (dwHandles[dwHandle] != NULL) {
		free(dwHandles[dwHandle]);
		dwHandles[dwHandle] = NULL;
	}
}

BOOL MyCreateDirectory(char *pszFolderPath, char *pszFolderName) {
	DWORD FileHandle = 0;
	u16 FstClus;
	u16 originClus;
	BOOL result = FALSE;
	RootEntry rootEntry;
	RootEntry* rootEntry_ptr = &rootEntry;
	if (strlen(pszFolderName) > 11 || strlen(pszFolderName) <= 0) return FALSE;
	int dataBase;
	int dBase;
	int loop, i, j;
	if (BPB_Read_Success == 1) {
		// 路径存在或者为根目录
		if ((FstClus = isPathExist(pszFolderPath)) || strlen(pszFolderPath) <= 3) {
			originClus = FstClus;
			if (isDirectoryExist(pszFolderName, FstClus)) {
				//cout << pszFolderPath << '\\' << pszFolderName << " has existed!" << endl;
			}
			else {
				do {
					if (FstClus == 0) {
						// 根目录区偏移
						dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec;
						loop = RootEntCnt;
					}
					else {
						// 数据区文件首址偏移
						dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (FstClus - 2) * BytsPerSec;
						loop = BytsPerSec / 32;
					}
					for (i = 0; i < loop; i++) {
						fseek(fp, dataBase, SEEK_SET);
						if (fread(rootEntry_ptr, 1, 32, fp) != -1) {
							// 目录项可用
							if (rootEntry_ptr->DIR_Name[0] == (char)0x00 || rootEntry_ptr->DIR_Name[0] == (char)0xE5) {
								initFileInfo(rootEntry_ptr, pszFolderName, 0x10, 0); // 文件夹大小为0
								if (rootEntry_ptr->DIR_FstClus == 0) return FALSE;
								fseek(fp, dataBase, SEEK_SET); // 磁头复位
								if (WriteToDisk_root(rootEntry_ptr, 32, dataBase) != 0) {
									// 创建 . 和 ..目录
									dBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (rootEntry_ptr->DIR_FstClus - 2) * BytsPerSec;
									fseek(fp, dBase, SEEK_SET);
									WriteToDisk_u8(setzero, BytsPerSec, dBase); // 目录创建初始清0
									// .
									fseek(fp, dBase, SEEK_SET);
									rootEntry_ptr->DIR_FileSize = 0;
									rootEntry_ptr->DIR_Name[0] = 0x2E;
									for (i = 1; i < 11; i++) {
										rootEntry_ptr->DIR_Name[i] = 0x20;
									}
									WriteToDisk_root(rootEntry_ptr, 32, dBase);
									// ..
									fseek(fp, (dBase + 32), SEEK_SET);
									rootEntry_ptr->DIR_Name[1] = 0x2E;
									rootEntry_ptr->DIR_FstClus = originClus;
									WriteToDisk_root(rootEntry_ptr, 32, dBase);
									result = TRUE;
									break;
								}
							}
						}
						dataBase += 32;
					}
					if (result) break;
				} while ((FstClus = getFATValue(FstClus)) != 0xFFF && FstClus != 0);
			}
		}
	}
	if (result) syncFat12();
	return result;
}

BOOL MyDeleteDirectory(char *pszFolderPath, char *pszFolderName) {
	u16 FstClus;
	BOOL result = FALSE;
	if (strlen(pszFolderName) > 11 || strlen(pszFolderName) <= 0) return FALSE;
	if (BPB_Read_Success == 1) {
		// 路径存在或者为根目录
		if ((FstClus = isPathExist(pszFolderPath)) || strlen(pszFolderPath) <= 3) {
			// 待删除目录存在
			if (isDirectoryExist(pszFolderName, FstClus)) {
				int dataBase;
				int loop;
				char directory[12];
				u8 del = 0xE5;
				RootEntry fd;
				RootEntry* fd_ptr = &fd;
				do {
					if (FstClus == 0) {
						// 根目录区偏移
						dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec;
						loop = RootEntCnt;
					}
					else {
						// 数据区文件首址偏移
						dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (FstClus - 2) * BytsPerSec;
						loop = BytsPerSec / 32;
					}
					for (int i = 0; i < loop; i++) {
						fseek(fp, dataBase, SEEK_SET);
						if (fread(fd_ptr, 1, 32, fp) != -1) {
							if (fd_ptr->DIR_Name[0] != (char)0xE5 && fd_ptr->DIR_Name[0] != (char)0 && fd_ptr->DIR_Name[0] != (char)0x2E) {
								// 目录0x10，文件0x20，卷标0x28
								if (fd_ptr->DIR_Attr == 0x10) {
									for (int j = 0; j < 11; j++) {
										if (fd_ptr->DIR_Name[j] != ' ') {
											directory[j] = fd_ptr->DIR_Name[j];
											if (j == 10) {
												directory[11] = '\0';
												break;
											}
										}
										else {
											directory[j] = '\0';
											break;
										}
									}
									// 忽略大小写比较
									if (_stricmp(directory, pszFolderName) == 0) {
										recursiveDeleteDirectory(fd_ptr->DIR_FstClus);
										// 删除该文件夹
										fseek(fp, dataBase, SEEK_SET);
										if (WriteToDisk_u8(&del, 1, dataBase) != 0) {
											result = recoverClus(fd_ptr->DIR_FstClus); // 传入首簇，回收
											break;
										}
									}
								}
							}
						}
						dataBase += 32;
					}
					if (result) break;
				} while ((FstClus = getFATValue(FstClus)) != 0xFFF && FstClus != 0);
			}
		}
	}
	if (result) syncFat12();
	return result;
}

DWORD MyWriteFile(DWORD dwHandle, char* pBuffer, DWORD dwBytesToWrite) {
	DWORD result = 0;
	FileHandle* hd = dwHandles[dwHandle];
	RootEntry* rootEntry_ptr = (RootEntry*)malloc(sizeof(RootEntry));
	if (hd == NULL || BPB_Read_Success == 0) return -1;
	u16 FstClus = hd->fileInfo.DIR_FstClus;
	LONG offset = hd->offset; // 文件指针当前偏移
	int curClusNum = offset / BytsPerSec; // 当前指针在第几个扇区
	int curClusOffset = offset % BytsPerSec; // 当前在扇区内偏移
	while (curClusNum) {
		if (getFATValue(FstClus) == 0xFFF) {
			break;
		}
		FstClus = getFATValue(FstClus);
		curClusNum--;
	}// 获取当前指针所指扇区
	int dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (FstClus - 2) * BytsPerSec;
	int dataOffset = dataBase + curClusOffset; // 拿到文件指针所指位置
	int lenOfBuffer = dwBytesToWrite; // 缓冲区待写入长度
	char* cBuffer = (char*)malloc(sizeof(u8)*lenOfBuffer);
	memcpy(cBuffer, pBuffer, lenOfBuffer); // 复制过来
	fseek(fp, dataOffset, SEEK_SET);
	if ((BytsPerSec - curClusOffset >= lenOfBuffer) && curClusNum == 0) {
		if (WriteToDisk_char(pBuffer, lenOfBuffer, dataOffset) == 0) {
			return -1;
		}
		result += lenOfBuffer;
	}
	else {
		DWORD temp;
		u16 tempClus;
		u16 bytes; // 每次读取的簇号
		u16* bytes_ptr = &bytes;
		int fatBase = RsvdSecCnt * BytsPerSec;
		int leftLen = lenOfBuffer;
		int hasWritten = 0;
		if (curClusNum == 0) {
			if (WriteToDisk_char(pBuffer, BytsPerSec - curClusOffset, dataOffset) == 0) {
				return -1;
			}
			temp = BytsPerSec - curClusOffset;
			result += temp; // 记录写入长度
			leftLen = lenOfBuffer - (BytsPerSec - curClusOffset); // 剩余长度
			hasWritten = BytsPerSec - curClusOffset;
		}
		do {
			tempClus = getFATValue(FstClus); // 尝试拿下一个FAT
			if (tempClus == 0xFFF) {
				tempClus = setFATValue(1);
				if (tempClus == 0) return -1; //分配簇失败
				fseek(fp, (fatBase + FstClus * 3 / 2), SEEK_SET);
				if (fread(bytes_ptr, 1, 2, fp) != -1) {
					if (FstClus % 2 == 0) {
						bytes = bytes >> 12;
						bytes = bytes << 12; // 保留高四位，低12位为0
						bytes = bytes | tempClus;
					}
					else {
						bytes = bytes << 12;
						bytes = bytes >> 12; // 保留低四位，高12位为0
						bytes = bytes | (tempClus << 4);
					}
					fseek(fp, (fatBase + FstClus * 3 / 2), SEEK_SET);
					if (WriteToDisk_clus(bytes_ptr, bytes, FstClus, (fatBase + FstClus * 3 / 2)) == 0) {
						return -1;
					}
				}
			}
			FstClus = tempClus; // 真正拿到下一个FAT
			dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (FstClus - 2) * BytsPerSec; // 刷新扇区偏移
			fseek(fp, dataBase, SEEK_SET); // 一定是从扇区头开始写
			if (leftLen > BytsPerSec) {
				if (WriteToDisk_char(&cBuffer[hasWritten], BytsPerSec, dataBase) == 0) {
					return -1;
				}
				temp = BytsPerSec;
				hasWritten += BytsPerSec;
			}
			else {
				if (WriteToDisk_char(&cBuffer[hasWritten], leftLen, dataBase) == 0) {
					return -1;
				}
				temp = leftLen;
				hasWritten += leftLen;
			}
			leftLen -= BytsPerSec;
			result += temp;
		} while (leftLen > 0);
	}
	// 刷新文件大小
	if ((offset + result) > hd->fileInfo.DIR_FileSize) {
		int dBase;
		BOOL isExist = FALSE;
		hd->fileInfo.DIR_FileSize += (offset + result) - hd->fileInfo.DIR_FileSize;
		// 遍历当前目录所有项目
		u16 parentClus = hd->parentClus;
		do {
			int loop;
			if (parentClus == 0) {
				dBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec;
				loop = RootEntCnt;
			}
			else {
				dBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (parentClus - 2) * BytsPerSec;
				loop = BytsPerSec / 32;
			}
			for (int i = 0; i < loop; i++) {
				fseek(fp, dBase, SEEK_SET);
				if (fread(rootEntry_ptr, 1, 32, fp) != -1) {
					if (rootEntry_ptr->DIR_Attr == 0x20) {
						if (_stricmp(rootEntry_ptr->DIR_Name, hd->fileInfo.DIR_Name) == 0) {
							fseek(fp, dBase, SEEK_SET);
							WriteToDisk_root(&hd->fileInfo, 32, dBase);
							isExist = TRUE;
							break;
						}
					}
				}
				dBase += 32;
			}
			if (isExist) break;
		} while ((parentClus = getFATValue(parentClus)) != 0xFFF && parentClus != 0);
		if (isExist) syncFat12();
	}
	MySetFilePointer(dwHandle, result, MY_FILE_CURRENT); //偏移量刷新
	return result;
}

BOOL MySetFilePointer(DWORD dwFileHandle, int nOffset, DWORD dwMoveMethod) {
	FileHandle* hd = dwHandles[dwFileHandle];
	if (hd == NULL || BPB_Read_Success == 0) return FALSE; // 句柄不存在
	LONG curOffset = nOffset + hd->offset; // current模式下偏移后的位置
	u16 currentClus = hd->fileInfo.DIR_FstClus; // 首簇
	int fileSize = hd->fileInfo.DIR_FileSize; // 文件大小
	int fileBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (currentClus - 2) * BytsPerSec;
	switch (dwMoveMethod) {
	case MY_FILE_BEGIN:
		if (nOffset < 0) {
			hd->offset = 0; // 小于0，置为0
		}
		else if (nOffset > fileSize) {
			hd->offset = fileSize;
		}
		else {
			hd->offset = nOffset;
		}
		break;
	case MY_FILE_CURRENT:
		if (curOffset < 0) {
			hd->offset = 0;
		}
		else if (curOffset > fileSize) {
			hd->offset = fileSize;
		}
		else {
			hd->offset = curOffset;
		}
		break;
	case MY_FILE_END:
		if (nOffset > 0) {
			hd->offset = fileSize;
		}
		else if (nOffset < -fileSize) {
			hd->offset = 0;
		}
		else {
			hd->offset = fileSize + nOffset;
		}
		break;
	}
	return TRUE;
}

DWORD MyReadFile(DWORD dwHandle, LPVOID pBuffer, DWORD dwBytesToRead) {
	DWORD result = 0;
	FileHandle* hd = dwHandles[dwHandle];
	if (hd == NULL || BPB_Read_Success == 0) return -1;
	u16 FstClus = hd->fileInfo.DIR_FstClus;
	LONG offset = hd->offset; // 文件指针当前偏移
	int curClusNum = offset / BytsPerSec; // 当前指针在第几个扇区
	int curClusOffset = offset % BytsPerSec; // 当前在扇区内偏移
	while (curClusNum) {
		if (getFATValue(FstClus) == 0xFFF) {
			break;
		}
		FstClus = getFATValue(FstClus);
		curClusNum--;
	}// 获取当前指针所指扇区
	if (curClusNum > 0 || offset > (int)hd->fileInfo.DIR_FileSize) return -1; // 超出文件偏移范围了
	int dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (FstClus - 2) * BytsPerSec;
	int dataOffset = dataBase + curClusOffset; // 拿到文件指针所指位置
	int lenOfBuffer = dwBytesToRead; // 缓冲区待读入长度
	if ((int)hd->fileInfo.DIR_FileSize - offset < lenOfBuffer) {
		lenOfBuffer = hd->fileInfo.DIR_FileSize - offset;
	}
	char* cBuffer = (char*)malloc(sizeof(u8)*lenOfBuffer); // 创建一个缓冲区
	memset(cBuffer, 0, lenOfBuffer);
	fseek(fp, dataOffset, SEEK_SET);
	// 读取
	if (BytsPerSec - curClusOffset >= lenOfBuffer) {
		if (fread(cBuffer, 1 , lenOfBuffer, fp) == -1) {
			return -1;
		}
	}
	else {
		DWORD temp;
		if (fread(cBuffer, 1, BytsPerSec - curClusOffset, fp) == -1) {
			return -1;
		}
		temp = BytsPerSec - curClusOffset;
		result += temp; // 记录读取到的长度
		int leftLen = lenOfBuffer - (BytsPerSec - curClusOffset); // 剩余长度
		int hasRead = BytsPerSec - curClusOffset;
		do {
			FstClus = getFATValue(FstClus); // 拿到下一个FAT
			if (FstClus == 0xFFF) {
				break;
			}
			dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (FstClus - 2) * BytsPerSec; // 刷新扇区偏移
			fseek(fp, dataBase, SEEK_SET);
			if (leftLen > BytsPerSec) {
				if (fread(&cBuffer[hasRead], 1, BytsPerSec, fp) == -1) {
					return -1;
				}
				temp = BytsPerSec;
				hasRead += BytsPerSec;
			}
			else {
				if (fread(&cBuffer[hasRead], 1, leftLen, fp) == -1) {
					return -1;
				}
				temp = leftLen;
				hasRead += leftLen;
			}
			leftLen -= BytsPerSec; // 直接减掉一个扇区，只要是<=0就退出循环
			result += temp;
		} while (leftLen > 0);
	}
	memcpy(pBuffer, cBuffer, lenOfBuffer); // 写入缓冲区
	MySetFilePointer(dwHandle, result, MY_FILE_CURRENT); //偏移量刷新
	return result;
}

BOOL MyDisplay() {
	char filename[13];
	int dataBase;
	BOOL isExist = FALSE;
	int loop, i, j;
	RootEntry* rootEntry_ptr = (RootEntry*)malloc(sizeof(RootEntry));
		// 根目录区偏移
	dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec;
	loop = RootEntCnt;
	for (int i = 0; i < loop; i++) {
		fseek(fp, dataBase, SEEK_SET);
		if (fread(rootEntry_ptr, 1, 32, fp) != -1) {
			// 目录0x10，文件0x20，卷标0x28
			if (rootEntry_ptr->DIR_Name[0] != (char)0xE5 && rootEntry_ptr->DIR_Name[0] != (char)0 && rootEntry_ptr->DIR_Name[0] != (char)0x2E) {
				int len_of_filename = 0;
				if (rootEntry_ptr->DIR_Attr == 0x20 || rootEntry_ptr->DIR_Attr == 0x10) {
					for (int j = 0; j < 11; j++) {
						if (rootEntry_ptr->DIR_Name[j] != ' ') {
							printf("%c", rootEntry_ptr->DIR_Name[j]);
						}
						else {
							if ((int)rootEntry_ptr->DIR_Name[j + 1] != 0x20 && (int)rootEntry_ptr->DIR_Name[j + 1] != 0x10) {
								printf(".");
							}
						}
					}
					printf("\n");
				}
			}
		}
		dataBase += 32;
	}
	return TRUE;
}

DWORD createHandle(RootEntry* FileInfo, u16 parentClus) {
	for (int j = 1; j < 1024; j++) {
		if (dwHandles[j] != NULL) {
			if (dwHandles[j]->fileInfo.DIR_FstClus == FileInfo->DIR_FstClus) {
				return j; // 说明该文件已被打开，不用重新申请句柄
			}
		}
	}
	FileHandle* hd = (FileHandle*)malloc(sizeof(FileHandle)); // 统一在这里malloc
	for (int i = 1; i < 1024; i++) {
		if (dwHandles[i] == NULL) {
			memcpy(&hd->fileInfo, FileInfo, 32);
			hd->offset = 0; // 偏移量初始化为0
			hd->parentClus = parentClus;
			dwHandles[i] = hd;
			return i; //申请到了return i
		}
	}
	return 0; //没有可用句柄 return 0
}

int isPathExist(char *pszFolderPath) {
	char directory[12];
	u16 FstClus = 0;
	if (strlen(pszFolderPath) <= 3) return 0;
	int i = 3, len = 0;
	while (pszFolderPath[i] != '\0' && len <= 11) {
		if (pszFolderPath[i] == '\\') {
			directory[len] = '\0';
			printf("%s", directory);
			if (FstClus = isDirectoryExist(directory, FstClus)) {
				len = 0;
			}
			else {
				len = 0;
				break;
			}
			i++;
		}
		else {
			if (len == 11) break;
			directory[len++] = pszFolderPath[i++];
		}
	}
	if (pszFolderPath[i] != '\0' && len == 11) return 0;
	if (len > 0) {
		directory[len] = '\0';
		printf("%s", directory);
		FstClus = isDirectoryExist(directory, FstClus);
	}
	return FstClus;
}

int isFileExist(char *pszFileName, u16 FstClus) {
	char filename[13];
	int dataBase;
	BOOL isExist = FALSE;
	int loop, i, j;
	RootEntry* rootEntry_ptr = (RootEntry*)malloc(sizeof(RootEntry));
	do {
		if (FstClus == 0) {
			// 根目录区偏移
			dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec;
			loop = RootEntCnt;
		}
		else {
			// 数据区文件首址偏移
			dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (FstClus - 2) * BytsPerSec;
			loop = BytsPerSec / 32;
		}
		for (int i = 0; i < loop; i++) {
			fseek(fp, dataBase, SEEK_SET);
			if (fread(rootEntry_ptr, 1, 32, fp) != -1) {
				// 目录0x10，文件0x20，卷标0x28
				if (rootEntry_ptr->DIR_Name[0] != (char)0xE5 && rootEntry_ptr->DIR_Name[0] != (char)0 && rootEntry_ptr->DIR_Name[0] != (char)0x2E) {
					int len_of_filename = 0;
					if (rootEntry_ptr->DIR_Attr == 0x20) {
						for (int j = 0; j < 11; j++) {
							if (rootEntry_ptr->DIR_Name[j] != ' ') {
								filename[len_of_filename++] = rootEntry_ptr->DIR_Name[j];
							}
							else {
								filename[len_of_filename++] = '.';
								while (rootEntry_ptr->DIR_Name[j] == ' ') j++;
								j--;
							}
						}
						filename[len_of_filename] = '\0';
						// 忽略大小写比较
						if (_stricmp(filename, pszFileName) == 0) {
							isExist = TRUE;
							break;
						}
					}
				}
			}
			dataBase += 32;
		}
		if (isExist) break;
	} while ((FstClus = getFATValue(FstClus)) != 0xFFF && FstClus != 0);
	return isExist;
}

u16 isDirectoryExist(char* FolderName, u16 FstClus) {
	char directory[12];
	int dataBase;
	u16 isExist = 0;
	int loop, i, j;
	RootEntry* rootEntry_ptr = (RootEntry*)malloc(sizeof(RootEntry));
	do {
		if (FstClus == 0) {
			dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec;
			loop = RootEntCnt;
		}
		else {
			dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (FstClus - 2) * BytsPerSec;
			loop = BytsPerSec / 32;
		}
		for (i = 0; i < loop; i++) {
			fseek(fp, dataBase, SEEK_SET);
			if (fread(rootEntry_ptr, 1, 32, fp) != -1) {
				if (rootEntry_ptr->DIR_Name[0] != (char)0xE5 && rootEntry_ptr->DIR_Name != (char)0 && rootEntry_ptr->DIR_Name[0] != (char)0x2E) {
					if (rootEntry_ptr->DIR_Attr == 0x10) {
						for (j = 0; j < 11; j++) {
							if (rootEntry_ptr->DIR_Name[j] != ' ') {
								directory[j] = rootEntry_ptr->DIR_Name[j];
								if (j == 10) {
									directory[11] = '\0';
									break;
								}
							}
							else {
								directory[j] = '\0';
								break;
							}
						}
						if (_stricmp(directory, FolderName) == 0) {
							isExist = rootEntry_ptr->DIR_FstClus;
							break;
						}
					}
				}
			}
			dataBase += 32;
		}
		if (isExist) {
			break;
		}
	} while ((FstClus = getFATValue(FstClus)) != 0xFFF && FstClus != 0);
	return isExist;
}

void initFileInfo(RootEntry* FileInfo_ptr, char* FileName, u8 FileAttr, u32 FileSize) {
	time_t ts = time(NULL);
	FileInfo_ptr->DIR_Attr = FileAttr;
	FileInfo_ptr->DIR_WrtDate = getDOSDate(ts);
	FileInfo_ptr->DIR_WrtTime = getDOSTime(ts);
	int i = 0;
	if (FileAttr == 0x10) {
		FileInfo_ptr->DIR_FileSize = 0;
		while (FileName[i] != '\0' && i < 11) {
			FileInfo_ptr->DIR_Name[i] = FileName[i];
			i++;
		}
		while (i < 11) {
			FileInfo_ptr->DIR_Name[i] = 0x20;
			i++;
		}
	}
	else {
		FileInfo_ptr->DIR_FileSize = FileSize;
		while (FileName[i] != '\0') {
			if (FileName[i] == '.') {
				int j = i;
				while (j < 8) {
					FileInfo_ptr->DIR_Name[j] = 0x20;
					j++;
				}
				i++;
				break;
			}
			else {
				FileInfo_ptr->DIR_Name[i] = FileName[i];
				i++;
			}

		}
		memcpy(&FileInfo_ptr->DIR_Name[8], &FileName[i], 3);
	}
	int clusNum;
	if ((FileSize % BytsPerSec) == 0 && FileSize != 0) {
		clusNum = FileSize / BytsPerSec;
	}
	else {
		clusNum = FileSize / BytsPerSec + 1;
	}
	FileInfo_ptr->DIR_FstClus = setFATValue(clusNum);
}

u16 setFATValue(int clusNum) {
	int fatBase = RsvdSecCnt * BytsPerSec;
	int fatPos = fatBase + 3; // 从2号簇开始查找，放空3字节
							  //先读出FAT项所在的两个字节
	u16 clus = 2;
	int i = 0;
	u16 bytes; // 每次读取的簇号
	u16* bytes_ptr = &bytes;
	u16 FstClus;
	u16 preClus;
	int loop = FATSz * BytsPerSec / 3 * 2 - 2; // 共有多少个簇
	do {
		fseek(fp,fatPos, SEEK_SET);
		if (fread(bytes_ptr,1 , 2, fp) != -1) {
			// 簇号为偶数 
			if (clus % 2 == 0) {
				bytes = bytes << 4;
				bytes = bytes >> 4; // 这边不移回来也可以，反正都是0
			}
			else {
				bytes = bytes >> 4;
			}
			if (bytes == 0x000) {
				if (i > 0) {
					fseek(fp, (fatBase + preClus * 3 / 2), SEEK_SET);
					if (fread(bytes_ptr, 1, 2, fp) != -1) {
						if (preClus % 2 == 0) {
							bytes = bytes >> 12;
							bytes = bytes << 12; // 保留高四位，低12位为0
							bytes = bytes | clus; // 与当前clus按位或
						}
						else {
							bytes = bytes << 12;
							bytes = bytes >> 12; // 保留低四位，高12位为0
							bytes = bytes | (clus << 4);
						}
						fseek(fp, (fatBase + preClus * 3 / 2), SEEK_SET);
						WriteToDisk_u16(bytes_ptr, 2, (fatBase + preClus * 3 / 2));
					}
				}
				else {
					FstClus = clus; // 保存首簇
				}
				preClus = clus;
				if (clusNum == ++i) break; // 到尾簇退出循环
			}
		}
		if (clus % 2 == 0) {
			fatPos++; // 往后偏一个字节
		}
		else {
			fatPos += 2; // 往后偏2个字节
		}
		clus++; // 簇号加一
		loop--;
	} while (loop > 0);
	// 尾簇补0xfff
	fseek(fp, (fatBase + preClus * 3 / 2), SEEK_SET);
	if (fread(bytes_ptr, 1, 2, fp) != -1) {
		if (preClus % 2 == 0) {
			bytes = bytes >> 12;
			bytes = bytes << 12; // 保留高四位，低12位为0
			bytes = bytes | 0x0FFF;
		}
		else {
			bytes = bytes << 12;
			bytes = bytes >> 12; // 保留低四位，高12位为0
			bytes = bytes | 0xFFF0;
		}
		fseek(fp, (fatBase + preClus * 3 / 2), SEEK_SET);
		WriteToDisk_u16(bytes_ptr, 2, (fatBase + preClus * 3 / 2));
	}
	// 簇没分配成功，个数不够
	if (clusNum != i) {
		recoverClus(FstClus); // 分配失败，回滚操作
		return 0;
	}
	else {
		return FstClus;
	}
}

BOOL recoverClus(u16 fileClus) {
	// 回收簇
	u16 bytes;
	u16* bytes_ptr = &bytes;
	// 下一簇为末尾簇退出循环
	while (fileClus != 0xFFF) {
		int clusBase = RsvdSecCnt * BytsPerSec + fileClus * 3 / 2;
		u16 tempClus = getFATValue(fileClus); // 暂存下一簇，当前簇内容刷新成0
		fseek(fp,clusBase, SEEK_SET);
		if (fread(bytes_ptr, 1, 2, fp) != -1) {
			if (fileClus % 2 == 0) {
				bytes = bytes >> 12;
				bytes = bytes << 12; // 低12位置0
			}
			else {
				bytes = bytes << 12;
				bytes = bytes >> 12; // 高12位置0
			}
			fseek(fp, clusBase, SEEK_SET);
			WriteToDisk_u16(bytes_ptr, 2, clusBase); // 写回，回收该簇
		}
		fileClus = tempClus; // 更新偏移量
	}
	return TRUE;
}

int WriteToDisk_root(RootEntry* lpbuffer, int NumberOfBytesToWrite, int offset) {
	int block_num = offset / 512;
	int block_addr = offset % 512;
	int track = block_num / 18; //磁道
	int sector = block_num % 18; //扇区
	int disk_face = 0;
	int i = 0;
	if (track > C * H) {
		printf("Write address overflow, please check your operation!");
		return FALSE;
	}
	else if (track > H) {
		disk_face = 1;
	}
	/*struct RootEntry {
	char DIR_Name[11]; // 文件名8字节，扩展名3字节
	u8   DIR_Attr;      //文件属性  
	char reserved[10]; //保留字段
	u16  DIR_WrtTime; //修改时间
	u16  DIR_WrtDate; // 修改日期
	u16  DIR_FstClus;   //开始簇号  
	u32  DIR_FileSize;
};*/
	strcpy(ldiskr[disk_face][track][sector]->sector + block_addr, lpbuffer->DIR_Name);
	memcpy(ldiskr[disk_face][track][sector]->sector + block_addr + 11, &lpbuffer->DIR_Attr, 1);
	memcpy(ldiskr[disk_face][track][sector]->sector + block_addr + 22, &lpbuffer->DIR_WrtTime, 2);
	memcpy(ldiskr[disk_face][track][sector]->sector + block_addr + 24, &lpbuffer->DIR_WrtDate, 2);
	memcpy(ldiskr[disk_face][track][sector]->sector + block_addr + 26, &lpbuffer->DIR_FstClus, 2);
	memcpy(ldiskr[disk_face][track][sector]->sector + block_addr + 28, &lpbuffer->DIR_FileSize, 4);
	return 1;
}

int WriteToDisk_u16(u16* lpbuffer, int NumberOfBytesToWrite, int offset) {
	int block_num = offset / 512;
	int block_addr = offset % 512;
	int track = block_num / 18; //磁道
	int sector = block_num % 18; //扇区
	int disk_face = 0;
	int i = 0;
	if (track > C * H) {
		printf("Write address overflow, please check your operation!");
		return FALSE;
	}
	else if (track > H) {
		disk_face = 1;
	}
	ldiskr[disk_face][track][sector]->sector[block_addr] = *(char*)lpbuffer;
	ldiskr[disk_face][track][sector]->sector[block_addr + 1] = *((char*)lpbuffer + 1);
	return 1;
}

int WriteToDisk_u8(u8* lpbuffer, int NumberOfBytesToWrite, int offset) {
	int block_num = offset / 512;
	int block_addr = offset % 512;
	int track = block_num / 18; //磁道
	int sector = block_num % 18; //扇区
	int disk_face = 0;
	int i = 0;
	if (track > C * H) {
		printf("Write address overflow, please check your operation!");
		return FALSE;
	}
	else if (track > H) {
		disk_face = 1;
	}
	ldiskr[disk_face][track][sector]->sector[block_addr] = *(char*)lpbuffer;
	return 1;
}

int WriteToDisk_char(char* lpbuffer, int NumberOfBytesToWrite, int offset) {
	int block_num = offset / 512;
	int block_addr = offset % 512;
	int track = block_num / 18; //磁道
	int sector = block_num % 18; //扇区
	int disk_face = 0;
	int i = 0;
	int j = 0;
	int flag = 0;
	if (track > C * H) {
		printf("Write address overflow, please check your operation!");
		return FALSE;
	}
	else if (track > H) {
		disk_face = 1;
	}
	if (NumberOfBytesToWrite > (((80 - track) * (18 - sector) * 512 + (512 - block_addr)) + ((1 - disk_face) * 80 * 18 * 512))) {
		printf("File is too big! Try another one");
		return FALSE;
	}
	for (j = 0; j < NumberOfBytesToWrite; j++) {
		if (*(lpbuffer + j) == '\0') {
			break;
		}
		if (block_addr + (j % 512) <= 511 && flag == 0) {
			ldiskr[disk_face][track][sector]->sector[block_addr + j] = *(lpbuffer + j);
		}
		else if (flag == 1) {
			ldiskr[disk_face][track][sector]->sector[(j % 512)] = *lpbuffer + j;
		}
		else {
			flag = 1;
			if (sector == 17 && track != 79) {
				sector = 0;
				track++;
			}
			else if (sector == 17 && track == 79) {
				sector = 0;
				track = 0;
				disk_face++;
			}
			else {
				sector++;
			}
		}
	}
	return 1;
}

int WriteToDisk_clus(u16* lpbuffer, u16 bytes, u16 FstClus, int offset) {
	int block_num = offset / 512;
	int block_addr = offset % 512;
	int track = block_num / 18; //磁道
	int sector = block_num % 18; //扇区
	int disk_face = 0;
	int i = 0;
	u8 temp1, temp2;
	if (track > C * H) {
		printf("Write address overflow, please check your operation!");
		return FALSE;
	}
	else if (track > H) {
		disk_face = 1;
	}
	ldiskr[disk_face][track][sector]->sector[block_addr] = *(char*)lpbuffer;
	temp1 = ldiskr[disk_face][track][sector]->sector[block_addr + 1];
	temp2 = *((char*)lpbuffer + 1);
	if (FstClus % 2 == 0) {
		temp1 = temp1 << 4; // 保留高四位，低12位为0
		temp2 = temp2 >> 4;
		temp1 = temp1 | temp2;
	}
	else {
		temp1 = temp1 >> 4;
		temp2 = temp2 << 4;
		temp1 = temp1 | temp2; // 保留低四位，高12位为0
	}
	ldiskr[disk_face][track][sector]->sector[block_addr + 1] = temp1;
	return 1;
}

BOOL writeEmptyClus(u16 FstClus, RootEntry* FileInfo) {
	int dataBase;
	u16 originClus;
	BOOL success = FALSE;
	RootEntry * rootEntry_ptr = (RootEntry*)malloc(sizeof(RootEntry));
	do {
		int loop;
		originClus = FstClus; // 保存非0xfff簇号
		if (FstClus == 0) {
			// 根目录区偏移
			dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec;
			loop = RootEntCnt;
		}
		else {
			// 数据区文件首址偏移
			dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (FstClus - 2) * BytsPerSec;
			loop = BytsPerSec / 32;
		}
		for (int i = 0; i < loop; i++) {
			fseek(fp, dataBase, SEEK_SET);
			if (fread(rootEntry_ptr, 1, 32, fp) != -1) {
				// 说明该目录项可用
				if (rootEntry_ptr->DIR_Name[0] == (char)0x00 || rootEntry_ptr->DIR_Name[0] == (char)0xE5) {
					fseek(fp, dataBase, SEEK_SET);
					if (WriteToDisk_root(FileInfo, 32, dataBase) != 0) {
						success = TRUE;
						break;
					}
				}
			}
			dataBase += 32;
		}
		if (success) break;
	} while ((FstClus = getFATValue(FstClus)) != 0xFFF && FstClus != 0);
	if (success == FALSE && FstClus != 0) { // 目录空间不足且不是根目录
		u16 bytes;
		u16* bytes_ptr = &bytes;
		int fatBase = RsvdSecCnt * BytsPerSec;
		u16 tempClus = setFATValue(1);
		if (tempClus == 0) return FALSE;
		fseek(fp, (fatBase + originClus * 3 / 2), SEEK_SET); // 尾簇号偏移
		dataBase = ftell(fp);
		fseek(fp, dataBase, SEEK_SET);
		if (fread(rootEntry_ptr, 1, 2, fp) != -1) {
			if (originClus % 2 == 0) {
				bytes = bytes >> 12;
				bytes = bytes << 12; // 保留高四位，低12位为0
				bytes = bytes | tempClus;
			}
			else {
				bytes = bytes << 12;
				bytes = bytes >> 12; // 保留低四位，高12位为0
				bytes = bytes | (tempClus << 4);
			}
			fseek(fp, (fatBase + originClus * 3 / 2), SEEK_SET);
			if (WriteToDisk_u16(bytes_ptr, 2, (fatBase + originClus * 3 / 2)) == 0) {
				return FALSE;
			}
			dataBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (tempClus - 2) * BytsPerSec;
			fseek(fp, dataBase, SEEK_SET);
			WriteToDisk_u8(setzero, BytsPerSec, dataBase); // 清0
			fseek(fp, dataBase, SEEK_SET);
			if (WriteToDisk_root(FileInfo, 32, dataBase) != 0) {
				success = TRUE;
			}
		}
	}
	return success;
}

void syncFat12() {
	printf("working...\n");
}

u16 getFATValue(u16 FstClus) {
	int fatBase = RsvdSecCnt * BytsPerSec;
	int fatPos = fatBase + FstClus * 3 / 2;
	int type;
	u16 bytes;
	u16* bytes_ptr = &bytes;
	if (FstClus % 2 == 0) {
		type = 0;
	}
	else {
		type = 1;
	}
	fseek(fp, fatPos, SEEK_SET);
	if (fread(bytes_ptr, 1, 2, fp) != -1) {
		if (type == 0) {
			bytes = bytes << 4;
			bytes = bytes >> 4;
		}
		else {
			bytes = bytes >> 4;
		}
		return bytes;
	}
	else {
		return 0xFFF;
	}
}

int getDOSDate(time_t ts) {
	struct tm *res;

	res = localtime(&ts);

	return (res->tm_year - 80) * 512 + (res->tm_mon + 1) * 32 + res->tm_mday;
}

int getDOSTime(time_t ts) {
	struct tm *res;

	res = localtime(&ts);

	return res->tm_hour * 2048 + res->tm_min * 32 + res->tm_sec / 2;
}

void Disk_Open() {
	int c, h, b;
	int flag = 0;
	fp = fopen("filesim", "rb");
	BPB_Read_Success = BPB_Read();
	if (BPB_Read_Success == 0) {
		printf("File Error! Abort!\n");
		exit(0);
	}
	fclose(fp);
	fp = fopen("filesim", "r");
	for (c = 0; c < C; c++) {
		for (h = 0; h < H; h++) {
			for (b = 0; b < B; b++) {
				ldiskr[c][h][b] = (Disk)malloc(sizeof(Block));
				ldiskr[c][h][b]->block_pos = ftell(fp);
				fread(ldiskr[c][h][b]->sector, 1, 512, fp);
				if (flag == 0) {
					//fseek(fp, -3, SEEK_CUR);
					flag = 1;
				}
			}
		}
	}
}

void Disk_Shutdown() {
	int c, h, b, i;
	fclose(fp);
	fp = fopen("filesim", "wb");
	for (c = 0; c < C; c++) {
		for (h = 0; h < H; h++) {
			for (b = 0; b < B; b++) {
				fwrite(ldiskr[c][h][b]->sector, 1, 512, fp);
			}
		}
	}
	fclose(fp);
}

void recursiveDeleteDirectory(u16 fClus) {
	u8 del = 0xE5;
	// 递归删除文件夹下的文件和目录
	// fClus 保存待删除文件夹首簇
	RootEntry fdd;
	RootEntry* fdd_ptr = &fdd;
	int fBase = (RsvdSecCnt + NumFATs * FATSz) * BytsPerSec + RootEntCnt * 32 + (fClus - 2) * BytsPerSec; // 找到该文件夹的扇区偏移
																										  // 遍历待删除目录下的所有目录项删除掉
	do {
		for (int k = 0; k < BytsPerSec / 32; k++) {
			fseek(fp, fBase, SEEK_SET);
			if (fread(fdd_ptr, 1, 32, fp) != -1) {
				// 文件就直接把第一字节改了就成
				if (fdd_ptr->DIR_Name[0] != (char)0xE5 && fdd_ptr->DIR_Name[0] != (char)0 && fdd_ptr->DIR_Name[0] != (char)0x2E) {
					if (fdd_ptr->DIR_Attr == 0x20) {
						fseek(fp, fBase, SEEK_SET);
						WriteToDisk_u8(&del, 1, fBase);
						recoverClus(fdd_ptr->DIR_FstClus); // 回收文件簇
					}
					else if (fdd_ptr->DIR_Attr == 0x10) {
						// 文件夹递归调用删除其底下的目录项
						fseek(fp, fBase, SEEK_SET);
						WriteToDisk_u8(&del, 1, fBase);
						recursiveDeleteDirectory(fdd_ptr->DIR_FstClus); // 递归调用
						recoverClus(fdd_ptr->DIR_FstClus); // 回收目录簇
					}
				}
			}
			fBase += 32;
		}
	} while ((fClus = getFATValue(fClus)) != 0xFFF);
}

void HelpDisplay() {
	char* Operation = (char*)malloc(sizeof(char));
	scanf("%s", Operation);
	if (strcmp(Operation, expression.ls) == 0) {
		printf("This command can display all the folder and file name in the disk.\n");
	}
	else if (strcmp(Operation, expression.mkfle) == 0) {
		printf("This command can create a file in the disk.\n");
	}
	else if (strcmp(Operation, expression.rmfle) == 0) {
		printf("This command can delete a specific file in the disk.\n");
	}
	else if (strcmp(Operation, expression.mkdir) == 0) {
		printf("This command can create a folder on the disk.\n");
	}
	else if (strcmp(Operation, expression.rmdir) == 0) {
		printf("This command can delete a folder on the disk.\n");
	}
	else if (strcmp(Operation, expression.edit) == 0) {
		printf("This command can edit a file on the disk.\n");
	}
	else if (strcmp(Operation, expression.disp) == 0) {
		printf("This command can display the content in a file.\n");
	}
	else {
		printf("Please enter the right command that you want to see.\n");
	}
}


int main() {
	DWORD filehandle;
	int c, h, b;
	int i;
	char *temp = (char*)malloc(sizeof(char));
	char dir[20] = "c:";
	char* file = (char*)malloc(sizeof(char));
	char flag = 0;
	char rBuffer[1025] = { 0 };
	char pBuffer[768] = { 0 };
	const char* enter = "\n";
	char* Operation = (char*)malloc(sizeof(char));
	printf("OS Lab5 文件系统实验\n");
	printf("孙睿 16281079 安全1601\n\n\n");
	while (1) {
		printf("FileSys > ");
		scanf("%s", Operation);
		if (strcmp(Operation, expression.ls) == 0) {
			Disk_Open();
			MyDisplay();
			Disk_Shutdown();
			continue;
		}
		if (strcmp(Operation, expression.mkfle) == 0) {
			Disk_Open();
			scanf("%s", file);
			filehandle = MyCreateFile(dir, file);
			if (filehandle != 0) {
				printf("Create successfully!\n");
			}
			else {
				printf("Create failed!\n");
			}
			Disk_Shutdown();
			continue;
		}
		if (strcmp(Operation, expression.rmfle) == 0) {
			Disk_Open();
			scanf("%s", file);
			filehandle = MyDeleteFile(dir, file);
			if (filehandle != 0) {
				printf("Delete successfully!\n");
			}
			else {
				printf("Delete failed!\n");
			}
			Disk_Shutdown();
			continue;
		}
		if (strcmp(Operation, expression.mkdir) == 0) {
			Disk_Open();
			scanf("%s", file);
			filehandle = MyCreateDirectory(dir, file);
			if (filehandle != 0) {
				printf("Create successfully!\n");
			}
			else {
				printf("Create failed!\n");
			}
			Disk_Shutdown();
			continue;
		}
		if (strcmp(Operation, expression.rmdir) == 0) {
			Disk_Open();
			scanf("%s", file);
			filehandle = MyDeleteDirectory(dir, file);
			if (filehandle != 0) {
				printf("Delete successfully!\n");
			}
			else {
				printf("Delete failed!\n");
			}
			Disk_Shutdown();
			continue;
		}
		if (strcmp(Operation, expression.edit) == 0) {
			Disk_Open();
			scanf("%s", file);
			filehandle = MyOpenFile(dir, file);
			if (filehandle != 0) {
				printf("Open successfully!\n");
			}
			else {
				printf("Open failed!\n");
				continue;
			}
			system("cls");
			while(1) {
				scanf("%s", temp);
				if (strcmp(temp, expression.exit) == 0) {
					break;
				}
				strcat(pBuffer, temp);
			}
			flag = MyWriteFile(filehandle, pBuffer, strlen(pBuffer));
			printf("操作系统A2018 文件系统实验\n");
			printf("CopyRight by 林子芃 15281266 计科1503\n\n\n");
			if (flag != -1) {
				printf("Write successfully!\n");
			}
			else {
				printf("Write failed!\n");
			}
			MyCloseFile(filehandle);
			Disk_Shutdown();
			flag = 0;
			continue;
		}
		if (strcmp(Operation, expression.disp) == 0) {
			Disk_Open();
			scanf("%s", file);
			filehandle = MyOpenFile(dir, file);
			if (filehandle != 0) {
				printf("Open successfully!\n");
			}
			else {
				printf("Open failed!\n");
				continue;
			}
			flag = MyReadFile(filehandle, rBuffer, 1024);
			printf("%s", rBuffer);
			printf("\n\n");
			if (flag != -1) {
				printf("Read successfully!\n");
			}
			else {
				printf("Read failed!\n");
			}
			MyCloseFile(filehandle);
			Disk_Shutdown();
			flag = 0;
			continue;
		}
		if (strcmp(Operation, expression.help) == 0) {
			HelpDisplay();
			continue;
		}
		if (strcmp(Operation, expression.exit) == 0) {
			printf("Thank you for playing ! Bye !\n");
			system("pause");
			break;
		}

	}
	//Disk_Open();
	//MyDisplay();
	/*
	filehandle = MyCreateFile(dir, file);
	Disk_Shutdown();
	Disk_Open();
	filehandle = MyOpenFile(dir, file);
	MyWriteFile(filehandle, pBuffer, 768);
	*/
	//MyDeleteFile(dir, file);
	/*
	MyCloseFile(filehandle);
	Disk_Shutdown();
	Disk_Open();
	filehandle = MyOpenFile(dir, file);
	MyReadFile(filehandle, &rBuffer, 1024);
	MyCloseFile(filehandle);
	printf("%s\n", rBuffer);
	//MyDisplay();
	*/
	//MyDisplay();
	//Disk_Shutdown();
	return 0;
}
