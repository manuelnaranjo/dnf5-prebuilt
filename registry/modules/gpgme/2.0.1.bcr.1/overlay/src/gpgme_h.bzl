"Custom rule for generating gpgme.h from gpgme.h.in with a configurable host triplet."

def _gpgme_h_impl(ctx):
    subs = dict(ctx.attr.substitutions)
    subs["@GPGME_CONFIG_HOST@"] = ctx.attr.config_host
    ctx.actions.expand_template(
        template = ctx.file.template,
        output = ctx.outputs.out,
        substitutions = subs,
    )

gpgme_h = rule(
    implementation = _gpgme_h_impl,
    attrs = {
        "template": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "out": attr.output(mandatory = True),
        "config_host": attr.string(
            mandatory = True,
            doc = "Host triplet for @GPGME_CONFIG_HOST@, e.g. x86_64-pc-linux-gnu. Supports select().",
        ),
        "substitutions": attr.string_dict(
            doc = "Additional static substitutions (no select() needed).",
        ),
    },
)
