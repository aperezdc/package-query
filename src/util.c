/*
 *  util.c
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
#include <alpm.h>
#include <alpm_list.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <libintl.h>


#include "util.h"
#include "alpm-query.h"
#include "aur.h"
#include "color.h"

#define _(x) gettext(x)
#define FORMAT_LOCAL_PKG "lF134"

static alpm_list_t *results=NULL;

/* Results */
typedef struct _results_t
{
	void *ele;
	unsigned short type;
} results_t;

static results_t *results_new (void *ele, unsigned short type)
{
	results_t *r = NULL;
	MALLOC (r, sizeof (results_t));
	if (type == R_AUR_PKG)
		r->ele = (void *) aur_pkg_dup ((aurpkg_t *) ele);
	else
		r->ele = ele;
	r->type = type;
	return r;
}
	
static results_t *results_free (results_t *r)
{
	if (r->type == R_AUR_PKG) aur_pkg_free (r->ele);
	free (r);
	return NULL;
}
	
static const char *results_name (const results_t *r)
{
	switch (r->type)
	{
		case R_ALPM_PKG:
			return alpm_pkg_get_name ((pmpkg_t *) r->ele);
		case R_AUR_PKG:
			return aur_pkg_get_name ((aurpkg_t *) r->ele);
		default:
			return NULL;
	}
}

static time_t results_installdate (const results_t *r)
{
	const char *r_name;
	pmpkg_t *pkg = NULL;
	time_t idate=0;
	if (r->type==R_AUR_PKG) return 0;
	r_name = results_name (r);
	pkg = alpm_db_get_pkg(alpm_option_get_localdb(), r_name);
	if (pkg) idate = alpm_pkg_get_installdate(pkg);
	return idate;
}
			

static off_t results_isize (const results_t *r)
{
	if (r->type==R_AUR_PKG) return 0;
	return alpm_pkg_get_isize(r->ele);
}
		
static int results_votes (const results_t *r)
{
	switch (r->type)
	{
		case R_AUR_PKG:
			return aur_pkg_get_votes ((aurpkg_t *) r->ele);
		default:
			return 0;
	}
}
			
			
static int results_cmp (const results_t *r1, const results_t *r2)
{
	return strcmp (results_name (r1), results_name (r2));
}

static int results_installdate_cmp (const results_t *r1, const results_t *r2)
{
	if (results_installdate (r1)>results_installdate (r2))
		return 1;
	else if (results_installdate (r1)>results_installdate (r2))
		return -1;
	else
		return 0;
}


static int results_isize_cmp (const results_t *r1, const results_t *r2)
{
	if (results_isize (r1)>results_isize (r2))
		return 1;
	else if (results_isize (r1)>results_isize (r2))
		return -1;
	else
		return 0;
}


static int results_votes_cmp (const results_t *r1, const results_t *r2)
{
	return (results_votes (r1) - results_votes (r2));
}

void print_or_add_result (void *pkg, unsigned short type)
{
	if (config.sort==0)
	{
		print_package ("", pkg, (type == R_ALPM_PKG) ? alpm_pkg_get_str : aur_get_str);
		return;
	}
	else
	{
		results = alpm_list_add (results, results_new (pkg, type));
	}
}
		
void show_results ()
{
	alpm_list_t *i;
	alpm_list_fn_cmp fn_cmp=NULL;
	if (results!=NULL)
	{
		switch (config.sort)
		{
			case 'n': fn_cmp = (alpm_list_fn_cmp) results_cmp; break;
			case 'w': fn_cmp = (alpm_list_fn_cmp) results_votes_cmp; break;
			case '1': fn_cmp = (alpm_list_fn_cmp) results_installdate_cmp; break;
			case '2': fn_cmp = (alpm_list_fn_cmp) results_isize_cmp; break;
		}
		if (fn_cmp)
			results = alpm_list_msort (results, alpm_list_count (results), fn_cmp);
		for(i = results; i; i = alpm_list_next(i))
		{
			results_t *r = alpm_list_getdata(i);
			if (r->type == R_ALPM_PKG)
				print_package ("", r->ele, alpm_pkg_get_str);
			else if (r->type == R_AUR_PKG)
				print_package ("", r->ele, aur_get_str);
		}
		alpm_list_free_inner (results, (alpm_list_fn_free) results_free);
		alpm_list_free (results);
		results = NULL;
	}
}

