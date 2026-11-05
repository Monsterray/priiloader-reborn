/*
 * Host tests for priiloader's HTTP client (src/priiloader/source/HTTP_Parser.cpp, built unchanged).
 *
 * The network is a fake: net_send reads the request, the reply comes from a table of canned
 * responses keyed by host and path, and net_recv hands it out in chunks and then reports the
 * connection closed. The fake counts open sockets and the MEM2 allocator counts live blocks, so every
 * test also checks that nothing leaked. A body length the code should never ask for (an allocation
 * or a receive of more than the 16 MB cap, a malloc of 0 bytes) ends the test as failed.
 */
#define HOSTCHECK_MAIN
#include "hostcheck.h"

#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "HTTP_Parser.h"
#include "tls.h"

#define MAX_BODY (16 * 1024 * 1024)

/* ---- the fake network ---- */

struct Route {
	std::string host, path, response;
	bool https;
};
struct Sock {
	bool open;
	std::string reply;
	size_t pos;
	bool tls;
};
struct tls_conn {
	int sock; /* index into socks */
};

static std::vector<Route> routes;
static std::vector<Sock> socks;
static std::vector<std::string> requests; /* "host path" of every GET, in order */
static int openSockets;
static size_t chunk;
static bool resolveFails;
static bool connectFails;
static int tlsOpenError; /* 0, or the TLS_ERR_* tls_open returns */
static bool tlsTruncates; /* the end of a reply is TLS_ERR_TRUNCATED, not a clean close */
static int tlsOpened; /* tls_open calls that succeeded */
static int pollTimeouts; /* net_poll calls to answer with a timeout */
static std::string lastUserAgent, lastAccept;
static int liveBlocks;
static int allocFailAfter; /* -1: never; n: the n+1th allocation fails */
static size_t maxRecvLen;
static u32 fakeMs; /* the fake clock, advanced by recvCostMs on every receive past slowAfterPos (no real sleeping) */
static u32 recvCostMs;
static size_t slowAfterPos;
static int reallocCalls, mallocCalls;
static s32 lastPollTimeout;

static void reset(void)
{
	routes.clear();
	socks.clear();
	requests.clear();
	openSockets = 0;
	chunk = 1000;
	resolveFails = false;
	connectFails = false;
	tlsOpenError = 0;
	tlsTruncates = false;
	tlsOpened = 0;
	pollTimeouts = 0;
	lastUserAgent = lastAccept = "";
	liveBlocks = 0;
	allocFailAfter = -1;
	maxRecvLen = 0;
	fakeMs = 5000;
	recvCostMs = 0;
	slowAfterPos = 0;
	reallocCalls = mallocCalls = 0;
	lastPollTimeout = 0;
	/* sockets start at 1: HttpGet treats 0 as "no socket" */
	Sock none = { false, "", 0, false };
	socks.push_back(none);
}

