/*
 * Host tests for the GitHub update logic (src/priiloader/source/update_github.cpp, built unchanged): the releases
 * JSON reader, the tag format, the choice of the release to offer, the version rules both update sources share,
 * the sha256 digest check and the changelog text.
 */
#define HOSTCHECK_MAIN
#include "hostcheck.h"

#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "update_github.h"

static const char* GOOD_DIGEST = "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static std::string asset(const std::string& name, const char* digest = NULL, const std::string& url = "")
{
	std::string u = url.empty() ? "https://github.com/o/r/releases/download/x/" + name : url;
	std::string a = "{\"url\":\"https://api.github.com/a/1\",\"id\":7,\"name\":\"" + name + "\",\"size\":1234,\"browser_download_url\":\"" + u + "\"";
	if (digest)
		a += std::string(",\"digest\":\"") + digest + "\"";
	return a + ",\"uploader\":{\"login\":\"x\",\"id\":1}}";
}

static std::string release(const std::string& tag, bool draft, bool pre, const std::string& assets, const std::string& body = "notes")
{
	return "{\"url\":\"u\",\"tag_name\":\"" + tag + "\",\"draft\":" + (draft ? "true" : "false") + ",\"prerelease\":" +
		(pre ? "true" : "false") + ",\"body\":\"" + body + "\",\"assets\":[" + assets + "],\"author\":{\"a\":[1,2,{\"b\":null}]}}";
}

static std::string list(const std::vector<std::string>& items)
{
	std::string s = "[";
	for (size_t i = 0; i < items.size(); i++)
		s += (i ? "," : "") + items[i];
	return s + "]";
}

static std::string full(const std::string& tag, bool draft = false, bool pre = false)
{
	return release(tag, draft, pre, asset("Priiloader_Update.dol", GOOD_DIGEST) + "," + asset("other.zip"));
}

static int parse(const std::string& json, std::vector<GhRelease>& out, std::string* msg = NULL)
{
	return gh_parse_releases(json.data(), json.size(), out, msg);
}

static version_t V(int a, int b, int c, int rc = 0)
{
	version_t v = { (unsigned char)a, (unsigned char)b, (unsigned char)c, (unsigned char)rc };
	return v;
}

static void test_empty_list(void)
{
	std::vector<GhRelease> r;
	CHECK(parse("[]", r) == GH_PARSE_OK && r.empty(), "empty");
	CHECK(parse("  [ \n ] \n", r) == GH_PARSE_OK && r.empty(), "empty with space");
	GhPick prod, rc;
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_NONE && rc.status == GH_PICK_NONE, "nothing to pick");
}

static void test_only_drafts(void)
{
	std::vector<GhRelease> r;
	CHECK(parse(list({ full("v1.0.0", true), full("v1.1.0-rc1", true, true) }), r) == GH_PARSE_OK && r.size() == 2, "parse");
	GhPick prod, rc;
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_NONE && rc.status == GH_PICK_NONE, "a draft was picked");
}

static void test_prerelease_flag(void)
{
	std::vector<GhRelease> r;
	//v2.0.0 is flagged prerelease without an rc tag: never a release. v1.5.0-rc2 is the rc.
	CHECK(parse(list({ full("v2.0.0", false, true), full("v1.5.0-rc2", false, true), full("v1.4.0") }), r) == GH_PARSE_OK, "parse");
	GhPick prod, rc;
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_OK && prod.tag == "v1.4.0", "prod '%s'", prod.tag.c_str());
	CHECK(rc.status == GH_PICK_OK && rc.tag == "v1.5.0-rc2" && rc.version.sub_version == 2, "rc '%s'", rc.tag.c_str());
	CHECK(prod.version.minor == 4 && prod.version.sub_version == 0, "prod version");
	//with rc updates off the menu ignores rc: that is update_rc_available's showRc
	CHECK(!update_rc_available(V(1, 4, 0), rc.version, false), "rc shown when off");
	CHECK(update_rc_available(V(1, 4, 0), rc.version, true), "rc not offered when on");
}

static void test_newest_wins_whatever_the_order(void)
{
	std::vector<GhRelease> r;
	CHECK(parse(list({ full("v1.2.0"), full("v1.10.0"), full("v1.9.9"), full("v0.99.99"), full("v1.11.0-rc3"), full("v1.11.0-rc10"), full("v1.11.0-rc2") }), r) == GH_PARSE_OK, "parse");
	GhPick prod, rc;
	gh_select(r, prod, rc);
	CHECK(prod.tag == "v1.10.0", "prod %s", prod.tag.c_str());
	CHECK(rc.tag == "v1.11.0-rc10", "rc %s", rc.tag.c_str());
}

