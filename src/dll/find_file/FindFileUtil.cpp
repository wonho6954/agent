/*
 * Copyright (C) 2020-2025 ASHINi corp. 
 * 
 * This library is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU Lesser General Public 
 * License as published by the Free Software Foundation; either 
 * version 2.1 of the License, or (at your option) any later version. 
 * 
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
 * Lesser General Public License for more details. 
 * 
 * You should have received a copy of the GNU Lesser General Public 
 * License along with this library; if not, write to the Free Software 
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA 
 * 
 */


#include "stdafx.h"
#include "com_struct.h"
#include "com_struct_find_work.h"
#include "ThreadFinder.h"
#include "FindFileUtil.h"
//--------------------------------------------------------------

CFindFileUtil::CFindFileUtil(void)
{
	m_tASIDFFDLLUtil = NULL;
	m_tASIFIDLLUtil = NULL;
}
//--------------------------------------------------------------

CFindFileUtil::~CFindFileUtil(void)
{
	SAFE_DELETE(m_tASIDFFDLLUtil);
	SAFE_DELETE(m_tASIFIDLLUtil);
}
//--------------------------------------------------------------

INT32		CFindFileUtil::Init(PASI_FF_INIT_INFO pAfii)
{
	String strTFName;
	INT32 nTFMaxNum = 0;
	INT32 nCpuCount = 0;
	ASI_DFILE_FMT_INIT tADFI;
	if(pAfii == NULL)
		return -1;
	
	memcpy(&m_tAFII, pAfii, sizeof(m_tAFII));
	if(m_tAFII.szDocFileFmtDLLPath[0] == 0 || m_tAFII.szFileInfoDLLPath[0] == 0)	
		return -2;
	SetLogFileInfo(m_tAFII.szLogPath, m_tAFII.szLogFile, m_tAFII.nRemainLog);

	m_tASIDFFDLLUtil = new CASIDFFDLLUtil();
	if(m_tASIDFFDLLUtil == NULL)
		return -3;

	if(m_tASIDFFDLLUtil->LoadLibraryExt(m_tAFII.szDocFileFmtDLLPath) != 0)
	{
		SAFE_DELETE(m_tASIDFFDLLUtil);
		return -4;
	}

	m_tASIFIDLLUtil = new CASIFIDLLUtil();
	if(m_tASIFIDLLUtil == NULL)
	{
		SAFE_DELETE(m_tASIDFFDLLUtil);
		return -5;
	}
	
	if(m_tASIFIDLLUtil->LoadLibraryExt(m_tAFII.szFileInfoDLLPath) != 0)
	{
		SAFE_DELETE(m_tASIDFFDLLUtil);
		SAFE_DELETE(m_tASIFIDLLUtil);
		return -6;
	}

	strncpy(tADFI.szLogPath , m_tAFII.szLogPath, CHAR_MAX_SIZE-1);
	strncpy(tADFI.szLogFile, m_tAFII.szLogFile, CHAR_MAX_SIZE-1);
	tADFI.nRemainLog = m_tAFII.nRemainLog;
	m_tASIDFFDLLUtil->ASIDFF_SetDFFmtInit(&tADFI);
	
	nTFMaxNum = m_tAFII.nFinderThreadMaxNum;
	if(!nTFMaxNum)
	{
		nCpuCount = get_cpu_core_num();
		nTFMaxNum = 1;
		if(nCpuCount > 2)
			nTFMaxNum = nCpuCount - 2;
		WriteLogN("auto detect number of process in system : [%d]", nTFMaxNum);
	}

	m_nPreSearchLevel = 0;

	while(nTFMaxNum--)
	{
		strTFName = SPrintf("finder %.2d", nTFMaxNum);

		CThreadFinder* tTF = new CThreadFinder();
		if(tTF != NULL)
		{
			tTF->SetOwnerClass(this);
			tTF->CreateThreadExt(strTFName);
			m_tFinderThreadList.push_back((UINT64)tTF);
		}
	}

	return 0;
}
//--------------------------------------------------------------

INT32		CFindFileUtil::Release()
{
	{
		TListID64Itor begin, end;
		begin = m_tFinderThreadList.begin();	end = m_tFinderThreadList.end();
		for(begin; begin != end; begin++)
		{
			CThreadFinder* tTF = (CThreadFinder*)(*begin);
			if(tTF != NULL)
			{
				tTF->SetContinue(0);
				StopThread_Common(tTF);
				delete tTF;
			}
		}
	}

	{
		if(m_tASIFIDLLUtil)
			m_tASIFIDLLUtil->FreeLibraryExt();
		if(m_tASIDFFDLLUtil)
			m_tASIDFFDLLUtil->FreeLibraryExt();
	}

	{
		ClearFindSubDirItem();		WriteLogN("clear find sub dir items..");
		ClearFindFileDirItem();		WriteLogN("clear find find dir items..");
		ClearFindFileWork();		WriteLogN("clear find file work..");
	}
	return 0;
}
//--------------------------------------------------------------

INT32		CFindFileUtil::StopThread_Common(CThreadBase* tThreadObject, UINT32 nWaitTime)
{
	INT32 nLWaitTime = 0;
	INT32 nStatus = 0;

	if(tThreadObject == NULL)
		return 0;
	if(m_SemExt.CreateEvent() == FALSE)
		return -1;
	
	nLWaitTime = nWaitTime * 100;

	while(m_SemExt.WaitForSingleObject(100) == WAIT_TIMEOUT)
	{
		if(tThreadObject->IsRunThread() == 0)
			break;
		if(!nLWaitTime)
			break;

		--nLWaitTime;
	}
	m_SemExt.CloseHandle(); 
	if(!nLWaitTime)
	{
		WriteLogN("thread terminate fail : over wait time [%d]", nWaitTime);
		return -10;
	}
	return 0;
}
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------

INT32		CFindFileUtil::SetPreSearchLevel(UINT32 nLevel)
{
	m_nPreSearchLevel = nLevel;
	return 0;
}
//--------------------------------------------------------------------

