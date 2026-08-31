#pragma once

// ---------------------------------------------------------------------------
// Application version. Shared by version.rc (VERSIONINFO resource) and
// instance_guard.cpp (single-instance / new-version-replaces-old check).
//
// NOTE: keep this file pure ASCII. version.h is also included by version.rc,
// and the Windows resource compiler (rc.exe) decodes source files using the
// system ANSI code page; non-ASCII (e.g. UTF-8 Chinese) comments break its
// line parsing and cause RC1004/RC2104 errors.
//
// To publish a new version, bump the four numbers below and rebuild:
//   - capswriter.exe / capswriter-gui.exe file properties show the version
//   - launching the newer build terminates an older running instance and
//     takes over (see InstanceGuard)
// ---------------------------------------------------------------------------
#define CAPSWRITER_VERSION_MAJOR 1
#define CAPSWRITER_VERSION_MINOR 0
#define CAPSWRITER_VERSION_PATCH 0
#define CAPSWRITER_VERSION_BUILD 0

// FILEVERSION resource needs a comma-separated 4-tuple.
#define CAPSWRITER_VERSION_FILE CAPSWRITER_VERSION_MAJOR, CAPSWRITER_VERSION_MINOR, CAPSWRITER_VERSION_PATCH, CAPSWRITER_VERSION_BUILD

// "1.0.0.0" string for FileVersion / ProductVersion string resources.
#define CAPSWRITER_VERSION_STR_IMPL(major, minor, patch, build) #major "." #minor "." #patch "." #build
#define CAPSWRITER_VERSION_STR CAPSWRITER_VERSION_STR_IMPL(CAPSWRITER_VERSION_MAJOR, CAPSWRITER_VERSION_MINOR, CAPSWRITER_VERSION_PATCH, CAPSWRITER_VERSION_BUILD)
