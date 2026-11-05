/*
 * update_probe: exercises Priiloader's GitHub updater path (HttpGetUrl, update_github) in Dolphin or on a Wii.
 *
 *   a) the releases of Monsterray/priiloader-reborn through HttpGetUrl with the options main.cpp uses, parsed and
 *      selected (the repository has no releases: "no updates" is the expected answer)
 *   b) the newest Mbed-TLS release from the API: its sha256sum.txt and an older .tar.bz2 are downloaded through
 *      browser_download_url (a redirect chain to another host) and checked with gh_check_digest
 *   c) for each download a raw look (tls_*) at the same chain: was there a Content-Length, how did the body end
 *      (close_notify or not)
 *
 * Every line the test is judged by starts with "UPDATEPROBE:", on screen and in the debug log (SYS_Report).
 * The last line is "UPDATEPROBE: RESULT PASS" or "... FAIL".
 */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <network.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <string>
#include <vector>
#ifdef PROBE_NETLOG
#include "hbc_netlog.h"
#endif
#include "HTTP_Parser.h"
#include "update_github.h"
#include "mem2_manager.h"
#include "tls.h"

static void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

static void say(const char* fmt, ...)
{
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	printf("%s\n", line);
	SYS_Report("%s\n", line);
}

//the shim for src/priiloader/source/gecko.cpp, which HTTP_Parser.cpp logs through
void gprintf(const char* str, ...)
{
	char line[256];
	va_list ap;
	va_start(ap, str);
	vsnprintf(line, sizeof(line), str, ap);
	va_end(ap);
	SYS_Report("%s\n", line);
}

//src/priiloader/source/Global.cpp's InitNetwork is not linked; HttpGetUrl does not need it
s8 InitNetwork() { return 0; }
void ClearScreen() {}

static void video_init(void)
{
	VIDEO_Init();
	GXRModeObj* rmode = VIDEO_GetPreferredMode(NULL);
	void* xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
}

//----- c) the raw look at a download chain -----

static bool split_url(const std::string& url, std::string& host, std::string& path)
{
	if (url.compare(0, 8, "https://") != 0)
		return false;
	size_t slash = url.find('/', 8);
	host = url.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
	path = slash == std::string::npos ? "/" : url.substr(slash);
	return !host.empty();
}

