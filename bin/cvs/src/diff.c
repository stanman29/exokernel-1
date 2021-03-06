/*
 * Copyright (c) 1992, Brian Berliner and Jeff Polk
 * Copyright (c) 1989-1992, Brian Berliner
 * 
 * You may distribute under the terms of the GNU General Public License as
 * specified in the README file that comes with the CVS 1.4 kit.
 * 
 * Difference
 * 
 * Run diff against versions in the repository.  Options that are specified are
 * passed on directly to "rcsdiff".
 * 
 * Without any file arguments, runs diff against all the currently modified
 * files.
 */

#include "cvs.h"

enum diff_file
{
    DIFF_ERROR,
    DIFF_ADDED,
    DIFF_REMOVED,
    DIFF_DIFFERENT,
    DIFF_SAME
};

static Dtype diff_dirproc PROTO ((void *callerdat, char *dir,
				  char *pos_repos, char *update_dir,
				  List *entries));
static int diff_filesdoneproc PROTO ((void *callerdat, int err,
				      char *repos, char *update_dir,
				      List *entries));
static int diff_dirleaveproc PROTO ((void *callerdat, char *dir,
				     int err, char *update_dir,
				     List *entries));
static enum diff_file diff_file_nodiff PROTO ((struct file_info *finfo,
					       Vers_TS *vers,
					       enum diff_file));
static int diff_fileproc PROTO ((void *callerdat, struct file_info *finfo));
static void diff_mark_errors PROTO((int err));

static char *diff_rev1, *diff_rev2;
static char *diff_date1, *diff_date2;
static char *use_rev1, *use_rev2;

#ifdef SERVER_SUPPORT
/* Revision of the user file, if it is unchanged from something in the
   repository and we want to use that fact.  */
static char *user_file_rev;
#endif

static char *options;
/* FIXME: arbitrary limit (security hole, if the client passes us
   data which overflows it).  */
static char opts[PATH_MAX];
static int diff_errors;
static int empty_files = 0;

/* FIXME: should be documenting all the options here.  They don't
   perfectly match rcsdiff options (for example, we always support
   --ifdef and --context, but rcsdiff only does if diff does).  */
static const char *const diff_usage[] =
{
    "Usage: %s %s [-lN] [rcsdiff-options]\n",
    "    [[-r rev1 | -D date1] [-r rev2 | -D date2]] [files...] \n",
    "\t-l\tLocal directory only, not recursive\n",
    "\t-D d1\tDiff revision for date against working file.\n",
    "\t-D d2\tDiff rev1/date1 against date2.\n",
    "\t-N\tinclude diffs for added and removed files.\n",
    "\t-r rev1\tDiff revision for rev1 against working file.\n",
    "\t-r rev2\tDiff rev1/date1 against rev2.\n",
    NULL
};

/* I copied this array directly out of diff.c in diffutils 2.7, after
   removing the following entries, none of which seem relevant to use
   with CVS:
     --help
     --version
     --recursive
     --unidirectional-new-file
     --starting-file
     --exclude
     --exclude-from
     --sdiff-merge-assist

   I changed the options which take optional arguments (--context and
   --unified) to return a number rather than a letter, so that the
   optional argument could be handled more easily.  I changed the
   --paginate and --brief options to return a number, since -l and -q
   mean something else to cvs diff.

   The numbers 129- that appear in the fourth element of some entries
   tell the big switch in `diff' how to process those options. -- Ian

   The following options, which diff lists as "An alias, no longer
   recommended" have been removed: --file-label --entire-new-file
   --ascii --print.  */

