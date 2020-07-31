#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>

#include "libmilter/mfapi.h"

#ifndef bool
# define bool   int
# define TRUE   1
# define FALSE  0
#endif /* ! bool */

typedef enum {both, mo, mt} direction_t;

struct mlfiPriv
{
	char *eml2a_fname;
	FILE *eml2a_fp;
	bool is_mms;
	direction_t direction;
};

struct list
{
	char *key;
	char *value;
	struct list *next;
};

struct list *config = NULL;
char hostname[1024] = "";

#define CONF_FILE 				"/etc/archiving/eml2a-milter.conf"
#define ARCHIVING_EXECUTABLE 	"/usr/bin/eml2a"
#define TMPFILE_PATH 			"/var/lib/eml2a/messages/msg.XXXXXX"
#define PID_FILE 				"/var/run/eml2a/eml2a-milter.pid"

#define MLFIPRIV        ((struct mlfiPriv *) smfi_getpriv(ctx))

typedef enum {delete, nodelete} delete_action;

sfsistat eml2a_cleanup(SMFICTX *, sfsistat, delete_action);

sfsistat eml2a_connect(ctx, hostname, hostaddr)
	SMFICTX *ctx;char *hostname;_SOCK_ADDR *hostaddr;
{
	/* continue processing */
	return SMFIS_CONTINUE;
}

sfsistat eml2a_helo(ctx, helohost)
	SMFICTX *ctx;char *helohost;
{
	/* continue processing */
	return SMFIS_CONTINUE;
}

sfsistat eml2a_envfrom(ctx, argv)
	SMFICTX *ctx;char **argv;
{
	/* continue processing */
	return SMFIS_CONTINUE;
}

sfsistat eml2a_envrcpt(ctx, argv)
	SMFICTX *ctx;char **argv;
{
	/* this is where we decide if we are going to archive the message or
	 * simply allow it through. This is done based on the list of email addresses
	 * we're interested in, saved in the config file
	 */
	char *mailaddr = smfi_getsymval(ctx, "{rcpt_addr}");

	struct list *thisitem = config;

	enum { unknown, mms, sms } type = unknown;
	direction_t direction = both;

	while (thisitem)
	{
		if (!strcasecmp(thisitem->value, mailaddr))
		{
			if (!strcmp(thisitem->key, "sms"))
			{
				syslog(LOG_INFO, "%s matches sms config item", mailaddr);
				type = sms;
				direction = both;
				break;
			}
			else if (!strcmp(thisitem->key, "sms-mo"))
			{
				syslog(LOG_INFO, "%s matches sms-mo config item", mailaddr);
				type = sms;
				direction = mo;
				break;
			}
			else if (!strcmp(thisitem->key, "sms-mt"))
			{
				syslog(LOG_INFO, "%s matches sms-mt config item", mailaddr);
				type = sms;
				direction = mt;
				break;
			}
			else if (!strcmp(thisitem->key, "mms"))
			{
				syslog(LOG_INFO, "%s matches mms config item", mailaddr);
				type = mms;
				direction = both;
				break;
			}
			else if (!strcmp(thisitem->key, "mms-mo"))
			{
				syslog(LOG_INFO, "%s matches mms-mo config item", mailaddr);
				type = mms;
				direction = mo;
				break;
			}
			else if (!strcmp(thisitem->key, "mms-mt"))
			{
				syslog(LOG_INFO, "%s matches mms-mt config item", mailaddr);
				type = mms;
				direction = mt;
				break;
			}
		}
		thisitem = thisitem->next;
	}

	if (type == unknown)
	{
		syslog(LOG_INFO, "%s doesn't match any sms or mms addresses", mailaddr);
		// this tells postfix the email's ok and not to bother calling us again
		return SMFIS_ACCEPT;
	}
	else
	{
		struct mlfiPriv *priv;
		int fd = -1;

		/* allocate some private memory */
		priv = malloc(sizeof *priv);
		if (priv == NULL)
		{
			/* malloc failed */
			syslog (LOG_ERR, "Couldn't allocate context");
			/* can't accept this message right now */
			return SMFIS_TEMPFAIL;
		}
		memset(priv, '\0', sizeof *priv);

		/* save the private data */
		smfi_setpriv(ctx, priv);

		/* open a file to store this message */
		if ((priv->eml2a_fname = strdup(TMPFILE_PATH)) == NULL)
		{
			syslog (LOG_ERR, "Error copying temp filename %s: %s", TMPFILE_PATH, strerror(errno));
			return eml2a_cleanup(ctx, SMFIS_TEMPFAIL, delete);
		}
		if ((fd = mkstemp(priv->eml2a_fname)) == -1)
		{
			syslog (LOG_ERR, "mkstemp(%s) failed: %s", TMPFILE_PATH, strerror(errno));
			return eml2a_cleanup(ctx, SMFIS_TEMPFAIL, delete);
		}
		if ((priv->eml2a_fp = fdopen(fd, "w+")) == NULL)
		{
			syslog (LOG_ERR, "fdopen(%s) failed: %s", priv->eml2a_fname, strerror(errno));
			close(fd);
			return eml2a_cleanup(ctx, SMFIS_TEMPFAIL, delete);
		}

		priv->is_mms = (type == mms);
		priv->direction = direction;
	}

	/* continue processing */
	return SMFIS_CONTINUE;
}