extern "C" {

s32 net_socket(u32, u32, u32)
{
	Sock s = { true, "", 0, false };
	socks.push_back(s);
	openSockets++;
	return (s32)socks.size() - 1;
}

struct hostent* net_gethostbyname(const char*)
{
	static char addr[4] = { 1, 2, 3, 4 };
	static char* list[2] = { addr, NULL };
	static struct hostent h = { (char*)"fake", NULL, 2, 4, list };
	return resolveFails ? NULL : &h;
}

s32 net_connect(s32, struct sockaddr*, socklen_t)
{
	return connectFails ? -1 : 0;
}

static std::string header_value(const std::string& req, const char* name)
{
	std::string key = std::string("\r\n") + name + ": ";
	size_t p = req.find(key);
	if (p == std::string::npos)
		return "";
	p += key.size();
	return req.substr(p, req.find("\r\n", p) - p);
}

/* the server side: parse the request on socket s, pick the canned reply (routes of the same scheme only) */
static void serve(s32 s, const void* data, s32 size)
{
	std::string req((const char*)data, (size_t)size);
	size_t p = req.find("GET ");
	size_t e = req.find(" HTTP/", p);
	size_t h = req.find("Host: ");
	size_t he = req.find("\r\n", h);
	if (p != 0 || e == std::string::npos || h == std::string::npos || he == std::string::npos)
		hc_hang("malformed request: %s", req.c_str());
	std::string path = req.substr(4, e - 4);
	std::string host = req.substr(h + 6, he - h - 6);
	requests.push_back((socks[s].tls ? "https:" : "") + host + " " + path);
	lastUserAgent = header_value(req, "User-Agent");
	lastAccept = header_value(req, "Accept");
	socks[s].reply = "HTTP/1.0 404 Not Found\r\n\r\n";
	for (size_t i = 0; i < routes.size(); i++)
		if (routes[i].host == host && routes[i].path == path && routes[i].https == socks[s].tls)
			socks[s].reply = routes[i].response;
}

s32 net_send(s32 s, const void* data, s32 size, u32)
{
	if (s <= 0 || s >= (s32)socks.size() || !socks[s].open)
		hc_hang("send on socket %d that is not open", s);
	serve(s, data, size);
	return size;
}

u64 gettime(void)
{
	return (u64)fakeMs * 60750;
}

static void spend(const Sock& k)
{
	if (k.pos >= slowAfterPos)
		fakeMs += recvCostMs;
}

s32 net_poll(struct pollsd*, s32, s32 timeout)
{
	lastPollTimeout = timeout;
	if (pollTimeouts > 0) {
		pollTimeouts--;
		return 0;
	}
	return 1;
}

/* the fake TLS layer: the same canned replies, over connections that are counted like sockets */
int tls_open(tls_conn** out, const char*, uint16_t port, uint32_t)
{
	*out = NULL;
	if (port != 443)
		hc_hang("tls_open on port %d", (int)port);
	if (tlsOpenError)
		return tlsOpenError;
	Sock k = { true, "", 0, true };
	socks.push_back(k);
	openSockets++;
	tlsOpened++;
	tls_conn* c = new tls_conn;
	c->sock = (int)socks.size() - 1;
	*out = c;
	return 0;
}

int tls_write(tls_conn* c, const void* data, size_t len)
{
	if (!socks[c->sock].open)
		hc_hang("tls_write on a closed connection");
	serve(c->sock, data, (s32)len);
	return (int)len;
}

int tls_read(tls_conn* c, void* mem, size_t len)
{
	Sock& k = socks[c->sock];
	if (!k.open)
		hc_hang("tls_read on a closed connection");
	if (len > (size_t)MAX_BODY + 1024)
		hc_hang("tls_read asked for %u bytes", (unsigned)len);
	if (len > maxRecvLen)
		maxRecvLen = len;
	size_t n = k.reply.size() - k.pos;
	if (n > len)
		n = len;
	if (n > chunk)
		n = chunk;
	if (n == 0 && tlsTruncates)
		return TLS_ERR_TRUNCATED;
	spend(k);
	memcpy(mem, k.reply.data() + k.pos, n);
	k.pos += n;
	return (int)n;
}

void tls_close(tls_conn* c)
{
	if (c == NULL)
		return;
	if (!socks[c->sock].open)
		hc_hang("tls connection closed twice");
	socks[c->sock].open = false;
	openSockets--;
	delete c;
}

const char* tls_strerror(int err)
{
	return err == TLS_ERR_CLOCK ? "clock wrong" : "tls error";
}

s32 net_recv(s32 s, void* mem, s32 len, u32)
{
	if (s <= 0 || s >= (s32)socks.size() || !socks[s].open)
		hc_hang("recv on socket %d that is not open", s);
	if (len < 0 || (size_t)len > (size_t)MAX_BODY + 1024)
		hc_hang("net_recv asked for %d bytes", len);
	if ((size_t)len > maxRecvLen)
		maxRecvLen = (size_t)len;
	Sock& k = socks[s];
	size_t n = k.reply.size() - k.pos;
	if (n > (size_t)len)
		n = (size_t)len;
	if (n > chunk)
		n = chunk;
	spend(k);
	memcpy(mem, k.reply.data() + k.pos, n);
	k.pos += n;
	return (s32)n; /* 0: the server closed the connection */
}

s32 net_close(s32 s)
{
	if (s <= 0 || s >= (s32)socks.size() || !socks[s].open)
		hc_hang("close of socket %d that is not open", s);
	socks[s].open = false;
	openSockets--;
	return 0;
}

void DCFlushRange(void*, u32) {}
void DCInvalidateRange(void*, u32) {}

/* ---- MEM2, on the heap, counting ---- */

static bool allocFails(void)
{
	if (allocFailAfter < 0)
		return false;
	return allocFailAfter-- == 0;
}

void* mem2_malloc(u32 size, const int)
{
	if (size == 0)
		hc_hang("malloc of 0 bytes (a length that wrapped)");
	if (size > MAX_BODY + 64)
		hc_hang("malloc of %u bytes, beyond the cap", size);
	if (allocFails())
		return NULL;
	liveBlocks++;
	mallocCalls++;
	return malloc(size);
}

void* mem2_realloc(void* ptr, u32 size, const int)
{
	if (size == 0 || size > MAX_BODY + 64)
		hc_hang("realloc to %u bytes", size);
	reallocCalls++;
	if (allocFails())
		return NULL;
	return realloc(ptr, size);
}

void mem2_free(void* ptr, const int)
{
	if (!ptr)
		return;
	liveBlocks--;
	free(ptr);
}

} /* extern "C" */

/* ---- helpers ---- */

static std::string body_of(size_t n)
{
	std::string s;
	for (size_t i = 0; i < n; i++)
		s += (char)('A' + (i * 7 + i / 13) % 26);
	return s;
}

static void route(const char* host, const char* path, const std::string& response)
{
	Route r = { host, path, response, false };
	routes.push_back(r);
}

static void routes_tls(const char* host, const std::string& path, const std::string& response)
{
	Route r = { host, path, response, true };
	routes.push_back(r);
}

static std::string ok(const std::string& headers, const std::string& body)
{
	return "HTTP/1.1 200 OK\r\n" + headers + "\r\n" + body;
}

static std::string moved(const char* status, const std::string& locationHeader)
{
	return std::string("HTTP/1.1 ") + status + " Moved\r\n" + locationHeader + "\r\n";
}

