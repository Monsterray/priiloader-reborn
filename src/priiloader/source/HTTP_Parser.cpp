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
#include <algorithm>
#include <cctype>
#include <string>
#include "HTTP_Parser.h"
#include "../../Shared/gitrev.h"
static s16 lastStatusCode;

//the biggest body we accept: the version file, changelogs and the update DOL are all far smaller
#define HTTP_MAX_BODY (16 * 1024 * 1024)
//how many redirects we follow before giving up
#define HTTP_MAX_REDIRECTS 5

s16 GetLastHttpReply( void )
{
	return lastStatusCode;
}

static std::string ToLower(std::string s)
{
	for (size_t i = 0; i < s.size(); i++)
		s[i] = static_cast<char>(tolower(static_cast<unsigned char>(s[i])));
	return s;
}

static std::string Trim(const std::string& s)
{
	size_t b = 0, e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t'))
		b++;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
		e--;
	return s.substr(b, e - b);
}

//fetch one url. returns the body size (>= 0) when the reply was a 200, a negative error otherwise.
//when the reply was a redirect, redirect is set and location holds the Location header.
//the socket is closed before returning, whatever the outcome. dataPtr is only allocated when the return is > 0.
static s32 HttpFetch(const char* host, const char* file, int externalSocket, u8*& dataPtr, std::string& location, bool& redirect)
{
	const s32 bufferSize = 1024;
	char buffer[bufferSize];
	std::string lowerHeader;
	std::string httpHeader;
	s32 ret = 0;
	s32 bytesTransfered = 0;
	u32 dataRead = 0;
	u32 dataSize = 0;
	bool badLength = false;
	int socket = 0;

	redirect = false;
	location = "";
	lastStatusCode = 0;

	//setup the http request
	char httpRequest[bufferSize / 2] = { 0 };
	snprintf(httpRequest, bufferSize / 2 - 1, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Priiloader/%s(Nintendo Wii) DacoTacoIsGod/1.1 \r\n\r\n", file, host, GIT_REV_STR);

	//setup socket
	if (externalSocket == 0)
	{
		socket = ConnectSocket(host, 80);
		if (socket < 0)
			return socket;
	}
	else
	{
		socket = externalSocket;
	}

	//Set Get Request
	bytesTransfered = net_send(socket, httpRequest, strlen(httpRequest), 0);
	if (bytesTransfered <= 0)
	{
		net_close(socket);
		return -5;
	}

	//Read HTTP Headers
	for (s32 i = 0;;)
	{
		bytesTransfered = net_recv(socket, &buffer[i], 1, 0);
		if (bytesTransfered <= 0 || (i + bytesTransfered) >= bufferSize)
		{
			net_close(socket);
			return -6;
		}

		//newline (0x0D0A) means we have a http header to parse
		if (i >= 1 && buffer[i - 1] == 0x0D && buffer[i] == 0x0A)
		{
			//headers are done. the rest is all data.
			if (i <= 1)
				break;

			//remove newline and parse header
			buffer[i - 1] = 0;
			httpHeader = buffer;
			lowerHeader = ToLower(httpHeader);

			//HTTP reply header -> "HTTP/1.1 [reply number like '200'] [name of code. 200 = 'OK']"
			if (lowerHeader.rfind("http/", 0) == 0)
			{
				size_t beginStatusCode = lowerHeader.find(' ');
				long code = 0;
				if (beginStatusCode != std::string::npos)
				{
					const char* start = lowerHeader.c_str() + beginStatusCode + 1;
					char* end = NULL;
					code = strtol(start, &end, 10);
					if ((end - start) != 3 || (*end != 0 && *end != ' '))
						code = 0;
				}
				if (code < 100 || code > 599)
				{
					net_close(socket);
					return -7;
				}
				lastStatusCode = static_cast<s16>(code);
			}
			else
			{
				size_t colon = httpHeader.find(':');
				if (colon != std::string::npos)
				{
					std::string name = lowerHeader.substr(0, colon);
					std::string value = Trim(httpHeader.substr(colon + 1));
					if (name == "content-length")
					{
						//digits only: no sign, nothing negative or huge
						char* end = NULL;
						unsigned long long len = 0;
						if (!value.empty() && value[0] >= '0' && value[0] <= '9')
							len = strtoull(value.c_str(), &end, 10);
						if (end == NULL || *end != 0 || len > HTTP_MAX_BODY)
							badLength = true;
						else
							dataSize = static_cast<u32>(len);
					}
					else if (name == "location")
					{
						location = value;
					}
				}
			}

			i = 0;
			memset(buffer, 0, bufferSize);
			continue;
		}

		i += bytesTransfered;
	}

	memset(buffer, 0, bufferSize);

	//Check status code
	switch (lastStatusCode)
	{
	case 200: // OK
		if (badLength)
		{
			net_close(socket);
			return -13;
		}
		break;
	case 302:
	case 301: // Resource Moved
		net_close(socket);
		redirect = true;
		return 0;
	case 404: //File Not Found. nothing special we can do :)
	default: //o ow, a reply we dont understand yet D: close connection and bail out
		net_close(socket);
		return -10;
	}

	//http headers have been parsed. lets receive the actual data now.
	if (dataSize > 0)
	{
		dataPtr = static_cast<u8*>(mem_malloc(dataSize));
		if (dataPtr == NULL)
		{
			net_close(socket);
			return -11;
		}
		memset(dataPtr, 0, dataSize);
	}

	ret = 0;
	while(1)
	{
		u32 len = bufferSize;
		void* netBuffer = buffer;
		if(dataSize > 0)
		{
			netBuffer = dataPtr + dataRead;
			len = dataSize - dataRead;
		}
		else
			memset(buffer, 0, bufferSize);

		bytesTransfered = net_recv(socket, netBuffer, len, 0);
		//connection closed or no more data
		if (bytesTransfered <= 0)
		{
			if (dataSize > 0)
				ret = -12;

			break;
		}

		DCFlushRange(netBuffer, bytesTransfered);
		DCInvalidateRange(netBuffer, bytesTransfered);

		//reallocate dataPtr as we read from it if we dont know the expected size
		if (dataSize == 0)
		{
			if (dataRead + static_cast<u32>(bytesTransfered) > HTTP_MAX_BODY)
			{
				ret = -13;
				break;
			}

			u8* newPtr = dataPtr == NULL
				? static_cast<u8*>(mem_malloc(bytesTransfered))
				: static_cast<u8*>(mem_realloc(dataPtr, dataRead + bytesTransfered));
			if (newPtr == NULL)
			{
				//dataPtr, if any, is still ours to free
				ret = -11;
				break;
			}
			dataPtr = newPtr;

			memcpy(dataPtr + dataRead, buffer, bytesTransfered);
			DCFlushRange(dataPtr + dataRead, bytesTransfered);
			DCInvalidateRange(dataPtr + dataRead, bytesTransfered);
		}

		dataRead += bytesTransfered;
		if (dataSize > 0 && dataRead >= dataSize)
			break;
	}

	net_close(socket);
	if (ret >= 0)
		ret = dataRead;

	if (ret <= 0 && dataPtr != NULL)
	{
		mem_free(dataPtr);
	}

	return ret;
}

s32 HttpGet(const char* host, const char* file, u8*& dataPtr, const int* externalSocket)
{
	//reset last reply
	lastStatusCode = 0;

	//URL_REQUEST
	if (dataPtr)
	{
		mem_free(dataPtr);
	}

	//did we get a host of filename?
	if (host == NULL || file == NULL)
		return -4;

	std::string currentHost = host;
	std::string currentFile = file;
	std::string location;
	int socket = (externalSocket != NULL) ? *externalSocket : 0;

	for (int redirects = 0; redirects <= HTTP_MAX_REDIRECTS; redirects++)
	{
		bool redirect = false;
		s32 ret = HttpFetch(currentHost.c_str(), currentFile.c_str(), socket, dataPtr, location, redirect);
		socket = 0; //the socket is closed by now
		if (!redirect)
			return ret;

		//we only support http links. no https or w/e, sorry
		if (location.empty())
			return -8;

		if (location[0] == '/')
		{
			//same host, new file
			currentFile = location;
			continue;
		}

		if (ToLower(location.substr(0, 7)) != "http://")
			return -9;

		std::string rest = location.substr(7);
		size_t slash = rest.find('/');
		std::string newHost = rest.substr(0, slash);
		if (newHost.empty() || newHost.find(':') != std::string::npos)
			return -9;

		currentHost = newHost;
		currentFile = (slash == std::string::npos) ? "/" : rest.substr(slash);
	}

	//too many redirects
	return -14;
}

s32 ConnectSocket(const char *hostname, u32 port)
{
	s32 socket = 0;
	struct sockaddr_in connect_addr;
	if ( (socket = net_socket(AF_INET,SOCK_STREAM,IPPROTO_IP)) < 0)
	{
		return -1;
	}
	else
	{
		//resolving hostname
		const struct hostent *host = 0;
		host = net_gethostbyname(hostname);
		if ( host == NULL )
		{
			//resolving failed
			net_close(socket);
			return -2;
		}
		else
		{
			connect_addr.sin_family = AF_INET;
			connect_addr.sin_port = port;
			memcpy(&connect_addr.sin_addr, host->h_addr_list[0], host->h_length);
		}
	}
	if (net_connect(socket, reinterpret_cast<struct sockaddr*>(&connect_addr) , sizeof(connect_addr)) == -1 )
	{
		net_close(socket);
		return -3;
	}
	return socket;
}
