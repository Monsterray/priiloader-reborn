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

#define MAX_BODY (16 * 1024 * 1024)

/* ---- the fake network ---- */

struct Route {
	std::string host, path, response;
};
struct Sock {
	bool open;
	std::string reply;
	size_t pos;
};

static std::vector<Route> routes;
static std::vector<Sock> socks;
static std::vector<std::string> requests; /* "host path" of every GET, in order */
static int openSockets;
static size_t chunk;
static bool resolveFails;
static bool connectFails;
static int liveBlocks;
static int allocFailAfter; /* -1: never; n: the n+1th allocation fails */
static size_t maxRecvLen;

static void reset(void)
{
	routes.clear();
	socks.clear();
	requests.clear();
	openSockets = 0;
	chunk = 1000;
	resolveFails = false;
	connectFails = false;
	liveBlocks = 0;
	allocFailAfter = -1;
	maxRecvLen = 0;
	/* sockets start at 1: HttpGet treats 0 as "no socket" */
	Sock none = { false, "", 0 };
	socks.push_back(none);
}

extern "C" {

s32 net_socket(u32, u32, u32)
{
	Sock s = { true, "", 0 };
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

s32 net_send(s32 s, const void* data, s32 size, u32)
{
	if (s <= 0 || s >= (s32)socks.size() || !socks[s].open)
		hc_hang("send on socket %d that is not open", s);
	std::string req((const char*)data, (size_t)size);
	size_t p = req.find("GET ");
	size_t e = req.find(" HTTP/", p);
	size_t h = req.find("Host: ");
	size_t he = req.find("\r\n", h);
	if (p != 0 || e == std::string::npos || h == std::string::npos || he == std::string::npos)
		hc_hang("malformed request: %s", req.c_str());
	std::string path = req.substr(4, e - 4);
	std::string host = req.substr(h + 6, he - h - 6);
	requests.push_back(host + " " + path);
	socks[s].reply = "HTTP/1.0 404 Not Found\r\n\r\n";
	for (size_t i = 0; i < routes.size(); i++)
		if (routes[i].host == host && routes[i].path == path)
			socks[s].reply = routes[i].response;
	return size;
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
	return malloc(size);
}

void* mem2_realloc(void* ptr, u32 size, const int)
{
	if (size == 0 || size > MAX_BODY + 64)
		hc_hang("realloc to %u bytes", size);
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
	Route r = { host, path, response };
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
	route("h", "/f", ok("", body_of(3000)));
	chunk = 700;
	allocFailAfter = 2; /* the first chunk and the first growth work, the next growth fails */
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
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, reset);
}
