"""Macro for nettle cc_test targets."""

load("@rules_cc//cc:defs.bzl", "cc_test")

def nettle_test(name, extra_deps = []):
    """Create a cc_test for a single nettle test binary."""
    cc_test(
        name = name,
        srcs = [name + ".c"],
        defines = ["HAVE_CONFIG_H"],
        deps = [
            ":testutils",
            "//:hogweed",
            "//:nettle",
            "//:nettle_hdrs",
        ] + extra_deps,
    )