static struct option const longopts[] =
{
    {"ignore-blank-lines", 0, 0, 'B'},
    {"context", 2, 0, 143},
    {"ifdef", 1, 0, 147},
    {"show-function-line", 1, 0, 'F'},
    {"speed-large-files", 0, 0, 'H'},
    {"ignore-matching-lines", 1, 0, 'I'},
    {"label", 1, 0, 'L'},
    {"new-file", 0, 0, 'N'},
    {"initial-tab", 0, 0, 'T'},
    {"width", 1, 0, 'W'},
    {"text", 0, 0, 'a'},
    {"ignore-space-change", 0, 0, 'b'},
    {"minimal", 0, 0, 'd'},
    {"ed", 0, 0, 'e'},
    {"forward-ed", 0, 0, 'f'},
    {"ignore-case", 0, 0, 'i'},
    {"paginate", 0, 0, 144},
    {"rcs", 0, 0, 'n'},
    {"show-c-function", 0, 0, 'p'},

    /* This is a potentially very useful option, except the output is so
       silly.  It would be much better for it to look like "cvs rdiff -s"
       which displays all the same info, minus quite a few lines of
       extraneous garbage.  */
    {"brief", 0, 0, 145},

    {"report-identical-files", 0, 0, 's'},
    {"expand-tabs", 0, 0, 't'},
    {"ignore-all-space", 0, 0, 'w'},
    {"side-by-side", 0, 0, 'y'},
    {"unified", 2, 0, 146},
    {"left-column", 0, 0, 129},
    {"suppress-common-lines", 0, 0, 130},
    {"old-line-format", 1, 0, 132},
    {"new-line-format", 1, 0, 133},
    {"unchanged-line-format", 1, 0, 134},
    {"line-format", 1, 0, 135},
    {"old-group-format", 1, 0, 136},
    {"new-group-format", 1, 0, 137},
    {"unchanged-group-format", 1, 0, 138},
    {"changed-group-format", 1, 0, 139},
    {"horizon-lines", 1, 0, 140},
    {"binary", 0, 0, 142},
    {0, 0, 0, 0}
};

