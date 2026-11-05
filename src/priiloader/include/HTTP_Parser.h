/*

HTTP Parser by DacoTaco
Copyright (C) 2013-2019  DacoTaco

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


*/
//Note by DacoTaco : yes this isn't the prettiest HTTP Parser alive but it does the job better then loads of http parsers so stop complaining :)

#ifndef _HTTP_PARSER_H_
#define _HTTP_PARSER_H_

//includes
//---------------
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <network.h>
#include <unistd.h>
#include <string.h>
#include "gecko.h"
#include "Global.h"
#include "mem2_manager.h"

//defines
//----------------

//the error HttpGet / HttpGetUrl return (negative), besides the ones of the plain HTTP path (-1..-14, see HttpErrorText)
#define HTTP_ERR_TLS -15 //the TLS connection failed, GetLastTlsError() says why
#define HTTP_ERR_DEADLINE -16 //the whole call took longer than HttpOptions::deadlineMs

//options of HttpGetUrl. Zero fields mean the defaults of HttpGet.
struct HttpOptions
{
	const char* userAgent;	//NULL: Priiloader's old User-Agent (what the DacoTaco server sees)
	const char* accept;		//an Accept header, NULL: none
	u32 maxBody;			//the biggest body accepted, 0: 16 MB
	u32 maxHeaderLine;		//the longest header line accepted, 0: 1024 (redirect addresses of CDNs need 4096)
	u32 timeoutMs;			//how long any wait for the network may last, 0: 15000
	u32 deadlineMs;			//how long the whole call may last, redirects included, 0: 120000 (the update DOL gets 600000)
	u32 errorBody;			//keep up to this many bytes of the body of a non-200 reply (GetLastHttpErrorBody), 0: none
};

//functions
//--------------
//plain HTTP (port 80) GET of file on host, following redirects (also to https, never from https to http)
s32 HttpGet(const char *host, const char *file, u8*& Data, const int* externalSocket);
//GET of an http:// or https:// url (no user info, no port), following up to 5 redirects that may change host and scheme
s32 HttpGetUrl(const char* url, const HttpOptions* options, u8*& Data);
s32 ConnectSocket(const char *hostname, u32 port);
s16 GetLastHttpReply( void );
//the start of the body of the last non-200 reply (HttpOptions::errorBody bytes at most), "" when none was kept
const char* GetLastHttpErrorBody( void );
//the TLS_ERR_* of the last failed https connection (0 when the last HttpGet* did not fail in TLS)
int GetLastTlsError( void );
//a short English text for a negative HttpGet / HttpGetUrl result (for the TLS error it is tls_strerror's text)
const char* HttpErrorText(s32 ret);

#endif