target_t *target_parse (const char *str)
{
	target_t *ret=NULL;
	char *c, *s=(char *)str;
	MALLOC (ret, sizeof (target_t));
	if ((c = strchr (s, '/')) != NULL)
	{
		/* target include db ("db/pkg*") */
		ret->db = strndup (s, (c-s) / sizeof(char));
		s = ++c;
	}
	else 
		ret->db = NULL;

	if ((c=strstr (s, "<=")) != NULL)
	{
		ret->mod=PM_DEP_MOD_LE;
		ret->ver=strdup (&(c[2]));
	}
	else if ((c=strstr (s, ">=")) != NULL)
	{
		ret->mod=PM_DEP_MOD_GE;
		ret->ver=strdup (&(c[2]));
	}
	else if ((c=strchr (s, '<')) != NULL)
	{
		ret->mod=PM_DEP_MOD_LT;
		ret->ver=strdup (&(c[1]));
	}
	else if ((c=strchr (s, '>')) != NULL)
	{
		ret->mod=PM_DEP_MOD_GT;
		ret->ver=strdup (&(c[1]));
	}
	else if ((c=strchr (s, '=')) != NULL)
	{
		ret->mod=PM_DEP_MOD_EQ;
		ret->ver=strdup (&(c[1]));
	}
	else
	{
		ret->mod=PM_DEP_MOD_ANY;
		ret->ver=NULL;
	}
	if (c)
		ret->name=strndup (s, (c-s) / sizeof(char));
	else
		ret->name=strdup (s);
	return ret;
}

target_t* target_free (target_t *t)
{
	if (t == NULL)
		return NULL;
	FREE (t->db);
	FREE (t->name);
	FREE (t->ver);
	FREE (t);
	return NULL;
}
	
int target_check_version (target_t *t, const char *ver)
{
	int ret;
	if (t->mod==PM_DEP_MOD_ANY) return 1;
	ret = alpm_pkg_vercmp (ver, t->ver);
	switch (t->mod)
	{
		case PM_DEP_MOD_LE: return (ret<=0);
		case PM_DEP_MOD_GE: return (ret>=0);
		case PM_DEP_MOD_LT: return (ret<0);
		case PM_DEP_MOD_GT: return (ret>0);
		case PM_DEP_MOD_EQ: return (ret==0);
		default: return 1;
	}
}

int target_compatible (target_t *t1, target_t *t2)
{
	if (t2->mod != PM_DEP_MOD_EQ && t2->mod != PM_DEP_MOD_ANY)
		return 0;
	if (strcmp (t1->name, t2->name) == 0 &&
		(t1->mod == PM_DEP_MOD_ANY || t2->mod == PM_DEP_MOD_ANY ||
		target_check_version (t1, t2->ver)))
			return 1;
	return 0;
}

string_t *string_new ()
{
	string_t *str = NULL;
	MALLOC (str, sizeof (string_t));
	str->s = NULL;
	str->size = 0;
	str->used = 0;
	return str;
}

void string_free (string_t *dest)
{
	if (dest == NULL)
		return;
	FREE (dest->s);
	FREE (dest);
}

char *string_free2 (string_t *dest)
{
	char *s;
	if (dest == NULL)
		return NULL;
	s = dest->s;
	FREE (dest);
	return s;
}

string_t *string_ncat (string_t *dest, const char *src, size_t n)
{
	if (!src || !n) return dest;
	if (dest->size <= dest->used+n)
	{
		dest->size += ((n/PATH_MAX) + 1) * PATH_MAX;
		/* dest->size += dest->used + n + 1; */
		REALLOC (dest->s, dest->size * sizeof (char));
		if (dest->used == 0) dest->s[0]='\0';
	}
	dest->used += n;
	strncat (dest->s, src, n);
	return dest;
}

string_t *string_cat (string_t *dest, const char *src)
{
	return string_ncat (dest, src, strlen (src));
}

string_t *string_fcat (string_t *dest, const char *format, ...)
{
	if (!format || !strlen (format)) return dest;
	char *s;
	va_list args;
	va_start(args, format);
	vasprintf(&s, format, args);
	if (!s)
	{
		perror ("vasprintf");
		exit (1);
	}
	dest =  string_cat (dest, s);
	free (s);
	return dest;
}

const char *string_cstr (string_t *str)
{
	return (const char *) ((str->s) ? str->s : "");
}

