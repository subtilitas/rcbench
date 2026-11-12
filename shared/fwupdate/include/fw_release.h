/**
 * @file fw_release.h
 * @brief What GitHub says about the panel's releases, read strictly.
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
 *            that are no draft, and the highest of the panel's release
 *            line: GitHub's order is by date, not by version.
 *
 * The panel's release line: 0.22 is the last release for the ESP32-S3
 * panel, and later releases are for the ESP32-P4 panel.  The ESP32-P4
 * panel's line holds every release: a check offers any release newer than
 * the running build (FW_REL_LINE_CAPPED 0).  Held to the ESP32-S3 panel's
 * line (fw_release_set_line_capped()), a check on either channel offers a
 * release of FW_REL_LINE_MAJOR.FW_REL_LINE_MINOR only, a 0.22.x patch
 * release newer than the running build; 0.23.0 and later are never
 * offered.  Where STABLE's one tag is past the line and newer than the
 * running build, the check reads the release list once more for the
 * line's newest.
 *
 * And, for a download, two files of that release: SHA256SUMS and
 * rcbench-panel-p4-vX.Y.Z.bin, each a redirect from github.com to a
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

/** @brief The repository releases come from. */
#define FW_REL_REPO        "subtilitas/rcbench"
/** @brief Redirects to the newest release. */
#define FW_REL_LATEST_URL  "https://github.com/" FW_REL_REPO "/releases/latest"
/** @brief A release's page, before its tag. */
#define FW_REL_TAG_PREFIX  "https://github.com/" FW_REL_REPO "/releases/tag/"
/** @brief A release's downloads, before the tag and the file name. */
#define FW_REL_DL_PREFIX   "https://github.com/" FW_REL_REPO "/releases/download/"
/** @brief The checksum file's name in a release. */
#define FW_REL_SUMS_NAME   "SHA256SUMS"
/** The newest release, testing or stable: GitHub's REST API. */
#define FW_REL_API_URL     "https://api.github.com/repos/" FW_REL_REPO \
                           "/releases?per_page=10"
/** The headers the API request carries besides the User-Agent. */
#define FW_REL_API_ACCEPT  "application/vnd.github+json"
#define FW_REL_API_VERSION "2022-11-28" /**< the X-GitHub-Api-Version value */

/**
 * @name The panel's release line
 * @brief MAJOR and MINOR of the releases a check held to the line offers:
 *        0.22, the last release line for the ESP32-S3 panel.
 * @{
 */
#define FW_REL_LINE_MAJOR  0u    /**< the line's MAJOR */
#define FW_REL_LINE_MINOR  22u   /**< the line's MINOR */
/** @} */

/**
 * @brief Whether a check is held to FW_REL_LINE_MAJOR.FW_REL_LINE_MINOR at
 *        start: 0 for the ESP32-P4 panel, which is offered every release
 *        newer than its build; 1 for the ESP32-S3 panel's 0.22 line.
 */
#ifndef FW_REL_LINE_CAPPED
#define FW_REL_LINE_CAPPED 0
#endif

/** @brief The longest tag, "v65535.65535.65535", in bytes. */
#define FW_REL_TAG_MAX     18u
/** @brief The longest version text, "65535.65535.65535", with its terminator. */
#define FW_REL_VER_TEXT    18u
/** @brief The longest asset name, "rcbench-panel-p4-v65535.65535.65535.bin", with its terminator. */
#define FW_REL_NAME_MAX    40u
/** @brief A redirect's Location, in bytes; a longer one is refused. */
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

/**
 * @brief The English name of @p e, for the console.
 *
 * @param e  The error.
 * @return The name; "?" for a value out of range.
 */
const char *fw_rel_err_str(fw_rel_err_t e);

/**
 * @brief Read "MAJOR.MINOR.PATCH" from exactly @p n bytes of @p s, by the
 * tag's rule: digits only, at most 5 a part, each part 0 to 65535, no leading
 * zero.
 *
 * @param s    The text, not terminated.
 * @param n    Its length, bytes.
 * @param out  Receives the version.
 * @return true when the text is a version.
 */
