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

// HttpFileGetUtil.h: interface for the CHttpFileGetUtil class.
//
//////////////////////////////////////////////////////////////////////

#ifndef _HTTP_GET_UTIL_H__
#define _HTTP_GET_UTIL_H__


#include <string>

using namespace std;

#ifndef RECV_FILE_MAX_SIZE
#define RECV_FILE_MAX_SIZE		10240
#endif

#define SEND_RQ(s, MSG)	if(s!=-1){send(s,MSG,strlen(MSG),0);}
#define CLOSE_SOCK(s)	if(s!=-1){close(s);}


class CHttpFileGetUtil  
{
public:
	void SetLogPath(char *pLogPath, char *pLogFile, INT32 nRemainLog, UINT32 nFileLogRetention);
	INT32 SendData(INT32 nSock, char *pMessage);
	INT32 GetFile_Address(LPSTR lpAddress, LPSTR lpFile, LPSTR lpSaveedFileName, INT32 nPort = 80);
	INT32 GetFile_Host(LPSTR lpHostName, LPSTR lpFile, LPSTR lpSaveedFileName, INT32 nPort = 80);
	void  EnableHttpUtil(INT32 nEnable = 1);
public:	
	CHttpFileGetUtil();
	virtual ~CHttpFileGetUtil();

private:
	INT32   m_nContinue;
	CMutexExt m_MutexHttpLog;

	INT32	m_nRemainDebugLog;
	UINT32	m_nFileLogRetention;
	char	m_acLogPath[MAX_PATH];
	char	m_acLogFile[MAX_PATH];

private:
	INT32 GetHeaderResult(string strHeader);
	INT32 GetRecvFileSize(string strHeader);
	void WriteLog(char *fmt,...);
};

#endif /*_HTTP_GET_UTIL_H__*/