struct Result {
	s32 ret;
	u8* data;
};

static Result get(const char* host, const char* path)
{
	Result r = { 0, NULL };
	r.ret = HttpGet(host, path, r.data, NULL);
	return r;
}

/* the common end of every test: nothing open, nothing leaked after the caller frees */
#define FINISH(r) do { \
	mem_free((r).data); \
	CHECK(openSockets == 0, "%d sockets left open", openSockets); \
	CHECK(liveBlocks == 0, "%d heap blocks leaked", liveBlocks); \
} while (0)

/* ---- tests ---- */

static void test_content_length(void)
{
	std::string body = body_of(5000);
	chunk = 777;
	route("h", "/f", ok("Content-Length: 5000\r\n", body));
	Result r = get("h", "/f");
	CHECK(r.ret == 5000, "ret %d", r.ret);
	CHECK(r.data && memcmp(r.data, body.data(), 5000) == 0, "body differs");
	CHECK(GetLastHttpReply() == 200, "status %d", GetLastHttpReply());
	FINISH(r);
}

static void test_no_content_length(void)
{
	std::string body = body_of(3000);
	chunk = 700;
	route("h", "/f", ok("Content-Type: text/plain\r\n", body));
	Result r = get("h", "/f");
	CHECK(r.ret == 3000, "ret %d", r.ret);
	CHECK(r.data && memcmp(r.data, body.data(), 3000) == 0, "body differs");
	FINISH(r);
}

static void test_empty_body_no_headers(void)
{
	route("h", "/f", "HTTP/1.0 200\r\n\r\n");
	Result r = get("h", "/f");
	CHECK(r.ret == 0, "ret %d", r.ret);
	FINISH(r);
}

static void test_header_case(void)
{
	/* after the 5 byte body comes data that must not be returned */
	const char* variants[] = {
		"CONTENT-LENGTH: 5\r\n",
		"content-length:5\r\n",
		"Content-length:   5  \r\n",
		"X-Other: a:b\r\nContent-Length: 5\r\nServer: x\r\n",
	};
	for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
		reset();
		route("h", "/f", ok(variants[i], "helloTRAILING"));
		Result r = get("h", "/f");
		CHECK(r.ret == 5, "variant %d: ret %d", (int)i, r.ret);
		CHECK(r.data && memcmp(r.data, "hello", 5) == 0, "variant %d: body differs", (int)i);
		FINISH(r);
	}
}

static void test_bad_content_length(void)
{
	const char* variants[] = {
		"Content-Length: -5\r\n",
		"Content-Length: -2147483649\r\n",
		"Content-Length: 4294967295\r\n",
		"Content-Length: 4294967296\r\n",
		"Content-Length: 99999999999999999999\r\n",
		"Content-Length: 16777217\r\n",
		"Content-Length: 12abc\r\n",
		"Content-Length: \r\n",
	};
	for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
		reset();
		route("h", "/f", ok(variants[i], "some data"));
		Result r = get("h", "/f");
		CHECK(r.ret < 0, "variant %d (%s): ret %d, not rejected", (int)i, variants[i], r.ret);
		CHECK(r.data == NULL, "variant %d: data returned with an error", (int)i);
		CHECK(maxRecvLen <= 1024, "variant %d: net_recv asked for %u bytes", (int)i, (unsigned)maxRecvLen);
		FINISH(r);
	}
}

static void test_largest_content_length(void)
{
	/* exactly at the cap is accepted (the fake serves only part: that is a short read, not a bad length) */
	route("h", "/f", ok("Content-Length: 16777216\r\n", "short"));
	Result r = get("h", "/f");
	CHECK(r.ret == -12, "ret %d", r.ret);
	FINISH(r);
}

static void test_redirect_absolute(void)
{
	std::string body = body_of(2000);
	route("old", "/a", moved("301", "Location: http://new.example/Files/Update.DOL\r\n"));
	route("new.example", "/Files/Update.DOL", ok("Content-Length: 2000\r\n", body));
	Result r = get("old", "/a");
	CHECK(r.ret == 2000, "ret %d", r.ret);
	CHECK(r.data && memcmp(r.data, body.data(), 2000) == 0, "body differs");
	CHECK(requests.size() == 2 && requests[1] == "new.example /Files/Update.DOL", "second request '%s'",
	      requests.size() > 1 ? requests[1].c_str() : "(none)");
	FINISH(r);
}

static void test_redirect_relative_and_case(void)
{
	route("h", "/a", moved("302", "LOCATION: /b/File.bin\r\n"));
	route("h", "/b/File.bin", ok("content-LENGTH: 4\r\n", "data"));
	Result r = get("h", "/a");
	CHECK(r.ret == 4, "ret %d", r.ret);
	CHECK(r.data && memcmp(r.data, "data", 4) == 0, "body differs");
	FINISH(r);
}

static void test_redirect_chain_then_unsized(void)
{
	route("a", "/", moved("302", "Location: http://b/x\r\n"));
	route("b", "/x", moved("301", "Location: http://c\r\n"));
	route("c", "/", ok("", "tail"));
	Result r = get("a", "/");
	CHECK(r.ret == 4, "ret %d", r.ret);
	CHECK(r.data && memcmp(r.data, "tail", 4) == 0, "body differs");
	CHECK(requests.size() == 3, "%d requests", (int)requests.size());
	FINISH(r);
}