sfsistat eml2a_header(ctx, headerf, headerv)
	SMFICTX *ctx;char *headerf;unsigned char *headerv;
{
	/* write the header to the log file */
	if (fprintf(MLFIPRIV->eml2a_fp, "%s: %s\n", headerf, headerv) == EOF)
	{
		/* write failed */
		syslog (LOG_ERR, "Couldn't write file %s: %s", MLFIPRIV->eml2a_fname, strerror(errno));
		return eml2a_cleanup(ctx, SMFIS_TEMPFAIL, delete);
	}

	/* continue processing */
	return SMFIS_CONTINUE;
}

sfsistat eml2a_eoh(ctx)
	SMFICTX *ctx;
{
	/* output the blank line between the header and the body */
	if (fprintf(MLFIPRIV->eml2a_fp, "\n") == EOF)
	{
		/* write failed */
		syslog (LOG_ERR, "Couldn't write file %s: %s", MLFIPRIV->eml2a_fname, strerror(errno));
		return eml2a_cleanup(ctx, SMFIS_TEMPFAIL, delete);
	}

	/* continue processing */
	return SMFIS_CONTINUE;
}

sfsistat eml2a_body(ctx, bodyp, bodylen)
	SMFICTX *ctx;unsigned char *bodyp;size_t bodylen;
{
	struct mlfiPriv *priv = MLFIPRIV;

	/* output body block to log file */
	if (fwrite(bodyp, bodylen, 1, priv->eml2a_fp) != 1)
	{
		/* write failed */
		syslog (LOG_ERR, "Couldn't write file %s: %s", priv->eml2a_fname, strerror(errno));
		return eml2a_cleanup(ctx, SMFIS_TEMPFAIL, delete);
	}

	/* continue processing */
	return SMFIS_CONTINUE;
}

sfsistat eml2a_eom(ctx)
	SMFICTX *ctx;
{
	struct mlfiPriv *priv = MLFIPRIV;
	bool ok = FALSE;

	/* close the archive file */
	if (priv->eml2a_fp != NULL && fclose(priv->eml2a_fp) == EOF)
	{
		syslog (LOG_ERR, "Couldn't close archive file %s: %s", priv->eml2a_fname, strerror(errno));
		return eml2a_cleanup(ctx, SMFIS_TEMPFAIL, delete);
	}
	else
		priv->eml2a_fp = NULL;

	char command[2048];
	int result;
	sprintf(command, "%s --%s --hostname %s %s < %s",
			ARCHIVING_EXECUTABLE,
			priv->is_mms ? "mms" : "sms",
			hostname,
			priv->direction == both ? "" : (priv->direction == mo ? "--mo" : "--mt"),
			priv->eml2a_fname);
	syslog (LOG_INFO, "Running archiving command %s", command);

	result = system (command);

	if (result > 0)
		syslog (LOG_ERR, "Error archiving message %s (%d)", priv->eml2a_fname, result);
	else if (result < 0)
		syslog (LOG_ERR, "Error running archiving command %s (%d): %s", priv->eml2a_fname, result, strerror(errno));
	else
	{
		syslog (LOG_INFO, "Message archived ok: %s", priv->eml2a_fname);
		ok = TRUE;
	}

	return eml2a_cleanup(ctx, SMFIS_DISCARD, ok ? delete : nodelete);
}

