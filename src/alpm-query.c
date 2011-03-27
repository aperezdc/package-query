/*
 *  alpm-query.c
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
#include <stdio.h>
#include <string.h>
#include <alpm.h>
#include <alpm_list.h>
#include <limits.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include "util.h"
#include "alpm-query.h"
typedef const char *(*retcharfn) (void *);

void init_path ()
{
	if (config.dbpath[0]=='\0') 
		sprintf (config.dbpath, "%s%s", config.root_dir, DBPATH);
	if (config.config_file[0]=='\0')
		sprintf (config.config_file, "%s%s", config.root_dir, CONFFILE);
	if (config.root_dir[0]=='\0') strcpy (config.root_dir, ROOTDIR);
}

int init_alpm ()
{
	init_path();
	if (alpm_initialize()!=	0)
	{
		fprintf(stderr, "failed to initialize alpm library (%s)\n", 
			alpm_strerrorlast());
		return 0;
	}
	if (alpm_option_set_root (config.root_dir)!=0)
	{
		fprintf (stderr, "problem setting rootdir '%s' (%s)\n", 
			config.root_dir, alpm_strerrorlast());
		return 0;
	}
	if (alpm_option_set_dbpath (config.dbpath)!=0)
	{
		fprintf (stderr, "problem setting dbpath '%s' (%s)\n", 
			config.dbpath, alpm_strerrorlast());
		return 0;
	}
	return 1;
}

/* From pacman 3.4.0, set arch */
static void setarch(const char *arch)
{
	if (strcmp(arch, "auto") == 0) {
		struct utsname un;
		uname(&un);
		alpm_option_set_arch(un.machine);
	} else {
		alpm_option_set_arch(arch);
	}
}

int parse_config_file (alpm_list_t **dbs, const char *config_file, int reg)
{
	char line[PATH_MAX+1];
	char *ptr;
	char *equal;
	FILE *conf;
	static int file_closed=0;
	static pmdb_t *db=NULL;
	static int in_option=0;
	if ((conf = fopen (config_file, "r")) == NULL)
	{
		fprintf(stderr, "Unable to open file: %s\n", config_file);
		return 0;
	}
	while (fgets (line, PATH_MAX, conf))
	{
		strtrim (line);
		if(strlen(line) == 0 || line[0] == '#') {
			continue;
		}
		if((ptr = strchr(line, '#'))) {
			*ptr = '\0';
		}

		if (line[0] == '[' && line[strlen(line)-1] == ']')
		{
			line[strlen(line)-1] = '\0';
			ptr = &(line[1]);
			if (strcmp (ptr, "options") != 0)
			{
				in_option=0;
				if (reg)
				{
					if ((db = alpm_db_register_sync(ptr)) == NULL)
					{
						fprintf(stderr, 
							"could not register '%s' database (%s)\n", ptr,
							alpm_strerrorlast());
						file_closed=1;
						fclose (conf);
						return 0;
					}
				}
				else
					*dbs = alpm_list_add (*dbs, strdup (ptr));
			}
			else
				in_option=1;
		}
		else
		{
			equal = strchr (line, '=');
			if (equal != NULL && equal != &(line[strlen (line)-1]))
			{
				ptr = &(equal[1]);
				*equal = '\0';
				strtrim (line);
				if (strcmp (line, "Include") == 0)
				{
					strtrim (ptr);
					if (!parse_config_file (dbs, ptr, reg))
					{
						if (!file_closed) fclose (conf);
						return 0;
					}
				}
				else if (reg && strcmp (line, "Server") == 0 && db != NULL)
				{
					strtrim (ptr);
					const char *arch = alpm_option_get_arch();
					char *server = strreplace (ptr, "$repo", alpm_db_get_name (db));
					if (arch)
					{
						char *temp=server;
						server = strreplace(temp, "$arch", arch);
						free(temp);
					}
					alpm_db_setserver(db, server);
					FREE (server);
				}
				else if (reg && in_option)
				{
					if (strcmp (line, "Architecture") == 0)
					{
						strtrim (ptr);
						setarch (ptr);
					}
					else if (!config.custom_dbpath && 
						strcmp (line, "DBPath") == 0)
					{
						strtrim (ptr);
						strcpy (config.dbpath, ptr);
						if (alpm_option_set_dbpath (config.dbpath)!=0)
						{
							fprintf (stderr, "problem setting dbpath '%s' (%s)\n", 
								config.dbpath, alpm_strerrorlast());
							file_closed=1;
							fclose (conf);
							return 0;
						}					
					}
				}
			}
			else if (in_option)
			{
				if (strcmp (line, "ShowSize") == 0)
					config.show_size=1;
			}
		}
	}
	fclose (conf);
	return 1;
}


