/*
 * simbes.c
 *
 *  Created on: 2 Oct 2011
 *      Author: Jim
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <iconv.h>
#include <errno.h>
#include <direct.h>

typedef short unsigned int char16_t;

typedef char16_t CHAR;
#define CHARCONST(str) u ## str

static char *columns = "\"Name.ID\",\"Email Address\",\"Type of Message\",\"To\",\"From\",\"Callback Phone Number\",\"Body\",\"Send/Received Date\",\"Server Log Date\",\"Overall Message Status\",\"Command\",\"UID\"\r\n";
static char *separator = "\",\"";
static char *begin = "\"";
static char *end = "\"\r\n";

char *random_text(char *buff, size_t max_len);

size_t strCHARbytes(CHAR *s)
{
	size_t len = 0;
	while (s[len] != CHARCONST('\0'))
		len++;
	return len * sizeof(CHAR);
}

void usage()
{
	fprintf (stderr, "usage: [-h][-pdTtfbsui <param>]\n"
		"\t-x: [off] message can be ignored\n"
		"\t-h: help (this output)\n"
		"\n\tMessage content:\n\n"
		"\t-r <seed>: [time(NULL)] seed for the random number generator (unsigned long)\n"
		"\t-d \"<date>\": [now] date/time formatted as \"YYYY-MM-DD HH:MM:SS\"\n"
		"\t-e \"<addresses>\": [BESUser@galaxkey.com] comma separated list of users to select randomly\n"
		"\t-T <type>: [random] Incoming/Outgoing\n"
		"\t-t <to>: [random] recipient number (E164)\n"
		"\t-f <from>: [random] sender number (E164)\n"
		"\t-b \"<body>\": [random] SMS message text\n"
		"\t-s <status>: [random] Tx_Sent/Tx_Pending/Tx_Sending/Rx_Received\n"
		"\t-u <update>: [random] Update/Add\n"
		"\t-i <id>: [random] large negative integer\n"
		"\n\tOutput control:\n\n"
		"\t-p <path>: [.] path to log file directory\n"
		"\t-n count: [1] messages to generate\n"
	);
	exit(0);
}

CHAR *chartoCHAR(iconv_t handle, const char *str)
{
	size_t inlen = strlen(str), outlen = inlen*sizeof(CHAR);
	CHAR *outbuff = (CHAR*) malloc(outlen+sizeof(CHAR));
	CHAR *outbufptr = outbuff;

	if (!outbuff)
	{
		fprintf(stderr, "chartoCHAR: error allocating %lu bytes\n", (unsigned long) outlen+sizeof(CHAR));
		exit(1);
	}
	else if (iconv (handle, (char **) &str, &inlen, (char **) &outbufptr, &outlen) == (size_t) -1)
	{
		fprintf(stderr, "chartoCHAR: iconv failed: %s\n", strerror(errno));
		exit(1);
	}

	outbufptr[0] = 0;
	return outbuff;
}

char * random_phone(char *num, size_t min, size_t max)
{
	static char *numbuff = "0123456789";
	size_t len = min + (rand() % (max - min));
	for (int k = 0; k < len; ++k)
	{
		if (k == 0)
			num[k] = numbuff[1 + (rand() % 9)];
		else
			num[k] = numbuff[rand() % 10];
	}
	num[len] = 0;

	return num;
}

int main(int argc, char *argv[])
{
	char *path = ".";

	time_t now = time(NULL);
	struct tm timestruct;
	char longdate[64], pathdate[9];

	int emailaddresscount = 1;
	char *defemailaddress[] = { "BESUser@galaxkey.com" };
	char **emailaddress = defemailaddress;

	char *Type = NULL;
	char *To = NULL;
	char *From = NULL;
	char *Body = NULL;
	char *Status = NULL;
	char *Update = NULL;
	char *ID = NULL;

	int num_messages = 1;

	int include_dead_messages = 0;

	char logfilename[256];
	FILE *fp;
	int dirres;

	unsigned long random_seed = time(NULL);

	iconv_t ichandle = iconv_open ("UTF-16LE", "UTF-8");
	if (ichandle == (iconv_t) -1)
	{
		fprintf(stderr, "iconv failed to open: %s\n", strerror(errno));
		exit(1);
	}

	for (int arg = 1; arg < argc; arg += 2)
	{
		if (argv[arg][0] != '-')
		{
			usage();
			exit(0);
		}

		switch(argv[arg][1])
		{
			case 'h':
				usage();

			case 'x':
				include_dead_messages = 1;
				arg++;

			case 'p':
				if (argc <= (arg+1))
					usage();
				path = argv[arg+1];
				fprintf(stdout, "using path: %s\n", path);
				break;

			case 'd':
				if (sscanf(argv[arg+1], "%d-%d-%d %d:%d:%d",
						&timestruct.tm_year,
						&timestruct.tm_mon,
						&timestruct.tm_mday,
						&timestruct.tm_hour,
						&timestruct.tm_min,
						&timestruct.tm_sec) != 6)
				{
					fprintf(stderr, "Invalid date format: %s. Must be \"YYYY-MM-DD HH:MM:SS\" in quotes\n", argv[arg+1]);
					exit(1);
				}
				else
				{
					timestruct.tm_year -= 1900;
					timestruct.tm_mon -= 1;

					char *datebuff = strdup(asctime(&timestruct));
					char *crlf = strpbrk(datebuff, "\r\n");
					if (crlf)
						*crlf = 0;
					fprintf(stdout, "using date: %s\n", datebuff);
					free(datebuff);

					now = mktime(&timestruct);
				}

			case 'e':
			{
				emailaddresscount = 0;
				char *argcopy = strdup(argv[arg+1]);
				for (char *ptr = strtok(argcopy, ",\""); ptr != NULL; ptr = strtok(NULL, ",\""), ++emailaddresscount);
				if (emailaddresscount == 0)
				{
					fprintf(stderr, "Invalid email address list: %s. Must be quoted, eg \"a@r.com,b@r.com\"\n", argv[arg+1]);
					exit(1);
				}
				else
				{
					free(argcopy);
					char *argcopy = strdup(argv[arg+1]);
					emailaddress = (char **) malloc (emailaddresscount*sizeof(char*));
					int k = 0;
					for (char *ptr = strtok(argcopy, ",\""); ptr != NULL; ptr = strtok(NULL, ",\""), ++k)
						emailaddress[k] = strdup(ptr);
					fprintf(stdout, "using list of %d email addresses\n", emailaddresscount);
				}
				free(argcopy);
				break;
			}

			case 'T':
				Type = argv[arg+1];
				fprintf(stdout, "using type: %s\n", Type);
				break;

			case 't':
				To = argv[arg+1];
				fprintf(stdout, "using To number: %s\n", To);
				break;

			case 'f':
				From = argv[arg+1];
				fprintf(stdout, "using From number: %s\n", From);
				break;

			case 'b':
				Body = argv[arg+1];
				fprintf(stdout, "using supplied body\n");
				break;

			case 's':
				Status = argv[arg+1];
				fprintf(stdout, "using status: %s\n", Status);
				break;

			case 'u':
				Update = argv[arg+1];
				fprintf(stdout, "using update: %s\n", Update);
				break;

			case 'i':
				ID = argv[arg+1];
				fprintf(stdout, "using id: %s\n", ID);
				break;

			case 'n':
				num_messages = atoi(argv[arg+1]);
				fprintf(stdout, "adding %d messages\n", num_messages);
				break;

			case 'r':
				random_seed = strtoul(argv[arg+1], NULL, 0);
				break;
		}
	}

	fprintf(stdout, "using random seed: %lx\n", random_seed);
	srand(random_seed);

	timestruct = *localtime(&now);
	strftime(longdate, sizeof(longdate), "%Y/%m/%d %H:%M:%S", &timestruct);
	strftime(pathdate, sizeof(pathdate), "%Y%m%d", &timestruct);

	sprintf(logfilename, "%s/%s", path, pathdate);

	dirres = mkdir (logfilename/*, 0777*/);
	if (dirres != 0 && errno != EEXIST)
	{
		fprintf(stderr, "Error creating directory %s: %s\n", logfilename, strerror(errno));
		exit(1);
	}

	sprintf(logfilename, "%s/%s/SMSLOG_%s.CSV", path, pathdate, pathdate);

	fp = fopen(logfilename, "r+b");
	if (!fp)
	{
		fp = fopen(logfilename, "ab");
		if (!fp)
		{
			fprintf(stderr, "error opening %s: %s\n", logfilename, strerror(errno));
			exit(1);
		}
		else
		{
			CHAR *wcols = chartoCHAR(ichandle, columns);
			unsigned char header[2] = { 0xFF, 0xFE };
			if (fwrite (header, 2, 1, fp) != 1)
			{
				fprintf(stderr, "error writing magic bytes to %s: %s\n", logfilename, strerror(errno));
				exit(1);
			}
			else if (fwrite (wcols, strCHARbytes(wcols), 1, fp) != 1)
			{
				fprintf(stderr, "error writing column headers to %s (%lu bytes): %s\n", logfilename, (unsigned long) strCHARbytes(wcols), strerror(errno));
				exit(1);
			}

			free(wcols);
		}
	}
	else
	{
		fseek(fp, 0, SEEK_END);
	}

	if (1)
	{
		CHAR *wbeg = chartoCHAR(ichandle, begin);
		CHAR *wsep = chartoCHAR(ichandle, separator);
		CHAR *wend = chartoCHAR(ichandle, end);
		size_t len;

		for (int k = 0; k < num_messages; ++k)
		{
			int Outgoing;
			char phbuff[16], bodybuff[400], idbuff[16];
			char *TmpType = Type;
			static char *status[] = { "Tx_Sent","Tx_Pending","Tx_Sending" };
			char data[1024*64], *pdata = data;

			if (!TmpType)
				TmpType = rand()&1 ? "Incoming" : "Outgoing";

			Outgoing = (strcmp(TmpType, "Outgoing") == 0);

			if (!ID)
				sprintf(idbuff, "%u",
						((rand() << 24) & 0xFF000000) |
						((rand() << 16) & 0x00FF0000) |
						((rand() << 8)  & 0x0000FF00) |
						(rand()         & 0x000000FF));

			CHAR *args[] = {
					chartoCHAR(ichandle, "BES User.1"),
					chartoCHAR(ichandle, emailaddress[rand()%emailaddresscount]),
					chartoCHAR(ichandle, TmpType),
					Outgoing ? chartoCHAR(ichandle, (To ? To : random_phone(phbuff, 9, 15))) : 0,
					Outgoing ? 0 : chartoCHAR(ichandle, (From ? From : random_phone(phbuff, 9, 15))),
					chartoCHAR(ichandle, ""),
					chartoCHAR(ichandle, (Body != 0) ? Body : random_text(bodybuff, sizeof(bodybuff) - 1)),
					chartoCHAR(ichandle, longdate),
					chartoCHAR(ichandle, longdate),
					chartoCHAR(ichandle, Status ? Status : (Outgoing ? (include_dead_messages ? status[rand()%3] : "Tx_Sent") : "Rx_Received")),
					chartoCHAR(ichandle, Update ? Update : ((include_dead_messages && !(rand()&7)) ? "Add" : "Update")),
					chartoCHAR(ichandle, ID ? ID : idbuff)
			};

			for (int j = 0; j < sizeof(args)/sizeof(CHAR*); ++j)
			{
				len = strCHARbytes(!j ? wbeg : wsep);
				memcpy(pdata, !j ? wbeg : wsep, len);
				pdata += len;

				if (args[j])
				{
					len = strCHARbytes(args[j]);
					memcpy(pdata, args[j], len);
					pdata += len;
					free (args[j]);
				}
			}

			len = strCHARbytes(wend);
			memcpy(pdata, wend, len);
			pdata += len;

			if (fwrite (data, pdata-data, 1, fp) != 1)
			{
				fprintf(stderr, "error writing data %s: %s\n", logfilename, strerror(errno));
				exit(1);
			}
		}

		free(wsep);
		free(wbeg);
		free(wend);
	}

	fclose(fp);
	return 0;
}
