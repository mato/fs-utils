/* $NetBSD: rm.c,v 1.2 2009/11/05 14:39:16 stacktic Exp $ */
/* from */
/* NetBSD: rm.c,v 1.49 2009/02/14 08:05:04 lukem Exp  */

/*-
 * Copyright (c) 1990, 1993, 1994, 2003
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "fs-utils.h"

#ifndef lint
__COPYRIGHT("@(#) Copyright (c) 1990, 1993, 1994\
 The Regents of the University of California.  All rights reserved.");
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)rm.c	8.8 (Berkeley) 4/27/95";
#else
__RCSID("$NetBSD: rm.c,v 1.2 2009/11/05 14:39:16 stacktic Exp $");
#endif
#endif /* not lint */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#if HAVE_NBCOMPAT_H
#include <nbcompat.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <locale.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef S_IFWHT
#define S_IFWHT (0)
#define S_ISWHT(x) (0)
#endif

#include <rump/rump_syscalls.h>

#include <fts2fsufts.h>
#include <fsu_utils.h>
#include <fsu_mount.h>

#define lstat(a, b) rump_sys_lstat(a, b)
#define rmdir(a) rump_sys_rmdir(a)
#define unlink(a) rump_sys_unlink(a)
#define undelete(a) (0) /* not supported */
#define access(a, b) (0) /* we always have access */



int dflag, eval, fflag, iflag, Pflag, stdin_ok, vflag, Wflag;

int	check(char *, char *, struct stat *);
void	checkdot(char **);
void	rm_file(char **);
int	rm_overwrite(char *, struct stat *);
void	rm_tree(char **);
void	usage(void);
int	main(int, char *[]);

/*
 * For the sake of the `-f' flag, check whether an error number indicates the
 * failure of an operation due to an non-existent file, either per se (ENOENT)
 * or because its filename argument was illegal (ENAMETOOLONG, ENOTDIR).
 */
#define NONEXISTENT(x) \
    ((x) == ENOENT || (x) == ENAMETOOLONG || (x) == ENOTDIR)

/*
 * rm --
 *	This rm is different from historic rm's, but is expected to match
 *	POSIX 1003.2 behavior.  The most visible difference is that -f
 *	has two specific effects now, ignore non-existent files and force
 * 	file removal.
 */
int
main(int argc, char *argv[])
{
	int ch, rflag;

	setprogname(argv[0]);
	(void)setlocale(LC_ALL, "");

	if (fsu_mount(&argc, &argv, MOUNT_READWRITE) != 0)
		errx(-1, NULL);

	Pflag = rflag = 0;
	while ((ch = getopt(argc, argv, "dfiPRrvW")) != -1)
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		case 'f':
			fflag = 1;
			iflag = 0;
			break;
		case 'i':
			fflag = 0;
			iflag = 1;
			break;
		case 'P':
			Pflag = 1;
			break;
		case 'R':
		case 'r':			/* Compatibility. */
			rflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		case 'W':
			Wflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		if (fflag)
			return 0;
		usage();
	}

	checkdot(argv);

	if (*argv) {
		stdin_ok = isatty(STDIN_FILENO);

		if (rflag)
			rm_tree(argv);
		else
			rm_file(argv);
	}

	exit(eval);
	/* NOTREACHED */
}