alpm_list_t * get_db_sync ()
{
	alpm_list_t *dbs=NULL;
	init_path();
	parse_config_file (&dbs, config.config_file, 0);
	return dbs;
}

int init_db_sync (const char * config_file)
{
	return parse_config_file (NULL, config.config_file, 1);
}

int filter (pmpkg_t *pkg, unsigned int _filter)
{
	if (_filter & F_FOREIGN)
		return (get_sync_pkg (pkg) == NULL);
	if ((_filter & F_EXPLICIT) == F_EXPLICIT && alpm_pkg_get_reason(pkg) != PM_PKG_REASON_EXPLICIT)
		return 0;
	if ((_filter & F_DEPS) == F_DEPS && alpm_pkg_get_reason(pkg) != PM_PKG_REASON_DEPEND)
		return 0;
	if (_filter & F_UNREQUIRED)
	{
		alpm_list_t *requiredby = alpm_pkg_compute_requiredby(pkg);
		if (requiredby)
		{
			FREELIST(requiredby);
			return 0;
		}
	}
	if (_filter & F_UPGRADES)
		if (!alpm_sync_newversion (pkg, alpm_option_get_syncdbs()))
			return 0;
	if (_filter & F_GROUP)
		if (!alpm_pkg_get_groups (pkg))
			return 0;
	return 1;
}
int filter_state (pmpkg_t *pkg)
{
	int ret=0;
	if (filter (pkg, F_FOREIGN)) ret |= F_FOREIGN;
	if (filter (pkg, F_EXPLICIT)) ret |= F_EXPLICIT;
	if (filter (pkg, F_DEPS)) ret |= F_DEPS;
	if (filter (pkg, F_UNREQUIRED)) ret |= F_UNREQUIRED;
	if (filter (pkg, F_UPGRADES)) ret |= F_UPGRADES;
	if (filter (pkg, F_GROUP)) ret |= F_GROUP;
	return ret;
}


