// HiddensRemover.cpp : 定义控制台应用程序的入口点。
//

#if defined(WIN32)

// Exclude rarely-used stuff from Windows headers
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif	//WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <io.h>

#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#else

#include <unistd.h>
#include <dirent.h>
#include <ftw.h>
#include <sys/param.h>

#endif

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>

#ifndef R_OK
#define R_OK		(4)		/* Test for read permission.  */
#endif

#ifndef W_OK
#define W_OK		(2)		/* Test for write permission.  */
#endif

#ifndef F_OK
#define F_OK		(0)		/* Test for existence.  */
#endif

#ifdef WIN32
#define PATH_SEPARATOR_CHAR				'\\'
#define PATH_SEPARATOR_STRING			"\\"
#else
#define PATH_SEPARATOR_CHAR				'/'
#define PATH_SEPARATOR_STRING			"/"
#endif

using namespace std;

//////////////////////////////////////////////////////////////////////////

#define EXEC_BINARY_NAME				"EasyLuaEncryptor"
#define EXEC_BINARY_VERSION				"0.0.1"

#define XXTEA_KEY						"2dxLua"
#define XXTEA_KEY_LENGTH				(sizeof(XXTEA_KEY) - 1)
#define XXTEA_SIGN						"XXTEA"
#define XXTEA_SIGN_LENGTH				(sizeof(XXTEA_SIGN) - 1)

//////////////////////////////////////////////////////////////////////////

#include "xxtea/xxtea.h"

static void PrintInfo(void)
{
	printf("************************************\r\n");
	printf("* %s  Ver. %s\r\n", EXEC_BINARY_NAME, EXEC_BINARY_VERSION);
	printf("* Powered by Xin Zhang\r\n");
	printf("* %s %s\r\n", __TIME__, __DATE__);
	printf("*\r\n");
	printf("* Usage:\r\n");
	printf("* ------\r\n");
	printf("* %s [-compile] <Dir>\r\n", EXEC_BINARY_NAME);
	printf("************************************\r\n");
}

static bool s_bCompileToByteCode = false;
static vector<string> s_vectorLuaFiles;

static void SearchLuaFilesInDirectory(string & strPath)
{
#ifdef WIN32

	string strToScan = strPath + "*.*";
	WIN32_FIND_DATAA FindFileData = { 0 };
	HANDLE hFind = ::FindFirstFileA(strToScan.c_str(), &FindFileData);

	if (INVALID_HANDLE_VALUE == hFind)
	{
		printf("Cannot be accessed: %s\r\n", strPath.c_str());
		return;
	}

	while (TRUE)
	{
		string strFileName = FindFileData.cFileName;

		if ('.' == strFileName[0])
		{
			//
		}
		else
		{
			bool bIsDir = (0 != (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));

			if (bIsDir)
			{
				string strSubDir = strPath + strFileName + PATH_SEPARATOR_STRING;
				SearchLuaFilesInDirectory(strSubDir);
			}
			else if (strFileName.length() > 4)
			{
				string strSubFile = strPath + strFileName;
				const char * pLast4Chars = &strSubFile.at(strSubFile.length() - 4);

				if (0 == memcmp(pLast4Chars, ".lua", 4))
				{
					s_vectorLuaFiles.push_back(strSubFile);
				}
			}
		}

		if (!FindNextFileA(hFind, &FindFileData))
		{
			break;
		}
	}

	FindClose(hFind);

#else

	DIR * dir;
	struct dirent * entry;
	struct stat statbuf;

	if (!(dir = opendir(strPath.c_str())))
	{
		return;
	}

	while ((entry = readdir(dir)))
	{
		string strFileName = entry->d_name;

		if ('.' == strFileName[0])
		{
			//
		}
		else
		{
			string strSub = strPath + entry->d_name;

			lstat(strSub.c_str(), &statbuf);

			bool bIsDir = S_ISDIR(statbuf.st_mode);

			if (bIsDir)
			{
				strSub += PATH_SEPARATOR_STRING;
				SearchLuaFilesInDirectory(strSub);
			}
			else if (strFileName.length() > 4)
			{
				const char * pLast4Chars = &strSub.at(strSub.length() - 4);

				if (0 == memcmp(pLast4Chars, ".lua", 4))
				{
					s_vectorLuaFiles.push_back(strSub);
				}
			}
		}
	}

	closedir(dir);

#endif
}