int
diff (argc, argv)
    int argc;
    char **argv;
{
    char tmp[50];
    int c, err = 0;
    int local = 0;
    int which;
    int option_index;

    if (argc == -1)
	usage (diff_usage);

    /*
     * Note that we catch all the valid arguments here, so that we can
     * intercept the -r arguments for doing revision diffs; and -l/-R for a
     * non-recursive/recursive diff.
     */
#ifdef SERVER_SUPPORT
    /* Need to be able to do this command more than once (according to
       the protocol spec, even if the current client doesn't use it).  */
    opts[0] = '\0';
#endif
    optind = 1;
    while ((c = getopt_long (argc, argv,
	       "abcdefhilnpstuwy0123456789BHNRTC:D:F:I:L:U:V:W:k:r:",
			     longopts, &option_index)) != -1)
    {
	switch (c)
	{
	    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
	    case 'h': case 'i': case 'n': case 'p': case 's': case 't':
	    case 'u': case 'w': case 'y': case '0': case '1': case '2':
	    case '3': case '4': case '5': case '6': case '7': case '8':
	    case '9': case 'B': case 'H': case 'T':
		(void) sprintf (tmp, " -%c", (char) c);
		(void) strcat (opts, tmp);
		break;
	    case 'C': case 'F': case 'I': case 'L': case 'U': case 'V':
	    case 'W':
		(void) sprintf (tmp, " -%c", (char) c);
		strcat (opts, tmp);
		strcat (opts, optarg);
		break;
	    case 147:
		/* --ifdef.  */
		strcat (opts, " -D");
		strcat (opts, optarg);
		break;
	    case 129: case 130: case 131: case 132: case 133: case 134:
	    case 135: case 136: case 137: case 138: case 139: case 140:
	    case 141: case 142: case 143: case 144: case 145: case 146:
		strcat (opts, " --");
		strcat (opts, longopts[option_index].name);
		if (longopts[option_index].has_arg == 1
		    || (longopts[option_index].has_arg == 2
			&& optarg != NULL))
		{
		    strcat (opts, "=");
		    strcat (opts, optarg);
		}
		break;
	    case 'R':
		local = 0;
		break;
	    case 'l':
		local = 1;
		break;
	    case 'k':
		if (options)
		    free (options);
		options = RCS_check_kflag (optarg);
		break;
	    case 'r':
		if (diff_rev2 != NULL || diff_date2 != NULL)
		    error (1, 0,
		       "no more than two revisions/dates can be specified");
		if (diff_rev1 != NULL || diff_date1 != NULL)
		    diff_rev2 = optarg;
		else
		    diff_rev1 = optarg;
		break;
	    case 'D':
		if (diff_rev2 != NULL || diff_date2 != NULL)
		    error (1, 0,
		       "no more than two revisions/dates can be specified");
		if (diff_rev1 != NULL || diff_date1 != NULL)
		    diff_date2 = Make_Date (optarg);
		else
		    diff_date1 = Make_Date (optarg);
		break;
	    case 'N':
		empty_files = 1;
		break;
	    case '?':
	    default:
		usage (diff_usage);
		break;
	}
    }
    argc -= optind;
    argv += optind;

    /* make sure options is non-null */
    if (!options)
	options = xstrdup ("");

#ifdef CLIENT_SUPPORT
    if (client_active) {
	/* We're the client side.  Fire up the remote server.  */
	start_server ();
	
	ign_setup ();

	if (local)
	    send_arg("-l");
	if (empty_files)
	    send_arg("-N");
	send_option_string (opts);
	if (diff_rev1)
	    option_with_arg ("-r", diff_rev1);
	if (diff_date1)
	    client_senddate (diff_date1);
	if (diff_rev2)
	    option_with_arg ("-r", diff_rev2);
	if (diff_date2)
	    client_senddate (diff_date2);

	send_file_names (argc, argv, SEND_EXPAND_WILD);
#if 0
	/* FIXME: We shouldn't have to send current files to diff two
	   revs, but it doesn't work yet and I haven't debugged it.
	   So send the files -- it's slower but it works.
	   gnu@cygnus.com Apr94 */
	/* Send the current files unless diffing two revs from the archive */
	if (diff_rev2 == NULL && diff_date2 == NULL)
#endif
	send_files (argc, argv, local, 0);

	send_to_server ("diff\012", 0);
        err = get_responses_and_close ();
	free (options);
	return (err);
    }
#endif

    if (diff_rev1 != NULL)
	tag_check_valid (diff_rev1, argc, argv, local, 0, "");
    if (diff_rev2 != NULL)
	tag_check_valid (diff_rev2, argc, argv, local, 0, "");

    which = W_LOCAL;
    if (diff_rev1 != NULL || diff_date1 != NULL)
	which |= W_REPOS | W_ATTIC;

    wrap_setup ();

    /* start the recursion processor */
    err = start_recursion (diff_fileproc, diff_filesdoneproc, diff_dirproc,
			   diff_dirleaveproc, NULL, argc, argv, local,
			   which, 0, 1, (char *) NULL, 1);

    /* clean up */
    free (options);
    return (err);
}

/*
 * Do a file diff
 */
/* ARGSUSED */
static int
diff_fileproc (callerdat, finfo)
    void *callerdat;
    struct file_info *finfo;
{
    int status, err = 2;		/* 2 == trouble, like rcsdiff */
    Vers_TS *vers;
    enum diff_file empty_file = DIFF_DIFFERENT;
    char *tmp;
    char *tocvsPath;
    char fname[PATH_MAX];

#ifdef SERVER_SUPPORT
    user_file_rev = 0;
#endif
    vers = Version_TS (finfo, NULL, NULL, NULL, 1, 0);

