/*
 * This file was generated by the mkinit program.
 */

#include "shell.h"
#include "mystring.h"
#include "eval.h"
#include "input.h"
#include "error.h"
#include <stdlib.h>
#include "options.h"
#include "redir.h"
#include <signal.h>
#include "trap.h"
#include "output.h"
#include "memalloc.h"
#include "var.h"



#define ATABSIZE 39
#define MAXPWD 256
#define main echocmd
#define ALL (E_OPEN|E_CREAT|E_EXEC)
#define EV_EXIT 01		/* exit after evaluating tree */
#define EV_TESTED 02		/* exit status is checked; ignore -e flag */
#define EV_BACKCMD 04		/* command executing within back quotes */
#define CMDTABLESIZE 31		/* should be prime */
#define ARB 1			/* actual size determined at run time */
#define NEWARGS 5
#define MAXHISTLOOPS	4	/* max recursions through fc */
#define DEFEDITOR	"ed"	/* default editor *should* be $EDITOR */
#define editing (Eflag || Vflag)
#define EOF_NLEFT -99		/* value of parsenleft when EOF pushed back */
#define MAXCMDTEXT	200
#define MAXMBOXES 10
#define PROFILE 0
#define MINSIZE 504		/* minimum size of a block */
#define DEFINE_OPTIONS
#define EOFMARKLEN 79
#define GDB_HACK 1 /* avoid local declarations which gdb can't handle */
#define EMPTY -2		/* marks an unused slot in redirtab */
#define PIPESIZE 4096		/* amount of buffering in a pipe */
#define S_DFL 1			/* default signal handling (SIG_DFL) */
#define S_CATCH 2		/* signal is caught */
#define S_IGN 3			/* signal is ignored (SIG_IGN) */
#define S_HARD_IGN 4		/* signal is ignored permenantly */
#define S_RESET 5		/* temporary - to reset a hard ignored sig */
#define OUTBUFSIZ BUFSIZ
#define BLOCK_OUT -2		/* output to a fixed block of memory */
#define MEM_OUT -3		/* output to dynamically allocated memory */
#define OUTPUT_ERR 01		/* error occurred on output */
#define TEMPSIZE 24
#define VTABSIZE 39



extern void rmaliases();

extern int evalskip;		/* set if we are skipping commands */
extern int loopnest;		/* current loop nesting level */

extern void deletefuncs();

struct strpush {
	struct strpush *prev;	/* preceding string on stack */
	char *prevstring;
	int prevnleft;
	int prevlleft;
	struct alias *ap;	/* if push was associated with an alias */
};

struct parsefile {
	struct parsefile *prev;	/* preceding file on stack */
	int linno;		/* current line */
	int fd;			/* file descriptor (or -1 if string) */
	int nleft;		/* number of chars left in this line */
	int lleft;		/* number of chars left in this buffer */
	char *nextc;		/* next char in buffer */
	char *buf;		/* input buffer */
	struct strpush *strpush; /* for pushing strings at this level */
	struct strpush basestrpush; /* so pushing one is fast */
};

extern int parsenleft;		/* copy of parsefile->nleft */
extern int parselleft;		/* copy of parsefile->lleft */
extern struct parsefile basepf;	/* top level input file */

extern short backgndpid;	/* pid of last background process */
extern int jobctl;

extern int tokpushback;		/* last token pushed back */
extern int checkkwd;            /* 1 == check for kwds, 2 == also eat newlines */

struct redirtab {
	struct redirtab *next;
	short renamed[10];
};

extern struct redirtab *redirlist;

extern char sigmode[NSIG];	/* current value of signal */

extern void shprocvar();



/*
 * Initialization code.
 */

void
init() {

      /* from input.c: */
      {
	      extern char basebuf[];

	      basepf.nextc = basepf.buf = basebuf;
      }

      /* from var.c: */
      {
	      char **envp;
	      extern char **environ;

	      initvar();
	      for (envp = environ ; *envp ; envp++) {
		      if (strchr(*envp, '=')) {
			      setvareq(*envp, VEXPORT|VTEXTFIXED);
		      }
	      }
      }
}



/*
 * This routine is called when an error or an interrupt occurs in an
 * interactive shell and control is returned to the main command loop.
 */

void
reset() {

      /* from eval.c: */
      {
	      evalskip = 0;
	      loopnest = 0;
	      funcnest = 0;
      }

      /* from input.c: */
      {
	      if (exception != EXSHELLPROC)
		      parselleft = parsenleft = 0;	/* clear input buffer */
	      popallfiles();
      }

      /* from parser.c: */
      {
	      tokpushback = 0;
	      checkkwd = 0;
      }

      /* from redir.c: */
      {
	      while (redirlist)
		      popredir();
      }

      /* from output.c: */
      {
	      out1 = &output;
	      out2 = &errout;
	      if (memout.buf != NULL) {
		      ckfree(memout.buf);
		      memout.buf = NULL;
	      }
      }
}



/*
 * This routine is called to initialize the shell to run a shell procedure.
 */

void
initshellproc() {

      /* from alias.c: */
      {
	      rmaliases();
      }

      /* from eval.c: */
      {
	      exitstatus = 0;
      }

      /* from exec.c: */
      {
	      deletefuncs();
      }

      /* from input.c: */
      {
	      popallfiles();
      }

      /* from jobs.c: */
      {
	      backgndpid = -1;
#if JOBS
	      jobctl = 0;
#endif
      }

      /* from options.c: */
      {
	      int i;

	      for (i = 0; i < NOPTS; i++)
		      optlist[i].val = 0;
	      optschanged();

      }

      /* from redir.c: */
      {
	      clearredir();
      }

      /* from trap.c: */
      {
	      char *sm;

	      clear_traps();
	      for (sm = sigmode ; sm < sigmode + NSIG ; sm++) {
		      if (*sm == S_IGN)
			      *sm = S_HARD_IGN;
	      }
      }

      /* from var.c: */
      {
	      shprocvar();
      }
}
