/*

Merge a list of rpms into a single archive similar to rpm2archive.cc from
upstream rpm.

Copyright (C) 2026  Manuel Naranjo
Copyright (C) 1998 by Red Hat Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#include <rpm/rpmlib.h>
#include <rpm/rpmfi.h>
#include <rpm/rpmstring.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmio.h>
#include <rpm/rpmurl.h>
#include <rpm/rpmts.h>

#include <popt.h>

#include <archive.h>
#include <archive_entry.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>

#include <string>
#include <unordered_set>

#define BUFSIZE (128*1024)

static int compress = -1;           /* -1 = auto-detect from output extension */
static const char *format = NULL;   /* NULL = auto-detect from output extension */
static const char *output = NULL;   /* mandatory -o/--output */
static int stop_on_error = 0;

static struct poptOption optionsTable[] = {
    { "output", 'o', POPT_ARG_STRING, &output, 0,
        "output archive file (required)",
        "FILE" },
    { "nocompression", 'n', POPT_ARG_VAL, &compress, 0,
        "create uncompressed archive",
        NULL },
    { "format", 'f', POPT_ARG_STRING, &format, 0,
        "archive format (pax|cpio)",
        NULL },
    { "stop-on-error", '\0', POPT_ARG_VAL, &stop_on_error, 1,
        "stop processing on first error",
        NULL },
    POPT_AUTOHELP
    POPT_TABLEEND
};

static void fill_archive_entry(struct archive_entry * entry, rpmfi fi,
				char **hardlink)
{
    archive_entry_clear(entry);
    const char * dn = rpmfiDN(fi);
    if (!strcmp(dn, "")) dn = "/";
    struct stat sb;

    char * filename = rstrscat(NULL, ".", dn, rpmfiBN(fi), NULL);
    archive_entry_copy_pathname(entry, filename);
    free(filename);

    rpmfiStat(fi, 0, &sb);
    archive_entry_copy_stat(entry, &sb);

    archive_entry_set_uname(entry, rpmfiFUser(fi));
    archive_entry_set_gname(entry, rpmfiFGroup(fi));

    if (S_ISLNK(sb.st_mode))
	archive_entry_set_symlink(entry, rpmfiFLink(fi));

    if (sb.st_nlink > 1) {
	if (rpmfiArchiveHasContent(fi)) {
	    /* hardlink sizes are special, see rpmfiStat() */
	    archive_entry_set_size(entry, rpmfiFSize(fi));
	    free(*hardlink);
	    *hardlink = rstrdup(archive_entry_pathname(entry));
	} else {
	    archive_entry_set_hardlink(entry, *hardlink);
	}
    }
}

static int write_file_content(struct archive * a, char * buf, rpmfi fi)
{
    rpm_loff_t left = rpmfiFSize(fi);
    size_t len, read;

    while (left) {
	len = (left > BUFSIZE ? BUFSIZE : left);
	read = rpmfiArchiveRead(fi, buf, len);
	if (read==len) {
	    if (archive_write_data(a, buf, len) < 0) {
		fprintf(stderr, "Error writing archive: %s\n",
				archive_error_string(a));
		break;
	    }
	} else {
	    fprintf(stderr, "Error reading file from rpm payload\n");
	    break;
	}
	left -= len;
    }

    return (left > 0);
}

/* This code sets the charset of the open archive. Messing with the
   locale is currently the only way to do it, see:
   https://github.com/libarchive/libarchive/pull/1966
 */
static void set_archive_utf8(struct archive * a)
{
#ifdef ENABLE_NLS
    const char * t = setlocale(LC_CTYPE, NULL);
    char * old_ctype = t ? rstrdup(t) : NULL;
    (void) setlocale(LC_CTYPE, C_LOCALE);
    (void) archive_write_set_options(a, "hdrcharset=UTF-8");
    if (old_ctype) {
	(void) setlocale(LC_CTYPE, old_ctype);
	free(old_ctype);
    }
#endif
}