    if (diff_rev2 != NULL || diff_date2 != NULL)
    {
	/* Skip all the following checks regarding the user file; we're
	   not using it.  */
    }
    else if (vers->vn_user == NULL)
    {
	/* The file does not exist in the working directory.  */
	if ((diff_rev1 != NULL || diff_date1 != NULL)
	    && vers->srcfile != NULL)
	{
	    /* The file does exist in the repository.  */
	    if (empty_files)
		empty_file = DIFF_REMOVED;
	    else
	    {
		int exists;

		exists = 0;
		/* special handling for TAG_HEAD */
		if (diff_rev1 && strcmp (diff_rev1, TAG_HEAD) == 0)
		    exists = vers->vn_rcs != NULL;
		else
		{
		    Vers_TS *xvers;

		    xvers = Version_TS (finfo, NULL, diff_rev1, diff_date1,
					1, 0);
		    exists = xvers->vn_rcs != NULL;
		    freevers_ts (&xvers);
		}
		if (exists)
		    error (0, 0,
			   "%s no longer exists, no comparison available",
			   finfo->fullname);
		freevers_ts (&vers);
		diff_mark_errors (err);
		return (err);
	    }
	}
	else
	{
	    error (0, 0, "I know nothing about %s", finfo->fullname);
	    freevers_ts (&vers);
	    diff_mark_errors (err);
	    return (err);
	}
    }
    else if (vers->vn_user[0] == '0' && vers->vn_user[1] == '\0')
    {
	if (empty_files)
	    empty_file = DIFF_ADDED;
	else
	{
	    error (0, 0, "%s is a new entry, no comparison available",
		   finfo->fullname);
	    freevers_ts (&vers);
	    diff_mark_errors (err);
	    return (err);
	}
    }
    else if (vers->vn_user[0] == '-')
    {
	if (empty_files)
	    empty_file = DIFF_REMOVED;
	else
	{
	    error (0, 0, "%s was removed, no comparison available",
		   finfo->fullname);
	    freevers_ts (&vers);
	    diff_mark_errors (err);
	    return (err);
	}
    }
    else
    {
	if (vers->vn_rcs == NULL && vers->srcfile == NULL)
	{
	    error (0, 0, "cannot find revision control file for %s",
		   finfo->fullname);
	    freevers_ts (&vers);
	    diff_mark_errors (err);
	    return (err);
	}
	else
	{
	    if (vers->ts_user == NULL)
	    {
		error (0, 0, "cannot find %s", finfo->fullname);
		freevers_ts (&vers);
		diff_mark_errors (err);
		return (err);
	    }
#ifdef SERVER_SUPPORT
	    else if (!strcmp (vers->ts_user, vers->ts_rcs)) 
	    {
		/* The user file matches some revision in the repository
		   Diff against the repository (for remote CVS, we might not
		   have a copy of the user file around).  */
		user_file_rev = vers->vn_user;
	    }
#endif
	}
    }

    empty_file = diff_file_nodiff (finfo, vers, empty_file);
    if (empty_file == DIFF_SAME || empty_file == DIFF_ERROR)
    {
	freevers_ts (&vers);
	if (empty_file == DIFF_SAME)
	    return (0);
	else
	{
	    diff_mark_errors (err);
	    return (err);
	}
    }

    if (empty_file == DIFF_DIFFERENT)
    {
	int dead1, dead2;

	if (use_rev1 == NULL)
	    dead1 = 0;
	else
	    dead1 = RCS_isdead (vers->srcfile, use_rev1);
	if (use_rev2 == NULL)
	    dead2 = 0;
	else
	    dead2 = RCS_isdead (vers->srcfile, use_rev2);

	if (dead1 && dead2)
	{
	    freevers_ts (&vers);
	    return (0);
	}
	else if (dead1)
	{
	    if (empty_files)
	        empty_file = DIFF_ADDED;
	    else
	    {
		error (0, 0, "%s is a new entry, no comparison available",
		       finfo->fullname);
		freevers_ts (&vers);
		diff_mark_errors (err);
		return (err);
	    }
	}
	else if (dead2)
	{
	    if (empty_files)
		empty_file = DIFF_REMOVED;
	    else
	    {
		error (0, 0, "%s was removed, no comparison available",
		       finfo->fullname);
		freevers_ts (&vers);
		diff_mark_errors (err);
		return (err);
	    }
	}
    }

