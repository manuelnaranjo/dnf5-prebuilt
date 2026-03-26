"""Shared constants for the util-linux Bazel build."""

# Version derived from tools/git-version-gen (without the leading 'v').
PACKAGE_VERSION = "2.41.3"
PACKAGE = "util-linux"

LIBBLKID_DATE = "15-Dec-2025"

# Split for major.minor.patch integers used in header substitutions.
_VER = PACKAGE_VERSION.split(".")
MAJOR = _VER[0]
MINOR = _VER[1]
PATCH = _VER[2]

# Common C compilation flags applied to all util-linux cc_library / cc_binary targets.
# -include config.h mimics meson's:
#   add_project_arguments('-include', build_dir / 'config.h', language : 'c')
# config.h is reachable because every target lists //:config in deps, which
# adds the Bazel output directory to the -iquote search path.
COPTS = [
    "-D_GNU_SOURCE",
    "-D_FILE_OFFSET_BITS=64",
    "-fsigned-char",
    "-include",
    "config.h",
    # Suppress warnings that are noisy or come from generated code.
    "-Wno-unused-parameter",
    "-Wno-missing-field-initializers",
    "-Wno-sign-compare",
]