static void test_redirect_loop(void)
{
	route("a", "/x", moved("302", "Location: http://b/y\r\n"));
	route("b", "/y", moved("302", "Location: http://a/x\r\n"));
	Result r = get("a", "/x");
	CHECK(r.ret < 0, "ret %d", r.ret);
	CHECK(requests.size() <= 8, "%d requests: the loop was not limited", (int)requests.size());
	FINISH(r);
}

static void test_redirect_unsupported(void)
{
	route("h", "/https", moved("301", "Location: https://h/x\r\n"));
	route("h", "/none", moved("301", ""));
	route("h", "/empty", moved("301", "Location:\r\n"));
	const char* paths[] = { "/https", "/none", "/empty" };
	for (size_t i = 0; i < 3; i++) {
		Result r = get("h", paths[i]);
		CHECK(r.ret < 0, "%s: ret %d", paths[i], r.ret);
		CHECK(r.data == NULL, "%s: data returned", paths[i]);
		CHECK(openSockets == 0, "%s: %d sockets left open", paths[i], openSockets);
	}
}

static void test_status_404(void)
{
	route("h", "/f", "HTTP/1.1 404 Not Found\r\nContent-Length: 3\r\n\r\nabc");
	Result r = get("h", "/f");
	CHECK(r.ret < 0, "ret %d", r.ret);
	CHECK(GetLastHttpReply() == 404, "status %d", GetLastHttpReply());
	FINISH(r);
}

static void test_bad_status_line(void)
{
	route("h", "/f", "HTTP/1.1 abc OK\r\n\r\n");
	Result r = get("h", "/f");
	CHECK(r.ret == -7, "ret %d", r.ret);
	FINISH(r);
}

static void test_truncated_body(void)
{
	std::string body = body_of(40);
	route("h", "/f", ok("Content-Length: 100\r\n", body));
	Result r = get("h", "/f");
	CHECK(r.ret < 0, "ret %d", r.ret);
	CHECK(r.data == NULL, "data returned with an error");
	FINISH(r);
}

static void test_truncated_headers(void)
{
	route("h", "/f", "HTTP/1.1 200 OK\r\nContent-Len");
	route("h", "/g", "");
	const char* paths[] = { "/f", "/g" };
	for (size_t i = 0; i < 2; i++) {
		Result r = get("h", paths[i]);
		CHECK(r.ret < 0, "%s: ret %d", paths[i], r.ret);
		CHECK(openSockets == 0, "%s: %d sockets left open", paths[i], openSockets);
	}
}

static void test_header_too_long(void)
{
	route("h", "/f", ok("X-Long: " + std::string(2000, 'a') + "\r\n", "x"));
	Result r = get("h", "/f");
	CHECK(r.ret < 0, "ret %d", r.ret);
	FINISH(r);
}

static void test_resolve_failure(void)
{
	resolveFails = true;
	Result r = get("h", "/f");
	CHECK(r.ret == -2, "ret %d", r.ret);
	FINISH(r);
}

static void test_connect_failure(void)
{
	connectFails = true;
	Result r = get("h", "/f");
	CHECK(r.ret == -3, "ret %d", r.ret);
	FINISH(r);
}

static void test_alloc_failure_unsized(void)
{
	route("h", "/f", ok("", body_of(20000)));
	chunk = 700;
	allocFailAfter = 2; /* the first 4 KB and the first doubling work, the next doubling fails */
	Result r = get("h", "/f");
	CHECK(r.ret < 0, "ret %d", r.ret);
	CHECK(r.data == NULL, "data returned with an error");
	FINISH(r);
}

static void test_alloc_failure_sized(void)
{
	route("h", "/f", ok("Content-Length: 10\r\n", "0123456789"));
	allocFailAfter = 0;
	Result r = get("h", "/f");
	CHECK(r.ret < 0, "ret %d", r.ret);
	FINISH(r);
}

static void test_null_arguments(void)
{
	u8* data = NULL;
	CHECK(HttpGet(NULL, "/f", data, NULL) == -4, "null host");
	CHECK(HttpGet("h", NULL, data, NULL) == -4, "null file");
	CHECK(openSockets == 0, "%d sockets", openSockets);
}

/* ---- HTTPS (the fake TLS layer) ---- */

static Result geturl(const char* url, const HttpOptions* o)
{
	Result r = { 0, NULL };
	r.ret = HttpGetUrl(url, o, r.data);
	return r;
}