int search_pkg_by_type (pmdb_t *db, alpm_list_t **targets, int query_type)
{
	int ret=0;
	alpm_list_t *t;
	alpm_list_t *i, *j;

	alpm_list_t *(*f)(pmpkg_t *);
	retcharfn g;
	int free_fn_ret=0;
	/* free_fn_ret=1 to free f() return
	 * free_fn_ret=2 to free g() return 
	 *            =3 to free both
	 */
	target_arg_t *ta=NULL;

	g = NULL;
	switch (query_type)
	{
		case OP_Q_DEPENDS:
			f = alpm_pkg_get_depends;
			g = (retcharfn) alpm_dep_compute_string;
			free_fn_ret = 2;
			break;
		case OP_Q_CONFLICTS: f = alpm_pkg_get_conflicts; break;
		case OP_Q_PROVIDES: f = alpm_pkg_get_provides; break;
		case OP_Q_REPLACES: f = alpm_pkg_get_replaces; break;
		case OP_Q_REQUIRES:
			f = alpm_pkg_compute_requiredby;
			free_fn_ret = 3;
			break;
		default: return 0;
	}
	ta = target_arg_init (NULL, NULL, NULL);


	for(i = alpm_db_get_pkgcache(db); i && *targets; i = alpm_list_next(i))
	{
		pmpkg_t *pkg = alpm_list_getdata(i);
		alpm_list_t *pkg_info_list = f(pkg);
		for(j = pkg_info_list; j && *targets; j = alpm_list_next(j))
		{
			char *str;
			str = (char *) ((g) ? g(alpm_list_getdata(j)) : alpm_list_getdata(j));
			target_t *t1 = target_parse (str);
			if (free_fn_ret & 1) FREE (str);
			for(t = *targets; t; t = alpm_list_next(t))
			{
				target_t *t2 = target_parse (alpm_list_getdata(t));
				if (t2->db && strcmp (t2->db, alpm_db_get_name (db))!=0)
				{
					target_free (t2);
					continue;
				}
				if (target_compatible (t1, t2) && filter (pkg, config.filter))
				{
					ret++;
					if (target_arg_add (ta, alpm_list_getdata(t), pkg))
						print_package (alpm_list_getdata(t), pkg, alpm_pkg_get_str);
				}
				target_free (t2);
			}
			*targets = target_arg_clear (ta, *targets);
			target_free (t1);
		}
		if (free_fn_ret & 2) alpm_list_free (pkg_info_list);
	}
	*targets = target_arg_close (ta, *targets);
	return ret;
}

int search_pkg_by_name (pmdb_t *db, alpm_list_t **targets)
{
	int ret=0;
	alpm_list_t *t;
	pmpkg_t *pkg_found;
	const char *db_name = alpm_db_get_name (db);
	target_arg_t *ta = target_arg_init (NULL, NULL, NULL);
	for(t = *targets; t; t = alpm_list_next(t))
	{
		const char *target=alpm_list_getdata(t);
		target_t *t1 = target_parse (target);
		if (t1->db && strcmp (t1->db, db_name)!=0)
		{
			target_free (t1);
			continue;
		}
		pkg_found = alpm_db_get_pkg (db, t1->name);
		if (pkg_found != NULL
			&& filter (pkg_found, config.filter)
			&& target_check_version (t1, alpm_pkg_get_version (pkg_found)))
		{
			ret++;
			if (target_arg_add (ta, target, pkg_found))
				print_package (target, pkg_found, alpm_pkg_get_str);
		}
		target_free (t1);
	}
	*targets = target_arg_close (ta, *targets);
	return ret;
}

void print_grp (pmgrp_t *grp)
{
	if (!config.list_group)
	{
		alpm_list_t *i;
		for (i=alpm_grp_get_pkgs (grp); i; i=alpm_list_next (i))
			print_package (alpm_grp_get_name (grp), alpm_list_getdata(i), alpm_pkg_get_str);
	}
	else
		print_package ("", grp, alpm_grp_get_str);
}

int list_grp (pmdb_t *db, alpm_list_t *targets)
{
	int ret=0;
	pmgrp_t *grp;
	alpm_list_t *t;
	if (targets)
	{
		for (t=targets; t; t = alpm_list_next (t))
		{
			const char *grp_name = alpm_list_getdata (t);
			grp = alpm_db_readgrp (db, grp_name);
			if (grp) 
			{
				ret++;
				print_grp (grp);
			}
		}
	}
	else
	{
		for(t = alpm_db_get_grpcache(db); t; t = alpm_list_next(t)) 
		{
			ret++;
			print_grp (alpm_list_getdata(t));
		}
	}
	return ret;
}	

int search_pkg (pmdb_t *db, alpm_list_t *targets)
{
	int ret=0;
	alpm_list_t *res, *t;

	res = alpm_db_search(db, targets);
	for(t = res; t; t = alpm_list_next(t)) 
	{
		pmpkg_t *info = alpm_list_getdata(t);
		if (!filter (info, config.filter)) continue;
		ret++;
		print_or_add_result ((void *) info, R_ALPM_PKG);
	}
	alpm_list_free (res);
	return ret;
}