bool fw_release_parse_version(const char *s, size_t n, fw_version_t *out);

/**
 * @brief The release a redirect from FW_REL_LATEST_URL names.
 *
 * @param loc  The Location header's value, not terminated.
 * @param n    Its length, bytes.
 * @param cut  true when the header was longer than the buffer that took it.
 * @param out  Receives the release.
 * @return FW_REL_OK and @p out, or MISSING, LONG, CHAR, PLACE or VERSION.
 */
fw_rel_err_t fw_release_parse_tag(const char *loc, size_t n, bool cut,
                                  fw_version_t *out);

/** What a step does with an HTTP status. */
typedef enum {
    FW_REL_BODY = 0,    /**< 200: read the body                          */
    FW_REL_REDIRECT,    /**< 301, 302, 303, 307, 308: a Location follows  */
    FW_REL_REFUSED,     /**< anything else                                */
} fw_rel_status_t;

/**
 * @brief What a step does with an HTTP status.
 *
 * @param status  The status code.
 * @return FW_REL_BODY for 200, FW_REL_REDIRECT for 301, 302, 303, 307 and 308,
 *         FW_REL_REFUSED for any other.
 */
fw_rel_status_t fw_release_status(int status);

/**
 * @brief Whether a download may follow a redirect.
 *
 * @param loc   The Location header's value, not terminated.
 * @param n     Its length, bytes.
 * @param cut   true when the header was longer than the buffer that took it.
 * @param hops  The redirects already followed.
 * @return FW_REL_OK, or MISSING, LONG, CHAR, PLACE (not https://, or no
 *         path), HOST or HOPS.
 */
fw_rel_err_t fw_release_redirect(const char *loc, size_t n, bool cut,
                                 unsigned hops);

/** The hosts a download follows, NULL-terminated. */
extern const char *const FW_REL_HOSTS[];

/**
 * @brief Check a body's announced size.
 *
 * @param length   Content-Length as the client read it; -1 for none.
 * @param chunked  true for a chunked body.
 * @param cap      The most bytes taken.
 * @return FW_REL_OK, or NO_LENGTH, EMPTY or TOO_BIG.
 */
fw_rel_err_t fw_release_length(int64_t length, bool chunked, uint32_t cap);

/**
 * @brief The SHA-256 SHA256SUMS gives for @p name.
 *
 * @param text  The file's text, not terminated.
 * @param n     Its length, bytes.
 * @param name  The file name looked for.
 * @param out   Receives the digest.
 * @return FW_REL_OK and @p out, or LONG, SUMS_BAD, SUMS_NONE or SUMS_TWICE.
 */
fw_rel_err_t fw_release_parse_sums(const char *text, size_t n,
                                   const char *name,
                                   uint8_t out[FW_SHA256_BYTES]);

/**
 * @brief The scan of the API's answer for the highest release.
 *
 * Fed the body as it comes, in pieces of any size, and nothing of it kept
 * but the release under way's tag, the highest so far and the highest of
 * the panel's release line so far.  84 bytes, on the caller's stack.
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
    bool         ended;   /**< the scan has ended; err holds why          */
    uint8_t      tn;      /**< tag bytes kept                            */
    char         tag[FW_REL_TAG_MAX]; /**< the tag under way's bytes       */
    uint8_t      dn;      /**< draft's value bytes, up to 255             */
    char         dval[5]; /**< "false" is the longest kept                */
    bool         have_tag;   /**< the release under way's               */
    bool         have_draft; /**< the release under way's "draft" was read */
    bool         draft;      /**< the release under way is a draft       */
    bool         have_best;  /**< best holds a release                   */
    uint8_t      releases;   /**< read whole                             */
    fw_version_t cur;     /**< the release under way's tag                */
    fw_version_t best;    /**< the highest no-draft release so far        */
    bool         have_line; /**< line holds a release of the line   */
    fw_version_t line;    /**< the highest no-draft one of the line so far */
    fw_rel_err_t err;     /**< once ended                                */
    fw_version_t found;   /**< once ended with FW_REL_OK                 */
} fw_rel_scan_t;

/**
 * @brief Start a scan.
 *
 * @param s  The scan.
 */
