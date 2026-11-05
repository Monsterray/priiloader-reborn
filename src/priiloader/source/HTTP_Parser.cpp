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
#include <poll.h>
#include <ogc/lwp_watchdog.h>
#include "HTTP_Parser.h"
#include "tls.h"
#include "../../Shared/gitrev.h"
static s16 lastStatusCode;
static int lastTlsError;
static std::string lastErrorBody;

//the biggest body we accept: the version file, changelogs and the update DOL are all far smaller
#define HTTP_MAX_BODY (16 * 1024 * 1024)
//how many redirects we follow before giving up
#define HTTP_MAX_REDIRECTS 5
//a reply's headers, all together
#define HTTP_MAX_HEADERS (32 * 1024)
#define HTTP_DEFAULT_LINE 1024
#define HTTP_DEFAULT_TIMEOUT 15000
//the whole of one HttpGet / HttpGetUrl call, redirects included: a server that trickles a byte every 14 s must not hold an update for hours
#define HTTP_DEFAULT_DEADLINE 120000

s16 GetLastHttpReply( void )
{
	return lastStatusCode;
}

int GetLastTlsError( void )
{
	return lastTlsError;
}

const char* GetLastHttpErrorBody( void )
{
	return lastErrorBody.c_str();
}

const char* HttpErrorText(s32 ret)
{
	if (ret == HTTP_ERR_TLS)
		return tls_strerror(lastTlsError);
	switch (ret)
	{
		case -1: return "no network socket";
		case -2: return "host name not found";
		case -3: return "could not connect";
		case -4: return "bad address";
		case -5: return "could not send the request";
		case -6: return "no reply from the server";
		case -7: return "the server's reply is not HTTP";
		case -8: return "redirect without an address";
		case -9: return "redirect not allowed";
		case -10: return "the server refused the request";
		case -11: return "out of memory";
		case -12: return "the download was cut short";
		case -13: return "the reply is too big or of unknown length";
		case -14: return "too many redirects";
		case HTTP_ERR_DEADLINE: return "the server is too slow";
		default: return "network error";
	}
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

/* ---- the connection: a plain socket or a TLS connection ---- */

struct Conn
{
	int sock;		//plain socket, 0 when none
	tls_conn* tls;	//TLS connection, NULL when plain
	u32 timeoutMs;
	u32 startMs;	//when the call began (NowMs)
	u32 deadlineMs;	//how long the whole call may last
	bool expired;	//the deadline ended a receive
};

//a millisecond clock that wraps after 49 days: only differences are used
static u32 NowMs(void)
{
	return ticks_to_millisecs(gettime());
}

static void ConnClose(Conn& c)
{
	if (c.tls != NULL)
		tls_close(c.tls);
	else if (c.sock > 0)
		net_close(c.sock);
	c.tls = NULL;
	c.sock = 0;
}

//0 on success, else a negative HttpGet error (HTTP_ERR_TLS with lastTlsError set)
static s32 ConnOpen(Conn& c, bool https, const char* host, int externalSocket)
{
	c.sock = 0;
	c.tls = NULL;
	if (https)
	{
		int r = tls_open(&c.tls, host, 443, c.timeoutMs);
		if (r < 0)
		{
			c.tls = NULL;
			lastTlsError = r;
			return HTTP_ERR_TLS;
		}
		return 0;
	}
	if (externalSocket != 0)
	{
		c.sock = externalSocket;
		return 0;
	}
	s32 s = ConnectSocket(host, 80);
	if (s < 0)
		return s;
	c.sock = s;
	return 0;
}

static bool ConnSend(Conn& c, const void* data, size_t len)
{
	if (c.tls != NULL)
	{
		int r = tls_write(c.tls, data, len);
		if (r < 0)
			lastTlsError = r;
		return r == static_cast<int>(len);
	}
	const char* p = static_cast<const char*>(data);
	while (len > 0)
	{
		s32 n = net_send(c.sock, p, len, 0);
		if (n <= 0)
			return false;
		p += n;
		len -= n;
	}
	return true;
}

//bytes read (> 0), 0 when the peer closed, < 0 on an error or timeout
static s32 ConnRecv(Conn& c, void* buf, s32 len)
{
	if (NowMs() - c.startMs >= c.deadlineMs)
	{
		c.expired = true;
		return -1;
	}
	if (c.tls != NULL)
	{
		int r = tls_read(c.tls, buf, len);
		if (r < 0)
			lastTlsError = r;
		return r;
	}
	struct pollsd sd;
	sd.socket = c.sock;
	sd.events = POLLIN;
	sd.revents = 0;
	//never wait past the deadline
	u32 wait = c.timeoutMs;
	u32 elapsed = NowMs() - c.startMs;
	u32 left = elapsed < c.deadlineMs ? c.deadlineMs - elapsed : 1;
	if (wait > left)
		wait = left;
	s32 ready = net_poll(&sd, 1, static_cast<s32>(wait));
	if (ready <= 0)
		return -1; //timeout or error
	return net_recv(c.sock, buf, len, 0);
}

/* ---- one request ---- */

struct Target
{
	bool https;
	std::string host;
	std::string file;
};

//fetch one url. returns the body size (>= 0) when the reply was a 200, a negative error otherwise.
//when the reply was a redirect, redirect is set and location holds the Location header.
//the connection is closed before returning, whatever the outcome. dataPtr is only allocated when the return is > 0.
static s32 HttpFetch(const Target& target, const HttpOptions& opt, u32 startMs, int externalSocket, u8*& dataPtr, std::string& location, bool& redirect)
{
	const s32 bufferSize = 1024;
	char buffer[bufferSize];
	std::string line;
	s32 ret = 0;
	s32 bytesTransfered = 0;
	u32 dataRead = 0;
	u32 dataSize = 0;
	u32 capacity = 0;
	bool badLength = false;
	size_t headerBytes = 0;
	const u32 maxBody = opt.maxBody ? opt.maxBody : HTTP_MAX_BODY;
	const u32 maxLine = opt.maxHeaderLine ? opt.maxHeaderLine : HTTP_DEFAULT_LINE;

	redirect = false;
	location = "";
	lastStatusCode = 0;

	//setup the http request
	std::string httpRequest = "GET " + target.file + " HTTP/1.0\r\nHost: " + target.host + "\r\n";
	if (opt.userAgent != NULL)
		httpRequest += std::string("User-Agent: ") + opt.userAgent + "\r\n";
	else
		httpRequest += std::string("User-Agent: Priiloader/") + GIT_REV_STR + "(Nintendo Wii) DacoTacoIsGod/1.1 \r\n";
	if (opt.accept != NULL)
		httpRequest += std::string("Accept: ") + opt.accept + "\r\n";
	httpRequest += "\r\n";

	Conn conn;
	conn.timeoutMs = opt.timeoutMs ? opt.timeoutMs : HTTP_DEFAULT_TIMEOUT;
	conn.startMs = startMs;
	conn.deadlineMs = opt.deadlineMs ? opt.deadlineMs : HTTP_DEFAULT_DEADLINE;
	conn.expired = false;
	s32 opened = ConnOpen(conn, target.https, target.host.c_str(), externalSocket);
	if (opened < 0)
		return opened;

	//Set Get Request
	if (!ConnSend(conn, httpRequest.data(), httpRequest.size()))
	{
		ConnClose(conn);
		return -5;
	}

	//Read HTTP Headers
	for (;;)
	{
		char c = 0;
		bytesTransfered = ConnRecv(conn, &c, 1);
		if (bytesTransfered <= 0 || ++headerBytes > HTTP_MAX_HEADERS)
		{
			bool expired = conn.expired;
			ConnClose(conn);
			return expired ? HTTP_ERR_DEADLINE : -6;
		}

		if (c != '\n' || line.empty() || line[line.size() - 1] != '\r')
		{
			//not the end of a line yet (a bare LF is part of the line, as it always was)
			line.push_back(c);
			if (line.size() >= maxLine)
			{
				ConnClose(conn);
				return -6;
			}
			continue;
		}

		//newline (0x0D0A) means we have a http header to parse
		//headers are done. the rest is all data.
		if (line.size() <= 1)
			break;

		//remove the CR and parse the header
		line.resize(line.size() - 1);
		const std::string& httpHeader = line;
		std::string lowerHeader = ToLower(httpHeader);

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
				ConnClose(conn);
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
					if (end == NULL || *end != 0 || len > maxBody)
						badLength = true;
					else
						dataSize = static_cast<u32>(len);
				}
				else if (name == "transfer-encoding" && ToLower(value) != "identity")
				{
					//we asked for HTTP/1.0, which has no chunks: a server that sends them anyway is not understood
					badLength = true;
				}
				else if (name == "location")
				{
					location = value;
				}
			}
		}

		line.clear();
	}

	//Check status code
	switch (lastStatusCode)
	{
	case 200: // OK
		if (badLength)
		{
			ConnClose(conn);
			return -13;
		}
		break;
	case 302:
	case 301:
	case 303:
	case 307:
	case 308: // Resource Moved
		ConnClose(conn);
		redirect = true;
		return 0;
	case 404: //File Not Found. nothing special we can do :)
	default: //o ow, a reply we dont understand yet D: close connection and bail out
		//the start of the body, when the caller wants to read what the server says (GitHub's rate limit message)
		while (opt.errorBody > 0 && lastErrorBody.size() < opt.errorBody)
		{
			u32 want = opt.errorBody - static_cast<u32>(lastErrorBody.size());
			s32 n = ConnRecv(conn, buffer, want < static_cast<u32>(bufferSize) ? static_cast<s32>(want) : bufferSize);
			if (n <= 0)
				break;
			lastErrorBody.append(buffer, n);
		}
		ConnClose(conn);
		return -10;
	}

	//http headers have been parsed. lets receive the actual data now.
	if (dataSize > 0)
	{
		dataPtr = static_cast<u8*>(mem_malloc(dataSize));
		if (dataPtr == NULL)
		{
			ConnClose(conn);
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

		bytesTransfered = ConnRecv(conn, netBuffer, len);
		//connection closed or no more data
		if (bytesTransfered <= 0)
		{
			//a short sized body; or a timeout or a TLS connection that ended without close_notify, when the size is unknown
			if (dataSize > 0 || bytesTransfered < 0)
				ret = conn.expired ? HTTP_ERR_DEADLINE : -12;

			break;
		}

		DCFlushRange(netBuffer, bytesTransfered);
		DCInvalidateRange(netBuffer, bytesTransfered);

		//reallocate dataPtr as we read from it if we dont know the expected size
		if (dataSize == 0)
		{
			if (dataRead + static_cast<u32>(bytesTransfered) > maxBody)
			{
				ret = -13;
				break;
			}

			//grow by doubling (never past maxBody): one copy per doubling, not one per receive
			u32 need = dataRead + static_cast<u32>(bytesTransfered);
			if (need > capacity)
			{
				u64 newCap = capacity ? static_cast<u64>(capacity) * 2 : 4096;
				while (newCap < need)
					newCap *= 2;
				if (newCap > maxBody)
					newCap = maxBody;
				u8* newPtr = dataPtr == NULL
					? static_cast<u8*>(mem_malloc(static_cast<u32>(newCap)))
					: static_cast<u8*>(mem_realloc(dataPtr, static_cast<u32>(newCap)));
				if (newPtr == NULL)
				{
					//dataPtr, if any, is still ours to free
					ret = -11;
					break;
				}
				dataPtr = newPtr;
				capacity = static_cast<u32>(newCap);
			}

			memcpy(dataPtr + dataRead, buffer, bytesTransfered);
			DCFlushRange(dataPtr + dataRead, bytesTransfered);
			DCInvalidateRange(dataPtr + dataRead, bytesTransfered);
		}

		dataRead += bytesTransfered;
		if (dataSize > 0 && dataRead >= dataSize)
			break;
	}

	ConnClose(conn);
	if (ret >= 0)
		ret = dataRead;

	if (ret <= 0 && dataPtr != NULL)
	{
		mem_free(dataPtr);
	}

	return ret;
}