static void test_https_basic(void)
{
	std::string body = body_of(1500);
	routes_tls("api.github.com", "/repos/o/r/releases?per_page=10", ok("Content-Length: 1500\r\n", body));
	HttpOptions o = {};
	o.userAgent = "priiloader-reborn/1.2.3";
	o.accept = "application/vnd.github+json";
	chunk = 400;
	Result r = geturl("https://api.github.com/repos/o/r/releases?per_page=10", &o);
	CHECK(r.ret == 1500, "ret %d", r.ret);
	CHECK(r.data && memcmp(r.data, body.data(), 1500) == 0, "body differs");
	CHECK(requests.size() == 1 && requests[0] == "https:api.github.com /repos/o/r/releases?per_page=10", "request '%s'",
	      requests.empty() ? "(none)" : requests[0].c_str());
	CHECK(lastUserAgent == "priiloader-reborn/1.2.3", "user agent '%s'", lastUserAgent.c_str());
	CHECK(lastAccept == "application/vnd.github+json", "accept '%s'", lastAccept.c_str());
	CHECK(tlsOpened == 1, "%d TLS connections", tlsOpened);
	FINISH(r);
}

static void test_plain_default_user_agent_is_the_old_one(void)
{
	route("h", "/f", ok("Content-Length: 2\r\n", "ok"));
	Result r = get("h", "/f");
	CHECK(r.ret == 2, "ret %d", r.ret);
	CHECK(lastUserAgent.find("Priiloader/") == 0 && lastUserAgent.find("DacoTacoIsGod") != std::string::npos, "user agent '%s'", lastUserAgent.c_str());
	CHECK(lastAccept.empty(), "an Accept header the old path never sent");
	FINISH(r);
}

static void test_https_cross_host_redirect(void)
{
	std::string body = body_of(3000);
	std::string longUrl = "https://release-assets.githubusercontent.com/github-production-release-asset/1/2?sp=r&sig=" + std::string(900, 'x');
	routes_tls("github.com", "/o/r/releases/download/v1/Priiloader_Update.dol", moved("302", "Location: " + longUrl + "\r\n"));
	routes_tls("release-assets.githubusercontent.com", longUrl.substr(strlen("https://release-assets.githubusercontent.com")),
	           ok("Content-Length: 3000\r\n", body));
	HttpOptions o = {};
	o.maxHeaderLine = 4096;
	Result r = geturl("https://github.com/o/r/releases/download/v1/Priiloader_Update.dol", &o);
	CHECK(r.ret == 3000, "ret %d", r.ret);
	CHECK(r.data && memcmp(r.data, body.data(), 3000) == 0, "body differs");
	CHECK(requests.size() == 2 && requests[1].find("https:release-assets.githubusercontent.com /github-production") == 0, "second request");
	CHECK(tlsOpened == 2, "%d TLS connections", tlsOpened);
	FINISH(r);
}

static void test_https_long_location_needs_the_bigger_line(void)
{
	std::string longUrl = "https://b.example/p?sig=" + std::string(1500, 'x');
	routes_tls("a.example", "/", moved("302", "Location: " + longUrl + "\r\n"));
	Result r = geturl("https://a.example/", NULL);
	CHECK(r.ret == -6, "ret %d (the default line limit is 1024)", r.ret);
	FINISH(r);
}

static void test_https_relative_redirect_stays_https(void)
{
	routes_tls("h", "/a", moved("302", "Location: /b\r\n"));
	routes_tls("h", "/b", ok("Content-Length: 4\r\n", "data"));
	route("h", "/b", ok("Content-Length: 4\r\n", "PLAN"));
	Result r = geturl("https://h/a", NULL);
	CHECK(r.ret == 4 && r.data && memcmp(r.data, "data", 4) == 0, "ret %d", r.ret);
	FINISH(r);
}

static void test_https_to_http_refused(void)
{
	routes_tls("h", "/a", moved("301", "Location: http://h/b\r\n"));
	route("h", "/b", ok("Content-Length: 4\r\n", "data"));
	Result r = geturl("https://h/a", NULL);
	CHECK(r.ret == -9, "ret %d", r.ret);
	CHECK(r.data == NULL, "data returned");
	CHECK(requests.size() == 1, "%d requests: the http address was fetched", (int)requests.size());
	FINISH(r);
}

static void test_http_to_https_allowed(void)
{
	route("old", "/a", moved("301", "Location: https://new.example/x\r\n"));
	routes_tls("new.example", "/x", ok("Content-Length: 4\r\n", "data"));
	Result r = get("old", "/a");
	CHECK(r.ret == 4, "ret %d", r.ret);
	CHECK(tlsOpened == 1, "%d TLS connections", tlsOpened);
	FINISH(r);
}

static void test_https_redirect_loop(void)
{
	routes_tls("a", "/x", moved("302", "Location: https://b/y\r\n"));
	routes_tls("b", "/y", moved("302", "Location: https://a/x\r\n"));
	Result r = geturl("https://a/x", NULL);
	CHECK(r.ret == -14, "ret %d", r.ret);
	CHECK(requests.size() <= 8, "%d requests", (int)requests.size());
	FINISH(r);
}

static void test_tls_open_failure(void)
{
	tlsOpenError = TLS_ERR_CLOCK;
	Result r = geturl("https://api.github.com/x", NULL);
	CHECK(r.ret == HTTP_ERR_TLS, "ret %d", r.ret);
	CHECK(GetLastTlsError() == TLS_ERR_CLOCK, "tls error %d", GetLastTlsError());
	CHECK(strcmp(HttpErrorText(r.ret), "clock wrong") == 0, "text '%s'", HttpErrorText(r.ret));
	CHECK(requests.empty(), "a request was sent");
	FINISH(r);
	/* the error is forgotten by the next call */
	tlsOpenError = 0;
	routes_tls("h", "/", ok("Content-Length: 1\r\n", "x"));
	r = geturl("https://h/", NULL);
	CHECK(r.ret == 1 && GetLastTlsError() == 0, "ret %d tls %d", r.ret, GetLastTlsError());
	FINISH(r);
}

