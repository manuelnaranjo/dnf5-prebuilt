# Common compiler options applied to all dnf5 targets.
COPTS = [
    "-std=c++20",
    "-fvisibility=hidden",
    "-fvisibility-inlines-hidden",
    "-Wall",
    "-Wextra",
    "-Wcast-align",
    "-Wformat-nonliteral",
    "-Wmissing-format-attribute",
    "-Wsign-compare",
    "-Wsign-conversion",
    "-Wtype-limits",
    "-Wuninitialized",
    "-Wwrite-strings",
    "-Wconversion",
]

# Defines that must propagate to all consumers of libdnf5 public headers.
LIBDNF5_DEFINES = [
    # Required for C++20: renames `requires` to `dep_requires` in libsolv solvable structs
    "LIBSOLV_SOLVABLE_PREPEND_DEP",
    # ENABLE_SOLV_FOCUSNEW is ON by default in CMake
    "LIBSOLV_FLAG_FOCUSNEW=1",
    # Project version
    "PROJECT_VERSION_PRIME=5",
    "PROJECT_VERSION_MAJOR=4",
    "PROJECT_VERSION_MINOR=0",
    "PROJECT_VERSION_MICRO=0",
]
