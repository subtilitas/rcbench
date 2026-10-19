/*
 * What GitHub says about the panel's releases, read strictly.
 *
 * The panel asks GitHub two things over HTTPS (HTTP, hypertext transfer
 * protocol, over TLS, transport layer security).  Which release is the
 * newest, by one of two requests, after SETUP > UPDATES' RELEASES:
 *
 *   STABLE   a GET of FW_REL_LATEST_URL, answered with a redirect whose
 *            Location names the tag (".../releases/tag/v0.20.0"); only that
 *            header is read, no body.  GitHub's latest release is never a
 *            pre-release, so this names a stable release only.
 *   TESTING  a GET of FW_REL_API_URL, GitHub's REST (representational
 *            state transfer) API, answered with a JSON (JavaScript Object
 *            Notation) array of the FW_REL_API_RELEASES newest releases by
 *            date, testing or stable.  fw_release_scan_feed() reads each
 *            one's "tag_name" and "draft", a byte at a time, without
 *            keeping the answer, and names the highest version among those
 *            that are no draft: GitHub's order is by date, not by version.
 *
 * And, for a download, two files of that release: SHA256SUMS and
 * rcbench-panel-vX.Y.Z.bin, each a redirect from github.com to a
 * githubusercontent.com host and then the file.
 *
 * Every piece of an answer is held to a fixed form here, before anything
 * acts on it:
 *
 *   tag        "v" and MAJOR.MINOR.PATCH, digits only, each 0 to 65535, no
 *              leading zero (0 itself is fine), nothing after it; the whole
 *              Location exactly FW_REL_TAG_PREFIX and the tag
 *   redirect   "https://", a host of FW_REL_HOSTS exactly, then a path;
 *              printable ASCII (American Standard Code for Information
 *              Interchange) only, at most FW_REL_URL_MAX bytes, at most
 *              FW_REL_HOPS_MAX of them in one fetch
 *   length     SHA256SUMS and the image: a Content-Length, not chunked,
 *              1 byte to the cap
 *   API answer status 200; a Content-Length of 1 to FW_REL_API_MAX bytes,
 *              or chunked (Transfer-Encoding: chunked) and its releases
 *              read within FW_REL_SCAN_MAX bytes; an array of
 *              objects, the first FW_REL_API_RELEASES of them read, each
 *              with exactly one "tag_name", a JSON string of at most
 *              FW_REL_TAG_MAX bytes without escapes or control bytes, "v"
 *              and MAJOR.MINOR.PATCH as a tag, and at most one "draft",
 *              true or false; nesting at most FW_REL_SCAN_DEPTH levels,
 *              every bracket closed by its own kind.  Any release out of
 *              this form refuses the whole answer, a draft's included:
 *              nothing is offered rather than a release picked around it
 *   SHA256SUMS at most FW_REL_SUMS_MAX bytes of lines "<64 lowercase hex>",
 *              two spaces or a space and '*', an optional "./", a file name
 *              of letters, digits, '.', '-' and '_', and '\n'; any other
 *              line refuses the whole file; the image's name on exactly one
 *              line
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_RELEASE_H
#define RCBENCH_FW_RELEASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw_image.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FW_REL_REPO        "subtilitas/rcbench"
#define FW_REL_LATEST_URL  "https://github.com/" FW_REL_REPO "/releases/latest"
#define FW_REL_TAG_PREFIX  "https://github.com/" FW_REL_REPO "/releases/tag/"
#define FW_REL_DL_PREFIX   "https://github.com/" FW_REL_REPO "/releases/download/"
#define FW_REL_SUMS_NAME   "SHA256SUMS"
/** The newest release, testing or stable: GitHub's REST API. */
#define FW_REL_API_URL     "https://api.github.com/repos/" FW_REL_REPO \
                           "/releases?per_page=10"
/** The headers the API request carries besides the User-Agent. */
#define FW_REL_API_ACCEPT  "application/vnd.github+json"
#define FW_REL_API_VERSION "2022-11-28"

