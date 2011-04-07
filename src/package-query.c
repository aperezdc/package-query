/*
 *  package-query.c
 *
 *  Copyright (c) 2010-2011 Tuxce <tuxce.net@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#if defined(GIT_VERSION)
#undef PACKAGE_VERSION
#define PACKAGE_VERSION GIT_VERSION
#endif

#include <stdio.h>
#include <string.h>
#include <alpm.h>
#include <alpm_list.h>
#include <limits.h>
#include <unistd.h>
#include <getopt.h>
#include <locale.h>
#include <libintl.h>
#include <signal.h>

#include "util.h"
#include "color.h"
#include "alpm-query.h"
#include "aur.h"

#define N_DB     1
#define N_TARGET 2

#define SETQUERY(x) do { \
if (config.op) break; \
config.op = OP_QUERY; config.query = x; \
need |= N_TARGET | N_DB; } while (0)

extern char *optarg;
extern int optind;

static alpm_list_t *targets=NULL;
static int alpm_initialized=0;

void cleanup (int ret)
{
	static int cleaned=0;
	if (cleaned) return;
	cleaned=1;
	if (alpm_initialized && alpm_release()==-1)
		fprintf (stderr, alpm_strerrorlast());
	FREELIST(targets);
	FREE (config.myname);
	alpm_cleanup ();
	aur_cleanup ();
	color_cleanup ();
	exit (ret);
}

void handler (int signum)
{
	cleanup (signum);
}

void init_config (const char *myname)
{
	char *_myname=strdup (myname);
	config.myname = strdup(basename(_myname));
	FREE (_myname);
	config.aur = 0;
	config.aur_fetch = 0;
	config.aur_foreign = 0;
	config.colors = 1; 
	config.custom_dbpath = 0;
	config.custom_out = 0; 
	config.db_local = 0;
	config.db_sync = 0;
	config.escape = 0;
	config.filter = 0;
	config.get_res = 0;
	config.insecure = 0;
	config.is_file = 0;
	config.just_one = 0;
	config.list = 0;
	config.list_group = 0;
	config.numbering = 0;
	config.op = 0;
	config.quiet = 0;
	config.query=ALL;
	config.show_size = 0;
	strcpy (config.aur_url, AUR_BASE_URL);
	strcpy (config.config_file, "");
	strcpy (config.csep, " ");
	strcpy (config.dbpath, "");
	strcpy (config.format_out, "");
	strcpy (config.root_dir, "");
	config.sort = 0;
}




void version ()
{
	printf ("%s %s\n", config.myname, PACKAGE_VERSION);
	exit (0);
}

void usage (unsigned short _error)
{
	fprintf(stderr, "Query alpm database and/or AUR\n");
	fprintf(stderr, "Usage: %s [options] [targets ...]\n", config.myname);
	if (_error)
	{
		fprintf(stderr, "More information: %s --help\n\n", config.myname);
		exit (1);
	}
	fprintf(stderr, "\nwhere options include:");
	fprintf(stderr, "\n\t-1 --just-one show the first answer only");
	fprintf(stderr, "\n\t-A --aur query AUR database");
	fprintf(stderr, "\n\t-b --dbpath <database path> : default %s", DBPATH);
	fprintf(stderr, "\n\t-c --config <configuration file> : default %s", CONFFILE);
	fprintf(stderr, "\n\t-x --escape escape \" on output");
	fprintf(stderr, "\n\t-f --format <format>");
	fprintf(stderr, "\n\t-i --info search by name");
	fprintf(stderr, "\n\t-L --list-repo list configured repository");
	fprintf(stderr, "\n\t-l --list list repository content");
	fprintf(stderr, "\n\t-m --foreign search if foreign package exist in AUR (-AQm)");
	fprintf(stderr, "\n\t-p --file query file package");
	fprintf(stderr, "\n\t-q --quiet quiet");
	fprintf(stderr, "\n\t-Q --query search in local database");
	fprintf(stderr, "\n\t-r --root <root path> : default %s", ROOTDIR);
	fprintf(stderr, "\n\t-s --search search");
	fprintf(stderr, "\n\t-S --sync search in sync database");
	fprintf(stderr, "\n\t--sort [n,w,1,2] sort search by name, votes, install date, size");
	fprintf(stderr, "\n\t--query-type query type");
	fprintf(stderr, "\n\t-u --upgrades list updates available");
	fprintf(stderr, "\n\t-h --help show help");
	fprintf(stderr, "\n\nquery type:");
	fprintf(stderr, "\n\tdepends: depends on one of target");
	fprintf(stderr, "\n\tconflicts: conflicts with one of target");
	fprintf(stderr, "\n\tprovides: provides one of target");
	fprintf(stderr, "\n\treplaces: replaces one of target");
	fprintf(stderr, "\n\nformat:");
	fprintf(stderr, "\n\ta: arch");
	fprintf(stderr, "\n\tb: backups");
	fprintf(stderr, "\n\td: description");
	fprintf(stderr, "\n\tc: conflicts");
	fprintf(stderr, "\n\ti: if AUR, show the ID");
	fprintf(stderr, "\n\tl: local version");
	fprintf(stderr, "\n\tn: name");
	fprintf(stderr, "\n\to: out of date (0,1)");
	fprintf(stderr, "\n\tr: repo name");
	fprintf(stderr, "\n\ts: (sync) repo name");
	fprintf(stderr, "\n\tt: target");
	fprintf(stderr, "\n\tv: version, depends on search target");
	fprintf(stderr, "\n\tw: votes from AUR");
	fprintf(stderr, "\n");
	exit (0);
}


int deal_db (pmdb_t *db)
{
	int ret;
	switch (config.op)
	{
		case OP_LIST_REPO:
		case OP_LIST_REPO_S:
			return list_db (db, targets);
		case OP_INFO:
		case OP_INFO_P:
			ret = search_pkg_by_name (db, &targets);
			if (!ret && config.op == OP_INFO_P)
				ret = search_pkg_by_type (db, &targets, OP_Q_PROVIDES);
			return ret;
		case OP_SEARCH: return search_pkg (db, targets);
		case OP_LIST_GROUP:
			return list_grp (db, targets);
		case OP_QUERY:
			return search_pkg_by_type (db, &targets, config.query);
		default: return 0;
	}
}

int main (int argc, char **argv)
{
	int ret=0, i;
	int need=0, given=0, cycle_db=0, db_order=0;
	alpm_list_t *t;

	struct sigaction a;
	init_config (argv[0]);
	a.sa_handler = handler;
	sigemptyset(&a.sa_mask);
	a.sa_flags = 0;
	sigaction(SIGINT, &a, NULL);
	sigaction(SIGTERM, &a, NULL);

	int opt;
	int opt_index=0;
	static struct option opts[] =
	{
		{"query",      no_argument,       0, 'Q'},
		{"sync",       no_argument,       0, 'S'},
		{"dbpath",     required_argument, 0, 'b'},
		{"deps",       no_argument,       0, 'd'},
		{"explicit",   no_argument,       0, 'e'},
		{"groups",     no_argument,       0, 'g'},
		{"help",       no_argument,       0, 'h'},
		{"info",       no_argument,       0, 'i'},
		{"list",       no_argument,       0, 'l'},
		{"foreign",    no_argument,       0, 'm'},
		{"file",       no_argument,       0, 'p'},
		{"quiet",      no_argument,       0, 'q'},
		{"root",       required_argument, 0, 'r'},
		{"search",     no_argument,       0, 's'},
		{"unrequired", no_argument,       0, 't'},
		{"upgrades",   no_argument,       0, 'u'},
		{"config",     required_argument, 0, 'c'},
		{"just-one",   no_argument,       0, '1'},
		{"aur",        no_argument,       0, 'A'},
		{"escape",     no_argument,       0, 'x'},
		{"format",     required_argument, 0, 'f'},
		{"list-repo",  required_argument, 0, 'L'},
		{"query-type", required_argument, 0, 1000},
		{"csep",       required_argument, 0, 1001},
		{"sort",       required_argument, 0, 1002},
		{"nocolor",    no_argument,       0, 1003},
		{"number",     no_argument,       0, 1004},
		{"get-res",    no_argument,       0, 1005},
		{"show-size",  no_argument,       0, 1006},
		{"aur-url",    required_argument, 0, 1007},
		{"insecure",   no_argument,       0, 1008},
		{"qdepends",   no_argument,       0, 1009},
		{"qconflicts", no_argument,       0, 1010},
		{"qprovide",   no_argument,       0, 1011},
		{"qreplaces",  no_argument,       0, 1012},
		{"qrequires",  no_argument,       0, 1013},
		{"version",    no_argument,       0, 'v'},

		{0, 0, 0, 0}
	};

	
	while ((opt = getopt_long (argc, argv, "1Ac:b:def:ghiLlmpQqr:Sstuvx", opts, &opt_index)) != -1) 
	{
		switch (opt) 
		{
			case '1':
				config.just_one = 1;
				break;
			case 'A':
				if (config.aur) break;
				config.aur = ++db_order;
				given |= N_DB;
				break;
			case 'c':
				strcpy (config.config_file, optarg);
				break;
			case 'b':
				strcpy (config.dbpath, optarg);
				config.custom_dbpath = 1;
				break;
			case 'd':
				config.filter |= F_DEPS;
				break;
			case 'e':
				config.filter |= F_EXPLICIT;
				break;
			case 'x':
				config.escape = 1;
				break;
			case 'f':
				config.custom_out = 1;
				config.colors=0;
				strcpy (config.format_out, optarg);
				format_str (config.format_out);
				break;
			case 'g':
				if (config.op) break;
				config.op = OP_LIST_GROUP;
				config.filter |= F_GROUP;
				cycle_db = 1;
				break;
			case 'i':
				if (config.op)
				{
					config.op |= OP_INFO_P;
					break;
				}
				config.op = OP_INFO;
				need |= N_TARGET | N_DB;
				break;
			case 'L':
				config.list = 1;
				break;
			case 'l':
				if (config.op) break;
				config.op = OP_LIST_REPO;
				need |= N_DB;
				cycle_db = 1;
				break;
			case 'm':
				config.filter |= F_FOREIGN;
				break;
			case 'p':
				config.is_file = 1;
				need |= N_TARGET;
				break;
			case 'Q':
				if (config.db_local) break;
				config.db_local = ++db_order;
				given |= N_DB;
				break;
			case 'q':
				config.quiet = 1;
				break;
			case 'r':
				strcpy(config.root_dir, optarg);
				if (config.root_dir[strlen(config.root_dir)] != '/')
					strcat (config.root_dir, "/");
				break;
			case 's':
				if (config.op) break;
				config.op = OP_SEARCH;
				need |= N_DB;
				cycle_db = 1;
				break;
			case 'S':
				if (config.db_sync) break;
				config.db_sync = ++db_order;
				given |= N_DB;
				break;
			case 't':
				config.filter |= F_UNREQUIRED;
				break;
			case 1000: /* --query-type */
				if (config.op) break;
				config.op = OP_QUERY;
				if (strcmp (optarg, "depends")==0)
					config.query = OP_Q_DEPENDS;
				else if (strcmp (optarg, "conflicts")==0)
					config.query = OP_Q_CONFLICTS;
				else if (strcmp (optarg, "provides")==0)
					config.query = OP_Q_PROVIDES;
				else if (strcmp (optarg, "replaces")==0)
					config.query = OP_Q_REPLACES;
				need |= N_TARGET | N_DB;
				break;
			case 1009: /* --qdepends */
				SETQUERY (OP_Q_DEPENDS); break;
			case 1010: /* --qconflicts */
				SETQUERY (OP_Q_CONFLICTS); break;
			case 1011: /* --qprovides */
				SETQUERY (OP_Q_PROVIDES); break;
			case 1012: /* --qreplaces */
				SETQUERY (OP_Q_REPLACES); break;
			case 1013: /* --qrequires */
				SETQUERY (OP_Q_REQUIRES); break;
			case 1001: /* --csep */
				strncpy (config.csep, optarg, SEP_LEN);
				format_str (config.csep);
				break;
			case 1002: /* --sort */
				config.sort = optarg[0];
				break;
			case 1003: /* --nocolor */
				config.colors=0;
				break;
			case 1004: /* --number */
				config.numbering = 1;
				break;
			case 1005: /* --get-res */
				if (dup2(FD_RES, FD_RES) == FD_RES)
					config.get_res = 1;
				break;
			case 1006: /* --show-size */
				config.show_size = 1;
				break;
			case 1007: /* --aur-url */
				strcpy (config.aur_url, optarg);
				break;
			case 1008: /* --insecure */
				config.insecure = 1;
				break;
			case 'u':
				config.filter |= F_UPGRADES;
				break;
			case 'v':
				version(); break;
			case 'h': usage (0); break;
			default: /* '?' */
				usage (1);
		}
	}
	if (config.list)
	{
		/* -L displays respository list and exits. */
		alpm_list_t *dbs = get_db_sync ();
		if (dbs)
		{
			for(t = dbs; t; t = alpm_list_next(t))
				printf ("%s\n", (char *)alpm_list_getdata(t));
			FREELIST (dbs);
		}
		cleanup (0);
	}
	if (config.colors)
	{
		if (isatty (1))
			color_init();
		else 
			config.colors = 0;
	}
	if (!config.custom_out)
	{
		/* TODO: specific package-query locale ? */
		setlocale (LC_ALL, "");
		bindtextdomain ("yaourt", LOCALEDIR);
		textdomain ("yaourt");
	}
	if ((need & N_DB) && !(given & N_DB))
	{
		fprintf(stderr, "search or information must have database target (-{Q,S,A}).\n");
		exit(1);
	}
	for (i = optind; i < argc; i++)
	{
		if (!config.just_one ||
		    !alpm_list_find_str (targets, argv[i]))
			targets = alpm_list_add(targets, strdup(argv[i]));
	}
	if (i!=optind) 
	{
		given |= N_TARGET;
	}
	if ((need & N_TARGET) && !(given & N_TARGET))
	{
		fprintf(stderr, "no targets specified.\n");
		usage(1);
	}
	if (targets == NULL)
	{
		if (config.op == OP_SEARCH)	config.op = OP_LIST_REPO_S;
		/* print groups instead of packages */
		if (config.op == OP_LIST_GROUP) config.list_group = 1;
	}
	else if (!config.op && (given & N_DB)) /* Show info by default */
		config.op = OP_INFO;
	if ((alpm_initialized = init_alpm()) == 0) cleanup(1);
	if (!init_db_sync ()) cleanup(1);
	if (config.is_file)
	{
		for(t = targets; t; t = alpm_list_next(t))
		{
			pmpkg_t *pkg=NULL;
			const char *filename = alpm_list_getdata(t);
			if (alpm_pkg_load (filename, 0, &pkg)!=0 || pkg==NULL)
			{
				fprintf(stderr, "unable to read %s.\n", filename);
				continue;
			}
			print_package (filename, pkg, alpm_pkg_get_str);
			ret++;
		}
		cleanup(!ret);
	}

	if  (cycle_db || targets)
	{
		for (i=1; i<=db_order && (cycle_db || targets); i++)
		{
			/*printf ("%d, aur %d, local %d, sync %d\n", i, config.aur, config.db_local, config.db_sync);*/
			if (config.db_sync == i)
				for(t = alpm_option_get_syncdbs(); t; t = alpm_list_next(t))
					ret += deal_db (alpm_list_getdata (t));
			else if (config.db_local == i)
				ret += deal_db (alpm_option_get_localdb());
			else if (config.aur == i)
				switch (config.op)
				{
					case OP_INFO:
					case OP_INFO_P: ret += aur_info (&targets); break;
					case OP_SEARCH: ret += aur_search (targets); break;
					default: break;
				}
		}
	}
	else if (!config.aur && config.db_local)
		ret += alpm_search_local (config.filter, NULL, NULL);
	else if (config.aur && config.db_local && !(given & N_TARGET))
	{
		if (config.filter == F_FOREIGN)
		{
			/* -AQm */
			config.aur_foreign = 1;
			alpm_search_local (F_FOREIGN, "%n", &targets);
			ret += aur_info (&targets);
		}
		else if (config.filter == F_UPGRADES)
		{
			/* -AQu */
			ret += alpm_search_local (config.filter, NULL, NULL);
			alpm_search_local (F_FOREIGN, "%n>%v", &targets);
			ret += aur_info (&targets);
		}
	}

	show_results();

	/* Some cleanups */
	cleanup(!ret);
	return 0;
}

/* vim: set ts=4 sw=4 noet: */