void
rm_tree(char **argv)
{
	FTS *fts;
	FTSENT *p;
	int flags, needstat, rval;

	/*
	 * Remove a file hierarchy.  If forcing removal (-f), or interactive
	 * (-i) or can't ask anyway (stdin_ok), don't stat the file.
	 */
	needstat = !fflag && !iflag && stdin_ok;

	/*
	 * If the -i option is specified, the user can skip on the pre-order
	 * visit.  The fts_number field flags skipped directories.
	 */
#define	SKIPPED	1

	flags = FTS_PHYSICAL;
	if (!needstat)
		flags |= FTS_NOSTAT;
	if (Wflag)
		flags |= FTS_WHITEOUT;
	if ((fts = fts_open(argv, flags, NULL)) == NULL)
		err(1, "fts_open failed");
	while ((p = fts_read(fts)) != NULL) {

		switch (p->fts_info) {
		case FTS_DNR:
			if (!fflag || p->fts_errno != ENOENT) {
				warnx("%s: %s", p->fts_path,
						strerror(p->fts_errno));
				eval = 1;
			}
			continue;
		case FTS_ERR:
			errx(EXIT_FAILURE, "%s: %s", p->fts_path,
					strerror(p->fts_errno));
			/* NOTREACHED */
		case FTS_NS:
			/*
			 * FTS_NS: assume that if can't stat the file, it
			 * can't be unlinked.
			 */
			if (fflag && NONEXISTENT(p->fts_errno))
				continue;
			if (needstat) {
				warnx("%s: %s", p->fts_path,
						strerror(p->fts_errno));
				eval = 1;
				continue;
			}
			break;
		case FTS_D:
			/* Pre-order: give user chance to skip. */
			if (!fflag && !check(p->fts_path, p->fts_accpath,
			    p->fts_statp)) {
				(void)fts_set(fts, p, FTS_SKIP);
				p->fts_number = SKIPPED;
			}
			continue;
		case FTS_DP:
			/* Post-order: see if user skipped. */
			if (p->fts_number == SKIPPED)
				continue;
			break;
		default:
			if (!fflag &&
			    !check(p->fts_path, p->fts_accpath, p->fts_statp))
				continue;
		}

		/*
		 * If we can't read or search the directory, may still be
		 * able to remove it.  Don't print out the un{read,search}able
		 * message unless the remove fails.
		 */
		switch (p->fts_info) {
		case FTS_DP:
		case FTS_DNR:
			rval = rmdir(p->fts_accpath);
			if (rval != 0 && fflag && errno == ENOENT)
				continue;
			break;

		case FTS_W:
			rval = undelete(p->fts_accpath);
			if (rval != 0 && fflag && errno == ENOENT)
				continue;
			break;

		default:
			if (Pflag) {
				if (rm_overwrite(p->fts_accpath, NULL))
					continue;
			}
			rval = unlink(p->fts_accpath);
			if (rval != 0 && fflag && NONEXISTENT(errno))
				continue;
			break;
		}
		if (rval != 0) {
			warn("%s", p->fts_path);
			eval = 1;
		} else if (vflag)
			(void)printf("%s\n", p->fts_path);
	}
	if (errno)
		err(1, "fts_read");
	fts_close(fts);
}

void
rm_file(char **argv)
{
	struct stat sb;
	int rval;
	char *f;

	/*
	 * Remove a file.  POSIX 1003.2 states that, by default, attempting
	 * to remove a directory is an error, so must always stat the file.
	 */
	while ((f = *argv++) != NULL) {
		/* Assume if can't stat the file, can't unlink it. */
		if (lstat(f, &sb)) {
			if (Wflag) {
				sb.st_mode = S_IFWHT|S_IWUSR|S_IRUSR;
			} else {
				if (!fflag || !NONEXISTENT(errno)) {
					warn("%s", f);
					eval = 1;
				}
				continue;
			}
		} else if (Wflag) {
			warnx("%s: %s", f, strerror(EEXIST));
			eval = 1;
			continue;
		}

		if (S_ISDIR(sb.st_mode) && !dflag) {
			warnx("%s: is a directory", f);
			eval = 1;
			continue;
		}
		if (!fflag && !S_ISWHT(sb.st_mode) && !check(f, f, &sb))
			continue;
		if (S_ISWHT(sb.st_mode))
			rval = undelete(f);
		else if (S_ISDIR(sb.st_mode))
			rval = rmdir(f);
		else {
			if (Pflag) {
				if (rm_overwrite(f, &sb))
					continue;
			}
			rval = unlink(f);
		}
		if (rval && (!fflag || !NONEXISTENT(errno))) {
			warn("%s", f);
			eval = 1;
		}
		if (vflag && rval == 0)
			(void)printf("%s\n", f);
	}
}