char *strtrim(char *str)
{
	char *pch = str;

	if(str == NULL || *str == '\0') {
		/* string is empty, so we're done. */
		return(str);
	}

	while(isspace(*pch)) {
		pch++;
	}
	if(pch != str) {
		memmove(str, pch, (strlen(pch) + 1));
	}

	/* check if there wasn't anything but whitespace in the string. */
	if(*str == '\0') {
		return(str);
	}

	pch = (str + (strlen(str) - 1));
	while(isspace(*pch)) {
		pch--;
	}
	*++pch = '\0';

	return(str);
}

char *concat_str_list (alpm_list_t *l)
{
	char *ret;
	int len=0, j=0;
	alpm_list_t *i;
	if (!l)
	{
		return NULL;
	}
	else
	{
		for(i = l; i; i = alpm_list_next(i)) 
		{
			/* data's len + space for separator */
			len += strlen (alpm_list_getdata (i)) + strlen (config.csep);
		}
		if (len)
		{
			len++; /* '\0' at the end */
			CALLOC (ret, len, sizeof (char));
			strcpy (ret, "");
			for(i = l; i; i = alpm_list_next(i)) 
			{
				if (j++>0) strcat (ret, config.csep);
				strcat (ret, alpm_list_getdata (i));
			}
		}
		else
		{
			return NULL;
		}
	}
	return ret;
}


void format_str (char *s)
{
	char *c=s; int mod;
	while ((c=strchr (c, '\\')))
	{
		mod = 1;
		switch (c[1])
		{
			case '\\': c[0] = '\\'; break;
			case 'e': c[0] = '\033'; break;
			case 'n': c[0] = '\n'; break;
			case 'r': c[0] = '\r'; break;
			case 't': c[0] = '\t'; break;
			default: mod = 0; break;
		}
		if (mod)
			memcpy (&(c[1]), &(c[2]), (strlen (&(c[2]))+1) * sizeof (char));
		c++;
	}
}

static void print_escape (const char *str)
{
	const char *c=str;
	while (*c!='\0')
	{
		if (*c == '"')
			putchar ('\\');
		putchar (*c);
		c++;
	}
}

char * itostr (int i)
{
	char *is;
	asprintf (&is, "%d", i);
	return is;
}

char * ltostr (long i)
{
	char *is;
	asprintf (&is, "%ld", i);
	return is;
}

/* Helper function for strreplace */
static void _strnadd(char **str, const char *append, unsigned int count)
{
	if(*str) {
		*str = realloc(*str, strlen(*str) + count + 1);
	} else {
		*str = calloc(sizeof(char), count + 1);
	}

	strncat(*str, append, count);
}

/* Replace all occurances of 'needle' with 'replace' in 'str', returning
 * a new string (must be free'd) */
char *strreplace(const char *str, const char *needle, const char *replace)
{
	const char *p, *q;
	p = q = str;

	char *newstr = NULL;
	unsigned int needlesz = strlen(needle),
							 replacesz = strlen(replace);

	while (1) {
		q = strstr(p, needle);
		if(!q) { /* not found */
			if(*p) {
				/* add the rest of 'p' */
				_strnadd(&newstr, p, strlen(p));
			}
			break;
		} else { /* found match */
			if(q > p){
				/* add chars between this occurance and last occurance, if any */
				_strnadd(&newstr, p, q - p);
			}
			_strnadd(&newstr, replace, replacesz);
			p = q + needlesz;
		}
	}

	return newstr;
}

static int getcols(void)
{
	if (!isatty (1)) {
		/* We will default to 80 columns if we're not a tty
		 * this seems a fairly standard file width.
		 */
		return 80;
	} else {
#ifdef TIOCGSIZE
		struct ttysize win;
		if(ioctl(1, TIOCGSIZE, &win) == 0) {
			return win.ts_cols;
		}
#elif defined(TIOCGWINSZ)
		struct winsize win;
		if(ioctl(1, TIOCGWINSZ, &win) == 0) {
			return win.ws_col;
		}
#endif
		/* If we can't figure anything out, we'll just assume 80 columns */
		/* TODO any problems caused by this assumption? */
		return 80;
	}
	/* Original envvar way - prone to display issues
	const char *cenv = getenv("COLUMNS");
	if(cenv != NULL) {
		return atoi(cenv);
	}
	return -1;
	*/
}