UINT32		CFindFileUtil::GetPreSearchLevel(String strSearchPath)
{
	if(m_nPreSearchLevel)	return m_nPreSearchLevel;

	if(strSearchPath.empty())
		return 3;

	CTokenString Token((BYTE *)strSearchPath.c_str(), strSearchPath.length(), '/');
	INT32	nRtn = Token.GetRemainTokenCount();

	if(UINT32(nRtn) > m_tAFII.nAutoSearchDirLevel)
		nRtn = 0;
	else
		nRtn = m_tAFII.nAutoSearchDirLevel - nRtn;

	WriteLogN("auto pre-search level is : [%d/%d][%s]", nRtn, m_tAFII.nAutoSearchDirLevel, strSearchPath.c_str());
	return nRtn;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::AddSearchDirPath(UINT32 nOrderID, LPCTSTR lpSearchRoot)
{
	TMapIDListStrItor find = m_tSearchListMap.find(nOrderID);
	if(find == m_tSearchListMap.end())
	{
		TListStr tDirList;
		m_tSearchListMap[nOrderID] = tDirList;
		find = m_tSearchListMap.find(nOrderID);
	}

	find->second.push_back(lpSearchRoot);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::ClearSearchDirPath(UINT32 nOrderID)
{
	m_tSearchListMap.erase(nOrderID);
	return 0;
}
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------

INT32		CFindFileUtil::AddFileFindOption(UINT32 nOrderID, UINT32 nFindOption)
{
	m_tFindOptionMap[nOrderID] = nFindOption;
	WriteLogN("file find option : [%d][%d]", nOrderID, nFindOption);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::ClearFileFindOption(UINT32 nOrderID)
{
	m_tFindOptionMap.erase(nOrderID);
	return 0;
}
//--------------------------------------------------------------------

UINT32		CFindFileUtil::GetFileFindOption(UINT32 nOrderID)
{
	TMapIDItor find = m_tFindOptionMap.find(nOrderID);
	if(find == m_tFindOptionMap.end())	return 0;

	return find->second;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::SearchDirFile(UINT32 nOrderID)
{
	String strSR;
	INT32 nLen = 0;
	TListStrItor begin, end;
	TMapIDListStrItor find = m_tSearchListMap.find(nOrderID);
	if(find == m_tSearchListMap.end())	return -1;

	if(InitFindFileWork(nOrderID))
	{
		return -1;
	}

	SetFindFileWork_SearchPath(nOrderID, 0, find->second.size());

	begin = find->second.begin();	end = find->second.end();
	for(begin; begin != end; begin++)
	{
		strSR = _strlwr(begin->c_str());
		nLen = begin->length();
		if(nLen < 2)
			return -1;

		PreSearchDir(nOrderID, *begin);	
		SetFindFileWork_SearchPath(nOrderID, 1, 1);
	}
	return 0;	
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::SearchDirFileThread(UINT32 nOrderID)
{
	String strSR;
	INT32 nLen = 0;
	TListStrItor begin, end;
	TMapIDListStrItor find = m_tSearchListMap.find(nOrderID);
	if(find == m_tSearchListMap.end())	return -1;

	if(InitFindFileWork(nOrderID))
	{
		return -1;
	}

	INT32 nTRtn = -1;
	SetFindFileWork_SearchPath(nOrderID, 0, find->second.size());
	
	begin = find->second.begin();	end = find->second.end();
	for(begin; begin != end; begin++)
	{
		strSR = _strlwr(begin->c_str());
		nLen = begin->length();
		if(nLen < 2)
		{
			return -1;
		}

		if(PreSearchDirThread(nOrderID, *begin))
		{
			SetFindFileWork_SearchPath(nOrderID, 1, 1);
			continue;
		}

		nTRtn = 0;
		SetFindFileWork_SearchPath(nOrderID, 1, 1);
	}	
	return nTRtn;	
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::PreSearchDir(UINT32 nOrderID, String strRootPath)
{	
	CHAR szFindDir[MAX_PATH] = {0, };
	UINT32 nDirNum = 0;
	INT32 nSubDirSearch = 0;
	int nLen;
	TListFindFileItem tAFFIList;
	TListStr tNameList, tLastNameList;
	String strFindDirPath;
	nLen = strRootPath.length();
	
	if(strRootPath.length() < 2)
		return -1;
	
	strncpy(szFindDir, strRootPath.c_str(), MAX_PATH-1);

	if(szFindDir[nLen - 1] == '*')
	{
		nSubDirSearch = 2;
		szFindDir[nLen - 2] = 0;
	}
	strFindDirPath = SPrintf(szFindDir);
	Recursive_SearchDirFile(nOrderID, strFindDirPath, "", nSubDirSearch, nDirNum, tAFFIList);
	if(tAFFIList.size())
	{
		AddFindFileItemList(nOrderID, 0, tAFFIList);
	}
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::PreSearchDirThread(UINT32 nOrderID, String strRootPath)
{	
	CHAR szFindDir[MAX_PATH] = {0, };
	INT32 nSubDirSearch = 0;
	INT32 nLen = 0;
	INT32 nPreSubDirSearchLevel = 0;
	TListStr tNameList, tLastNameList;
	TListFindFileItem tFindFileItemList;
	String strFindDirPath;

	nLen = strRootPath.length();
	if(strRootPath.length() < 2)
		return -1;

	strncpy(szFindDir, strRootPath.c_str(), MAX_PATH-1);
	if(szFindDir[nLen - 1] == '*')
	{
		nPreSubDirSearchLevel = GetPreSearchLevel(strRootPath);
		nSubDirSearch = 1;
		szFindDir[nLen - 2] = 0;
	}

	if(DirectoryExists(szFindDir) == FALSE)
	{
		WriteLogE("invalid directory path : [%s]", szFindDir);
		return -3;
	}

	strFindDirPath = SPrintf(szFindDir);

	Recursive_SearchDir(nOrderID, strFindDirPath, "", nPreSubDirSearchLevel, tNameList, &tLastNameList, &tFindFileItemList);
	WriteLogN("pre search dir thread : [%s] name (%d) last name (%d) file (%d)", szFindDir, tNameList.size(), tLastNameList.size(), tFindFileItemList.size());
	SetFindFileWork_TotalDir(nOrderID, tNameList.size());
	AddFindSubDirItem(nOrderID, nSubDirSearch, tLastNameList);
	AddFindFileItemList(nOrderID, tNameList.size(), tFindFileItemList);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::Recursive_SearchDir(UINT32 nOrderID, String strRootPath, String strSubDir, INT32 nSubDirSearch, TListStr& tNameList, TListStr* tLastNameList, TListFindFileItem* tFindFileItemList, PUINT32 pnContinue)
{	
	INT32 nExceptAllPath = (GetFileFindOption(nOrderID) & ASI_FF_FIND_OPTION_EXCLUDE_ALL_PATH);
	String strChkDirA;
	String strSubAddPathA;
	DIR *pDir = NULL;
	struct dirent *pDirEnt = NULL;
	String strFileNameA;
	UINT32 nMatchType = ASI_FF_FILE_FIND_TYPE_PATTERN;
	UINT32 dwFileLen = 0;
	BOOL bMatchCaseRetrun = FALSE;
	BOOL nContinue = TRUE;
	FIND_FILE_ITEM tAFFI;	
	String strFindDirA;

	memset(&tAFFI, 0, sizeof(tAFFI));
	if(strSubDir.empty() == FALSE)
	{
		strFindDirA = SPrintf("%s/%s", strRootPath.c_str(), strSubDir.c_str());	
	}
	else
	{
		strFindDirA = strRootPath;
	}

	strChkDirA = strFindDirA;
	if (nExceptAllPath)
	{
		if(IsExistExceptDir(nOrderID, strChkDirA.c_str()))
		{
			nSubDirSearch = 1;
			tNameList.push_back(strChkDirA);
		}
	}
	else
	{
		if(IsExistExceptDir(nOrderID, strChkDirA.c_str()))
		{
			WriteLogN("exist dir mask : [%s]", strChkDirA.c_str());
			return 0;
		}
		if(!nSubDirSearch)
		{
			if(tLastNameList)
				(*tLastNameList).push_back(strChkDirA);
			else
				tNameList.push_back(strChkDirA);
			return 0;
		}
		tNameList.push_back(strChkDirA);
	}

	strChkDirA = strFindDirA;

	pDir = opendir(strFindDirA.c_str());
	if (pDir == NULL)
	{
		return 0 ;
	}

	while((pnContinue ? *pnContinue : 1) && (pDirEnt = readdir(pDir)) != NULL && nContinue)
	{			
		if(_stricmp(pDirEnt->d_name, ".") && _stricmp(pDirEnt->d_name, ".."))
		{
			strFileNameA = String(pDirEnt->d_name);
			if(DT_DIR == pDirEnt->d_type)
			{
				if(nSubDirSearch)
				{
					strSubAddPathA = (strSubDir.empty() ? strFileNameA : strSubDir + "/" + strFileNameA);
					Recursive_SearchDir(nOrderID, strRootPath, strSubAddPathA, (nSubDirSearch - 1), tNameList, tLastNameList, tFindFileItemList, pnContinue);	
				}
			}
			else if(tFindFileItemList)
			{
				if (nExceptAllPath)
					bMatchCaseRetrun = TRUE;
				
				if(!nExceptAllPath && IsExistExceptDirFileMask(nOrderID, strChkDirA, strFileNameA) || (nExceptAllPath && !IsExistExceptDir(nOrderID, strChkDirA.c_str())))
				{
					continue;
				}
				else if(IsExistFileMask(nOrderID, strChkDirA, strFileNameA, nMatchType) && IsExistFileDateTime(nOrderID, strChkDirA, strFileNameA))
				{
					strSubAddPathA = (strSubDir.empty() ? strRootPath + "/" + strFileNameA : strRootPath + "/" + strSubDir + "/" + strFileNameA);
					get_file_size(strSubAddPathA.c_str(), &dwFileLen);
					tAFFI.nFileSize		= dwFileLen;
					tAFFI.strFilePathW	= ConvertWideString(strChkDirA);
					tAFFI.strFileNameW	= ConvertWideString(strFileNameA);
					tAFFI.strFilePath	= strChkDirA;
					tAFFI.strFileName	= strFileNameA;
					tAFFI.nFindType		= nMatchType;

					tFindFileItemList->push_back(tAFFI);
					if(tFindFileItemList->size() == 10)
					{
						AddFindFileItemList(nOrderID, 0, *tFindFileItemList); 
						tFindFileItemList->clear();
					}					
				}
			} 
		}
	}	

	closedir(pDir);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::Recursive_SearchFile(UINT32 nOrderID, String strSearchPath, TListFindFileItem& tFindFileItemList, PUINT32 pnContinue)
{
	String strDirA;	
	String strFileNameA;
	String strChkDirA = strSearchPath;
	DIR *pDir = NULL;
	struct dirent *pDirEnt = NULL;
	BOOL nContinue = TRUE;
	UINT32 nMatchType = 0;
	UINT32 dwFileLen = 0;
	FIND_FILE_ITEM tAFFI;

	memset(&tAFFI, 0, sizeof(tAFFI));
	pDir = opendir(strChkDirA.c_str());
	if (pDir == NULL)
	{
		return 0 ;
	}

	while((pnContinue ? *pnContinue : 1) && (pDirEnt = readdir(pDir)) != NULL && nContinue)
	{
		if(!_stricmp(pDirEnt->d_name, ".") || !_stricmp(pDirEnt->d_name, ".."))
		{
			continue;
		}
		strFileNameA = String(pDirEnt->d_name);
		nMatchType = ASI_FF_FILE_FIND_TYPE_PATTERN;
		if(DT_DIR == pDirEnt->d_type)
		{
			continue;
		}
		else if(IsExistExceptDirFileMask(nOrderID, strChkDirA, strFileNameA))
		{
			continue;
		}
		else if(!IsExistFileMask(nOrderID, strChkDirA, strFileNameA, nMatchType))
		{
			continue;
		}
		else if(!IsExistFileDateTime(nOrderID, strChkDirA, strFileNameA))
		{
			continue;
		}
		else
		{	
			strDirA = strChkDirA + "/" + strFileNameA;
			get_file_size(strDirA.c_str(), &dwFileLen);
			tAFFI.nFileSize		= dwFileLen;
			tAFFI.strFilePathW	= ConvertWideString(strChkDirA);
			tAFFI.strFileNameW	= ConvertWideString(strFileNameA);
			tAFFI.strFilePath	= strChkDirA;
			tAFFI.strFileName	= strFileNameA;
			tAFFI.nFindType		= nMatchType;
			tFindFileItemList.push_back(tAFFI);

			if(m_tAFII.nOnceFindFileNum == tFindFileItemList.size())
			{
				AddFindFileItemList(nOrderID, 0, tFindFileItemList);
				tFindFileItemList.clear(); 
			}
		}
	}

	closedir(pDir);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::Recursive_SearchDirFile(UINT32 nOrderID, String strSearchPath, String strSubDir, INT32 nSubDirSearch, UINT32& nDirNum, TListFindFileItem& tFindFileItemList)
{
	String strFindDirA;
	String strChkDirA;
	String strFileNameA;
	String strPathA;
	UINT32 nMatchType = 0;
	DIR *pDir = NULL;
	struct dirent *pDirEnt = NULL;
	BOOL nContinue = TRUE;
	UINT32 dwFileLen = 0;
	FIND_FILE_ITEM tAFFI;
	memset(&tAFFI, 0, sizeof(tAFFI));
	if(strSubDir.empty() == FALSE)
	{
		strFindDirA = SPrintf("%s/%s", strSearchPath.c_str(), strSubDir.c_str());
	}
	else
	{
		strFindDirA = strSearchPath;
	}
	
	strChkDirA = strFindDirA;
	
	if(IsExistExceptDir(nOrderID, strChkDirA.c_str()))
	{
		WriteLogN("except dir : [%d][%s]", nOrderID, strChkDirA.c_str());
		return 0;
	}
	nDirNum++;

	pDir = opendir(strChkDirA.c_str());
	if (pDir == NULL)
	{
		WriteLogE("[Recursive_SearchDirFile] fail to open %s (%u)", strChkDirA.c_str(), errno);
		return 0 ;
	}	

	while((pDirEnt = readdir(pDir)) != NULL && nContinue)
	{			
		if(_stricmp(pDirEnt->d_name, ".") && _stricmp(pDirEnt->d_name, ".."))
		{
			strFileNameA = String(pDirEnt->d_name);
			nMatchType = ASI_FF_FILE_FIND_TYPE_PATTERN;
			
			if(DT_DIR == pDirEnt->d_type)
			{
				if(nSubDirSearch)
				{
					strPathA = (strSubDir.empty() ? strFileNameA : strSubDir + "/" + strFileNameA);
					Recursive_SearchDirFile(nOrderID, strSearchPath, strPathA, nSubDirSearch, nDirNum, tFindFileItemList);	
				}
			}
			else if(IsExistExceptDirFileMask(nOrderID, strChkDirA, strFileNameA))
			{
				continue;
			}
			else if(!IsExistFileMask(nOrderID, strChkDirA, strFileNameA, nMatchType) || !IsExistFileDateTime(nOrderID, strChkDirA, strFileNameA))
			{
				continue;
			}
			else
			{
				strPathA = strChkDirA + "/" + strFileNameA;
				get_file_size(strPathA.c_str(), &dwFileLen);
				tAFFI.nFileSize		= dwFileLen;
				tAFFI.strFilePathW	= ConvertWideString(strChkDirA);
				tAFFI.strFileNameW	= ConvertWideString(strFileNameA);
				tAFFI.strFilePath	= strChkDirA;
				tAFFI.strFileName	= strFileNameA;
				tAFFI.nFindType		= nMatchType;
				tFindFileItemList.push_back(tAFFI);
			}
		}
	}
	
	closedir(pDir);
	return 0;
}
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------

INT32		CFindFileUtil::AddFileMask(UINT32 nOrderID, LPCTSTR lpFileMask)
{
	TMapStr tKeyMap;
	CHAR szFileExt[MAX_PATH] = {0, };
	INT32 nFileLen = 0;
	if(!lpFileMask || lpFileMask[0] == 0)
		return -1;
	
	strncpy(szFileExt, lpFileMask, MAX_PATH-3);
	szFileExt[MAX_PATH-3] = 0;
	_strlwr(szFileExt);
	nFileLen = (INT32)strlen(szFileExt);
	if(nFileLen < 1)
		return -2;

	if(szFileExt[0] != '*')
	{
		ReverseLPTSTR(szFileExt);
		strcat(szFileExt, ".");
		strcat(szFileExt, "*");
		szFileExt[MAX_PATH-1] = 0;
		ReverseLPTSTR(szFileExt);
	}

	TMapIDMapStrItor find = m_tFileMaskMap.find(nOrderID);
	if(find == m_tFileMaskMap.end())
	{
		m_tFileMaskMap[nOrderID] = tKeyMap;
		find = m_tFileMaskMap.find(nOrderID);
	}

	CTokenString Token(lpFileMask, nFileLen, '.', 1);
	String strDFFmt = Token.NextToken();

	find->second[szFileExt] = strDFFmt;
	WriteLogN("file mask filter : [%d][%s]:[%s]", nOrderID, szFileExt, strDFFmt.c_str());
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::DelFileMask(UINT32 nOrderID, LPCTSTR lpFileMask)
{
	CHAR szFileExt[MAX_PATH] = {0, };
	INT32 nFileLen = 0;

	if(!lpFileMask || lpFileMask[0] == 0)
		return -1;

	strncpy(szFileExt, lpFileMask, MAX_PATH-3);
	szFileExt[MAX_PATH-3] = 0;
	_strlwr(szFileExt);
	
	nFileLen = (INT32)strlen(szFileExt);
	if(nFileLen < 1)
		return -2;

	if(szFileExt[0] != '*')
	{
		ReverseLPTSTR(szFileExt);
		strcat(szFileExt, ".");
		strcat(szFileExt, "*");
		szFileExt[MAX_PATH-1] = 0;
		ReverseLPTSTR(szFileExt);
	}

	TMapIDMapStrItor find = m_tFileMaskMap.find(nOrderID);
	if(find == m_tFileMaskMap.end())
	{
		return 0;
	}

	TMapStrItor begin, end;
	begin = find->second.begin();	end = find->second.end();
	for(begin; begin != end; )
	{
		if(_stricmp(begin->first.c_str(), szFileExt))
		{
			begin++;
		}
		else
		{
			find->second.erase(begin);
			break;
		}
	}
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::ClearFileMask(UINT32 nOrderID)
{
	TMapIDMapStrItor find = m_tFileMaskMap.find(nOrderID);
	if(find == m_tFileMaskMap.end())
	{
		return 0;
	}

	find->second.clear();
	m_tFileMaskMap.erase(nOrderID);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::IsExistFileMask(UINT32 nOrderID, String strFilePath, String strFileName, UINT32& nMatchType)
{
	String strFileNameA;
	String strChkFileNameA;
	TMapIDMapStrItor find = m_tFileMaskMap.find(nOrderID);
	if(find == m_tFileMaskMap.end())
	{
		return 1;
	}

	if(find->second.empty())
		return 1;

	strFileNameA = strFileName;

	TMapStrItor begin, end;
	begin = find->second.begin();	end = find->second.end();
	for(begin; begin != end; begin++)
	{
		if(StringMatchSpec(strFileNameA.c_str(), (begin->first.c_str())))
		{
			nMatchType = ASI_FF_FILE_FIND_TYPE_PATTERN;
			return 1;
		}
	}

	if(GetFileFindOption(nOrderID) & ASI_FF_FIND_OPTION_USED_DOC_FILE_FORMAT)
	{
		strChkFileNameA = SPrintf("%s%s", strFilePath.c_str(), strFileName.c_str());
		if(IsExistFileMaskByDFF(nOrderID, strChkFileNameA))
		{
			nMatchType = ASI_FF_FILE_FIND_TYPE_DOC_FILE_FORMAT;
			return 1;
		}
	}

	return 0;
}
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------

INT32		CFindFileUtil::IsExistFileMaskByDFF(UINT32 nOrderID, String strFileFullName)
{
	ASI_DFILE_FMT_INFO tADFI;
	char acLogMsg[MAX_LOGMSG+1] = {0,};
	if(m_tASIFIDLLUtil->ASIFI_GetFileElfMagic((char *)strFileFullName.c_str()))	
	{
		return 0;
	}

	strncpy(tADFI.szFileName, strFileFullName.c_str(), CHAR_MAX_SIZE-1);
	tADFI.szFileName[CHAR_MAX_SIZE-1] = '\0';
	mbstowcs((wchar_t*)tADFI.wszFileName, tADFI.szFileName, CHAR_MAX_SIZE-1);
	tADFI.wszFileName[CHAR_MAX_SIZE-1] = L'\0';

	if(m_tASIDFFDLLUtil->ASIDFF_GetDFFmtInfo(&tADFI, acLogMsg) != 0)
	{
		if(acLogMsg[0] != 0)
			WriteLogE("fail to get file fmt info : %s", acLogMsg);
	}
	if(tADFI.nFmtType == ASIDFF_FILE_FMT_TYPE_UNKNOW)
		return 0;
	if(tADFI.szFmtType[0] == 0)
		return 0;

	TMapIDMapStrItor find = m_tFileMaskMap.find(nOrderID);
	if(find == m_tFileMaskMap.end())
	{
		return 1;
	}

	if(find->second.empty())
		return 1;

	TMapStrItor begin, end;
	begin = find->second.begin();	end = find->second.end();
	for(begin; begin != end; begin++)
	{
		if(!_stricmp(begin->second.c_str(), tADFI.szFmtType))
		{
			return 1;
		}
	}
	return 0;
}
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------

INT32		CFindFileUtil::AddExceptDir(UINT32 nOrderID, LPCTSTR lpDirPath)
{
	String strFileMask;
	TMapStrID tKeyMap;
	INT32 nIncludeSubPath = 0;
	INT32 nLen = 0;
	char *pExt = NULL;
	CHAR szDirPath[MAX_PATH] = {0, };
	if(!lpDirPath || lpDirPath[0] == 0)
		return -1;


	nLen = strlen(lpDirPath);
	if(nLen < 3)
		return -2;

	strncpy(szDirPath, lpDirPath, MAX_PATH-1);
	szDirPath[MAX_PATH-1] = 0;
	_strlwr(szDirPath);
	pExt = strrchr(szDirPath, '.');
	if(pExt != NULL)
		strFileMask = SPrintf("%s", pExt);
	if(!strFileMask.empty())
	{
		return AddExceptDirFileMask(nOrderID, lpDirPath);
	}
	else if(szDirPath[nLen - 1] == '*')
	{
		szDirPath[nLen - 2] = 0;
		nIncludeSubPath = 1;
	}

	TMapIDMapStrIDItor find = m_tExceptDirMap.find(nOrderID);
	if(find == m_tExceptDirMap.end())
	{
		m_tExceptDirMap[nOrderID] = tKeyMap;
		find = m_tExceptDirMap.find(nOrderID);
	}

	find->second[szDirPath] = nIncludeSubPath;
	WriteLogN("file exclude dir filter : [%d][%s]", nOrderID, lpDirPath);

	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::DelExceptDir(UINT32 nOrderID, LPCTSTR lpDirPath)
{
	String strFileMask;
	INT32 nIncludeSubPath = 0;
	INT32 nLen = 0;
	char *pExt = NULL;
	CHAR szDirPath[MAX_PATH] = {0, };
	if(!lpDirPath || lpDirPath[0] == 0)
		return -1;

	nLen = strlen(lpDirPath);

	if(nLen < 3)
		return -2;

	strncpy(szDirPath, lpDirPath, MAX_PATH-1);
	szDirPath[MAX_PATH-1] = 0;
	_strlwr(szDirPath);
	
	pExt = strrchr(szDirPath, '.');
	if(pExt != NULL)
		strFileMask = SPrintf("%s", pExt);

	if(!strFileMask.empty())
	{
		return DelExceptDirFileMask(nOrderID, lpDirPath);
	}
	if(szDirPath[nLen - 1] == '*')
	{
		szDirPath[nLen - 2] = 0;
	}

	TMapIDMapStrIDItor find = m_tExceptDirMap.find(nOrderID);
	if(find == m_tExceptDirMap.end())
	{
		return 0;
	}

	find->second.erase(szDirPath);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::ClearExceptDir(UINT32 nOrderID)
{
	TMapIDMapStrIDItor find = m_tExceptDirMap.find(nOrderID);
	if(find == m_tExceptDirMap.end())
	{
		return 0;
	}

	find->second.clear();	
	m_tExceptDirMap.erase(nOrderID);

	ClearExceptDirFileMask(nOrderID);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::IsExistExceptDir(UINT32 nOrderID, LPCTSTR lpDirPath)
{
	TMapIDMapStrIDItor find = m_tExceptDirMap.find(nOrderID);
	if(find == m_tExceptDirMap.end())
	{
		return 0;
	}

	TMapStrIDItor begin, end;
	begin = find->second.begin();	end = find->second.end();
	for(begin; begin != end; begin++)
	{
		if(StringMatchSpec(lpDirPath, begin->first.c_str()))
		{
			return 1;			
		}
	}
	return 0;
}
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------

INT32		CFindFileUtil::AddExceptDirFileMask(UINT32 nOrderID, LPCTSTR lpDirPathFileMask)
{
	String strFileMask;
	INT32 nIncludeSubPath = 0;
	INT32 nLen = 0;
	char *pExt = NULL;
	TMapIDMapStrItor find;
	TMapStr tKeyMap;
	CHAR szDirPath[MAX_PATH] = {0, };
	if(!lpDirPathFileMask || lpDirPathFileMask[0] == 0)
		return -1;

	nLen = strlen(lpDirPathFileMask);
	if(nLen < 3)
		return -2;

	strncpy(szDirPath, lpDirPathFileMask, MAX_PATH-1);
	szDirPath[MAX_PATH-1] = 0;
	_strlwr(szDirPath);

	pExt = strrchr(szDirPath, '.');
	if(pExt != NULL)
		strFileMask = SPrintf("%s", pExt);

	if(!strFileMask.empty())
	{
		szDirPath[nLen - strFileMask.length() - 1] = 0;
		strFileMask = SPrintf("%s", &pExt[1]);
	}

	find = m_tExceptDirFileMaskMap.find(nOrderID);
	if(find == m_tExceptDirFileMaskMap.end())
	{
		m_tExceptDirFileMaskMap[nOrderID] = tKeyMap;
		find = m_tExceptDirFileMaskMap.find(nOrderID);
	}

	find->second[szDirPath] = strFileMask;
	WriteLogN("file exclude dir file mask filter : [%d][%s]", nOrderID, lpDirPathFileMask);

	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::DelExceptDirFileMask(UINT32 nOrderID, LPCTSTR lpDirPathFileMask)
{
	String strFileMask;
	INT32 nIncludeSubPath = 0;
	INT32 nLen = 0;
	char *pExt = NULL;
	CHAR szDirPath[MAX_PATH] = {0, };
	if(!lpDirPathFileMask || lpDirPathFileMask[0] == 0)
		return -1;

	nLen = strlen(lpDirPathFileMask);
	if(nLen < 3)
		return -2;

	strncpy(szDirPath, lpDirPathFileMask, MAX_PATH-1);
	_strlwr(szDirPath);
	pExt = strrchr(szDirPath, '.');
	if(pExt != NULL)
		strFileMask = SPrintf("%s", pExt);
	if(!strFileMask.empty())
	{
		szDirPath[nLen - strFileMask.length() - 1] = 0;
	}

	TMapIDMapStrItor find = m_tExceptDirFileMaskMap.find(nOrderID);
	if(find == m_tExceptDirFileMaskMap.end())
	{
		return 0;
	}

	find->second.erase(szDirPath);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::ClearExceptDirFileMask(UINT32 nOrderID)
{
	TMapIDMapStrItor find = m_tExceptDirFileMaskMap.find(nOrderID);
	if(find == m_tExceptDirFileMaskMap.end())
	{
		return 0;
	}

	find->second.clear();	
	m_tExceptDirFileMaskMap.erase(nOrderID);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::IsExistExceptDirFileMask(UINT32 nOrderID, String strFilePath, String strFileName)
{
	TMapStrItor begin, end;
	String strFileMask;
	char *pExt = NULL;
	TMapIDMapStrItor find = m_tExceptDirFileMaskMap.find(nOrderID);
	if(find == m_tExceptDirFileMaskMap.end())
	{
		return 0;
	}

	if(find->second.empty())
		return 1;

	pExt = strrchr((char *)strFileName.c_str(), '.');
	if(pExt != NULL)
	{
		strFileMask = SPrintf("%s", &pExt[1]);
	}

	begin = find->second.begin();	end = find->second.end();
	for(begin; begin != end; begin++)
	{
		if(StringMatchSpec(strFilePath.c_str(), begin->first.c_str()) && StringMatchSpec(strFileMask.c_str(), begin->second.c_str()))
			return 1;
	}
	return 0;
}

//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------

INT32		CFindFileUtil::AddFileDateTime(UINT32 nOrderID, UINT32 nType, UINT32 nChkDT)
{
	TMapID tFDMap;
	TMapIDMapItor find = m_tFileDTMap.find(nOrderID);
	if(find == m_tFileDTMap.end())
	{
		m_tFileDTMap[nOrderID] = tFDMap;
		find = m_tFileDTMap.find(nOrderID);
	}

	find->second[nType] = nChkDT;
	WriteLogN("file date time filter : [%d][%d][%d]", nOrderID, nType, nChkDT);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::DelFileDateTime(UINT32 nOrderID, UINT32 nType)
{
	TMapIDMapItor find = m_tFileDTMap.find(nOrderID);
	if(find == m_tFileDTMap.end())
	{
		return 0;
	}

	find->second.erase(nType);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::ClearFileDateTime(UINT32 nOrderID)
{
	TMapIDMapItor find = m_tFileDTMap.find(nOrderID);
	if(find == m_tFileDTMap.end())
	{
		return 0;
	}

	find->second.clear();	
	m_tFileDTMap.erase(nOrderID);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::IsExistFileDateTime(UINT32 nOrderID, String strFilePath, String strFileName)
{
	String strChkFileNameA;
	UINT32 nCurDT = 0;
	UINT32 nCurCDT = 0, nCurADT = 0, nCurMDT = 0;
	UINT32 nExistInfo = 0;
	TMapIDItor begin, end;
	INT32 nRetVal = 0;

	//for test
	return 1;

	TMapIDMapItor find = m_tFileDTMap.find(nOrderID);
	if(find == m_tFileDTMap.end())
	{
		return 1;
	}

	if(find->second.empty())
		return 1;

	if(strFilePath.length() == 0)
	{
		WriteLogE("[IsExistFileDateTime] invalid input data");
		return 0;	
	}
		
	if(strFileName.length() != 0)
		strChkFileNameA = SPrintf("%s/%s", strFilePath.c_str(), strFileName.c_str());
	else
		strChkFileNameA = strFilePath;


	nCurDT = GetCurrentDateTimeInt();

	nRetVal = GetFileTimeInfo(strChkFileNameA.c_str(), &nCurCDT, &nCurMDT, &nCurADT);
	if(nRetVal != 0)
	{
		WriteLogE("[IsExistFileDateTime] fail to get file time %s (%d)", strChkFileNameA.c_str(), nRetVal);
		return 0;	
	}

	begin = find->second.begin();	end = find->second.end();
	for(begin; begin != end && !nExistInfo; begin++)
	{
		switch(begin->first)
		{
			case ASI_FF_FILE_DT_CHK_TYPE_CREATE:
			{
				if(begin->second == 0 || nCurCDT && nCurCDT < (nCurDT - begin->second))
				{
					nExistInfo = 1;
				}
				break;
			}
			case ASI_FF_FILE_DT_CHK_TYPE_ACCESS:
			{
				if(begin->second == 0 || nCurADT && nCurADT < (nCurDT - begin->second))
				{
					nExistInfo = 1;
				}
				break;
			}
			case ASI_FF_FILE_DT_CHK_TYPE_WRITE:
			{
				if(begin->second == 0 || nCurMDT && nCurMDT < (nCurDT - begin->second))
				{
					nExistInfo = 1;
				}
				break;
			}
		}
	}

	return nExistInfo;
}
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------

INT32		CFindFileUtil::StringMatchSpec(LPCTSTR pszTarget, LPCTSTR pszSpec) 
{
	const char *cp = NULL, *mp = NULL;
	if(pszTarget == NULL || pszSpec == NULL)
	{
		return 0;
	}
	while (*pszTarget)
	{
		if (*pszSpec == '*') 
		{
			if (!*++pszSpec) 
			{
				return 1;
			}
			mp = pszSpec;
			cp = pszTarget+1;
		} 
		else if (((TOLOWER(*pszSpec) == TOLOWER(*pszTarget)) && (*pszSpec != '#')) || (*pszSpec == '?') || ((*pszSpec == '#') && isdigit(*pszTarget))) 
		{
			pszSpec++;
			pszTarget++;
		} 
		else 
		{
			if (mp)
			{
				pszSpec = (LPTSTR)mp;
				pszTarget = (LPTSTR)cp++;
			}
			else
			{
				return 0;
			}
		}
	}

	while (*pszSpec == '*')
	{
		pszSpec++;
	}
	return !*pszSpec;
}
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------

INT32		CFindFileUtil::AddFindSubDirItem(UINT32 nOrderID, UINT32 nSubSearch, TListStr& tNameList)
{
	TListStrItor begin, end;
	PFIND_FILE_WORK pAFFI = NULL;
	FIND_DIR_ITEM tFDI;
	memset(&tFDI, 0, sizeof(tFDI));
	m_tFindSubDirItemMutex.Lock();
	begin = tNameList.begin();	end = tNameList.end();
	for(begin; begin != end; begin++)
	{
		tFDI.nOrderID		= nOrderID;
		tFDI.strSearchDir	= *begin;
		tFDI.strSearchDirW = ConvertWideString(tFDI.strSearchDir);
		tFDI.nSubSearch		= nSubSearch;
		m_tFindSubDirItemList.push_back(tFDI);
	}

	pAFFI = GetFindFileWork(nOrderID);
	if(pAFFI)
	{
		pAFFI->nDirSubSearchNum += tNameList.size();
	}

	m_tFindSubDirItemMutex.UnLock();
	return 0;
}
//-------------------------------------------------------------

INT32		CFindFileUtil::GetFindSubDirItem(FIND_DIR_ITEM& tFDI)
{
	INT32 nRtn = 0;
	TListFindDirItemItor begin, end;
	m_tFindSubDirItemMutex.Lock();
	begin = m_tFindSubDirItemList.begin();		end = m_tFindSubDirItemList.end();
	if(begin != end)
	{
		tFDI = *begin;
		m_tFindSubDirItemList.pop_front();
		nRtn = 1;
	}
	m_tFindSubDirItemMutex.UnLock();
	return nRtn;
}
//-------------------------------------------------------------

INT32		CFindFileUtil::ClearFindSubDirItem()
{
	m_tFindSubDirItemMutex.Lock();
	m_tFindSubDirItemList.clear();
	m_tFindSubDirItemMutex.UnLock();
	return 0;
}
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------

INT32		CFindFileUtil::AddFindFileDirItem(UINT32 nOrderID, TListStr& tNameList, PUINT32 pnWorkNum)
{
	TListStrItor begin, end;
	FIND_DIR_ITEM tFDI;
	m_tFindFileDirItemMutex.Lock();
	begin = tNameList.begin();	end = tNameList.end();
	for(begin; begin != end; begin++)
	{
		tFDI.nOrderID		= nOrderID;
		tFDI.strSearchDir	= *begin;	
		tFDI.strSearchDirW	= ConvertWideString(tFDI.strSearchDir);
		m_tFindFileDirItemList.push_back(tFDI);
	}
	if(pnWorkNum)	
		*pnWorkNum = m_tFindFileDirItemList.size();

	m_tFindFileDirItemMutex.UnLock();
	return 0;	
}
//-------------------------------------------------------------

INT32		CFindFileUtil::GetFindFileDirItem(FIND_DIR_ITEM& tFDI)
{
	INT32 nRtn = 0;
	TListFindDirItemItor begin, end;
	m_tFindFileDirItemMutex.Lock();
	begin = m_tFindFileDirItemList.begin();		end = m_tFindFileDirItemList.end();
	if(begin != end)
	{
		tFDI = *begin;
		m_tFindFileDirItemList.pop_front();
		nRtn = 1;
	}
	m_tFindFileDirItemMutex.UnLock();
	return nRtn;
}
//-------------------------------------------------------------

INT32		CFindFileUtil::GetFindFileDirItem(TListFindDirItem& tSFDWList, UINT32& nOrderID, UINT32 nLimited)
{
	INT32 nRtn = 0;
	TListFindDirItemItor begin, end;

	m_tFindFileDirItemMutex.Lock();
	begin = m_tFindFileDirItemList.begin();		end = m_tFindFileDirItemList.end();
	for(begin; begin != end; )
	{
		if(!nOrderID)
			nOrderID = begin->nOrderID;
		if(begin->nOrderID != nOrderID)
		{
			begin++;
			continue;
		}

		tSFDWList.push_back(*begin);
		m_tFindFileDirItemList.erase(begin++);
		nRtn = 1;
		nLimited--;

		if(!nLimited)
			break;
	}
	m_tFindFileDirItemMutex.UnLock();
	return nRtn;
}
//-------------------------------------------------------------

INT32		CFindFileUtil::ClearFindFileDirItem()
{
	m_tFindFileDirItemMutex.Lock();

	WriteLogN("clear find file dir item num : [%d]", m_tFindFileDirItemList.size());
	m_tFindFileDirItemList.clear();

	m_tFindFileDirItemMutex.UnLock();
	return 0;
}
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------

INT32		CFindFileUtil::InitFindFileWork(UINT32 nOrderID)
{
	FIND_FILE_WORK tSFFW;
	if(m_tFindFileWorkMap.find(nOrderID) != m_tFindFileWorkMap.end())
	{
		WriteLogN("already exist order id : [%d]", nOrderID);
		return -1;
	}

	tSFFW.tMutexExt		= new CMutexExt;
	tSFFW.nOrderID		= nOrderID;

	m_tFindFileWorkMap[nOrderID] = tSFFW;
	return 0;
}
//-------------------------------------------------------------

INT32		CFindFileUtil::DeleteFindFileWork(UINT32 nOrderID)
{
	PFIND_FILE_WORK pSFFW = GetFindFileWork(nOrderID);
	if(!pSFFW)
		return 0;

	CMutexExt* tMutexExt = (CMutexExt*)(pSFFW->tMutexExt);
	if(tMutexExt)
		delete tMutexExt;

	pSFFW->tFFIList.clear();
	m_tFindFileWorkMap.erase(nOrderID);

	ClearFileMask(nOrderID);
	ClearExceptDir(nOrderID);
	return 0;
}
//--------------------------------------------------------------------

INT32		CFindFileUtil::ClearFindFileWork()
{
	TMapFindFileWorkItor begin, end;
	CMutexExt* tMutexExt;
	begin = m_tFindFileWorkMap.begin();	end = m_tFindFileWorkMap.end();
	for(begin; begin != end; begin++)
	{
		tMutexExt = (CMutexExt*)(begin->second.tMutexExt);
		if(tMutexExt)
			delete tMutexExt;
		begin->second.tFFIList.clear();
	}
	m_tFindFileWorkMap.clear();
	return 0;
}
//--------------------------------------------------------------------

PFIND_FILE_WORK	CFindFileUtil::GetFindFileWork(UINT32 nOrderID)
{
	TMapFindFileWorkItor find = m_tFindFileWorkMap.find(nOrderID);
	if(find == m_tFindFileWorkMap.end())
		return NULL;
	return &(find->second);
}
//-------------------------------------------------------------

INT32		CFindFileUtil::SetFindFileWork_TotalDir(UINT32 nOrderID, UINT32 nDirNum, UINT32 nDirSubSearchedNum)
{
	CMutexExt* tMutexExt = NULL;
	PFIND_FILE_WORK pSFFW = GetFindFileWork(nOrderID);
	if(!pSFFW)
		return -1;

	tMutexExt = (CMutexExt*)pSFFW->tMutexExt;
	if(tMutexExt == NULL)
		return -2;

	tMutexExt->Lock();
	pSFFW->nDirTotalNum			+= nDirNum;
	pSFFW->nDirSubSearchedNum	+= nDirSubSearchedNum;
	tMutexExt->UnLock();
	return 0;
}
//-------------------------------------------------------------

INT32		CFindFileUtil::SetFindFileWork_SearchPath(UINT32 nOrderID, UINT32 nType, UINT32 nValue)
{
	CMutexExt* tMutexExt = NULL;
	PFIND_FILE_WORK pSFFW = GetFindFileWork(nOrderID);
	if(!pSFFW)
		return -1;
	tMutexExt = (CMutexExt*)pSFFW->tMutexExt;
	if(tMutexExt == NULL)
		return -2;

	tMutexExt->Lock();
	switch(nType)
	{
		case 0:
			pSFFW->nSearchPathNum = nValue;
			break;
		case 1:
			pSFFW->nSearchedPathNum += nValue;
			break;
	}
	tMutexExt->UnLock();
	return 0;
}
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------
//-------------------------------------------------------------

INT32		CFindFileUtil::AddFindFileItemList(UINT32 nOrderID, UINT32 nSearchDirNum, TListFindFileItem& tFindFileItemList)
{
	TListFindFileItemItor begin, end;
	CMutexExt* tMutexExt = NULL;
	PFIND_FILE_WORK pSFFW = GetFindFileWork(nOrderID);
	if(!pSFFW)
		return -1;

	tMutexExt = (CMutexExt*)pSFFW->tMutexExt;
	if(tMutexExt == NULL)
		return -2;

	tMutexExt->Lock();
	begin = tFindFileItemList.begin();	end = tFindFileItemList.end();
	for(begin; begin != end; begin++)
	{
		pSFFW->tFFIList.push_back(*begin);
	}
	pSFFW->nDirSearchedNum += nSearchDirNum;
	pSFFW->nFileTotalNum += tFindFileItemList.size();
	tMutexExt->UnLock();
	return 0;
}
//-------------------------------------------------------------

INT32		CFindFileUtil::AddFindFileItemList(UINT32 nOrderID, UINT32 nSearchDirNum, PFIND_FILE_ITEM pAFFI)
{
	CMutexExt* tMutexExt = NULL;
	PFIND_FILE_WORK pSFFW = GetFindFileWork(nOrderID);
	if(!pSFFW)
		return -1;
	tMutexExt = (CMutexExt*)(pSFFW->tMutexExt);
	if(tMutexExt == NULL)
		return -2;
	
	tMutexExt->Lock();
	if(pAFFI)
	{
		pSFFW->tFFIList.push_back(*pAFFI);
		pSFFW->nFileTotalNum += 1;
	}
	pSFFW->nDirSearchedNum += nSearchDirNum;		
	tMutexExt->UnLock();
	return 0;
}
//-------------------------------------------------------------

INT32		CFindFileUtil::GetFindFileItem(UINT32 nOrderID, PASI_FF_FILE_ITEM pAFFI, PUINT32 nBufNum, PASI_FF_FILE_RESULT pAFFR)
{
	UINT32 nIdx = 0;
	TListFindFileItemItor begin, end;
	CMutexExt* tMutexExt = NULL;
	PFIND_FILE_WORK pSFFW = GetFindFileWork(nOrderID);
	if(!pSFFW)
	{
		WriteLogE("[GetFindFileItem] fail to find file work (%d)", nOrderID);
		return -1;
	}

	tMutexExt = (CMutexExt*)(pSFFW->tMutexExt);
	if(tMutexExt == NULL)
	{
		WriteLogE("[GetFindFileItem] invalid mutex lock");
		return -2;
	}
	
	tMutexExt->Lock();
	if(pAFFI)
	{
		begin = pSFFW->tFFIList.begin();	end = pSFFW->tFFIList.end();
		for(begin; begin != end && (*nBufNum) > nIdx; )
		{
			strncpy(pAFFI[nIdx].szFilePath, begin->strFilePath.c_str(), MAX_PATH-1);
			pAFFI[nIdx].szFilePath[MAX_PATH-1] = '\0';
			strncpy(pAFFI[nIdx].szFileName, begin->strFileName.c_str(), MAX_PATH-1);
			pAFFI[nIdx].szFileName[MAX_PATH-1] = '\0';
			mbstowcs((wchar_t*)pAFFI[nIdx].wzFilePath, pAFFI[nIdx].szFilePath, MAX_PATH-1);
			pAFFI[nIdx].wzFilePath[MAX_PATH-1] = L'\0';
			mbstowcs((wchar_t*)pAFFI[nIdx].wzFileName, pAFFI[nIdx].szFileName, MAX_PATH-1);
			pAFFI[nIdx].wzFileName[MAX_PATH-1] = L'\0';
			
			pAFFI[nIdx].nFileSize = begin->nFileSize;
			pAFFI[nIdx].nFindType = begin->nFindType;

			pSFFW->tFFIList.erase(begin++);
			pSFFW->nFileWorkedNum += 1;
			nIdx++;
		}

		*nBufNum = nIdx;
	}	

	if(pAFFR)
	{
		pAFFR->nContinue = 1;
		pAFFR->nMoreFileItem = 1;
		pAFFR->nSearchPathNum		= pSFFW->nSearchPathNum;
		pAFFR->nSearchedPathNum		= pSFFW->nSearchedPathNum;
		pAFFR->nDirSubSearchNum		= pSFFW->nDirSubSearchNum;
		pAFFR->nDirSubSearchedNum	= pSFFW->nDirSubSearchedNum;
		pAFFR->nDirTotalNum			= pSFFW->nDirTotalNum;
		pAFFR->nDirSearchedNum		= pSFFW->nDirSearchedNum;
		pAFFR->nFileTotalNum		= pSFFW->nFileTotalNum;
		pAFFR->nFileWorkedNum		= pSFFW->nFileWorkedNum;	

		if((pAFFR->nSearchPathNum + pAFFR->nSearchedPathNum + pAFFR->nDirSearchedNum + pAFFR->nDirSubSearchedNum + pAFFR->nDirTotalNum + pAFFR->nDirSearchedNum + pAFFR->nFileTotalNum + pAFFR->nFileWorkedNum))
		{
			if(pAFFR->nSearchPathNum == pAFFR->nSearchedPathNum && pAFFR->nDirTotalNum == pAFFR->nDirSearchedNum && pAFFR->nDirSubSearchNum == pAFFR->nDirSubSearchedNum)
			{
				pAFFR->nContinue = 0;
				if((*nBufNum) == 0 && pAFFR->nFileTotalNum == pAFFR->nFileWorkedNum)
					pAFFR->nMoreFileItem = 0;

			}
		}
	}

	tMutexExt->UnLock();
	return 0;
}
//-------------------------------------------------------------