static std::string header_value(const std::string& headers, const char* name)
{
	std::string low = headers;
	for (size_t i = 0; i < low.size(); i++)
		low[i] = (char)tolower((unsigned char)low[i]);
	std::string key = std::string("\r\n") + name + ":";
	size_t p = low.find(key);
	if (p == std::string::npos)
		return "";
	p += key.size();
	while (p < headers.size() && headers[p] == ' ')
		p++;
	size_t e = headers.find("\r\n", p);
	return headers.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

//follows the redirects of url over tls_* and reports, for each hop, status, Content-Length, Transfer-Encoding and how
//the body of the last one ended. Returns the number of body bytes of the final answer, -1 on a failure.
static long raw_chain(const char* label, std::string url, std::string& endText)
{
	for (int hop = 0; hop < 6; hop++)
	{
		std::string host, path;
		if (!split_url(url, host, path))
		{
			say("UPDATEPROBE: %s raw hop %d: not an https url", label, hop);
			return -1;
		}
		tls_conn* c = NULL;
		int r = tls_open(&c, host.c_str(), 443, 20000);
		if (r < 0)
		{
			say("UPDATEPROBE: %s raw hop %d: open %s failed %d (%s)", label, hop, host.c_str(), r, tls_strerror(r));
			return -1;
		}
		std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host +
			"\r\nUser-Agent: priiloader-reborn/probe\r\nAccept: */*\r\nConnection: close\r\n\r\n";
		if (tls_write(c, req.data(), req.size()) < 0)
		{
			tls_close(c);
			return -1;
		}

		std::string headers;
		std::string body;
		bool inBody = false;
		int last = 1;
		char buf[4096];
		while (true)
		{
			last = tls_read(c, buf, sizeof(buf));
			if (last <= 0)
				break;
			if (!inBody)
			{
				headers.append(buf, last);
				size_t e = headers.find("\r\n\r\n");
				if (e != std::string::npos)
				{
					body = headers.substr(e + 4);
					headers.resize(e + 2);
					inBody = true;
				}
			}
			else
				body.append(buf, last);
		}
		tls_close(c);

		int status = 0;
		if (headers.size() > 12)
			status = atoi(headers.c_str() + 9);
		std::string cl = header_value(headers, "content-length");
		std::string te = header_value(headers, "transfer-encoding");
		std::string loc = header_value(headers, "location");
		endText = last == 0 ? "clean close_notify" : (last == TLS_ERR_TRUNCATED ? "closed WITHOUT close_notify" : tls_strerror(last));
		say("UPDATEPROBE: %s raw hop %d host=%s status=%d content-length=%s transfer-encoding=%s body=%u end=%s (%d)", label, hop,
			host.c_str(), status, cl.empty() ? "none" : cl.c_str(), te.empty() ? "none" : te.c_str(), (unsigned)body.size(),
			endText.c_str(), last);
		if (status >= 300 && status < 400 && !loc.empty())
		{
			url = loc;
			continue;
		}
		return status == 200 ? (long)body.size() : -1;
	}
	return -1;
}

//----- a) and b) through HttpGetUrl -----

static void options(HttpOptions& opt, char* ua, size_t size)
{
	snprintf(ua, size, "priiloader-reborn/%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
	opt = HttpOptions();
	opt.userAgent = ua;
	opt.maxHeaderLine = 4096; //the same as main.cpp's GithubOptions
}

static bool fetch_releases(const std::string& url, std::vector<GhRelease>& releases)
{
	char ua[64];
	HttpOptions opt;
	options(opt, ua, sizeof(ua));
	opt.accept = "application/vnd.github+json";
	opt.maxBody = GH_MAX_JSON;

	u8* json = NULL;
	u64 t0 = gettime();
	s32 size = HttpGetUrl(url.c_str(), &opt, json);
	say("UPDATEPROBE: GET %s -> %d (%s) in %u ms, tls error %d, HTTP %d", url.c_str(), (int)size,
		size < 0 ? HttpErrorText(size) : "ok", (unsigned)ticks_to_millisecs(gettime() - t0), GetLastTlsError(), (int)GetLastHttpReply());
	if (size <= 0 || json == NULL)
	{
		mem_free(json);
		return false;
	}
	std::string message;
	int parsed = gh_parse_releases(reinterpret_cast<const char*>(json), size, releases, &message);
	mem_free(json);
	say("UPDATEPROBE: parse: %d, %u releases %s", parsed, (unsigned)releases.size(), message.c_str());
	return parsed == GH_PARSE_OK;
}

static const GhAsset* find_asset(const GhRelease& r, const char* suffix)
{
	size_t sl = strlen(suffix);
	for (size_t i = 0; i < r.assets.size(); i++)
		if (r.assets[i].name.size() >= sl && r.assets[i].name.compare(r.assets[i].name.size() - sl, sl, suffix) == 0)
			return &r.assets[i];
	return NULL;
}

//downloads an asset through HttpGetUrl and checks its digest; also the raw look at the chain. true when all is as expected.
static bool download_and_check(const GhAsset& a)
{
	char ua[64];
	HttpOptions opt;
	options(opt, ua, sizeof(ua));
	opt.deadlineMs = 600000; //as main.cpp's DownloadGithubUpdate
	say("UPDATEPROBE: asset %s, %u bytes, digest %s", a.name.c_str(), (unsigned)a.size, a.digest.c_str());

	u8* data = NULL;
	u64 t0 = gettime();
	s32 size = HttpGetUrl(a.url.c_str(), &opt, data);
	unsigned ms = (unsigned)ticks_to_millisecs(gettime() - t0);
	say("UPDATEPROBE: download %s -> %d (%s) in %u ms, tls error %d, HTTP %d", a.name.c_str(), (int)size,
		size < 0 ? HttpErrorText(size) : "ok", ms, GetLastTlsError(), (int)GetLastHttpReply());
	if (size <= 0 || data == NULL)
	{
		mem_free(data);
		return false;
	}
	bool sizeOk = (uint32_t)size == a.size;
	bool digestOk = gh_check_digest(data, size, a.digest.c_str());
	//a corrupted copy must be refused
	data[size / 2] ^= 1;
	bool corruptRefused = !gh_check_digest(data, size, a.digest.c_str());
	mem_free(data);
	say("UPDATEPROBE: %s size %s, digest %s, corrupted copy %s", a.name.c_str(), sizeOk ? "matches" : "DIFFERS",
		digestOk ? "MATCHES" : "DIFFERS", corruptRefused ? "refused" : "NOT refused");
	return sizeOk && digestOk && corruptRefused;
}

int main(void)
{
	int pass = 1;

	video_init();
#ifdef PROBE_NETLOG
	hbc_netlog_init();
#endif
	SYS_STDIO_Report(false);
	say("UPDATEPROBE: start");
	AddMem2Area(14 * 1024 * 1024, OTHER_AREA);
	ISFS_Initialize();

	s32 ret;
	while ((ret = net_init()) == -EAGAIN)
		;
	if (ret < 0)
	{
		say("UPDATEPROBE: net_init failed: %d", (int)ret);
		say("UPDATEPROBE: RESULT FAIL");
		return 1;
	}
	u32 ip = net_gethostip();
	say("UPDATEPROBE: network up, ip %u.%u.%u.%u", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
	time_t now = time(NULL);
	char tbuf[64];
	strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", gmtime(&now));
	say("UPDATEPROBE: clock %s UTC", tbuf);

	//a) our repository
	{
		const std::string url = std::string("https://api.github.com/repos/") + "Monsterray" + "/" + "priiloader-reborn" + "/releases?per_page=30";
		std::vector<GhRelease> releases;
		std::string end;
		if (!fetch_releases(url, releases))
		{
			say("UPDATEPROBE: a) FAIL: releases of our repository not read");
			pass = 0;
		}
		else
		{
			GhPick prod, rc;
			gh_select(releases, prod, rc);
			say("UPDATEPROBE: a) %u releases; release pick status %d (%s), rc pick status %d", (unsigned)releases.size(), (int)prod.status,
				prod.tag.c_str(), (int)rc.status);
			if (releases.empty() && prod.status == GH_PICK_NONE && rc.status == GH_PICK_NONE)
				say("UPDATEPROBE: a) PASS: no releases, so no updates");
			else
				say("UPDATEPROBE: a) the repository now has releases; shown, not judged");
		}
		raw_chain("a", url, end);
	}

	//b) a real release with assets: Mbed-TLS
	{
		std::vector<GhRelease> releases;
		if (!fetch_releases("https://api.github.com/repos/Mbed-TLS/mbedtls/releases?per_page=3", releases) || releases.size() < 2)
		{
			say("UPDATEPROBE: b) FAIL: Mbed-TLS releases not read");
			pass = 0;
		}
		else
		{
			GhPick prod, rc;
			gh_select(releases, prod, rc);
			say("UPDATEPROBE: b) newest release %s, pick status %d (its tag is not vX.Y.Z: expected NONE = %d)", releases[0].tag.c_str(),
				(int)prod.status, (int)GH_PICK_NONE);
			const GhAsset* sums = find_asset(releases[0], "sha256sum.txt");
			const GhAsset* tar = find_asset(releases.back(), ".tar.bz2");
			if (prod.status != GH_PICK_NONE || rc.status != GH_PICK_NONE)
			{
				say("UPDATEPROBE: b) FAIL: gh_select picked a release whose tag is not vX.Y.Z");
				pass = 0;
			}
			if (sums == NULL || tar == NULL)
			{
				say("UPDATEPROBE: b) FAIL: assets not found");
				pass = 0;
			}
			else
			{
				std::string end;
				if (!download_and_check(*sums))
					pass = 0;
				raw_chain("sums", sums->url, end);
				if (!download_and_check(*tar))
					pass = 0;
			}
		}
	}

	struct mallinfo mi = mallinfo();
	say("UPDATEPROBE: heap: libc took %u bytes from the system (high-water), %u in use now; MEM1 arena lo 0x%08x hi 0x%08x",
		(unsigned)mi.arena, (unsigned)mi.uordblks, (unsigned)SYS_GetArena1Lo(), (unsigned)SYS_GetArena1Hi());
	say("UPDATEPROBE: RESULT %s", pass ? "PASS" : "FAIL");
	tls_shutdown();
#ifdef PROBE_NETLOG
	hbc_netlog_close();
#endif
	net_deinit();
	return pass ? 0 : 1;
}