static void indent (const char *str)
{
	char *s=NULL;
	char *c=NULL, *c1=NULL;
	int cur_col=4, cols;
	if (!str)
		return;
	s=strdup (str);
	c=s;
	cols=getcols();
	fprintf (stdout, "%-*s", 4, "");
	while ((c1=strchr (c, ' ')) != NULL)
	{
		c1[0]='\0';
		cur_col+=strlen (c)+1;
		if (cur_col >= cols)
		{
			fprintf (stdout, "\n%-*s%s ", 4, "", c);
			cur_col=4+strlen(c)+1;
		}
		else
			fprintf (stdout, "%s ", c);
		c=&c1[1];
	}
	cur_col+=strlen (c);
	if (cur_col >= cols && c!=s)
		fprintf (stdout, "\n%-*s%s\n", 4, "", c);
	else
		fprintf (stdout, "%s\n", c);
	FREE (s);
}

void color_print_package (void * p, printpkgfn f)
{
	string_t *cstr;
	static int number=0;
	const char *info, *lver;
	char *ver=NULL;
	cstr=string_new ();
	if (config.numbering)
	{
		/* Numbering list */
		cstr = string_fcat (cstr, "%s%d%s ", color (C_NB), ++number, color (C_NO));
	}
	if (config.get_res && config.aur_foreign)
	{
		dprintf (FD_RES, "local/");
	}
	else if (!config.aur_foreign)
	{
		info = f(p, 's');
		if (info)
		{
			if (config.get_res) dprintf (FD_RES, "%s/", info);
			cstr = string_fcat (cstr, "%s%s/%s", color_repo (info), info, color(C_NO));
		}
	}
	info=f(p, 'n');
	if (config.get_res) dprintf (FD_RES, "%s\n", info);
	cstr = string_fcat (cstr, "%s%s%s ", color(C_PKG), info, color(C_NO));
	if (config.list_group)
	{
		/* no more output for -[S,Q]g and no targets */
		fprintf (stdout, "%s\n", string_cstr (cstr));
		string_free (cstr);
		return;
	}
	lver = alpm_local_pkg_get_str (info, 'l');
	info = f(p, 'v');
	if (info) ver = strdup (info);
	if (config.aur_foreign)
	{
		/* show foreign package */
		info = f(p, 'm');
		if (ver && (!info || info[0]=='-'))
			cstr = string_fcat (cstr, "%s%s%s", color(C_ORPHAN), lver, color (C_NO));
		else
		{
			info = f(p, 'o');
			if (info && info[0]=='1')
				cstr = string_fcat (cstr, "%s%s%s", color(C_OD), lver, color (C_NO));
			else
				cstr = string_fcat (cstr, "%s%s%s", color(C_VER), lver, color (C_NO));
		}
		if (ver)
		{
			/* package found in AUR */
			if (alpm_pkg_vercmp (ver, lver)>0)
				cstr = string_fcat (cstr, " ( aur: %s )", ver);
			fprintf (stdout, "%saur/%s%s\n", color_repo ("aur"), color (C_NO), string_cstr (cstr));
			FREE (ver);
		}
		else
			fprintf (stdout, "%slocal/%s%s\n", color_repo ("local"), color (C_NO), string_cstr (cstr));
		string_free (cstr);
		return;
	}
	/* show version */
	cstr = string_fcat (cstr, "%s%s%s", color(C_VER), ver, color (C_NO));
	/* show size */
	if (config.show_size)
	{
		info = f(p, 'r');
		if (info)
		{
			if (strcmp (info, "aur")!=0)
				cstr = string_fcat (cstr, " [%.2f M]", (double) get_size_pkg (p) / (1024.0 * 1024));
		}
	}
	
	/* show groups */
	info = f(p, 'g');
	if (info)
	{
		cstr = string_fcat (cstr, " %s(%s)%s", color(C_GRP), info, color(C_NO));
	}
	/* show install information */
	if (lver)
	{
		info = f(p, 'r');
		if (info && strcmp (info, "local")!=0)
		{
			cstr = string_fcat (cstr, " %s[%s", color(C_INSTALLED), _("installed"));
			if (strcmp (ver, lver)!=0)
			{
				cstr = string_fcat (cstr, ": %s%s%s%s", color(C_LVER), lver, color (C_NO), color(C_INSTALLED));
			}
			cstr = string_fcat (cstr, "]%s", color(C_NO));
		}
	}
	/* ver no more needed */
	FREE (ver);

	info = f(p, 'o');
	if (info && info[0]=='1')
	{
		cstr = string_fcat (cstr, " %s(%s)%s", color(C_OD), _("Out of Date"), color(C_NO));
	}
	info = f(p, 'w');
	if (info)
	{
		cstr = string_fcat (cstr, " %s(%s)%s", color(C_VOTES), info, color(C_NO));
	}
	fprintf (stdout, "%s\n", string_cstr (cstr));
	string_free (cstr);
	/* if -Q or -Sl or -Sg <target>, don't display description */
	if (config.op != OP_SEARCH && config.op != OP_LIST_REPO_S) return;
	fprintf (stdout, "%s", color(C_DSC));
	indent (f(p, 'd'));
	fprintf (stdout, "%s", color(C_NO));
}