//the part of a url after the scheme: "host/path?query" into host and file. false when the host is empty or has a port or user info
static bool SplitHostAndFile(const std::string& rest, std::string& host, std::string& file)
{
	size_t slash = rest.find_first_of("/?#");
	host = rest.substr(0, slash);
	if (host.empty() || host.find(':') != std::string::npos || host.find('@') != std::string::npos)
		return false;
	if (slash == std::string::npos)
		file = "/";
	else if (rest[slash] == '/')
		file = rest.substr(slash);
	else
		file = "/" + rest.substr(slash);
	//a fragment is never sent
	size_t hash = file.find('#');
	if (hash != std::string::npos)
		file.resize(hash);
	return true;
}

//scheme of an absolute url: 1 https, 0 http, -1 neither. prefix is the length of "https://" or "http://"
static int UrlScheme(const std::string& url, size_t& prefix)
{
	std::string lower = ToLower(url.substr(0, 8));
	if (lower.rfind("https://", 0) == 0)
	{
		prefix = 8;
		return 1;
	}
	if (lower.rfind("http://", 0) == 0)
	{
		prefix = 7;
		return 0;
	}
	return -1;
}

//the fetch-and-follow loop shared by HttpGet and HttpGetUrl
static s32 HttpGetTarget(Target target, const HttpOptions& opt, u8*& dataPtr, const int* externalSocket)
{
	std::string location;
	int socket = (externalSocket != NULL) ? *externalSocket : 0;
	const u32 startMs = NowMs();

	for (int redirects = 0; redirects <= HTTP_MAX_REDIRECTS; redirects++)
	{
		bool redirect = false;
		s32 ret = HttpFetch(target, opt, startMs, target.https ? 0 : socket, dataPtr, location, redirect);
		socket = 0; //the socket is closed by now
		if (!redirect)
			return ret;

		if (location.empty())
			return -8;

		if (location[0] == '/')
		{
			//same host, same scheme, new file
			if (location.size() > 1 && location[1] == '/')
				return -9; //a protocol-relative address: not supported
			target.file = location;
			continue;
		}

		size_t prefix = 0;
		int scheme = UrlScheme(location, prefix);
		if (scheme < 0)
			return -9;

		//never leave https for http
		if (target.https && scheme == 0)
			return -9;

		std::string newHost, newFile;
		if (!SplitHostAndFile(location.substr(prefix), newHost, newFile))
			return -9;

		target.https = scheme == 1;
		target.host = newHost;
		target.file = newFile;
	}

	//too many redirects
	return -14;
}