void fw_release_scan_init(fw_rel_scan_t *s);

/**
 * @brief Feed @p n more bytes of the answer.
 *
 * Bytes fed after the scan has ended are not looked at.
 *
 * @param s  The scan.
 * @param d  The bytes.
 * @param n  Their number.
 * @return true once the scan has ended: the tag read, or the answer refused.
 */
bool fw_release_scan_feed(fw_rel_scan_t *s, const uint8_t *d, size_t n);

/**
 * @brief The scan's result once the answer is whole.
 *
 * @param s    The scan.
 * @param out  Receives the highest no-draft release.
 * @return FW_REL_OK and the release in @p out, or TAG_NONE (a release without
 *         a tag, none but drafts, or the answer ended before the array or the
 *         FW_REL_API_RELEASES-th release did), TAG_LONG, TAG_FORM, SCAN_LONG
 *         or VERSION.
 */
fw_rel_err_t fw_release_scan_end(const fw_rel_scan_t *s, fw_version_t *out);

/**
 * @brief The highest no-draft release of the panel's release line among
 *        those the scan read.
 * @param s   the scan, ended; NULL reads as none
 * @param out the release, when there is one; untouched otherwise; may be
 *            NULL
 * @return true once the scan ended with FW_REL_OK and read a no-draft
 *         release of the line; false otherwise
 */
bool fw_release_scan_line(const fw_rel_scan_t *s, fw_version_t *out);

/**
 * @brief Whether a version is of the panel's release line.
 * @param v the version
 * @return true for every version while the line is not capped
 *         (fw_release_line_capped()); once capped, true for MAJOR
 *         FW_REL_LINE_MAJOR and MINOR FW_REL_LINE_MINOR, any PATCH
 */
bool fw_release_in_line(fw_version_t v);

/**
 * @brief Whether the panel's release line is capped at
 *        FW_REL_LINE_MAJOR.FW_REL_LINE_MINOR.
 * @return FW_REL_LINE_CAPPED until fw_release_set_line_capped() sets it
 */
bool fw_release_line_capped(void);

/**
 * @brief Caps the panel's release line at
 *        FW_REL_LINE_MAJOR.FW_REL_LINE_MINOR, or lifts the cap.  The panel
 *        keeps FW_REL_LINE_CAPPED; the host suite holds both lines.
 * @param capped true for the ESP32-S3 panel's 0.22 line, false for every
 *               release
 */
void fw_release_set_line_capped(bool capped);

/**
 * @brief Whether a release is offered to the panel.
 *
 * Only one of the panel's release line (fw_release_in_line()) and strictly
 * newer than the running build.  Each part counts as a number: 0.22.10 is
 * newer than 0.22.9.
 *
 * @param found      the release a check found
 * @param running    the version the panel runs
 * @param running_ok false when the running version did not parse
 * @return true when @p found is offered; false always when @p running_ok
 *         is false
 */
bool fw_release_offered(fw_version_t found, fw_version_t running,
                        bool running_ok);

/**
 * @brief Write a version as "0.20.0" into @p out of FW_REL_VER_TEXT bytes.
 *
 * @param v    The version.
 * @param out  Receives the text, terminated.
 */
void fw_release_version_text(fw_version_t v, char out[FW_REL_VER_TEXT]);

/**
 * @brief The file name of a release's panel image.
 *
 * Writes "rcbench-panel-p4-v0.20.0.bin" for version 0.20.0.  The longest
 * name, for 65535.65535.65535, fills FW_REL_NAME_MAX bytes with its
 * terminator.
 *
 * @param v   Version of the release.
 * @param out Receives the name, terminated.
 */
void fw_release_image_name(fw_version_t v, char out[FW_REL_NAME_MAX]);

/**
 * @brief Write the download URL of @p file in release @p v into @p out.
 *
 * @param v     The release.
 * @param file  The file name.
 * @param out   Receives the URL, terminated.
 * @param cap   Size of @p out, bytes.
 * @return false when the URL does not fit.
 */
bool fw_release_url(fw_version_t v, const char *file, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_RELEASE_H */