static void test_stale_rc_is_dropped(void)
{
	std::vector<GhRelease> r;
	GhPick prod, rc;
	//a candidate of an older version, in either order
	CHECK(parse(list({ full("v1.2.0-rc1", false, true), full("v1.3.0") }), r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(prod.tag == "v1.3.0" && rc.status == GH_PICK_NONE, "older rc offered: '%s'", rc.tag.c_str());
	CHECK(parse(list({ full("v1.3.0"), full("v1.2.0-rc1", false, true) }), r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(prod.tag == "v1.3.0" && rc.status == GH_PICK_NONE, "older rc offered (other order)");
	//the candidate of the release that is out already
	CHECK(parse(list({ full("v1.3.0"), full("v1.3.0-rc4", false, true) }), r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(rc.status == GH_PICK_NONE, "rc of the released version offered");
	//a newer version's candidate stays; with no release at all any candidate stays
	CHECK(parse(list({ full("v1.3.0"), full("v1.3.1-rc1", false, true), full("v1.2.0-rc9", false, true) }), r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(rc.tag == "v1.3.1-rc1", "newer rc: '%s'", rc.tag.c_str());
	CHECK(parse(list({ full("v1.2.0-rc1", false, true) }), r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_NONE && rc.tag == "v1.2.0-rc1", "rc without a release");
}

static void test_odd_tags_ignored(void)
{
	std::vector<GhRelease> r;
	CHECK(parse(list({ full("1.2.3"), full("v1.2"), full("nightly"), full("v1.2.3x"), full("v1.2.3-rc0"), full("v256.0.0"), full("v1.2.3-beta"), full("v1.2.3-rc") }), r) == GH_PARSE_OK, "parse");
	GhPick prod, rc;
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_NONE && rc.status == GH_PICK_NONE, "a malformed tag was picked: %s", prod.tag.c_str());
}

static void test_tags(void)
{
	version_t v;
	bool rc = true;
	CHECK(gh_parse_tag("v1.2.3", &v, &rc) && !rc && v.major == 1 && v.minor == 2 && v.patch == 3 && v.sub_version == 0, "v1.2.3");
	CHECK(gh_parse_tag("v1.2.3-rc2", &v, &rc) && rc && v.sub_version == 2, "rc2");
	CHECK(gh_parse_tag("v255.255.255-rc255", &v, &rc) && v.major == 255 && v.sub_version == 255, "limits");
	CHECK(gh_parse_tag("v0.10.1", NULL, NULL), "null outputs");
	const char* bad[] = { "", "v", "v1", "v1.2", "1.2.3", "V1.2.3", "v1.2.3.4", "v1.2.3-rc0", "v1.2.3-rc", "v1.2.3-rc256", "v256.0.0",
		"v1.256.0", "v1.0.256", "v1.2.3x", "v1.2.3 ", " v1.2.3", "v1..3", "v-1.2.3", "v1.2.3-RC1", "v1.2.3-rc1x", "v01.2.3x", "v1.2.3-", "v0001.2.3", NULL };
	for (int i = 0; bad[i]; i++)
	{
		CHECK(!gh_parse_tag(bad[i], &v, &rc), "'%s' accepted", bad[i]);
	}
	CHECK(!gh_parse_tag(NULL, &v, &rc), "NULL");
}

static void test_assets_and_digests(void)
{
	std::vector<GhRelease> r;
	GhPick prod, rc;
	//no asset by that name
	CHECK(parse(list({ release("v1.0.0", false, false, asset("other.zip", GOOD_DIGEST)) }), r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_NO_ASSET && prod.url.empty(), "no asset: %d", prod.status);
	//no assets at all
	CHECK(parse(list({ release("v1.0.0", false, false, "") }), r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_NO_ASSET, "empty assets %d", prod.status);
	//missing digest, null digest, bad digest
	CHECK(parse(list({ release("v1.0.0", false, false, asset("Priiloader_Update.dol")) }), r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_NO_DIGEST, "missing digest %d", prod.status);
	std::string nul = "{\"name\":\"Priiloader_Update.dol\",\"browser_download_url\":\"https://x/y\",\"digest\":null}";
	CHECK(parse(list({ release("v1.0.0", false, false, nul) }), r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_NO_DIGEST && prod.url == "https://x/y", "null digest %d", prod.status);
	const char* bad[] = { "sha256:abc", "md5:0123456789abcdef0123456789abcdef", "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg",
		"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0", "SHA256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "", NULL };
	for (int i = 0; bad[i]; i++)
	{
		CHECK(parse(list({ release("v1.0.0", false, false, asset("Priiloader_Update.dol", bad[i])) }), r) == GH_PARSE_OK, "parse");
		gh_select(r, prod, rc);
		CHECK(prod.status == GH_PICK_NO_DIGEST, "digest '%s' accepted", bad[i]);
	}
	//the good one, with the asset not first, and the link kept
	CHECK(parse(list({ full("v1.0.0") }), r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_OK && prod.digest == GOOD_DIGEST && prod.size == 1234 &&
		prod.url == "https://github.com/o/r/releases/download/x/Priiloader_Update.dol", "good pick");
	//an asset with an empty download link is no asset
	CHECK(parse(list({ release("v1.0.0", false, false, "{\"name\":\"Priiloader_Update.dol\",\"digest\":\"" + std::string(GOOD_DIGEST) + "\"}") }), r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_NO_ASSET, "no link %d", prod.status);
}

static void test_malformed_json(void)
{
	std::vector<GhRelease> r;
	const char* bad[] = { "", "   ", "x", "[", "[{", "[{\"tag_name\":", "[{\"tag_name\":\"v1.0.0\"", "[{\"tag_name\":\"v1.0.0\"}", "[1,]", "[,]", "[{\"a\" 1}]",
		"[{\"a\":tru}]", "[{\"a\":\"unterminated}]", "[{\"a\":\"bad\\q\"}]", "[{\"a\":\"\\u12\"}]", "[] x", "[][]", "[{\"a\":1}{\"b\":2}]", "[{a:1}]", "null", "\"s\"", "12", NULL };
	for (int i = 0; bad[i]; i++)
	{
		int e = parse(bad[i], r);
		CHECK(e < 0 && r.empty(), "'%s' gave %d", bad[i], e);
	}
	CHECK(gh_parse_releases(NULL, 0, r, NULL) < 0, "NULL");
}

static void test_truncated_json(void)
{
	std::vector<GhRelease> r;
	std::string whole = list({ full("v1.0.0"), full("v1.1.0-rc1", false, true) });
	CHECK(parse(whole, r) == GH_PARSE_OK && r.size() == 2, "whole");
	for (size_t n = 0; n < whole.size(); n++)
	{
		int e = gh_parse_releases(whole.data(), n, r, NULL);
		CHECK(e < 0 && r.empty(), "cut at %d gave %d with %d releases", (int)n, e, (int)r.size());
	}
}

static void test_too_big(void)
{
	std::vector<GhRelease> r;
	std::string big = "[" + std::string(1100 * 1024, ' ') + "]";
	CHECK(parse(big, r) == GH_PARSE_TOO_BIG, "1100 KB");
	std::string edge = "[" + std::string(GH_MAX_JSON - 2, ' ') + "]";
	CHECK(edge.size() == GH_MAX_JSON && parse(edge, r) == GH_PARSE_OK, "exactly the limit");
}

static void test_depth_bomb(void)
{
	std::vector<GhRelease> r;
	std::string bomb(100000, '[');
	CHECK(parse(bomb, r) == GH_PARSE_TOO_DEEP, "arrays: %d", parse(bomb, r));
	std::string inner = "[{\"x\":" + std::string(50000, '[') + "}]";
	CHECK(parse(inner, r) == GH_PARSE_TOO_DEEP, "inside a release: %d", parse(inner, r));
	std::string obj;
	for (int i = 0; i < 5000; i++)
		obj += "{\"a\":";
	CHECK(parse("[" + obj, r) == GH_PARSE_TOO_DEEP, "objects");
	std::string asset_bomb = "[{\"tag_name\":\"v1.0.0\",\"assets\":[{\"name\":\"n\",\"u\":" + std::string(40000, '[') + "}]}]";
	CHECK(parse(asset_bomb, r) == GH_PARSE_TOO_DEEP, "in an asset");
	std::string err = "{\"message\":\"m\",\"x\":" + std::string(40000, '[');
	CHECK(parse(err, r) == GH_PARSE_TOO_DEEP, "in an error object");
	//a deep but legal value is skipped
	std::string ok = "[{\"tag_name\":\"v1.0.0\",\"x\":" + std::string(10, '[') + std::string(10, ']') + "}]";
	CHECK(parse(ok, r) == GH_PARSE_OK && r.size() == 1, "legal depth");
}

static void test_error_object(void)
{
	std::vector<GhRelease> r;
	std::string msg;
	CHECK(parse("{\"message\":\"API rate limit exceeded for 1.2.3.4.\",\"documentation_url\":\"https://docs\"}", r, &msg) == GH_PARSE_NOT_ARRAY, "rate limit");
	CHECK(msg == "API rate limit exceeded for 1.2.3.4.", "message '%s'", msg.c_str());
	CHECK(parse("{\"documentation_url\":\"x\",\"message\":\"Not Found\",\"status\":\"404\"}", r, &msg) == GH_PARSE_NOT_ARRAY && msg == "Not Found", "not found, reordered");
	CHECK(parse("{}", r, &msg) == GH_PARSE_NOT_ARRAY && msg.empty(), "empty object");
	CHECK(parse("{\"message\":\"x\"", r, &msg) == GH_PARSE_MALFORMED, "cut error object");
	CHECK(parse("{\"message\":\"x\"}", r, NULL) == GH_PARSE_NOT_ARRAY, "no message pointer");
}

static void test_thirty_releases_fit(void)
{
	//30 releases with a long changelog and a few assets each, as the API sends them, fit the reply cap
	std::vector<std::string> many;
	std::string notes(10000, 'n');
	for (int i = 0; i < GH_MAX_RELEASES; i++)
		many.push_back(release("v1.0." + std::to_string(i), false, false,
			asset("Priiloader_Update.dol", GOOD_DIGEST) + "," + asset("a.zip") + "," + asset("b.zip") + "," + asset("c.zip"), notes));
	std::string json = list(many);
	std::vector<GhRelease> r;
	CHECK(json.size() < GH_MAX_JSON, "%d bytes of 30 releases against the cap of %d", (int)json.size(), (int)GH_MAX_JSON);
	CHECK(parse(json, r) == GH_PARSE_OK && r.size() == GH_MAX_RELEASES, "parse of 30");
	//10 newer candidates do not hide the newest release in the first 30
	std::vector<std::string> rcs;
	for (int i = 0; i < 10; i++)
		rcs.push_back(full("v2.0.0-rc" + std::to_string(i + 1), false, true));
	rcs.push_back(full("v1.9.0"));
	CHECK(parse(list(rcs), r) == GH_PARSE_OK, "parse");
	GhPick prod, rc;
	gh_select(r, prod, rc);
	CHECK(prod.tag == "v1.9.0" && rc.tag == "v2.0.0-rc10", "prod '%s' rc '%s'", prod.tag.c_str(), rc.tag.c_str());
}

static void test_error_message_for_the_screen(void)
{
	const std::string limit = "{\"message\":\"API rate limit exceeded for 1.2.3.4. (But here's the good news: Authenticated requests get a higher rate limit.)\",\"documentation_url\":\"https://docs\",\"status\":\"403\"}";
	std::string m = gh_error_message(limit.data(), limit.size(), 46);
	CHECK(m == "API rate limit exceeded for 1.2.3.4. (But here", "cut at the width: '%s'", m.c_str());
	CHECK(m.size() <= 46, "width");
	//ASCII only, controls gone, no CR or LF
	const std::string odd = "{\"message\":\"  caf\\u00e9\\r\\nBad\\u0007\\ttext \\ud83d\\ude00 end  \"}";
	m = gh_error_message(odd.data(), odd.size(), 46);
	CHECK(m == "caf? Bad text ? end", "filtered: '%s'", m.c_str());
	for (size_t i = 0; i < m.size(); i++)
		CHECK((unsigned char)m[i] >= 0x20 && (unsigned char)m[i] < 0x7F, "byte %02x", (unsigned char)m[i]);
	//a body cut short after its message still gives it; no message, not JSON, empty and NULL give ""
	const std::string cut = "{\"message\":\"Rate limited\",\"documentation_url\":\"https://do";
	CHECK(gh_error_message(cut.data(), cut.size(), 46) == "Rate limited", "cut body");
	CHECK(gh_error_message("{}", 2, 46).empty(), "no message");
	CHECK(gh_error_message("<html>nope</html>", 17, 46).empty(), "html");
	CHECK(gh_error_message("", 0, 46).empty(), "empty");
	CHECK(gh_error_message(NULL, 0, 46).empty(), "NULL");
	CHECK(gh_ascii_line("a\x01\x02 \xC3\xA9\xC3\xA9 b", 10) == "a ?? b", "line");
	CHECK(gh_ascii_line(std::string(100, 'x'), 7) == "xxxxxxx", "cut");
}

static void test_escapes(void)
{
	std::vector<GhRelease> r;
	std::string body = "line1\\nline2 \\\"quoted\\\" tab\\t caf\\u00e9 \\ud83d\\ude00 slash\\/ back\\\\ lone\\ud83d end";
	CHECK(parse(list({ release("v1.0.0", false, false, "", body) }), r) == GH_PARSE_OK && r.size() == 1, "parse");
	const std::string& b = r[0].body;
	CHECK(b.find("line1\nline2 \"quoted\" tab\t caf") == 0, "plain escapes: %s", b.c_str());
	CHECK(b.find("caf\xC3\xA9 ") != std::string::npos, "u00e9 as UTF-8");
	CHECK(b.find("\xF0\x9F\x98\x80") != std::string::npos, "a surrogate pair is U+1F600");
	CHECK(b.find("slash/ back\\ lone? end") != std::string::npos, "lone surrogate: %s", b.c_str());
	//a tag with an escape does not become a different tag
	CHECK(parse("[{\"tag_name\":\"v1.0.\\u0030\",\"draft\":false,\"prerelease\":false}]", r) == GH_PARSE_OK && r[0].tag == "v1.0.0", "escaped tag");
	//lone low surrogate and bad second half
	CHECK(parse("[{\"body\":\"\\udc00 \\ud800\\u0041\"}]", r) == GH_PARSE_OK && r[0].body == "? ?A", "low and bad pair: %s", r[0].body.c_str());
	//a string cap: a huge body is cut, not refused
	std::string huge = release("v1.0.0", false, false, "", std::string(100000, 'a'));
	CHECK(parse("[" + huge + "]", r) == GH_PARSE_OK && r[0].body.size() == 16 * 1024, "body capped at %d", (int)r[0].body.size());
}

static void test_unknown_and_reordered_fields(void)
{
	std::vector<GhRelease> r;
	std::string a = "{\"digest\":\"" + std::string(GOOD_DIGEST) + "\",\"zzz\":[1,2.5e3,-4,\"s\",{\"q\":null}],\"browser_download_url\":\"https://x/d\",\"size\":99,\"name\":\"Priiloader_Update.dol\"}";
	std::string rel = "{\"assets\":[" + a + "],\"body\":\"b\",\"prerelease\":false,\"extra\":{\"deep\":{\"er\":[true,false,null]}},\"draft\":false,\"tag_name\":\"v3.4.5\"}";
	CHECK(parse("[" + rel + "]", r) == GH_PARSE_OK && r.size() == 1, "parse");
	GhPick prod, rc;
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_OK && prod.tag == "v3.4.5" && prod.size == 99 && prod.url == "https://x/d", "pick");
	//a missing draft or prerelease flag counts as set: nothing unflagged is published by accident
	CHECK(parse("[{\"tag_name\":\"v1.0.0\"}]", r) == GH_PARSE_OK, "parse");
	gh_select(r, prod, rc);
	CHECK(prod.status == GH_PICK_NONE, "release without flags was published");
	//wrong types are skipped, not fatal
	CHECK(parse("[{\"tag_name\":5,\"draft\":\"no\",\"assets\":7,\"body\":null},3,\"x\",[1]]", r) == GH_PARSE_OK && r.size() == 1, "wrong types");
	CHECK(r[0].tag.empty() && r[0].assets.empty(), "wrong types left a value");
	//huge sizes do not wrap
	CHECK(parse("[{\"assets\":[{\"size\":99999999999999999999}]}]", r) == GH_PARSE_OK && r[0].assets.size() == 1 && r[0].assets[0].size == 0xFFFFFFFFu, "size clamp");
}

static void test_limits(void)
{
	std::vector<GhRelease> r;
	std::vector<std::string> many;
	for (int i = 0; i < 100; i++)
		many.push_back(release("v1.0." + std::to_string(i), false, false, ""));
	CHECK(parse(list(many), r) == GH_PARSE_OK && r.size() == GH_MAX_RELEASES, "releases kept: %d", (int)r.size());
	std::string assets;
	for (int i = 0; i < 200; i++)
		assets += (i ? "," : "") + asset("a" + std::to_string(i));
	CHECK(parse(list({ release("v1.0.0", false, false, assets) }), r) == GH_PARSE_OK && r[0].assets.size() == GH_MAX_ASSETS, "assets kept: %d", (int)r[0].assets.size());
}

//what main.cpp had inline before GitHub support, kept here as the reference
static bool old_prod(version_t cur, version_t prod)
{
	bool isCurrentRCVersion = cur.sub_version > 0;
	return smaller_version(cur, prod) || (same_version(cur, prod) && isCurrentRCVersion);
}

static bool old_rc(version_t cur, version_t rc, bool show)
{
	bool isCurrentRCVersion = cur.sub_version > 0;
	return show &&
		((smaller_version(cur, rc) && !isCurrentRCVersion) ||
		 (same_version(cur, rc) && cur.sub_version < rc.sub_version));
}

static void test_version_rules_match_the_old_ones(void)
{
	int n = 0;
	for (int a = 0; a < 3; a++)
		for (int b = 0; b < 3; b++)
			for (int c = 0; c < 3; c++)
				for (int rc1 = 0; rc1 < 3; rc1++)
					for (int a2 = 0; a2 < 3; a2++)
						for (int b2 = 0; b2 < 3; b2++)
							for (int c2 = 0; c2 < 3; c2++)
								for (int rc2 = 0; rc2 < 3; rc2++)
								{
									version_t cur = V(a, b, c, rc1), other = V(a2, b2, c2, rc2);
									CHECK(update_prod_available(cur, other) == old_prod(cur, other), "prod %d.%d.%d rc%d vs %d.%d.%d", a, b, c, rc1, a2, b2, c2);
									CHECK(update_rc_available(cur, other, true) == old_rc(cur, other, true), "rc on %d.%d.%d rc%d vs %d.%d.%d rc%d", a, b, c, rc1, a2, b2, c2, rc2);
									CHECK(update_rc_available(cur, other, false) == false, "rc off");
									n++;
								}
	CHECK(n == 6561, "grid %d", n);
}

static void test_version_rules_by_example(void)
{
	CHECK(update_prod_available(V(0, 10, 1), V(0, 10, 2)), "newer patch");
	CHECK(update_prod_available(V(0, 10, 1), V(1, 0, 0)), "newer major");
	CHECK(!update_prod_available(V(0, 10, 1), V(0, 10, 1)), "same");
	CHECK(!update_prod_available(V(0, 10, 2), V(0, 10, 1)), "older");
	CHECK(update_prod_available(V(0, 10, 1, 2), V(0, 10, 1)), "an rc of the release moves to the release");
	CHECK(update_rc_available(V(0, 10, 1), V(0, 11, 0, 1), true), "rc of a newer version");
	CHECK(!update_rc_available(V(0, 10, 1, 1), V(0, 11, 0, 2), true), "an rc is not moved to another version's rc (the old rule)");
	CHECK(update_rc_available(V(0, 11, 0, 1), V(0, 11, 0, 2), true), "newer rc of the same version");
	CHECK(!update_rc_available(V(0, 11, 0, 2), V(0, 11, 0, 2), true), "same rc");
}

static void test_digest(void)
{
	//sha256("abc")
	const char* abc = "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	const char* ABC = "sha256:BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
	CHECK(gh_check_digest((const uint8_t*)"abc", 3, abc), "match");
	CHECK(gh_check_digest((const uint8_t*)"abc", 3, ABC), "match upper case");
	CHECK(!gh_check_digest((const uint8_t*)"abd", 3, abc), "mismatch");
	CHECK(!gh_check_digest((const uint8_t*)"abc", 2, abc), "short data");
	CHECK(!gh_check_digest((const uint8_t*)"abc", 3, "sha256:ba78"), "short digest");
	CHECK(!gh_check_digest((const uint8_t*)"abc", 3, "sha1:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "other algorithm");
	CHECK(!gh_check_digest((const uint8_t*)"abc", 3, ""), "empty");
	CHECK(!gh_check_digest((const uint8_t*)"abc", 3, NULL), "NULL digest");
	CHECK(!gh_check_digest(NULL, 3, abc), "NULL data");
	//empty data: sha256("")
	CHECK(gh_check_digest((const uint8_t*)"", 0, "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "empty data");
	//a megabyte, to take the multi-block path
	std::vector<uint8_t> big(1 << 20, 0);
	CHECK(gh_check_digest(big.data(), big.size(), "sha256:30e14955ebf1352266dc2ff8067e68104607e750abb9d3b36582b8af909fcb58"), "1 MiB of zeros");
	uint8_t raw[32];
	CHECK(gh_parse_digest(abc, raw) && raw[0] == 0xba && raw[31] == 0xad, "parse digest");
}

static void test_changelog(void)
{
	std::string out = gh_format_changelog("v1.2.3", "first line\r\nsecond\n\nthird caf\xC3\xA9 \xF0\x9F\x98\x80 end", 62);
	CHECK(out.find('\r') == std::string::npos, "CR left");
	CHECK(out.compare(0, 8, "v1.2.3\n\n") == 0, "heading '%s'", out.c_str());
	CHECK(out.find("third caf? ? end\n") != std::string::npos, "non-ASCII: '%s'", out.c_str());
	CHECK(out[out.size() - 1] == '\n', "ends with LF");
	for (size_t i = 0; i < out.size(); i++)
		CHECK((unsigned char)out[i] < 0x80 && ((unsigned char)out[i] >= 0x20 || out[i] == '\n'), "byte %02x at %d", (unsigned char)out[i], (int)i);
	//wrapping
	std::string words;
	for (int i = 0; i < 80; i++)
		words += "word" + std::to_string(i) + " ";
	out = gh_format_changelog("v1.0.0", words, 62);
	size_t start = 0;
	while (start < out.size())
	{
		size_t e = out.find('\n', start);
		CHECK(e != std::string::npos && e - start <= 62, "line of %d columns", (int)(e - start));
		start = e + 1;
	}
	//a word longer than the line is broken, not lost
	out = gh_format_changelog("v1.0.0", std::string(200, 'x'), 62);
	size_t xs = 0;
	for (size_t i = 0; i < out.size(); i++)
		xs += out[i] == 'x';
	CHECK(xs == 200, "x lost: %d", (int)xs);
	//control bytes go, tabs become spaces
	out = gh_format_changelog("v1.0.0", "a\tb\x01\x1b[31mc\x7f", 62);
	CHECK(out.find("a b[31mc\n") != std::string::npos, "controls: '%s'", out.c_str());
	//a very long text is cut
	std::string many;
	for (int i = 0; i < 100000; i++)
		many += "x\n";
	out = gh_format_changelog("v1.0.0", many, 62);
	CHECK(out.size() < 4000 && out.find("...") != std::string::npos, "cap: %d", (int)out.size());
	//empty body, tiny width
	out = gh_format_changelog("v1.0.0", "", 3);
	CHECK(out == "v1.0.0\n\n" || out == "v1.0.0\n", "empty body '%s'", out.c_str());
}

//the lines of a formatted changelog
static std::vector<std::string> changelog_lines(const std::string& out)
{
	std::vector<std::string> lines;
	size_t start = 0;
	while (start < out.size())
	{
		size_t e = out.find('\n', start);
		if (e == std::string::npos)
			e = out.size();
		lines.push_back(out.substr(start, e - start));
		start = e + 1;
	}
	return lines;
}

static void test_changelog_markdown(void)
{
	//the screen shows 50 columns
	CHECK(GH_CHANGELOG_WIDTH == 50, "width %d", GH_CHANGELOG_WIDTH);
	const std::string notes =
		"priiloader-reborn 1.0.0: the first stable release of this fork of DacoTaco's Priiloader.\r\n\r\n"
		"**Make a BootMii NAND backup before you install, update or remove Priiloader.**\r\n\r\n\r\n"
		"### Install or update\r\n"
		"- **New install:** unzip `priiloader-reborn-1.0.0-sd.zip` to the root of the SD card and run Priiloader.\r\n"
		"* A star item that is long enough to wrap onto a second line of the screen.\r\n"
		"12. A numbered item that is long enough to wrap onto a second line of the screen.\r\n"
		"The full list is in [CHANGELOG.md](https://github.com/Monsterray/priiloader-reborn/blob/master/CHANGELOG.md).\r\n"
		"100% done\r\n\r\n";
	std::string out = gh_format_changelog("v1.0.0", notes, GH_CHANGELOG_WIDTH);
	std::vector<std::string> lines = changelog_lines(out);
	for (size_t i = 0; i < lines.size(); i++)
	{
		const std::string& l = lines[i];
		CHECK(l.size() <= GH_CHANGELOG_WIDTH, "line %d has %d columns: '%s'", (int)i, (int)l.size(), l.c_str());
		CHECK(l.empty() || l[l.size() - 1] != ' ', "trailing space: '%s'", l.c_str());
		CHECK(!(i > 0 && l.empty() && lines[i - 1].empty()), "two blank lines at %d", (int)i);
		CHECK(l.find("**") == std::string::npos && l.find('`') == std::string::npos && l.find("](") == std::string::npos
			&& l.compare(0, 1, "#") != 0, "Markdown left: '%s'", l.c_str());
	}
	CHECK(!lines.empty() && !lines.back().empty(), "ends in a blank line");
	std::string flat;
	for (size_t i = 0; i < lines.size(); i++)
	{
		size_t a = lines[i].find_first_not_of(' ');
		flat += (a == std::string::npos ? std::string() : lines[i].substr(a)) + " ";
	}
	CHECK(flat.find("Make a BootMii NAND backup before you install, update or remove Priiloader.") != std::string::npos, "bold: '%s'", out.c_str());
	CHECK(out.find("\nInstall or update\n") != std::string::npos, "heading: '%s'", out.c_str());
	CHECK(flat.find("The full list is in CHANGELOG.md.") != std::string::npos, "link: '%s'", out.c_str());
	CHECK(out.find("\n100% done\n") != std::string::npos, "percent: '%s'", out.c_str());
	CHECK(flat.find("New install: unzip priiloader-reborn-1.0.0-sd.zip to the root") != std::string::npos, "words: '%s'", out.c_str());
	//list items: "- " and "* " start with "- ", numbered ones keep their number; the lines after the first are indented
	//under the item's text
	bool star = false, numbered = false;
	for (size_t i = 0; i + 1 < lines.size(); i++)
	{
		if (lines[i].compare(0, 13, "- A star item") == 0)
		{
			star = true;
			CHECK(lines[i + 1].compare(0, 2, "  ") == 0 && lines[i + 1][2] != ' ', "star item continues: '%s'", lines[i + 1].c_str());
		}
		if (lines[i].compare(0, 18, "12. A numbered ite") == 0)
		{
			numbered = true;
			CHECK(lines[i + 1].compare(0, 4, "    ") == 0 && lines[i + 1][4] != ' ', "numbered item continues: '%s'", lines[i + 1].c_str());
		}
	}
	CHECK(star && numbered, "list items: '%s'", out.c_str());
	//a word longer than the room after the indent is broken, not lost, and stays inside the width
	out = gh_format_changelog("v1.0.0", "- " + std::string(120, 'y'), 50);
	lines = changelog_lines(out);
	size_t ys = 0;
	for (size_t i = 0; i < lines.size(); i++)
	{
		CHECK(lines[i].size() <= 50, "long word line of %d", (int)lines[i].size());
		for (size_t k = 0; k < lines[i].size(); k++)
			ys += lines[i][k] == 'y';
	}
	CHECK(ys == 120, "y lost: %d", (int)ys);
	//a "#5", a lone "[x]", an unclosed link and a single '*' stay as they are
	out = gh_format_changelog("v1.0.0", "#5 and [x] and [y](z and a*b", 50);
	CHECK(out.find("#5 and [x] and [y](z and a*b\n") != std::string::npos, "plain text: '%s'", out.c_str());
}

static void reset(void)
{
}

static const struct hc_test tests[] = {
	{ "empty_list", test_empty_list, false },
	{ "changelog_markdown", test_changelog_markdown, false },
	{ "only_drafts", test_only_drafts, false },
	{ "prerelease_flag", test_prerelease_flag, false },
	{ "newest_wins_whatever_the_order", test_newest_wins_whatever_the_order, false },
	{ "stale_rc_is_dropped", test_stale_rc_is_dropped, false },
	{ "odd_tags_ignored", test_odd_tags_ignored, false },
	{ "tags", test_tags, false },
	{ "assets_and_digests", test_assets_and_digests, false },
	{ "malformed_json", test_malformed_json, false },
	{ "truncated_json", test_truncated_json, false },
	{ "too_big", test_too_big, false },
	{ "depth_bomb", test_depth_bomb, false },
	{ "error_object", test_error_object, false },
	{ "thirty_releases_fit", test_thirty_releases_fit, false },
	{ "error_message_for_the_screen", test_error_message_for_the_screen, false },
	{ "escapes", test_escapes, false },
	{ "unknown_and_reordered_fields", test_unknown_and_reordered_fields, false },
	{ "limits", test_limits, false },
	{ "version_rules_match_the_old_ones", test_version_rules_match_the_old_ones, false },
	{ "version_rules_by_example", test_version_rules_by_example, false },
	{ "digest", test_digest, false },
	{ "changelog", test_changelog, false },
};

int main(int argc, char** argv)
{
	return hc_main(tests, HC_COUNT(tests), argc, argv, reset);
}