static void test_tls_truncated(void)
{
	tlsTruncates = true;
	/* sized and complete: the missing close_notify does not matter */
	routes_tls("h", "/sized", ok("Content-Length: 5\r\n", "hello"));
	Result r = geturl("https://h/sized", NULL);
	CHECK(r.ret == 5, "sized: ret %d", r.ret);
	FINISH(r);
	/* of unknown size: a cut connection cannot be told from the end */
	reset();
	tlsTruncates = true;
	routes_tls("h", "/unsized", ok("", "hello"));
	r = geturl("https://h/unsized", NULL);
	CHECK(r.ret < 0, "unsized: ret %d", r.ret);
	CHECK(r.data == NULL, "data returned with an error");
	FINISH(r);
	/* of unknown size, closed cleanly: fine */
	reset();
	routes_tls("h", "/unsized", ok("", "hello"));
	r = geturl("https://h/unsized", NULL);
	CHECK(r.ret == 5, "clean close: ret %d", r.ret);
	FINISH(r);
}

static void test_https_body_cap(void)
{
	HttpOptions o = {};
	o.maxBody = 1000;
	routes_tls("h", "/sized", ok("Content-Length: 2000\r\n", body_of(2000)));
	routes_tls("h", "/unsized", ok("", body_of(3000)));
	routes_tls("h", "/exact", ok("Content-Length: 1000\r\n", body_of(1000)));
	chunk = 300;
	Result r = geturl("https://h/sized", &o);
	CHECK(r.ret == -13, "sized: ret %d", r.ret);
	FINISH(r);
	r = geturl("https://h/unsized", &o);
	CHECK(r.ret == -13, "unsized: ret %d", r.ret);
	CHECK(r.data == NULL, "unsized: data returned");
	FINISH(r);
	r = geturl("https://h/exact", &o);
	CHECK(r.ret == 1000, "exact: ret %d", r.ret);
	FINISH(r);
}

static void test_https_closed_on_every_path(void)
{
	routes_tls("h", "/404", "HTTP/1.1 404 Not Found\r\nContent-Length: 3\r\n\r\nabc");
	routes_tls("h", "/badstatus", "HTTP/1.1 abc OK\r\n\r\n");
	routes_tls("h", "/cutheaders", "HTTP/1.1 200 OK\r\nContent-Len");
	routes_tls("h", "/short", ok("Content-Length: 100\r\n", "abc"));
	routes_tls("h", "/badlen", ok("Content-Length: x\r\n", "abc"));
	routes_tls("h", "/nolocation", moved("302", ""));
	routes_tls("h", "/chunked", ok("Transfer-Encoding: chunked\r\n", "3\r\nabc\r\n0\r\n\r\n"));
	routes_tls("h", "/ftp", moved("302", "Location: ftp://h/x\r\n"));
	routes_tls("h", "/proto", moved("302", "Location: //evil/x\r\n"));
	const char* paths[] = { "/404", "/badstatus", "/cutheaders", "/short", "/badlen", "/nolocation", "/chunked", "/ftp", "/proto", "/none" };
	for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		std::string url = std::string("https://h") + paths[i];
		Result r = geturl(url.c_str(), NULL);
		CHECK(r.ret < 0, "%s: ret %d", paths[i], r.ret);
		CHECK(r.data == NULL, "%s: data returned", paths[i]);
		CHECK(openSockets == 0, "%s: %d connections left open", paths[i], openSockets);
	}
	CHECK(liveBlocks == 0, "%d heap blocks leaked", liveBlocks);
}

static void test_plain_timeout(void)
{
	route("h", "/f", ok("Content-Length: 3\r\n", "abc"));
	pollTimeouts = 1;
	Result r = get("h", "/f");
	CHECK(r.ret == -6, "ret %d", r.ret);
	FINISH(r);
}

static void test_geturl_bad_urls(void)
{
	const char* urls[] = { "ftp://h/x", "h/x", "https://", "https:///x", "https://h:8443/x", "https://user@h/x", "http://h:80/x", "" };
	for (size_t i = 0; i < sizeof(urls) / sizeof(urls[0]); i++) {
		Result r = geturl(urls[i], NULL);
		CHECK(r.ret == -4, "'%s': ret %d", urls[i], r.ret);
	}
	Result r = geturl(NULL, NULL);
	CHECK(r.ret == -4, "NULL: ret %d", r.ret);
	CHECK(requests.empty(), "requests were sent");
	FINISH(r);
}

static void test_geturl_query_without_path(void)
{
	routes_tls("h", "/?a=1", ok("Content-Length: 1\r\n", "x"));
	Result r = geturl("https://h?a=1#frag", NULL);
	CHECK(r.ret == 1, "ret %d", r.ret);
	CHECK(requests.size() == 1 && requests[0] == "https:h /?a=1", "request '%s'", requests.empty() ? "" : requests[0].c_str());
	FINISH(r);
}