int alpm_search_local (alpm_list_t **res)
{
	alpm_list_t *i;
	int ret=0;

	for(i = alpm_db_get_pkgcache(alpm_option_get_localdb());
	    i; i = alpm_list_next(i))
	{
		if (filter (alpm_list_getdata (i), config.filter))
		{
			if (res)
				*res = alpm_list_add (*res, strdup (alpm_pkg_get_name (alpm_list_getdata (i))));
			else
				print_or_add_result ((void *) alpm_list_getdata (i), R_ALPM_PKG);
			ret++;
		}
	}
	return ret;
}


int list_db (pmdb_t *db, alpm_list_t *targets)
{
	int ret=0;
	const char *db_name = alpm_db_get_name (db);
	const char *target=NULL;
	int db_present=0;
	alpm_list_t *t;
	alpm_list_t *i;

	if (targets)
	{
		for (t=targets; t; t = alpm_list_next (t))
		{
			target = alpm_list_getdata (t);
			if (strcmp (db_name, target) == 0)
			{
				db_present=1;
				break;
			}
		}
	}
	else
		db_present=1;
	if (!db_present) return 0;
	for(i = alpm_db_get_pkgcache(db); i; i = alpm_list_next(i))
	{
		pmpkg_t *info = alpm_list_getdata(i);
		print_or_add_result ((void *) info, R_ALPM_PKG);
		ret++;
	}
	return ret;
}

/* get_sync_pkg() returns the first pkg with same name in sync dbs */
pmpkg_t *get_sync_pkg (pmpkg_t *pkg)
{
	pmpkg_t *sync_pkg = NULL;
	const char *dbname;
	dbname = alpm_db_get_name (alpm_pkg_get_db (pkg));
	if (dbname==NULL || strcmp ("local", dbname)==0)
	{
		alpm_list_t *i;
		const char *pkgname;
		pkgname = alpm_pkg_get_name (pkg);
		for (i=alpm_option_get_syncdbs(); i; i = alpm_list_next (i))
		{
			sync_pkg = alpm_db_get_pkg (alpm_list_getdata(i), pkgname);
			if (sync_pkg) break;
		}
	}
	else sync_pkg = pkg;
	return sync_pkg;
}

off_t get_size_pkg (pmpkg_t *pkg)
{
	pmpkg_t *sync_pkg = get_sync_pkg (pkg);
	if (config.filter & F_UPGRADES)
	{
		if (sync_pkg) 
			return alpm_pkg_download_size (sync_pkg);
	}
	else if (sync_pkg != pkg)
		return alpm_pkg_get_isize (pkg);
	else
		return alpm_pkg_get_size (pkg);
	return 0;
}

off_t alpm_pkg_get_realsize (pmpkg_t *pkg)
{
	alpm_list_t *f, *files;
	int len, i, j;
	ino_t *inodes=NULL;
	struct stat buf;
	off_t size=0;
	files = alpm_pkg_get_files (pkg);
	if (files)
	{
		chdir (config.root_dir);
		len = alpm_list_count (files);
		CALLOC (inodes, len, sizeof (ino_t));
		j=0;
		for (f = files; f; f = alpm_list_next (f))
		{
			int found=0;
			const char *filename=alpm_list_getdata (f);
			if (lstat (filename, &buf) == -1 ||
				!(S_ISREG (buf.st_mode) || S_ISLNK(buf.st_mode)))
				continue;
			for (i=0; i<len && i<j && !found; i++)
				if (inodes[i] == buf.st_ino)
				{
					found=1;
					break;
				}
			if (found) continue;
			inodes[j++]=buf.st_ino;
			size+=buf.st_size;
		}
		FREE (inodes);
	}
	return size;
}

