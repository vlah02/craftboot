#pragma once
/* The build injects -DCRAFTBOOT_VERSION_GIT="<git describe --tags>" (see
 * Makefile); tags are the source of truth. The fallback covers builds
 * outside the Makefile (IDE indexers, ad-hoc compiles). */
#ifdef CRAFTBOOT_VERSION_GIT
#define CRAFTBOOT_VERSION CRAFTBOOT_VERSION_GIT
#else
#define CRAFTBOOT_VERSION "v3.0"
#endif