void print_package (const char * target, void * pkg, printpkgfn f)
{
	if (config.quiet) return;
	if (!config.custom_out) { color_print_package (pkg, f); return; }
	char *s = pkg_to_str (target, pkg, f, config.format_out);
	if (config.escape)
		print_escape (s);
	else
		printf ("%s\n", s);
	free (s);
	fflush (NULL);
}

char *pkg_to_str (const char * target, void * pkg, printpkgfn f, const char *format)
{
	if (!format) return NULL;
	const char *ptr, *end, *c;
	const char *info;
	string_t *ret = string_new();
	ptr = format;
	end = &(format[strlen(format)]);
	while ((c=strchr (ptr, '%')))
	{
		if (&(c[1])==end)
			break;
		if (c[1] == '%' )
		{
			ret = string_ncat (ret, ptr, (c-ptr));
			ret = string_cat (ret, "%%");
		}
		else
		{
			info = NULL;
			if (strchr (FORMAT_LOCAL_PKG, c[1]))
				info = alpm_local_pkg_get_str (f(pkg, 'n'), c[1]);
			else if (c[1]=='t')
				info = target; 
			else
				info = f (pkg, c[1]);
			if (c!=ptr) ret = string_ncat (ret, ptr, (c-ptr));
			if (info)
				ret = string_cat (ret, info);
			else
				ret = string_cat (ret, "-");
		}
		ptr = &(c[2]);
	}
	if (ptr != end) ret = string_ncat (ret, ptr, (end-ptr));
	return string_free2 (ret);
}

target_arg_t *target_arg_init (ta_dup_fn dup_fn, alpm_list_fn_cmp cmp_fn,
                               alpm_list_fn_free free_fn)
{
	target_arg_t *t;
	MALLOC (t, sizeof (target_arg_t));
	t->args = NULL;
	t->items = NULL;
	t->dup_fn = dup_fn;
	t->cmp_fn = cmp_fn;
	t->free_fn = free_fn;
	return t;
}

int target_arg_add (target_arg_t *t, const char *s, void *item)
{
	int ret=1;
	if (t && config.just_one)
	{
		if ((t->cmp_fn && alpm_list_find (t->items, item, t->cmp_fn)) ||
		    (!t->cmp_fn && alpm_list_find_ptr (t->items, item)))
			ret = 0;
		else if (t->dup_fn)
			t->items = alpm_list_add (t->items, t->dup_fn (item));
		else
			t->items = alpm_list_add (t->items, item);
		t->args = alpm_list_add (t->args, (void *) s);
	}
	return ret;
}

target_t *target_arg_free (target_arg_t *t)
{
	if (t)
	{
		if (t->free_fn)
			alpm_list_free_inner (t->items, (alpm_list_fn_free) t->free_fn);
		alpm_list_free (t->items);
		alpm_list_free (t->args);
		FREE (t);
	}
	return NULL;
}

alpm_list_t *target_arg_clear (target_arg_t *t, alpm_list_t *targets)
{
	alpm_list_t *i;
	char *data=NULL;
	if (t && targets && config.just_one)
	{
		for (i=t->args; i; i=alpm_list_next (i))
		{
			targets = alpm_list_remove_str (targets, alpm_list_getdata (i), &data);
			if (data) free (data);
		}
	}
	return targets;
}

alpm_list_t *target_arg_close (target_arg_t *t, alpm_list_t *targets)
{
	targets = target_arg_clear (t, targets);
	target_arg_free (t);
	return targets;
}

/* vim: set ts=4 sw=4 noet: */