/** "v65535.65535.65535": the longest tag. */
#define FW_REL_TAG_MAX     18u
/** "65535.65535.65535" and its terminator. */
#define FW_REL_VER_TEXT    18u
/** "rcbench-panel-v65535.65535.65535.bin" and its terminator. */
#define FW_REL_NAME_MAX    40u
/** A redirect's Location, in bytes; a longer one is refused. */
#define FW_REL_URL_MAX     2048u
/** Redirects followed in one fetch at most. */
#define FW_REL_HOPS_MAX    3u
/** SHA256SUMS at most, in bytes. */
#define FW_REL_SUMS_MAX    4096u
/** Releases the API's answer holds, and the scan reads, at most. */
#define FW_REL_API_RELEASES 10u
/** The API's answer at most, by its Content-Length, in bytes.  Each
 *  release carries its notes and its assets: the 10 newest on 2026-10-18,
 *  v0.12.0 to v0.20.0, are 108198 bytes, the largest one 13734. */
#define FW_REL_API_MAX     262144u
/** Bytes of the API's answer scanned at most: the whole of the largest
 *  answer taken. */
#define FW_REL_SCAN_MAX    FW_REL_API_MAX
/** JSON nesting the scan follows at most, the array included. */
#define FW_REL_SCAN_DEPTH  32u

/** Why an answer is refused; FW_REL_OK is none of them. */
typedef enum {
    FW_REL_OK = 0,
    FW_REL_MISSING,     /**< no Location, or an empty one                 */
    FW_REL_LONG,        /**< over its bound, or cut on the way in         */
    FW_REL_CHAR,        /**< a byte outside printable ASCII               */
    FW_REL_PLACE,       /**< not the expected scheme, host or path        */
    FW_REL_VERSION,     /**< the tag is not v + MAJOR.MINOR.PATCH         */
    FW_REL_STATUS,      /**< an HTTP status this step does not take       */
    FW_REL_HOPS,        /**< more than FW_REL_HOPS_MAX redirects          */
    FW_REL_HOST,        /**< a redirect to a host not in FW_REL_HOSTS     */
    FW_REL_NO_LENGTH,   /**< no Content-Length, or a chunked body where
                             one is needed                               */
    FW_REL_EMPTY,       /**< a Content-Length of 0, or an empty chunked
                             API answer                                  */
    FW_REL_TOO_BIG,     /**< a Content-Length over the cap               */
    FW_REL_SHORT,       /**< the body ended before its Content-Length    */
    FW_REL_OVERRUN,     /**< more body bytes than asked for              */
    FW_REL_SUMS_BAD,    /**< SHA256SUMS has a line out of form           */
    FW_REL_SUMS_NONE,   /**< SHA256SUMS has no line for the file          */
    FW_REL_SUMS_TWICE,  /**< SHA256SUMS names the file on two lines       */
    FW_REL_TAG_NONE,    /**< API: a release without tag_name, no release
                             but drafts, or the answer ended first       */
    FW_REL_TAG_LONG,    /**< API: tag_name over FW_REL_TAG_MAX bytes      */
    FW_REL_TAG_FORM,    /**< API: tag_name not a plain JSON string, or the
                             JSON around it out of form                  */
    FW_REL_SCAN_LONG,   /**< API: the releases not read in
                             FW_REL_SCAN_MAX bytes                       */
    FW_REL_COUNT
} fw_rel_err_t;

/** The English name of @p e, for the console. */
const char *fw_rel_err_str(fw_rel_err_t e);

/**
 * "MAJOR.MINOR.PATCH" in exactly @p n bytes of @p s, by the tag's rule: digits
 * only, at most 5 a part, each part 0 to 65535, no leading zero.
 */
bool fw_release_parse_version(const char *s, size_t n, fw_version_t *out);

/**
 * The release a redirect from FW_REL_LATEST_URL names: @p loc of @p n bytes,
 * not terminated; @p cut when the header was longer than the buffer that
 * took it.  FW_REL_OK and @p out, or MISSING, LONG, CHAR, PLACE or VERSION.
 */
fw_rel_err_t fw_release_parse_tag(const char *loc, size_t n, bool cut,
                                  fw_version_t *out);

/** What a step does with an HTTP status. */
typedef enum {
    FW_REL_BODY = 0,    /**< 200: read the body                          */
    FW_REL_REDIRECT,    /**< 301, 302, 303, 307, 308: a Location follows  */
    FW_REL_REFUSED,     /**< anything else                                */
} fw_rel_status_t;

fw_rel_status_t fw_release_status(int status);