sfsistat eml2a_abort(ctx)
	SMFICTX *ctx;
{
	return eml2a_cleanup(ctx, SMFIS_TEMPFAIL, delete);
}

sfsistat eml2a_cleanup(ctx, desired_outcome, action)
	SMFICTX *ctx;
	sfsistat desired_outcome;
	delete_action action;
{
	struct mlfiPriv *priv = MLFIPRIV;

	if (priv != NULL)
	{
		/* close the archive file */
		if (priv->eml2a_fp != NULL && fclose(priv->eml2a_fp) == EOF)
		{
			/* failed; we have to wait until later */
			syslog (LOG_ERR, "Couldn't close archive file %s: %s", priv->eml2a_fname, strerror(errno));
		}

		/* delete the file */
		if (action == delete)
		{
			syslog (LOG_INFO, "Removing archive file %s", priv->eml2a_fname);
			unlink(priv->eml2a_fname);
		}
		else
			syslog (LOG_WARNING, "NOT removing archive file %s (for debug)", priv->eml2a_fname);

		/* release private memory */
		if (priv->eml2a_fname != NULL)
			free(priv->eml2a_fname);

		free(priv);
		smfi_setpriv(ctx, NULL);
	}

	/* return status */
	return desired_outcome;
}

sfsistat eml2a_close(ctx)
	SMFICTX *ctx;
{
	return eml2a_cleanup(ctx, SMFIS_CONTINUE, delete);
}

struct smfiDesc smfilter =
{ "eml2a-milter", /* filter name */
	SMFI_VERSION, /* version code -- do not change */
	SMFIF_NONE,
	/* flags */
	eml2a_connect, /* connection info filter */
	eml2a_helo, /* SMTP HELO command filter */
	eml2a_envfrom, /* envelope sender filter */
	eml2a_envrcpt, /* envelope recipient filter */
	eml2a_header, /* header filter */
	eml2a_eoh, /* end of header */
	eml2a_body, /* body block filter */
	eml2a_eom, /* end of message */
	eml2a_abort, /* message aborted */
	eml2a_close /* connection cleanup */
};

static void usage(prog)
	char *prog;
{
	fprintf(stderr,
			"Usage: %s -p socket-addr [-t timeout]\n",
			prog);
}

char *trim(char *str, char *spaces)
{
	size_t len = strlen(str);
	size_t lensp = strlen(spaces);
	int k, i;

	if (len == 0 || lensp == 0)
		return str;

	for (k = 0; k < (int) len; ++k)
	{
		bool done = TRUE;
		for (i = 0; i < (int) lensp; ++i)
			if (str[k] == spaces[i])
			{
				done = FALSE;
				break;
			}
		if (done)
			break;
	}

	str += k;
	len -= k;

	for (k = (int)len-1; k >= 0; --k)
	{
		for (i = 0; i < (int) lensp; ++i)
			if (str[k] == spaces[i])
			{
				str[k] = 0;
				break;
			}
		if (str[k] != 0)
			break;
	}

	return str;
}