static void test_geturl_http(void)
{
	route("h", "/x", ok("Content-Length: 2\r\n", "ok"));
	Result r = geturl("HTTP://h/x", NULL);
	CHECK(r.ret == 2, "ret %d", r.ret);
	FINISH(r);
}

/* a server that sends a byte every 14 s (each wait is under the 15 s timeout) must not hold a call past its deadline */
static void test_deadline_trickle_headers(void)
{
	route("h", "/f", ok("Content-Length: 5\r\n", "abcde"));
	chunk = 1;
	recvCostMs = 14000;
	u32 start = fakeMs;
	Result r = geturl("http://h/f", NULL);
	CHECK(r.ret == HTTP_ERR_DEADLINE, "ret %d", r.ret);
	CHECK(fakeMs - start <= 120000 + 14000, "took %u ms", (unsigned)(fakeMs - start));
	CHECK(strstr(HttpErrorText(r.ret), "slow") != NULL, "text '%s'", HttpErrorText(r.ret));
	FINISH(r);
}

static void test_deadline_trickle_body_and_options(void)
{
	std::string body = body_of(400);
	std::string unsized = ok("", body);
	route("h", "/unsized", unsized);
	route("h", "/sized", ok("Content-Length: 400\r\n", body));
	routes_tls("h", "/tls", ok("Content-Length: 400\r\n", body));
	chunk = 10;
	recvCostMs = 14000;
	slowAfterPos = unsized.size() - 400; /* the headers at once, then the body trickles */
	HttpOptions o = {};
	o.deadlineMs = 60000;
	Result r = geturl("http://h/unsized", &o);
	CHECK(r.ret == HTTP_ERR_DEADLINE, "unsized: ret %d", r.ret);
	FINISH(r);
	slowAfterPos = ok("Content-Length: 400\r\n", "").size();
	r = geturl("http://h/sized", &o);
	CHECK(r.ret == HTTP_ERR_DEADLINE, "sized: ret %d", r.ret);
	FINISH(r);
	r = geturl("https://h/tls", &o);
	CHECK(r.ret == HTTP_ERR_DEADLINE, "tls: ret %d", r.ret);
	FINISH(r);
	/* a longer deadline lets the same trickle finish: 400 bytes in 10-byte pieces, 40 receives */
	o.deadlineMs = 40 * 14000 + 1000;
	r = geturl("http://h/sized", &o);
	CHECK(r.ret == 400, "long deadline: ret %d", r.ret);
	FINISH(r);
}

static void test_deadline_covers_redirects_and_clock_wrap(void)
{
	/* the deadline is for the whole call, redirects included. The clock starts just before its wrap */
	route("h", "/a", moved("302", "Location: /b\r\n"));
	route("h", "/b", moved("302", "Location: /c\r\n"));
	route("h", "/c", ok("Content-Length: 4\r\n", "done"));
	HttpOptions o = {};
	o.deadlineMs = 120000;
	/* each redirect reply is read byte by byte: 2 s a byte ends the call inside the second hop */
	fakeMs = 0xFFFFFFFFu - 20000;
	recvCostMs = 2000;
	chunk = 1000;
	Result r = geturl("http://h/a", &o);
	CHECK(r.ret == HTTP_ERR_DEADLINE, "ret %d", r.ret);
	CHECK(requests.size() >= 1, "no request");
	FINISH(r);
	/* the same chain at no cost, across the wrap */
	recvCostMs = 0;
	fakeMs = 0xFFFFFFFFu - 20;
	r = geturl("http://h/a", &o);
	CHECK(r.ret == 4, "no cost across the wrap: ret %d", r.ret);
	FINISH(r);
}

static void test_poll_wait_is_cut_to_the_deadline(void)
{
	route("h", "/f", ok("Content-Length: 3\r\n", "abc"));
	HttpOptions o = {};
	o.deadlineMs = 3000;
	Result r = geturl("http://h/f", &o);
	CHECK(r.ret == 3, "ret %d", r.ret);
	CHECK(lastPollTimeout > 0 && lastPollTimeout <= 3000, "poll waited up to %d ms", (int)lastPollTimeout);
	FINISH(r);
}

static void test_unsized_body_grows_by_doubling(void)
{
	std::string body = body_of(1024 * 1024);
	route("h", "/f", ok("", body));
	Result r = geturl("http://h/f", NULL);
	CHECK(r.ret == (s32)body.size(), "ret %d", r.ret);
	CHECK(r.data && memcmp(r.data, body.data(), body.size()) == 0, "body differs");
	CHECK(mallocCalls == 1 && reallocCalls >= 1 && reallocCalls <= 10, "%d mallocs, %d reallocs for 1 MB", mallocCalls, reallocCalls);
	FINISH(r);
	/* the growth stops at maxBody: a body of exactly the cap is kept, one byte more is refused */
	HttpOptions o = {};
	o.maxBody = 10000;
	std::string exact = body_of(10000);
	route("h", "/exact", ok("", exact));
	route("h", "/over", ok("", body_of(10001)));
	r = geturl("http://h/exact", &o);
	CHECK(r.ret == 10000 && r.data && memcmp(r.data, exact.data(), 10000) == 0, "exact: ret %d", r.ret);
	FINISH(r);
	r = geturl("http://h/over", &o);
	CHECK(r.ret == -13 && r.data == NULL, "over: ret %d", r.ret);
	FINISH(r);
}

