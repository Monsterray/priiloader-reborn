/*
 * update_github: see update_github.h. No console, no network: host-tested.
 */
#include <string.h>
#include <mbedtls/sha256.h>
#include "update_github.h"

/* ---- a bounded JSON reader ----
 * Walks the text once. Values it does not want are skipped (to a depth of JSON_MAX_DEPTH), strings are
 * unescaped into a std::string capped at the size the caller gives. Nothing here recurses deeper than the cap
 * or reads past the end. */

#define JSON_MAX_DEPTH 16

namespace {

struct Json
{
	const char* p;
	const char* end;
	int err; //a GhParseError, 0 while fine
};

void fail(Json& j, int code)
{
	if (j.err == 0)
		j.err = code;
}

void skip_ws(Json& j)
{
	while (j.p < j.end && (*j.p == ' ' || *j.p == '\t' || *j.p == '\n' || *j.p == '\r'))
		j.p++;
}

int hex4(Json& j)
{
	if (j.end - j.p < 4)
		return -1;
	int v = 0;
	for (int i = 0; i < 4; i++)
	{
		char c = j.p[i];
		int d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
		else return -1;
		v = v * 16 + d;
	}
	j.p += 4;
	return v;
}

void put_utf8(std::string& s, size_t cap, unsigned cp)
{
	char b[4];
	size_t n;
	if (cp < 0x80) { b[0] = (char)cp; n = 1; }
	else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
	else if (cp < 0x10000) { b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
	else { b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
	if (s.size() + n <= cap)
		s.append(b, n);
}

//j.p is on the opening quote. The whole string is consumed even when out is full (the rest is dropped).
bool parse_string(Json& j, std::string* out, size_t cap)
{
	if (j.p >= j.end || *j.p != '"')
	{
		fail(j, GH_PARSE_MALFORMED);
		return false;
	}
	j.p++;
	if (out)
		out->clear();
	while (j.p < j.end)
	{
		unsigned char c = (unsigned char)*j.p++;
		if (c == '"')
			return true;
		if (c != '\\')
		{
			if (out && out->size() < cap)
				out->push_back((char)c);
			continue;
		}
		if (j.p >= j.end)
			break;
		char e = *j.p++;
		char ch = 0;
		switch (e)
		{
			case '"': ch = '"'; break;
			case '\\': ch = '\\'; break;
			case '/': ch = '/'; break;
			case 'b': ch = '\b'; break;
			case 'f': ch = '\f'; break;
			case 'n': ch = '\n'; break;
			case 'r': ch = '\r'; break;
			case 't': ch = '\t'; break;
			case 'u':
			{
				int cp = hex4(j);
				if (cp < 0)
				{
					fail(j, GH_PARSE_MALFORMED);
					return false;
				}
				unsigned code = (unsigned)cp;
				if (code >= 0xD800 && code <= 0xDBFF)
				{
					//a surrogate pair, or a lone half (replaced)
					if (j.end - j.p >= 6 && j.p[0] == '\\' && j.p[1] == 'u')
					{
						const char* save = j.p;
						j.p += 2;
						int lo = hex4(j);
						if (lo >= 0xDC00 && lo <= 0xDFFF)
							code = 0x10000 + ((code - 0xD800) << 10) + ((unsigned)lo - 0xDC00);
						else
						{
							j.p = save;
							code = '?';
						}
					}
					else
						code = '?';
				}
				else if (code >= 0xDC00 && code <= 0xDFFF)
					code = '?';
				if (out)
					put_utf8(*out, cap, code);
				continue;
			}
			default:
				fail(j, GH_PARSE_MALFORMED);
				return false;
		}
		if (out && out->size() < cap)
			out->push_back(ch);
	}
	fail(j, GH_PARSE_MALFORMED); //ran off the end inside a string
	return false;
}

bool skip_value(Json& j, int depth);

bool literal(Json& j, const char* word)
{
	size_t n = strlen(word);
	if ((size_t)(j.end - j.p) < n || memcmp(j.p, word, n) != 0)
	{
		fail(j, GH_PARSE_MALFORMED);
		return false;
	}
	j.p += n;
	return true;
}

bool skip_number(Json& j)
{
	const char* s = j.p;
	while (j.p < j.end && ((*j.p >= '0' && *j.p <= '9') || *j.p == '-' || *j.p == '+' || *j.p == '.' || *j.p == 'e' || *j.p == 'E'))
		j.p++;
	if (j.p == s)
	{
		fail(j, GH_PARSE_MALFORMED);
		return false;
	}
	return true;
}

bool skip_value(Json& j, int depth)
{
	skip_ws(j);
	if (j.p >= j.end)
	{
		fail(j, GH_PARSE_MALFORMED);
		return false;
	}
	switch (*j.p)
	{
		case '"': return parse_string(j, NULL, 0);
		case 't': return literal(j, "true");
		case 'f': return literal(j, "false");
		case 'n': return literal(j, "null");
		case '{':
		case '[':
		{
			if (depth >= JSON_MAX_DEPTH)
			{
				fail(j, GH_PARSE_TOO_DEEP);
				return false;
			}
			char close = (*j.p == '{') ? '}' : ']';
			bool object = close == '}';
			j.p++;
			skip_ws(j);
			if (j.p < j.end && *j.p == close)
			{
				j.p++;
				return true;
			}
			for (;;)
			{
				skip_ws(j);
				if (object)
				{
					if (!parse_string(j, NULL, 0))
						return false;
					skip_ws(j);
					if (j.p >= j.end || *j.p != ':')
					{
						fail(j, GH_PARSE_MALFORMED);
						return false;
					}
					j.p++;
				}
				if (!skip_value(j, depth + 1))
					return false;
				skip_ws(j);
				if (j.p >= j.end)
				{
					fail(j, GH_PARSE_MALFORMED);
					return false;
				}
				if (*j.p == ',')
				{
					j.p++;
					continue;
				}
				if (*j.p == close)
				{
					j.p++;
					return true;
				}
				fail(j, GH_PARSE_MALFORMED);
				return false;
			}
		}
		default:
			return skip_number(j);
	}
}

//a boolean value; anything else counts as unsafeDefault
bool parse_bool(Json& j, bool unsafeDefault)
{
	skip_ws(j);
	if (j.p < j.end && *j.p == 't')
		return literal(j, "true") ? true : unsafeDefault;
	if (j.p < j.end && *j.p == 'f')
		return literal(j, "false") ? false : unsafeDefault;
	skip_value(j, 1);
	return unsafeDefault;
}

uint32_t parse_u32(Json& j)
{
	skip_ws(j);
	uint64_t v = 0;
	if (j.p < j.end && *j.p >= '0' && *j.p <= '9')
	{
		while (j.p < j.end && *j.p >= '0' && *j.p <= '9')
		{
			if (v < 0xFFFFFFFFull)
				v = v * 10 + (uint64_t)(*j.p - '0');
			j.p++;
		}
		if (j.p < j.end && (*j.p == '.' || *j.p == 'e' || *j.p == 'E'))
			skip_number(j);
	}
	else
		skip_value(j, 1);
	return v > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)v;
}

//string value or null (empty). Any other type is skipped and leaves out empty.
void parse_text(Json& j, std::string& out, size_t cap)
{
	skip_ws(j);
	if (j.p < j.end && *j.p == '"')
		parse_string(j, &out, cap);
	else
	{
		out.clear();
		skip_value(j, 1);
	}
}

bool expect(Json& j, char c)
{
	skip_ws(j);
	if (j.p >= j.end || *j.p != c)
	{
		fail(j, GH_PARSE_MALFORMED);
		return false;
	}
	j.p++;
	return true;
}

//after a value: true when a ',' follows (consumed), false at the close (consumed) or on error
bool next_member(Json& j, char close)
{
	skip_ws(j);
	if (j.p >= j.end)
	{
		fail(j, GH_PARSE_MALFORMED);
		return false;
	}
	if (*j.p == ',')
	{
		j.p++;
		return true;
	}
	if (*j.p == close)
	{
		j.p++;
		return false;
	}
	fail(j, GH_PARSE_MALFORMED);
	return false;
}

void parse_asset(Json& j, GhAsset& a)
{
	a.size = 0;
	if (!expect(j, '{'))
		return;
	skip_ws(j);
	if (j.p < j.end && *j.p == '}')
	{
		j.p++;
		return;
	}
	std::string key;
	do
	{
		skip_ws(j);
		if (!parse_string(j, &key, 64) || !expect(j, ':'))
			return;
		if (key == "name") parse_text(j, a.name, 256);
		else if (key == "browser_download_url") parse_text(j, a.url, 1024);
		else if (key == "digest") parse_text(j, a.digest, 128);
		else if (key == "size") a.size = parse_u32(j);
		else skip_value(j, 3);
		if (j.err)
			return;
	} while (next_member(j, '}'));
}

void parse_assets(Json& j, std::vector<GhAsset>& assets)
{
	skip_ws(j);
	if (j.p >= j.end || *j.p != '[')
	{
		skip_value(j, 2);
		return;
	}
	j.p++;
	skip_ws(j);
	if (j.p < j.end && *j.p == ']')
	{
		j.p++;
		return;
	}
	do
	{
		skip_ws(j);
		if (j.p < j.end && *j.p == '{')
		{
			GhAsset a;
			a.size = 0;
			parse_asset(j, a);
			if (j.err)
				return;
			if (assets.size() < GH_MAX_ASSETS)
				assets.push_back(a);
		}
		else if (!skip_value(j, 3))
			return;
	} while (next_member(j, ']'));
}

void parse_release(Json& j, GhRelease& r)
{
	r.draft = true; //until the file says otherwise: an unreadable flag must not publish a draft
	r.prerelease = true;
	if (!expect(j, '{'))
		return;
	skip_ws(j);
	if (j.p < j.end && *j.p == '}')
	{
		j.p++;
		return;
	}
	std::string key;
	do
	{
		skip_ws(j);
		if (!parse_string(j, &key, 64) || !expect(j, ':'))
			return;
		if (key == "tag_name") parse_text(j, r.tag, 64);
		else if (key == "draft") r.draft = parse_bool(j, true);
		else if (key == "prerelease") r.prerelease = parse_bool(j, true);
		else if (key == "body") parse_text(j, r.body, 16 * 1024);
		else if (key == "assets") { r.assets.clear(); parse_assets(j, r.assets); }
		else skip_value(j, 2);
		if (j.err)
			return;
	} while (next_member(j, '}'));
}

} //namespace

int gh_parse_releases(const char* json, size_t len, std::vector<GhRelease>& out, std::string* message)
{
	out.clear();
	if (message)
		message->clear();
	if (json == NULL)
		return GH_PARSE_MALFORMED;
	if (len > GH_MAX_JSON)
		return GH_PARSE_TOO_BIG;

	Json j = { json, json + len, 0 };
	skip_ws(j);
	if (j.p < j.end && *j.p == '{')
	{
		//an error object, such as {"message":"API rate limit exceeded", ...}
		j.p++;
		std::string key, text;
		skip_ws(j);
		if (j.p < j.end && *j.p == '}')
			return GH_PARSE_NOT_ARRAY;
		do
		{
			skip_ws(j);
			if (!parse_string(j, &key, 64) || !expect(j, ':'))
				break;
			if (key == "message" && message)
			{
				parse_text(j, text, 200);
				*message = text;
			}
			else
				skip_value(j, 2);
			if (j.err)
				break;
		} while (next_member(j, '}'));
		if (j.err == GH_PARSE_TOO_DEEP)
			return GH_PARSE_TOO_DEEP;
		return j.err ? GH_PARSE_MALFORMED : GH_PARSE_NOT_ARRAY;
	}
	if (!expect(j, '['))
		return GH_PARSE_MALFORMED;
	skip_ws(j);
	if (j.p < j.end && *j.p == ']')
		j.p++;
	else
	{
		do
		{
			skip_ws(j);
			if (j.p < j.end && *j.p == '{')
			{
				GhRelease r;
				r.draft = r.prerelease = true;
				parse_release(j, r);
				if (j.err)
					break;
				if (out.size() < GH_MAX_RELEASES)
					out.push_back(r);
			}
			else if (!skip_value(j, 1))
				break;
		} while (next_member(j, ']'));
	}
	if (j.err == 0)
	{
		skip_ws(j);
		if (j.p != j.end)
			j.err = GH_PARSE_MALFORMED; //text after the list
	}
	if (j.err)
	{
		out.clear();
		return j.err;
	}
	return GH_PARSE_OK;
}

/* ---- tags and selection ---- */

static bool parse_num(const char*& p, unsigned& v)
{
	const char* s = p;
	unsigned n = 0;
	while (*p >= '0' && *p <= '9' && p - s < 4)
		n = n * 10 + (unsigned)(*p++ - '0');
	if (p == s || p - s > 3 || n > 255 || (*p >= '0' && *p <= '9'))
		return false;
	v = n;
	return true;
}

bool gh_parse_tag(const char* tag, version_t* v, bool* isRc)
{
	if (tag == NULL || tag[0] != 'v')
		return false;
	const char* p = tag + 1;
	unsigned a, b, c, rc = 0;
	if (!parse_num(p, a) || *p++ != '.' || !parse_num(p, b) || *p++ != '.' || !parse_num(p, c))
		return false;
	bool rcTag = false;
	if (*p == '-')
	{
		if (p[1] != 'r' || p[2] != 'c')
			return false;
		p += 3;
		if (!parse_num(p, rc) || rc == 0)
			return false;
		rcTag = true;
	}
	if (*p != 0)
		return false;
	if (v)
	{
		v->major = (unsigned char)a;
		v->minor = (unsigned char)b;
		v->patch = (unsigned char)c;
		v->sub_version = (unsigned char)rc;
	}
	if (isRc)
		*isRc = rcTag;
	return true;
}

//what the release's update asset offers
static void pick_fill(const GhRelease& r, const version_t& ver, GhPick& pick)
{
	pick.version = ver;
	pick.tag = r.tag;
	pick.body = r.body;
	pick.url.clear();
	pick.digest.clear();
	pick.size = 0;
	pick.status = GH_PICK_NO_ASSET;
	for (size_t i = 0; i < r.assets.size(); i++)
	{
		const GhAsset& a = r.assets[i];
		if (a.name != GH_UPDATE_ASSET_NAME || a.url.empty())
			continue;
		pick.url = a.url;
		pick.size = a.size;
		pick.digest = a.digest;
		uint8_t raw[32];
		pick.status = gh_parse_digest(a.digest.c_str(), raw) ? GH_PICK_OK : GH_PICK_NO_DIGEST;
		return;
	}
}

static bool newer_than(const version_t& a, const version_t& b) //a > b, rc number included
{
	if (smaller_version(b, a))
		return true;
	return same_version(a, b) && a.sub_version > b.sub_version;
}

void gh_select(const std::vector<GhRelease>& releases, GhPick& prod, GhPick& rc)
{
	prod = GhPick();
	rc = GhPick();
	for (size_t i = 0; i < releases.size(); i++)
	{
		const GhRelease& r = releases[i];
		if (r.draft)
			continue;
		version_t v;
		bool isRc = false;
		if (!gh_parse_tag(r.tag.c_str(), &v, &isRc))
			continue;
		if (isRc)
		{
			//a release candidate: the tag says so, whatever the prerelease flag
			if (rc.status == GH_PICK_NONE || newer_than(v, rc.version))
				pick_fill(r, v, rc);
		}
		else
		{
			//a release must not be flagged prerelease
			if (r.prerelease)
				continue;
			if (prod.status == GH_PICK_NONE || newer_than(v, prod.version))
				pick_fill(r, v, prod);
		}
	}
	//a release candidate of a version that is out already, or older, is not an update
	if (rc.status != GH_PICK_NONE && prod.status != GH_PICK_NONE && !smaller_version(prod.version, rc.version))
		rc = GhPick();
}

bool update_prod_available(version_t cur, version_t prod)
{
	bool currentIsRc = cur.sub_version > 0;
	return smaller_version(cur, prod) || (same_version(cur, prod) && currentIsRc);
}

bool update_rc_available(version_t cur, version_t rc, bool showRc)
{
	bool currentIsRc = cur.sub_version > 0;
	return showRc &&
		((smaller_version(cur, rc) && !currentIsRc) ||
		 (same_version(cur, rc) && cur.sub_version < rc.sub_version));
}

/* ---- digest ---- */

bool gh_parse_digest(const char* digest, uint8_t out[32])
{
	if (digest == NULL || strncmp(digest, "sha256:", 7) != 0 || strlen(digest) != 7 + 64)
		return false;
	const char* h = digest + 7;
	for (int i = 0; i < 32; i++)
	{
		int v = 0;
		for (int k = 0; k < 2; k++)
		{
			char c = h[i * 2 + k];
			int d;
			if (c >= '0' && c <= '9') d = c - '0';
			else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
			else return false;
			v = v * 16 + d;
		}
		out[i] = (uint8_t)v;
	}
	return true;
}

bool gh_check_digest(const uint8_t* data, size_t len, const char* digest)
{
	uint8_t want[32], got[32];
	if (data == NULL || !gh_parse_digest(digest, want))
		return false;
	if (mbedtls_sha256(data, len, got, 0) != 0)
		return false;
	return memcmp(want, got, 32) == 0;
}

/* ---- the release notes on screen ---- */

std::string gh_ascii_line(const std::string& text, size_t width)
{
	std::string out;
	for (size_t i = 0; i < text.size() && out.size() < width + 1; i++)
	{
		unsigned char c = (unsigned char)text[i];
		if (c >= 0x80)
		{
			while (i + 1 < text.size() && ((unsigned char)text[i + 1] & 0xC0) == 0x80)
				i++;
			c = '?';
		}
		else if (c < 0x20 || c == 0x7F)
			c = ' ';
		if (c == ' ' && (out.empty() || out[out.size() - 1] == ' '))
			continue;
		out.push_back((char)c);
	}
	if (out.size() > width)
		out.resize(width);
	while (!out.empty() && out[out.size() - 1] == ' ')
		out.resize(out.size() - 1);
	return out;
}

std::string gh_error_message(const char* json, size_t len, size_t width)
{
	std::vector<GhRelease> ignored;
	std::string message;
	gh_parse_releases(json, len, ignored, &message);
	return gh_ascii_line(message, width);
}

//one line of release notes with its Markdown made plain: "### x" becomes "x", "**x**" and "`x`" become "x",
//"[text](url)" becomes "text", and a "* " or "+ " list marker becomes "- "
static std::string gh_plain_markdown(const std::string& in)
{
	size_t lead = 0;
	while (lead < in.size() && in[lead] == ' ')
		lead++;
	std::string s = in.substr(lead);
	if (!s.empty() && s[0] == '#')
	{
		size_t h = 0;
		while (h < s.size() && s[h] == '#')
			h++;
		if (h == s.size() || s[h] == ' ')
			s = s.substr(h < s.size() ? h + 1 : h);
	}
	else if (s.size() >= 2 && (s[0] == '*' || s[0] == '+') && s[1] == ' ')
		s[0] = '-';
	std::string out(lead, ' ');
	for (size_t i = 0; i < s.size(); i++)
	{
		if (s[i] == '`' || (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '*'))
		{
			if (s[i] == '*')
				i++;
			continue;
		}
		if (s[i] == '[')
		{
			size_t close = s.find(']', i + 1);
			if (close != std::string::npos && close + 1 < s.size() && s[close + 1] == '(')
			{
				size_t end = s.find(')', close + 2);
				if (end != std::string::npos)
				{
					out += gh_plain_markdown(s.substr(i + 1, close - i - 1));
					i = end;
					continue;
				}
			}
		}
		out.push_back(s[i]);
	}
	return out;
}

//how far the lines after the first of a paragraph are indented: under the text of a list item, else as the first
static size_t gh_hanging_indent(const std::string& line, size_t width)
{
	size_t n = 0;
	while (n < line.size() && line[n] == ' ')
		n++;
	if (line.size() >= n + 2 && line[n] == '-' && line[n + 1] == ' ')
		n += 2;
	else
	{
		size_t d = n;
		while (d < line.size() && line[d] >= '0' && line[d] <= '9')
			d++;
		if (d > n && d + 1 < line.size() && line[d] == '.' && line[d + 1] == ' ')
			n = d + 2;
	}
	return n > width / 2 ? width / 2 : n;
}

std::string gh_format_changelog(const std::string& tag, const std::string& body, size_t width)
{
	if (width < 20)
		width = 20;
	//the text as ASCII lines: a UTF-8 sequence becomes one '?', a tab a space, other control bytes go
	std::vector<std::string> source;
	std::string text = tag + "\n\n" + body;
	std::string cur;
	for (size_t i = 0; i < text.size(); )
	{
		unsigned char c = (unsigned char)text[i++];
		if (c == '\r' || c == '\n')
		{
			if (c == '\r' && i < text.size() && text[i] == '\n')
				i++;
			source.push_back(cur);
			cur.clear();
			continue;
		}
		if (c == '\t')
			c = ' ';
		else if (c < 0x20 || c == 0x7F)
			continue;
		else if (c >= 0x80)
		{
			while (i < text.size() && ((unsigned char)text[i] & 0xC0) == 0x80)
				i++;
			c = '?';
		}
		cur.push_back((char)c);
	}
	if (!cur.empty())
		source.push_back(cur);

	const size_t maxLines = 400;
	std::vector<std::string> lines;
	bool truncated = false;
	auto emit = [&](const std::string& l) {
		if (lines.size() >= maxLines)
			truncated = true;
		else
			lines.push_back(l);
	};
	for (size_t s = 0; s < source.size() && !truncated; s++)
	{
		std::string line = gh_plain_markdown(source[s]);
		while (!line.empty() && line[line.size() - 1] == ' ')
			line.resize(line.size() - 1);
		if (line.empty())
		{
			//one blank line between paragraphs, never two in a row
			if (!lines.empty() && !lines.back().empty())
				emit("");
			continue;
		}
		const size_t indent = gh_hanging_indent(line, width);
		//the first line keeps its marker; the words are wrapped after it
		std::string out = line.substr(0, indent);
		size_t pos = indent;
		bool empty = true; //no word on out yet
		while (pos < line.size() && !truncated)
		{
			while (pos < line.size() && line[pos] == ' ')
				pos++;
			if (pos >= line.size())
				break;
			size_t end = line.find(' ', pos);
			if (end == std::string::npos)
				end = line.size();
			std::string word = line.substr(pos, end - pos);
			pos = end;
			if (!empty && out.size() + 1 + word.size() > width)
			{
				emit(out);
				out.assign(indent, ' ');
				empty = true;
			}
			//a word longer than a line is broken
			while (out.size() + (empty ? 0 : 1) + word.size() > width)
			{
				if (!empty)
				{
					emit(out);
					out.assign(indent, ' ');
					empty = true;
				}
				size_t room = width - out.size();
				out += word.substr(0, room);
				word.erase(0, room);
				emit(out);
				out.assign(indent, ' ');
			}
			if (word.empty())
				continue;
			if (!empty)
				out.push_back(' ');
			out += word;
			empty = false;
		}
		if (!empty)
			emit(out);
	}
	while (!lines.empty() && lines.back().empty())
		lines.pop_back();

	std::string result;
	for (size_t i = 0; i < lines.size(); i++)
	{
		result += lines[i];
		result += '\n';
	}
	if (truncated)
		result += "...\n";
	return result;
}