    /* Output an "Index:" line for patch to use */
    (void) fflush (stdout);
    (void) printf ("Index: %s\n", finfo->fullname);
    (void) fflush (stdout);

    tocvsPath = wrap_tocvs_process_file(finfo->file);
    if (tocvsPath)
    {
	/* Backup the current version of the file to CVS/,,filename */
	sprintf(fname,"%s/%s%s",CVSADM, CVSPREFIX, finfo->file);
	if (unlink_file_dir (fname) < 0)
	    if (! existence_error (errno))
		error (1, errno, "cannot remove %s", finfo->file);
	rename_file (finfo->file, fname);
	/* Copy the wrapped file to the current directory then go to work */
	copy_file (tocvsPath, finfo->file);
    }

    if (empty_file == DIFF_ADDED || empty_file == DIFF_REMOVED)
    {
	/* This is file, not fullname, because it is the "Index:" line which
	   is supposed to contain the directory.  */
	(void) printf ("===================================================================\nRCS file: %s\n",
		       finfo->file);
	(void) printf ("diff -N %s\n", finfo->file);

	if (empty_file == DIFF_ADDED)
	{
	    if (use_rev2 == NULL)
		run_setup ("%s %s %s %s", DIFF, opts, DEVNULL, finfo->file);
	    else
	    {
		int retcode;

		tmp = cvs_temp_name ();
		retcode = RCS_checkout (vers->srcfile, (char *) NULL,
					use_rev2, (char *) NULL,
					(*options
					 ? options
					 : vers->options),
					tmp);
		if (retcode == -1)
		{
		    (void) CVS_UNLINK (tmp);
		    error (1, errno, "fork failed during checkout of %s",
			   vers->srcfile->path);
		}
		/* FIXME: what if retcode > 0?  */

		run_setup ("%s %s %s %s", DIFF, opts, DEVNULL, tmp);
	    }
	}
	else
	{
	    int retcode;

	    tmp = cvs_temp_name ();
	    retcode = RCS_checkout (vers->srcfile, (char *) NULL,
				    use_rev1, (char *) NULL,
				    *options ? options : vers->options,
				    tmp);
	    if (retcode == -1)
	    {
		(void) CVS_UNLINK (tmp);
		error (1, errno, "fork failed during checkout of %s",
		       vers->srcfile->path);
	    }
	    /* FIXME: what if retcode > 0?  */

	    run_setup ("%s %s %s %s", DIFF, opts, tmp, DEVNULL);
	}
    }
    else
    {
	if (use_rev2)
	{
	    run_setup ("%s%s -x,v/ %s %s -r%s -r%s", Rcsbin, RCS_DIFF,
		       opts, *options ? options : vers->options,
		       use_rev1, use_rev2);
	}
	else
	{
	    run_setup ("%s%s -x,v/ %s %s -r%s", Rcsbin, RCS_DIFF, opts,
		       *options ? options : vers->options, use_rev1);
	}
	run_arg (vers->srcfile->path);
    }

    switch ((status = run_exec (RUN_TTY, RUN_TTY, RUN_TTY,
	RUN_REALLY|RUN_COMBINED)))
    {
	case -1:			/* fork failed */
	    error (1, errno, "fork failed during rcsdiff of %s",
		   vers->srcfile->path);
	case 0:				/* everything ok */
	    err = 0;
	    break;
	default:			/* other error */
	    err = status;
	    break;
    }

    if (tocvsPath)
    {
	if (unlink_file_dir (finfo->file) < 0)
	    if (! existence_error (errno))
		error (1, errno, "cannot remove %s", finfo->file);

	rename_file (fname,finfo->file);
	if (unlink_file (tocvsPath) < 0)
	    error (1, errno, "cannot remove %s", finfo->file);
    }