/**
 * Whether a download may follow a redirect to @p loc (@p n bytes, @p cut as
 * above) after @p hops redirects already followed.  FW_REL_OK, or MISSING,
 * LONG, CHAR, PLACE (not https://, or no path), HOST or HOPS.
 */
fw_rel_err_t fw_release_redirect(const char *loc, size_t n, bool cut,
                                 unsigned hops);

/** The hosts a download follows, NULL-terminated. */
extern const char *const FW_REL_HOSTS[];

/**
 * A body's announced size: @p length as the client read Content-Length, -1
 * for none, and @p chunked.  FW_REL_OK, or NO_LENGTH, EMPTY or TOO_BIG
 * against @p cap bytes.
 */
fw_rel_err_t fw_release_length(int64_t length, bool chunked, uint32_t cap);

/**
 * The SHA-256 SHA256SUMS gives for @p name: @p text of @p n bytes, not
 * terminated.  FW_REL_OK and @p out, or LONG, SUMS_BAD, SUMS_NONE or
 * SUMS_TWICE.
 */
fw_rel_err_t fw_release_parse_sums(const char *text, size_t n,
                                   const char *name,
                                   uint8_t out[FW_SHA256_BYTES]);

/**
 * The scan of the API's answer for the highest release: fed the body as it
 * comes, in pieces of any size, and nothing of it kept but the release
 * under way's tag and the highest so far.  76 bytes, on the caller's
 * stack.
 */
typedef struct {
    uint32_t     seen;    /**< bytes fed                                 */
    uint32_t     arrays;  /**< bit d: nesting level d is an array         */
    uint8_t      depth;   /**< levels open                               */
    uint8_t      st;      /**< where in the JSON the next byte falls      */
    uint8_t      want;    /**< whose value follows: other, tag, draft     */
    uint8_t      kn;      /**< key bytes kept                            */
    char         key[8];  /**< "tag_name" is the longest key read         */
    bool         klong;   /**< the key is longer, or escaped              */
    bool         ended;
    uint8_t      tn;      /**< tag bytes kept                            */
    char         tag[FW_REL_TAG_MAX];
    uint8_t      dn;      /**< draft's value bytes, up to 255             */
    char         dval[5]; /**< "false" is the longest kept                */
    bool         have_tag;   /**< the release under way's               */
    bool         have_draft;
    bool         draft;
    bool         have_best;
    uint8_t      releases;   /**< read whole                             */
    fw_version_t cur;     /**< the release under way's tag                */
    fw_version_t best;    /**< the highest no-draft release so far        */
    fw_rel_err_t err;     /**< once ended                                */
    fw_version_t found;   /**< once ended with FW_REL_OK                 */
} fw_rel_scan_t;

void fw_release_scan_init(fw_rel_scan_t *s);

/**
 * @p n more bytes of the answer.  True once the scan has ended: the tag
 * read, or the answer refused; bytes fed after that are not looked at.
 */
bool fw_release_scan_feed(fw_rel_scan_t *s, const uint8_t *d, size_t n);

/**
 * The scan's result once the answer is whole: FW_REL_OK and the highest
 * no-draft release in @p out, or TAG_NONE (a release without a tag, none
 * but drafts, or the answer ended before the array or the
 * FW_REL_API_RELEASES-th release did), TAG_LONG, TAG_FORM, SCAN_LONG or
 * VERSION.
 */
fw_rel_err_t fw_release_scan_end(const fw_rel_scan_t *s, fw_version_t *out);

/**
 * Whether release @p found is offered to a panel that runs @p running:
 * only a strictly newer one, and none when the running version did not
 * parse (@p running_ok false).  Each part counts as a number: 0.10.0 is
 * newer than 0.9.0.
 */
bool fw_release_offered(fw_version_t found, fw_version_t running,
                        bool running_ok);

/** "0.20.0" into @p out of FW_REL_VER_TEXT bytes. */
void fw_release_version_text(fw_version_t v, char out[FW_REL_VER_TEXT]);

/** "rcbench-panel-v0.20.0.bin" into @p out of FW_REL_NAME_MAX bytes. */
void fw_release_image_name(fw_version_t v, char out[FW_REL_NAME_MAX]);

/**
 * The download URL of @p file in release @p v into @p out of @p cap bytes;
 * false when it does not fit.
 */
bool fw_release_url(fw_version_t v, const char *file, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_RELEASE_H */