static unsigned char * ReadFile(const char * szFilePath, size_t * pFileLength)
{
	if (pFileLength)
	{
		pFileLength[0] = 0;
	}
	else
	{
		return nullptr;
	}

	//////////////////////////////////////////////////////////////////////////

	if (!szFilePath || !szFilePath[0])
	{
		return nullptr;
	}

	//////////////////////////////////////////////////////////////////////////

	FILE * fileToRead = fopen(szFilePath, "rb");

	if (!fileToRead)
	{
		printf("%s: fopen error.\r\n", __FUNCTION__);

		return nullptr;
	}

	//////////////////////////////////////////////////////////////////////////

	int nFeekResult = fseek(fileToRead, 0, SEEK_END);
	long nFileSize = ftell(fileToRead);
	unsigned char * pBuffer = nullptr;

	if (0 != nFeekResult)
	{
		printf("%s: fseek error.\r\n", __FUNCTION__);
	}
	else if (nFileSize < 0)
	{
		printf("%s: ftell error.\r\n", __FUNCTION__);
	}
	else
	{
		pFileLength[0] = nFileSize;

		if ((pBuffer = (unsigned char *)malloc(sizeof(unsigned char) * (nFileSize + 1))))
		{
			if (nFileSize)
			{
				fseek(fileToRead, 0, SEEK_SET);

				if (nFileSize == fread(pBuffer, sizeof(unsigned char), nFileSize, fileToRead))
				{
					pBuffer[nFileSize] = 0;
				}
				else
				{
					printf("%s: fread error.\r\n", __FUNCTION__);
					free(pBuffer);
					pBuffer = nullptr;
				}
			}
			else
			{
				// Empty file won't be read.
				pBuffer[nFileSize] = 0;
			}
		}
		else
		{
			printf("%s: malloc error.\r\n", __FUNCTION__);
		}
	}

	fclose(fileToRead);

	return pBuffer;
}

static bool WriteFile(const char * szFilePath, const unsigned char * pDataToWrite, size_t nFileLength)
{
	if (!szFilePath || !szFilePath[0])
	{
		return false;
	}

	//////////////////////////////////////////////////////////////////////////

	FILE * fileToWrite = fopen(szFilePath, "wb");

	if (!fileToWrite)
	{
		printf("%s: fopen error.\r\n", __FUNCTION__);

		return false;
	}

	//////////////////////////////////////////////////////////////////////////

	bool bRet = false;

	if (nFileLength && pDataToWrite)
	{
		if (nFileLength == fwrite(pDataToWrite, sizeof(unsigned char), nFileLength, fileToWrite))
		{
			bRet = true;
		}
		else
		{
			printf("%s: fwrite error.\r\n", __FUNCTION__);
		}
	}
	else
	{
		printf("%s: Nothing to write.\r\n", __FUNCTION__);
		bRet = true;
	}

	fclose(fileToWrite);

	return bRet;
}