static void detect_format_and_compress(const char *outname)
{
    size_t len = strlen(outname);

    if (format == NULL) {
	if (len >= 7 && rstreq(outname + len - 7, ".tar.gz"))
	    format = "pax";
	else if (len >= 4 && rstreq(outname + len - 4, ".tgz"))
	    format = "pax";
	else if (len >= 4 && rstreq(outname + len - 4, ".tar"))
	    format = "pax";
	else if (len >= 8 && rstreq(outname + len - 8, ".cpio.gz"))
	    format = "cpio";
	else if (len >= 5 && rstreq(outname + len - 5, ".cpio"))
	    format = "cpio";
	else
	    format = "pax";
    }

    if (compress == -1) {
	if (len >= 3 && rstreq(outname + len - 3, ".gz"))
	    compress = 1;
	else if (len >= 4 && rstreq(outname + len - 4, ".tgz"))
	    compress = 1;
	else
	    compress = 0;
    }
}

static int process_one_package(rpmts ts, const char *filename,
				struct archive *a, char *buf,
				std::unordered_set<std::string> &seen_paths,
				int format_code)
{
    FD_t fdi;
    FD_t gzdi;
    Header h;
    int rc = 0;
    int e;
    char * rpmio_flags = NULL;
    struct archive_entry *entry;
    char * hardlink = NULL;

    fdi = Fopen(filename, "r.ufdio");

    if (Ferror(fdi)) {
	fprintf(stderr, "rpms2archive: %s: %s\n", filename, Fstrerror(fdi));
	Fclose(fdi);
	return 1;
    }

    rc = rpmReadPackageFile(ts, fdi, "rpms2archive", &h);

    switch (rc) {
    case RPMRC_OK:
    case RPMRC_NOKEY:
    case RPMRC_NOTTRUSTED:
	break;
    case RPMRC_NOTFOUND:
	fprintf(stderr, "rpms2archive: %s: argument is not an RPM package\n",
		filename);
	Fclose(fdi);
	return 1;
    case RPMRC_FAIL:
    default:
	fprintf(stderr, "rpms2archive: %s: error reading header from package\n",
		filename);
	Fclose(fdi);
	return 1;
    }

    {	const char *compr = headerGetString(h, RPMTAG_PAYLOADCOMPRESSOR);
	rpmio_flags = rstrscat(NULL, "r.", compr ? compr : "gzip", NULL);
    }

    gzdi = Fdopen(fdi, rpmio_flags);
    free(rpmio_flags);

    if (gzdi == NULL) {
	fprintf(stderr, "rpms2archive: %s: cannot re-open payload: %s\n",
		filename, Fstrerror(gzdi));
	headerFree(h);
	return 1;
    }

    entry = archive_entry_new();

    rpmfiles files = rpmfilesNew(NULL, h, 0, RPMFI_KEEPHEADER);
    rpmfi fi = rpmfiNewArchiveReader(gzdi, files,
	format_code == ARCHIVE_FORMAT_CPIO_SVR4_NOCRC ?
	    RPMFI_ITER_READ_ARCHIVE : RPMFI_ITER_READ_ARCHIVE_CONTENT_FIRST);

    while ((rc = rpmfiNext(fi)) >= 0) {
	fill_archive_entry(entry, fi, &hardlink);

	const char *path = archive_entry_pathname(entry);
	if (seen_paths.count(path)) {
	    fprintf(stderr, "Warning: skipping duplicate path %s (from %s)\n",
		    path, filename);
	    continue;
	}
	seen_paths.insert(path);

	e = archive_write_header(a, entry);
	if (e == ARCHIVE_FAILED && archive_errno(a) == ERANGE) {
	    fprintf(stderr, "Warning: file too large for format, skipping: %s\n",
			    rpmfiFN(fi));
	    continue;
	}
	if (e == ARCHIVE_WARN) {
	    fprintf(stderr, "Warning writing archive: %s (%d)\n",
			    archive_error_string(a), archive_errno(a));
	} else if (e != ARCHIVE_OK) {
	    fprintf(stderr, "Error writing archive: %s (%d)\n",
			    archive_error_string(a), archive_errno(a));
	    rc = 1;
	    break;
	}
	if (S_ISREG(archive_entry_mode(entry)) && rpmfiArchiveHasContent(fi)) {
	    if (write_file_content(a, buf, fi)) {
		rc = 1;
		break;
	    }
	}
    }

    if (rc == RPMERR_ITER_END)
	rc = 0;
    else if (rc > 0)
	rc = 1;

    free(hardlink);
    rpmfilesFree(files);
    rpmfiFree(fi);
    archive_entry_free(entry);
    Fclose(gzdi);
    headerFree(h);
    return rc;
}