const char *alpm_pkg_get_str (void *p, unsigned char c)
{
	pmpkg_t *pkg = (pmpkg_t *) p;
	static char *info=NULL;
	static int free_info=0;
	if (free_info)
	{
		free (info);
		free_info = 0;
	}
	info = NULL;
	switch (c)
	{
		case '2':
			info = ltostr (alpm_pkg_get_isize(pkg));
			free_info=1;
			break;
		case '5':
			pkg = get_sync_pkg (pkg);
			if (!pkg) break;
			info = ltostr (alpm_pkg_download_size(pkg));
			free_info=1;
			break;
		case 'a': info = (char *) alpm_pkg_get_arch (pkg); break;
		case 'b': 
			info = concat_str_list (alpm_pkg_get_backup (pkg)); 
			free_info = 1;
			break;
		case 'c': 
			info = concat_str_list (alpm_pkg_get_conflicts (pkg)); 
			free_info = 1;
			break;
		case 'd': info = (char *) alpm_pkg_get_desc (pkg); break;
		case 'g':
			info = concat_str_list (alpm_pkg_get_groups (pkg)); 
			free_info = 1;
			break;
		case 'n': info = (char *) alpm_pkg_get_name (pkg); break;
		case 's': 
			pkg = get_sync_pkg (pkg);
			if (!pkg) pkg = (pmpkg_t *) p;
		case 'r': info = (char *) alpm_db_get_name (alpm_pkg_get_db (pkg)); break;
		case 'u': 
			{
			const char *dburl= alpm_db_get_url((alpm_pkg_get_db (pkg)));
			if (!dburl) return NULL;
			const char *pkgfilename = alpm_pkg_get_filename (pkg);
			CALLOC (info, strlen (dburl) + strlen(pkgfilename) + 2, sizeof (char));
			sprintf (info, "%s/%s", dburl, pkgfilename);
			free_info = 1;
			}
			break;
		case 'V': 
			pkg = get_sync_pkg (pkg);
			if (!pkg) break;
		case 'v': info = (char *) alpm_pkg_get_version (pkg); break;
		default: return NULL; break;
	}
	return info;
}	


const char *alpm_local_pkg_get_str (const char *pkg_name, unsigned char c)
{
	pmpkg_t *pkg = NULL;
	static char *info=NULL;
	static int free_info=0;
	if (free_info)
	{
		free (info);
		free_info = 0;
	}
	info = NULL;
	if (!pkg_name) return NULL;
	pkg = alpm_db_get_pkg(alpm_option_get_localdb(), pkg_name);
	if (!pkg) return NULL;
	switch (c)
	{
		case 'l': info = (char *) alpm_pkg_get_version (pkg); break;
		case '1':
			{
				time_t idate;
				idate = alpm_pkg_get_installdate(pkg);
				if (!idate) break;
				CALLOC (info, 50, sizeof (char));
				strftime(info, 50, "%s", localtime(&idate));
				free_info=1;
			}
			break;
		case '3':
			info = ltostr (alpm_pkg_get_realsize (pkg));
			free_info=1;
			break;
		case '4': info = itostr (filter_state (pkg)); free_info=1; break;
		default: return NULL; break;
	}
	return info;
}	



const char *alpm_grp_get_str (void *p, unsigned char c)
{
	pmgrp_t *grp = (pmgrp_t *) p;
	static char *info=NULL;
	static int free_info=0;
	if (free_info)
	{
		free (info);
		free_info = 0;
	}
	info = NULL;
	switch (c)
	{
		case 'n': info = (char *) alpm_grp_get_name (grp); break;
		default: return NULL; break;
	}
	return info;
}

void alpm_cleanup ()
{
	alpm_pkg_get_str (NULL, 0);
	alpm_grp_get_str (NULL, 0);
	alpm_local_pkg_get_str (NULL, 0);
}

/* vim: set ts=4 sw=4 noet: */
