/*
 * The firmware version, as one number both images and the host suite agree on.
 *
 * It lives beside the link because its only job is to travel: the coprocessor
 * publishes it on the identity page and the panel prints what came back, so
 * the version on the wire and the version in the changelog are the same
 * claim.  Before this existed both images reported 0.0.0 while the repository
 * said otherwise, which is the kind of disagreement nobody notices until a
 * board in the field is asked what it is running.
 *
 * `tools/check_docs.py` holds these three numbers to the newest heading in
 * CHANGELOG.md, so cutting a release without moving them fails the build
 * rather than shipping a lie.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_VERSION_H
#define RCBENCH_VERSION_H

#define RCBENCH_VERSION_MAJOR 0
#define RCBENCH_VERSION_MINOR 3
#define RCBENCH_VERSION_PATCH 0

#define RCBENCH_STRINGIFY_(x) #x
#define RCBENCH_STRINGIFY(x)  RCBENCH_STRINGIFY_(x)

/** "0.3.0", for anything that prints it rather than sends it. */
#define RCBENCH_VERSION_STRING              \
    RCBENCH_STRINGIFY(RCBENCH_VERSION_MAJOR) "." \
    RCBENCH_STRINGIFY(RCBENCH_VERSION_MINOR) "." \
    RCBENCH_STRINGIFY(RCBENCH_VERSION_PATCH)

#endif /* RCBENCH_VERSION_H */