    if (empty_file == DIFF_REMOVED
	|| (empty_file == DIFF_ADDED && use_rev2 != NULL))
    {
	(void) CVS_UNLINK (tmp);
	free (tmp);
    }

    (void) fflush (stdout);
    freevers_ts (&vers);
    diff_mark_errors (err);
    return (err);
}

/*
 * Remember the exit status for each file.
 */
static void
diff_mark_errors (err)
    int err;
{
    if (err > diff_errors)
	diff_errors = err;
}

/*
 * Print a warm fuzzy message when we enter a dir
 *
 * Don't try to diff directories that don't exist! -- DW
 */
/* ARGSUSED */
static Dtype
diff_dirproc (callerdat, dir, pos_repos, update_dir, entries)
    void *callerdat;
    char *dir;
    char *pos_repos;
    char *update_dir;
    List *entries;
{
    /* XXX - check for dirs we don't want to process??? */

    /* YES ... for instance dirs that don't exist!!! -- DW */
    if (!isdir (dir) )
      return (R_SKIP_ALL);
  
    if (!quiet)
	error (0, 0, "Diffing %s", update_dir);
    return (R_PROCESS);
}

/*
 * Concoct the proper exit status - done with files
 */
/* ARGSUSED */
static int
diff_filesdoneproc (callerdat, err, repos, update_dir, entries)
    void *callerdat;
    int err;
    char *repos;
    char *update_dir;
    List *entries;
{
    return (diff_errors);
}

/*
 * Concoct the proper exit status - leaving directories
 */
/* ARGSUSED */
static int
diff_dirleaveproc (callerdat, dir, err, update_dir, entries)
    void *callerdat;
    char *dir;
    int err;
    char *update_dir;
    List *entries;
{
    return (diff_errors);
}

/*
 * verify that a file is different
 */
static enum diff_file
diff_file_nodiff (finfo, vers, empty_file)
    struct file_info *finfo;
    Vers_TS *vers;
    enum diff_file empty_file;
{
    Vers_TS *xvers;
    char *tmp;
    int retcode;

    /* free up any old use_rev* variables and reset 'em */
    if (use_rev1)
	free (use_rev1);
    if (use_rev2)
	free (use_rev2);
    use_rev1 = use_rev2 = (char *) NULL;

    if (diff_rev1 || diff_date1)
    {
	/* special handling for TAG_HEAD */
	if (diff_rev1 && strcmp (diff_rev1, TAG_HEAD) == 0)
	    use_rev1 = xstrdup (vers->vn_rcs);
	else
	{
	    xvers = Version_TS (finfo, NULL, diff_rev1, diff_date1, 1, 0);
	    if (xvers->vn_rcs != NULL)
		use_rev1 = xstrdup (xvers->vn_rcs);
	    freevers_ts (&xvers);
	}
    }
    if (diff_rev2 || diff_date2)
    {
	/* special handling for TAG_HEAD */
	if (diff_rev2 && strcmp (diff_rev2, TAG_HEAD) == 0)
	    use_rev2 = xstrdup (vers->vn_rcs);
	else
	{
	    xvers = Version_TS (finfo, NULL, diff_rev2, diff_date2, 1, 0);
	    if (xvers->vn_rcs != NULL)
		use_rev2 = xstrdup (xvers->vn_rcs);
	    freevers_ts (&xvers);
	}

	if (use_rev1 == NULL)
	{
	    /* The first revision does not exist.  If EMPTY_FILES is
               true, treat this as an added file.  Otherwise, warn
               about the missing tag.  */
	    if (use_rev2 == NULL)
		return DIFF_SAME;
	    else if (empty_files)
		return DIFF_ADDED;
	    else if (diff_rev1)
		error (0, 0, "tag %s is not in file %s", diff_rev1,
		       finfo->fullname);
	    else
		error (0, 0, "no revision for date %s in file %s",
		       diff_date1, finfo->fullname);
	    return DIFF_ERROR;
	}

	if (use_rev2 == NULL)
	{
	    /* The second revision does not exist.  If EMPTY_FILES is
               true, treat this as a removed file.  Otherwise warn
               about the missing tag.  */
	    if (empty_files)
		return DIFF_REMOVED;
	    else if (diff_rev2)
		error (0, 0, "tag %s is not in file %s", diff_rev2,
		       finfo->fullname);
	    else
		error (0, 0, "no revision for date %s in file %s",
		       diff_date2, finfo->fullname);
	    return DIFF_ERROR;
	}

	/* now, see if we really need to do the diff */
	if (strcmp (use_rev1, use_rev2) == 0)
	    return DIFF_SAME;
	else
	    return DIFF_DIFFERENT;
    }