int main(argc, argv)
	int argc;char **argv;
{
	bool setconn = FALSE;
	int c;
	const char *args = "p:t:h";
	extern char *optarg;
	int main_return;

	/* Process command line options */
	while ((c = getopt(argc, argv, args)) != -1)
	{
		switch (c)
		{
		case 'p':
			if (optarg == NULL || *optarg == '\0')
			{
				(void) fprintf(stderr, "Illegal conn: %s\n", optarg);
				exit(EX_USAGE);
			}
			if (smfi_setconn(optarg) == MI_FAILURE)
			{
				(void) fprintf(stderr, "smfi_setconn failed\n");
				exit(EX_SOFTWARE);
			}

			/*
			 **  If we're using a local socket, make sure it
			 **  doesn't already exist.  Don't ever run this
			 **  code as root!!
			 */

			if (strncasecmp(optarg, "unix:", 5) == 0)
				unlink(optarg + 5);
			else if (strncasecmp(optarg, "local:", 6) == 0)
				unlink(optarg + 6);
			setconn = TRUE;
			break;

		case 't':
			if (optarg == NULL || *optarg == '\0')
			{
				(void) fprintf(stderr, "Illegal timeout: %s\n", optarg);
				exit(EX_USAGE);
			}
			if (smfi_settimeout(atoi(optarg)) == MI_FAILURE)
			{
				(void) fprintf(stderr, "smfi_settimeout failed\n");
				exit(EX_SOFTWARE);
			}
			break;

		case 'h':
		default:
			usage(argv[0]);
			exit(EX_USAGE);
		}
	}

	if (!setconn)
	{
		fprintf(stderr, "%s: Missing required -p argument\n", argv[0]);
		usage(argv[0]);
		exit(EX_USAGE);
	}

	if (smfi_register(smfilter) == MI_FAILURE)
	{
		fprintf(stderr, "smfi_register failed\n");
		exit(EX_UNAVAILABLE);
	}

	FILE *fp = fopen(CONF_FILE, "r");
	if (!fp)
	{
		syslog (LOG_ERR, "Couldn't open config file %s: %s", CONF_FILE, strerror(errno));
		exit(EX_SOFTWARE);
	}
	else
	{
		struct list *lastlistitem = NULL;
		char item[4096];
		while(fgets(item, sizeof(item)-1, fp))
		{
			char *key = item;
			char *value;
			// truncate the comment portion, if any
			char *hash = strchr(item, '#');
			if (hash)
				*hash = 0;
			// find the =
			value = strchr(item, '=');
			if (value)
				*value++ = 0;
			else
				continue; // no =, not a valid setting line;

			key = trim(key, " \t\r\n");
			value = trim(value, " \t\r\n");

			struct list *listitem = (struct list *) malloc (sizeof(struct list));

			if (!listitem)
			{
				syslog (LOG_ERR, "Couldn't allocate config list item: %s", strerror(errno));
				exit(EX_SOFTWARE);
			}
			else
			{
				syslog (LOG_INFO, "Config item %s = %s", key, value);

				listitem->key = strdup(key);
				listitem->value = strdup(value);
				listitem->next = NULL;

				if (!listitem->key || !listitem->value)
				{
					syslog (LOG_ERR, "Couldn't allocate config list item strings: %s", strerror(errno));
					exit(EX_SOFTWARE);
				}

				if (!lastlistitem)
					config = listitem;
				else
					lastlistitem->next = listitem;
				lastlistitem = listitem;
			}
		}
		fclose(fp);
	}

	// from this point on we will use syslog

	openlog ("rm-eml2a-milter", LOG_PERROR | LOG_PID | LOG_NDELAY, LOG_LOCAL1);
	gethostname(hostname, sizeof(hostname)-1);
	syslog(LOG_INFO, "eml2a-milter starting on %s", hostname);

	int fdpid = open(PID_FILE, O_RDWR | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH);
	FILE *pidfile;
	if (fdpid == -1)
	{
		syslog (LOG_ERR, "Couldn't create pid file %s: %s", PID_FILE, strerror(errno));
		exit(EX_SOFTWARE);
	}
	else if ((pidfile = fdopen(fdpid, "w")) == NULL)
	{
		syslog (LOG_ERR, "Couldn't get file descriptor for pid file %s: %s", PID_FILE, strerror(errno));
		exit(EX_SOFTWARE);
	}
	else if ((fprintf(pidfile, "%lu\n", (unsigned long) getpid())) < 0)
	{
		syslog (LOG_ERR, "Couldn't write pid to pid file %s: %s", PID_FILE, strerror(errno));
		exit(EX_SOFTWARE);
	}
	else
	{
		fclose(pidfile);
		close(fdpid);

		main_return = smfi_main();

		// free the config list
		struct list *listitem = config;
		while(listitem)
		{
			struct list *nextitem = listitem->next;
			free (listitem->key);
			free (listitem->value);
			free (listitem);
			listitem = nextitem;
		}

		unlink(PID_FILE);
	}

	closelog ();

	return main_return;
}

/* eof */