int main(int argc, char *argv[])
{
    int rc = 0;
    poptContext optCon;
    const char *fn;
    int format_code = 0;

    rsetprogname(argv[0]);
    rpmReadConfigFiles(NULL, NULL);

    optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0);
    poptSetOtherOptionHelp(optCon, "[OPTIONS]* <RPMFILE...>");

    while ((rc = poptGetNextOpt(optCon)) != -1) {
	if (rc < 0) {
	    fprintf(stderr, "%s: %s\n",
		    poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		    poptStrerror(rc));
	    exit(EXIT_FAILURE);
	}
    }

    if (output == NULL) {
	fprintf(stderr, "Error: output file is required (-o/--output)\n");
	poptPrintUsage(optCon, stderr, 0);
	exit(EXIT_FAILURE);
    }

    if (!poptPeekArg(optCon)) {
	fprintf(stderr, "Error: at least one RPM file is required\n");
	poptPrintUsage(optCon, stderr, 0);
	exit(EXIT_FAILURE);
    }

    detect_format_and_compress(output);

    if (rstreq(format, "pax")) {
	format_code = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED;
    } else if (rstreq(format, "cpio")) {
	format_code = ARCHIVE_FORMAT_CPIO_SVR4_NOCRC;
    } else {
	fprintf(stderr, "Error: Format %s is not supported\n", format);
	exit(EXIT_FAILURE);
    }

    struct archive *a = archive_write_new();
    if (compress) {
	if (archive_write_add_filter_gzip(a) != ARCHIVE_OK) {
	    fprintf(stderr, "%s\n", archive_error_string(a));
	    exit(EXIT_FAILURE);
	}
    }

    if (archive_write_set_format(a, format_code) != ARCHIVE_OK) {
	fprintf(stderr, "Error: Format %s is not supported\n", format);
	exit(EXIT_FAILURE);
    }

    if (format_code == ARCHIVE_FORMAT_TAR_PAX_RESTRICTED)
	set_archive_utf8(a);

    if (archive_write_open_filename(a, output) != ARCHIVE_OK) {
	fprintf(stderr, "Error: Can't open output file: %s\n", output);
	exit(EXIT_FAILURE);
    }

    rpmts ts = rpmtsCreate();
    rpmVSFlags vsflags = 0;
    vsflags |= RPMVSF_MASK_NODIGESTS;
    vsflags |= RPMVSF_MASK_NOSIGNATURES;
    vsflags |= RPMVSF_NOHDRCHK;
    (void) rpmtsSetVSFlags(ts, vsflags);

    char * buf = (char *)rmalloc(BUFSIZE);
    std::unordered_set<std::string> seen_paths;

    int any_error = 0;
    int any_success = 0;

    while ((fn = poptGetArg(optCon)) != NULL) {
	int pkg_rc = process_one_package(ts, fn, a, buf, seen_paths, format_code);
	if (pkg_rc != 0) {
	    any_error = 1;
	    if (stop_on_error)
		break;
	} else {
	    any_success = 1;
	}
    }

    if (archive_write_close(a) != ARCHIVE_OK) {
	fprintf(stderr, "Error writing archive: %s\n", archive_error_string(a));
	any_error = 1;
    }
    archive_write_free(a);

    if (any_error && !any_success)
	unlink(output);

    buf = rfree(buf);
    rpmtsFree(ts);
    poptFreeContext(optCon);

    return any_error ? EXIT_FAILURE : EXIT_SUCCESS;
}