/*
 * rm_overwrite --
 *	Overwrite the file 3 times with varying bit patterns.
 *
 * This is an expensive way to keep people from recovering files from your
 * non-snapshotted FFS filesystems using fsdb(8).  Really.  No more.  Only
 * regular files are deleted, directories (and therefore names) will remain.
 * Also, this assumes a fixed-block file system (like FFS, or a V7 or a
 * System V file system).  In a logging file system, you'll have to have
 * kernel support.
 *
 * A note on standards:  U.S. DoD 5220.22-M "National Industrial Security
 * Program Operating Manual" ("NISPOM") is often cited as a reference
 * for clearing and sanitizing magnetic media.  In fact, a matrix of
 * "clearing" and "sanitization" methods for various media was given in
 * Chapter 8 of the original 1995 version of NISPOM.  However, that
 * matrix was *removed from the document* when Chapter 8 was rewritten
 * in Change 2 to the document in 2001.  Recently, the Defense Security
 * Service has made a revised clearing and sanitization matrix available
 * in Microsoft Word format on the DSS web site.  The standardization
 * status of this matrix is unclear.  Furthermore, one must be very
 * careful when referring to this matrix: it is intended for the "clearing"
 * prior to reuse or "sanitization" prior to disposal of *entire media*,
 * not individual files and the only non-physically-destructive method of
 * "sanitization" that is permitted for magnetic disks of any kind is
 * specifically noted to be prohibited for media that have contained
 * Top Secret data.
 *
 * It is impossible to actually conform to the exact procedure given in
 * the matrix if one is overwriting a file, not an entire disk, because
 * the procedure requires examination and comparison of the disk's defect
 * lists.  Any program that claims to securely erase *files* while
 * conforming to the standard, then, is not correct.  We do as much of
 * what the standard requires as can actually be done when erasing a
 * file, rather than an entire disk; but that does not make us conformant.
 *
 * Furthermore, the presence of track caches, disk and controller write
 * caches, and so forth make it extremely difficult to ensure that data
 * have actually been written to the disk, particularly when one tries
 * to repeatedly overwrite the same sectors in quick succession.  We call
 * fsync(), but controllers with nonvolatile cache, as well as IDE disks
 * that just plain lie about the stable storage of data, will defeat this.
 *
 * Finally, widely respected research suggests that the given procedure
 * is nowhere near sufficient to prevent the recovery of data using special
 * forensic equipment and techniques that are well-known.  This is
 * presumably one reason that the matrix requires physical media destruction,
 * rather than any technique of the sort attempted here, for secret data.
 *
 * Caveat Emptor.
 *
 * rm_overwrite will return 0 on success.
 */

int
rm_overwrite(char *file, struct stat *sbp)
{
	return 0;
}

int
check(char *path, char *name, struct stat *sp)
{
	int ch, first;
	char modep[15];

	/* Check -i first. */
	if (iflag)
		(void)fprintf(stderr, "remove '%s'? ", path);
	else {
		/*
		 * If it's not a symbolic link and it's unwritable and we're
		 * talking to a terminal, ask.  Symbolic links are excluded
		 * because their permissions are meaningless.  Check stdin_ok
		 * first because we may not have stat'ed the file.
		 */
		if (!stdin_ok || S_ISLNK(sp->st_mode) ||
		    !(access(name, W_OK) && (errno != ETXTBSY)))
			return (1);
		strmode(sp->st_mode, modep);
		if (Pflag) {
			warnx(
			    "%s: -P was specified but file could not"
			    " be overwritten", path);
			return 0;
		}
#ifndef __linux__
		(void)fprintf(stderr, "override %s%s%s:%s for '%s'? ",
		    modep + 1, modep[9] == ' ' ? "" : " ",
		    user_from_uid(sp->st_uid, 0),
		    group_from_gid(sp->st_gid, 0), path);
#endif
	}
	(void)fflush(stderr);

	first = ch = getchar();
	while (ch != '\n' && ch != EOF)
		ch = getchar();
	return (first == 'y' || first == 'Y');
}

/*
 * POSIX.2 requires that if "." or ".." are specified as the basename
 * portion of an operand, a diagnostic message be written to standard
 * error and nothing more be done with such operands.
 *
 * Since POSIX.2 defines basename as the final portion of a path after
 * trailing slashes have been removed, we'll remove them here.
 */
#define ISDOT(a) ((a)[0] == '.' && (!(a)[1] || ((a)[1] == '.' && !(a)[2])))
void
checkdot(char **argv)
{
	char *p, **save, **t;
	int complained;

	complained = 0;
	for (t = argv; *t;) {
		/* strip trailing slashes */
		p = strrchr(*t, '\0');
		while (--p > *t && *p == '/')
			*p = '\0';

		/* extract basename */
		if ((p = strrchr(*t, '/')) != NULL)
			++p;
		else
			p = *t;

		if (ISDOT(p)) {
			if (!complained++)
				warnx("\".\" and \"..\" may not be removed");
			eval = 1;
			for (save = t; (t[0] = t[1]) != NULL; ++t)
				continue;
			t = save;
		} else
			++t;
	}
}

void
usage(void)
{


	(void)fprintf(stderr, "usage: %s %s [-f|-i] [-dPRrvW] file ...\n",
	    getprogname(), fsu_mount_usage());

	exit(1);
	/* NOTREACHED */
}