    if ((diff_rev1 || diff_date1) && use_rev1 == NULL)
    {
	/* The first revision does not exist, and no second revision
           was given.  */
	if (empty_files)
	{
	    if (empty_file == DIFF_REMOVED)
		return DIFF_SAME;
	    else
	    {
#ifdef SERVER_SUPPORT
		if (user_file_rev && use_rev2 == NULL)
		    use_rev2 = xstrdup (user_file_rev);
#endif
		return DIFF_ADDED;
	    }
	}
	else
	{
	    if (diff_rev1)
		error (0, 0, "tag %s is not in file %s", diff_rev1,
		       finfo->fullname);
	    else
		error (0, 0, "no revision for date %s in file %s",
		       diff_date1, finfo->fullname);
	    return DIFF_ERROR;
	}
    }

#ifdef SERVER_SUPPORT
    if (user_file_rev)
    {
        /* drop user_file_rev into first unused use_rev */
        if (!use_rev1) 
	  use_rev1 = xstrdup (user_file_rev);
	else if (!use_rev2)
	  use_rev2 = xstrdup (user_file_rev);
	/* and if not, it wasn't needed anyhow */
	user_file_rev = 0;
    }

    /* now, see if we really need to do the diff */
    if (use_rev1 && use_rev2) 
    {
	if (strcmp (use_rev1, use_rev2) == 0)
	    return DIFF_SAME;
	else
	    return DIFF_DIFFERENT;
    }
#endif /* SERVER_SUPPORT */
    if (use_rev1 == NULL
	|| (vers->vn_user != NULL && strcmp (use_rev1, vers->vn_user) == 0))
    {
	if (strcmp (vers->ts_rcs, vers->ts_user) == 0 &&
	    (!(*options) || strcmp (options, vers->options) == 0))
	{
	    return DIFF_SAME;
	}
	if (use_rev1 == NULL)
	    use_rev1 = xstrdup (vers->vn_user);
    }

    /* If we already know that the file is being added or removed,
       then we don't want to do an actual file comparison here.  */
    if (empty_file != DIFF_DIFFERENT)
	return empty_file;

    /*
     * with 0 or 1 -r option specified, run a quick diff to see if we
     * should bother with it at all.
     */
    tmp = cvs_temp_name ();
    retcode = RCS_checkout (vers->srcfile, (char *) NULL, use_rev1,
			    (char *) NULL,
			    *options ? options : vers->options,
			    tmp);
    switch (retcode)
    {
	case 0:				/* everything ok */
	    if (xcmp (finfo->file, tmp) == 0)
	    {
		(void) CVS_UNLINK (tmp);
		free (tmp);
		return DIFF_SAME;
	    }
	    break;
	case -1:			/* fork failed */
	    (void) CVS_UNLINK (tmp);
	    error (1, errno, "fork failed during checkout of %s",
		   vers->srcfile->path);
	default:
	    break;
    }
    (void) CVS_UNLINK (tmp);
    free (tmp);
    return DIFF_DIFFERENT;
}