bool Encrypt(string & strLuaFile)
{
	cout << strLuaFile << endl;

	const char * szLuaFile = strLuaFile.c_str();
	size_t nFileLength;
	unsigned char * pFileContent = ReadFile(szLuaFile, &nFileLength);

	if (!pFileContent)
	{
		return false;
	}

	if (nFileLength >= XXTEA_SIGN_LENGTH && 0 == memcmp(pFileContent, XXTEA_SIGN, XXTEA_SIGN_LENGTH))
	{
		free(pFileContent);
		pFileContent = nullptr;

		cout << strLuaFile << " already encrypted." << endl;

		return false;
	}

	xxtea_long nEncryptedDataLength;
	unsigned char * pEncryptedData = xxtea_encrypt(pFileContent, (xxtea_long)nFileLength, (unsigned char *)XXTEA_KEY, XXTEA_KEY_LENGTH, &nEncryptedDataLength);

	free(pFileContent);
	pFileContent = nullptr;

	if (!pEncryptedData)
	{
		return false;
	}

	unsigned char * pDataToWrite = (unsigned char *)malloc(XXTEA_SIGN_LENGTH + nEncryptedDataLength);

	if (!pDataToWrite)
	{
		free(pEncryptedData);
		pEncryptedData = nullptr;

		return false;
	}

	memcpy(pDataToWrite, XXTEA_SIGN, XXTEA_SIGN_LENGTH);
	memcpy(pDataToWrite + XXTEA_SIGN_LENGTH, pEncryptedData, nEncryptedDataLength);

	free(pEncryptedData);
	pEncryptedData = nullptr;

	bool bRet = WriteFile(szLuaFile, pDataToWrite, XXTEA_SIGN_LENGTH + nEncryptedDataLength);

	free(pDataToWrite);
	pDataToWrite = nullptr;

	return bRet;
}

int main(int argc, char * argv[])
{
	PrintInfo();

	string strRoot = "." PATH_SEPARATOR_STRING;;

	if (argc >= 2)
	{
		for (int i = 1; i < argc; i++)
		{
			string strArg(argv[i]);

			if ("-compile" == strArg)
			{
				s_bCompileToByteCode = true;
			}
			else
			{
				strRoot = strArg;
			}
		}
	}

	char cLastChar = strRoot[strRoot.length() - 1];

	if (PATH_SEPARATOR_CHAR != cLastChar && '/' != cLastChar)
	{
		strRoot += PATH_SEPARATOR_STRING;
	}

	if (-1 != access(strRoot.c_str(), R_OK))
	{
		SearchLuaFilesInDirectory(strRoot);
	}
	else
	{
		return -1;
	}

	size_t nFileCount = s_vectorLuaFiles.size();
	const unsigned int nCpuCoreNumber = thread::hardware_concurrency();
	bool * bThreadEnds = new bool[nCpuCoreNumber];

	for (unsigned int nCurrentThreadNumber = 0; nCurrentThreadNumber < nCpuCoreNumber; nCurrentThreadNumber++)
	{
		bThreadEnds[nCurrentThreadNumber] = false;

		std::thread([nCurrentThreadNumber, nCpuCoreNumber, nFileCount, &bThreadEnds](void)
		{
			unsigned int nCurrentLuaFileNumber = nCurrentThreadNumber;

			while (nCurrentLuaFileNumber < nFileCount)
			{
				if (!Encrypt(s_vectorLuaFiles[nCurrentLuaFileNumber]))
				{
					cout << "Failed: " << s_vectorLuaFiles[nCurrentLuaFileNumber] << endl;
				}

				nCurrentLuaFileNumber += nCpuCoreNumber;
			}

			bThreadEnds[nCurrentThreadNumber] = true;
		}).detach();
	}

	while (true)
	{
		this_thread::sleep_for(chrono::milliseconds(200));

		bool bAllThreadsEnd = true;

		for (unsigned int nCurrentThreadNumber = 0; nCurrentThreadNumber < nCpuCoreNumber; nCurrentThreadNumber++)
		{
			bAllThreadsEnd &= bThreadEnds[nCurrentThreadNumber];

			if (!bAllThreadsEnd)
			{
				break;
			}
		}

		if (bAllThreadsEnd)
		{
			break;
		}
	}

	delete[] bThreadEnds;

#if defined(WIN32) && (defined(DEBUG) || defined(_DEBUG))
	getchar();
#endif

	return 0;
}