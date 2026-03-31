"""Custom rules for generating nettle data headers."""

def _ecc_header_impl(ctx):
    out = ctx.outputs.out
    ctx.actions.run_shell(
        inputs = [],
        outputs = [out],
        tools = [ctx.executable._eccdata],
        command = "{eccdata} {curve} {k} {c} 64 > {out}".format(
            eccdata = ctx.executable._eccdata.path,
            curve = ctx.attr.curve,
            k = ctx.attr.k,
            c = ctx.attr.c,
            out = out.path,
        ),
        mnemonic = "EccHeader",
        progress_message = "Generating %s" % out.short_path,
    )
    return [DefaultInfo(files = depset([out]))]

ecc_header = rule(
    implementation = _ecc_header_impl,
    attrs = {
        "curve": attr.string(mandatory = True),
        "k": attr.string(mandatory = True),
        "c": attr.string(mandatory = True),
        "out": attr.output(mandatory = True),
        "_eccdata": attr.label(
            default = "//:eccdata",
            executable = True,
            cfg = "exec",
        ),
    },
)

def _des_header_impl(ctx):
    out = ctx.outputs.out
    ctx.actions.run_shell(
        inputs = [],
        outputs = [out],
        tools = [ctx.executable._desdata],
        command = "{desdata} {table} > {out}".format(
            desdata = ctx.executable._desdata.path,
            table = ctx.attr.table,
            out = out.path,
        ),
        mnemonic = "DesHeader",
        progress_message = "Generating %s" % out.short_path,
    )
    return [DefaultInfo(files = depset([out]))]

des_header = rule(
    implementation = _des_header_impl,
    attrs = {
        "table": attr.string(mandatory = True),
        "out": attr.output(mandatory = True),
        "_desdata": attr.label(
            default = "//:desdata",
            executable = True,
            cfg = "exec",
        ),
    },
)