static void test_error_body_kept_for_a_non_200(void)
{
	std::string json = "{\"message\":\"API rate limit exceeded for 1.2.3.4.\",\"documentation_url\":\"https://docs\"}";
	routes_tls("api.github.com", "/r", "HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\n\r\n" + json);
	routes_tls("api.github.com", "/big", "HTTP/1.1 429 Too Many\r\n\r\n" + body_of(20000));
	HttpOptions o = {};
	o.errorBody = 4096;
	chunk = 50;
	Result r = geturl("https://api.github.com/r", &o);
	CHECK(r.ret == -10 && GetLastHttpReply() == 403, "ret %d reply %d", r.ret, (int)GetLastHttpReply());
	CHECK(json == GetLastHttpErrorBody(), "body '%s'", GetLastHttpErrorBody());
	FINISH(r);
	/* bounded */
	r = geturl("https://api.github.com/big", &o);
	CHECK(r.ret == -10 && GetLastHttpReply() == 429 && strlen(GetLastHttpErrorBody()) == 4096, "capped: %d bytes", (int)strlen(GetLastHttpErrorBody()));
	FINISH(r);
	/* not asked for: nothing kept */
	r = geturl("https://api.github.com/r", NULL);
	CHECK(r.ret == -10 && GetLastHttpErrorBody()[0] == 0, "not asked: '%s'", GetLastHttpErrorBody());
	FINISH(r);
	/* a later call starts clean */
	r = geturl("https://api.github.com/r", &o);
	FINISH(r);
	r = geturl("https://api.github.com/nothing", &o);
	CHECK(r.ret == -10 && GetLastHttpErrorBody()[0] == 0, "a 404 without a body keeps none");
	FINISH(r);
}

static const struct hc_test tests[] = {
	{ "content_length", test_content_length, false },
	{ "no_content_length", test_no_content_length, false },
	{ "empty_body_no_headers", test_empty_body_no_headers, false },
	{ "header_case", test_header_case, false },
	{ "bad_content_length", test_bad_content_length, false },
	{ "largest_content_length", test_largest_content_length, false },
	{ "redirect_absolute", test_redirect_absolute, false },
	{ "redirect_relative_and_case", test_redirect_relative_and_case, false },
	{ "redirect_chain_then_unsized", test_redirect_chain_then_unsized, false },
	{ "redirect_loop", test_redirect_loop, false },
	{ "redirect_unsupported", test_redirect_unsupported, false },
	{ "status_404", test_status_404, false },
	{ "bad_status_line", test_bad_status_line, false },
	{ "truncated_body", test_truncated_body, false },
	{ "truncated_headers", test_truncated_headers, false },
	{ "header_too_long", test_header_too_long, false },
	{ "resolve_failure", test_resolve_failure, false },
	{ "connect_failure", test_connect_failure, false },
	{ "alloc_failure_unsized", test_alloc_failure_unsized, false },
	{ "alloc_failure_sized", test_alloc_failure_sized, false },
	{ "null_arguments", test_null_arguments, false },
	{ "https_basic", test_https_basic, false },
	{ "plain_default_user_agent_is_the_old_one", test_plain_default_user_agent_is_the_old_one, false },
	{ "https_cross_host_redirect", test_https_cross_host_redirect, false },
	{ "https_long_location_needs_the_bigger_line", test_https_long_location_needs_the_bigger_line, false },
	{ "https_relative_redirect_stays_https", test_https_relative_redirect_stays_https, false },
	{ "https_to_http_refused", test_https_to_http_refused, false },
	{ "http_to_https_allowed", test_http_to_https_allowed, false },
	{ "https_redirect_loop", test_https_redirect_loop, false },
	{ "tls_open_failure", test_tls_open_failure, false },
	{ "tls_truncated", test_tls_truncated, false },
	{ "https_body_cap", test_https_body_cap, false },
	{ "https_closed_on_every_path", test_https_closed_on_every_path, false },
	{ "plain_timeout", test_plain_timeout, false },
	{ "geturl_bad_urls", test_geturl_bad_urls, false },
	{ "geturl_query_without_path", test_geturl_query_without_path, false },
	{ "geturl_http", test_geturl_http, false },
	{ "deadline_trickle_headers", test_deadline_trickle_headers, false },
	{ "deadline_trickle_body_and_options", test_deadline_trickle_body_and_options, false },
	{ "deadline_covers_redirects_and_clock_wrap", test_deadline_covers_redirects_and_clock_wrap, false },
	{ "poll_wait_is_cut_to_the_deadline", test_poll_wait_is_cut_to_the_deadline, false },
	{ "unsized_body_grows_by_doubling", test_unsized_body_grows_by_doubling, false },
	{ "error_body_kept_for_a_non_200", test_error_body_kept_for_a_non_200, false },
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, reset);
}
