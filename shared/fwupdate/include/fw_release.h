/*
 * What GitHub says about the panel's releases, read strictly.
 *
 * The panel asks GitHub two things over HTTPS (HTTP, hypertext transfer
 * protocol, over TLS, transport layer security).  Which release is the
 * newest: a GET of FW_REL_LATEST_URL, answered with a redirect whose
 * Location names the tag (".../releases/tag/v0.20.0"); only that header is
 * read, no body.  And, for a download, two files of that release:
 * SHA256SUMS and rcbench-panel-vX.Y.Z.bin, each a redirect from github.com
 * to a githubusercontent.com host and then the file.
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
 *   length     a Content-Length, not chunked, 1 byte to the cap
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
    FW_REL_NO_LENGTH,   /**< no Content-Length, or a chunked body         */
    FW_REL_EMPTY,       /**< a Content-Length of 0                        */
    FW_REL_TOO_BIG,     /**< a Content-Length over the cap               */
    FW_REL_SHORT,       /**< the body ended before its Content-Length    */
    FW_REL_OVERRUN,     /**< more body bytes than asked for              */
    FW_REL_SUMS_BAD,    /**< SHA256SUMS has a line out of form           */
    FW_REL_SUMS_NONE,   /**< SHA256SUMS has no line for the file          */
    FW_REL_SUMS_TWICE,  /**< SHA256SUMS names the file on two lines       */
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
