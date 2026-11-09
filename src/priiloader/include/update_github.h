/*
 * update_github: the parts of Priiloader's GitHub updater that need no console: reading the releases JSON,
 * the tag format, choosing the release to offer and checking a downloaded file against GitHub's digest.
 * Host-tested (tests/host/update). The release convention is in docs/updates.md.
 */
#ifndef PRIILOADER_UPDATE_GITHUB_H
#define PRIILOADER_UPDATE_GITHUB_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "version.h"

//the asset every release carries: Priiloader's installer DOL, the file the DacoTaco path calls Priiloader_Update.dol
#define GH_UPDATE_ASSET_NAME "Priiloader_Update.dol"
//the API reply is at most this big; more is refused. 30 releases (GH_MAX_RELEASES) with long changelogs and several assets
//each (about 2 KB of JSON per asset, a changelog of up to 10 KB) come to some 600 KB: the limit leaves room
#define GH_MAX_JSON (1024 * 1024)

struct GhAsset
{
	std::string name;
	uint32_t size;
	std::string url;	//browser_download_url
	std::string digest;	//"sha256:<hex>" or empty
};

struct GhRelease
{
	std::string tag;
	bool draft;
	bool prerelease;
	std::string body;
	std::vector<GhAsset> assets;
};

enum GhParseError
{
	GH_PARSE_OK = 0,
	GH_PARSE_MALFORMED = -1,	//not valid JSON, or cut short
	GH_PARSE_NOT_ARRAY = -2,	//valid JSON but no release list (an error object: message is filled in when it has one)
	GH_PARSE_TOO_BIG = -3,
	GH_PARSE_TOO_DEEP = -4
};

//parses the reply of GET /repos/<owner>/<repo>/releases. Unknown fields are skipped, field order does not matter, at most
//GH_MAX_RELEASES releases and GH_MAX_ASSETS assets each are kept. out is cleared first. message gets the top-level
//"message" of an error object.
#define GH_MAX_RELEASES 30
#define GH_MAX_ASSETS 64
int gh_parse_releases(const char* json, size_t len, std::vector<GhRelease>& out, std::string* message);

//"v1.2.3" or "v1.2.3-rc4". On success v has the three numbers, sub_version = the rc number (0 for a release) and
//isRc says which. Anything else (no v, a fourth number, a number over 255, "-rc0", trailing text) fails.
bool gh_parse_tag(const char* tag, version_t* v, bool* isRc);

enum GhPickStatus
{
	GH_PICK_NONE = 0,		//no such release
	GH_PICK_OK,
	GH_PICK_NO_ASSET,		//the release has no GH_UPDATE_ASSET_NAME (or no download link)
	GH_PICK_NO_DIGEST		//the asset has no usable "sha256:<64 hex>" digest
};

struct GhPick
{
	GhPickStatus status;
	version_t version;
	std::string tag;
	std::string body;
	std::string url;
	std::string digest;
	uint32_t size;
	GhPick() : status(GH_PICK_NONE), size(0) { version.major = version.minor = version.patch = version.sub_version = 0; }
};

//the newest published release (no draft, no prerelease, a plain vX.Y.Z tag) goes in prod, the newest release candidate
//(a vX.Y.Z-rcN tag, never a draft) in rc. A candidate that is not for a newer version than prod is dropped (v1.3.0-rc1
//next to v1.3.0 is the past, not an update). Releases whose tag does not parse are ignored. Whether either is newer than
//what runs is for update_prod_available / update_rc_available.
void gh_select(const std::vector<GhRelease>& releases, GhPick& prod, GhPick& rc);

//the rules both update sources share (they are the ones Priiloader always had). cur has the running version and its
//rc number (VERSION_RC, 0 for a release).
//a release update: a newer version, or the same version when an rc runs
bool update_prod_available(version_t cur, version_t prod);
//an rc update, only when rc updates are shown: a newer rc of a newer version while a release runs, or a newer rc of the same version
bool update_rc_available(version_t cur, version_t rc, bool showRc);

//"sha256:<64 hex>" (any case) into 32 bytes
bool gh_parse_digest(const char* digest, uint8_t out[32]);
//true when data hashes to the digest (and the digest parses)
bool gh_check_digest(const uint8_t* data, size_t len, const char* digest);

//one line of ASCII for the screen from a server's text: controls and line breaks become spaces, a UTF-8 sequence one '?',
//runs of spaces one space, the ends trimmed, cut at width characters
std::string gh_ascii_line(const std::string& text, size_t width);
//the "message" of the JSON error object GitHub sends with a non-200 reply (rate limit: 403 or 429), through gh_ascii_line.
//"" when the body has no message. A body that was cut short still gives the message when it came before the cut.
std::string gh_error_message(const char* json, size_t len, size_t width);

//the columns of the changelog screen: text starts at x 16 and PrintString draws a character only while it fits on the
//320-wide framebuffer (x + 7 < 318, 6 per character), so 50 characters show and the rest of a longer line is lost
#define GH_CHANGELOG_WIDTH 50

//the release notes as plain screen lines: LF only, ASCII only (anything else becomes '?'), a heading line with the tag
//first. Markdown is made plain (headings, **bold**, `code`, [links](url); "* " lists become "- "), words are wrapped at
//width columns, the lines after the first of a list item are indented under its text, and blank lines never repeat
std::string gh_format_changelog(const std::string& tag, const std::string& body, size_t width);

#endif