s32 HttpGet(const char* host, const char* file, u8*& dataPtr, const int* externalSocket)
{
	//reset last reply
	lastStatusCode = 0;
	lastTlsError = 0;
	lastErrorBody.clear();

	//URL_REQUEST
	if (dataPtr)
	{
		mem_free(dataPtr);
	}

	//did we get a host of filename?
	if (host == NULL || file == NULL)
		return -4;

	Target target;
	target.https = false;
	target.host = host;
	target.file = file;
	HttpOptions opt = {};
	return HttpGetTarget(target, opt, dataPtr, externalSocket);
}

s32 HttpGetUrl(const char* url, const HttpOptions* options, u8*& dataPtr)
{
	lastStatusCode = 0;
	lastTlsError = 0;
	lastErrorBody.clear();

	if (dataPtr)
	{
		mem_free(dataPtr);
	}
	if (url == NULL)
		return -4;

	size_t prefix = 0;
	int scheme = UrlScheme(url, prefix);
	if (scheme < 0)
		return -4;
	Target target;
	target.https = scheme == 1;
	if (!SplitHostAndFile(std::string(url).substr(prefix), target.host, target.file))
		return -4;

	HttpOptions opt = {};
	if (options != NULL)
		opt = *options;
	return HttpGetTarget(target, opt, dataPtr, NULL);
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
