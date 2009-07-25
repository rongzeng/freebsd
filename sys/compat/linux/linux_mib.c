/*-
 * Copyright (c) 1999 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/jail.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>

#include "opt_compat.h"

#ifdef COMPAT_LINUX32
#include <machine/../linux32/linux.h>
#else
#include <machine/../linux/linux.h>
#endif
#include <compat/linux/linux_mib.h>

struct linux_prison {
	char	pr_osname[LINUX_MAX_UTSNAME];
	char	pr_osrelease[LINUX_MAX_UTSNAME];
	int	pr_oss_version;
	int	pr_osrel;
};

static struct linux_prison lprison0 = {
	.pr_osname =		"Linux",
	.pr_osrelease =		"2.6.16",
	.pr_oss_version =	0x030600,
	.pr_osrel =		2006016
};

static unsigned linux_osd_jail_slot;

SYSCTL_NODE(_compat, OID_AUTO, linux, CTLFLAG_RW, 0,
	    "Linux mode");

static int
linux_sysctl_osname(SYSCTL_HANDLER_ARGS)
{
	char osname[LINUX_MAX_UTSNAME];
	int error;

	linux_get_osname(req->td, osname);
	error = sysctl_handle_string(oidp, osname, LINUX_MAX_UTSNAME, req);
	if (error || req->newptr == NULL)
		return (error);
	error = linux_set_osname(req->td, osname);
	return (error);
}

SYSCTL_PROC(_compat_linux, OID_AUTO, osname,
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_PRISON | CTLFLAG_MPSAFE,
	    0, 0, linux_sysctl_osname, "A",
	    "Linux kernel OS name");

static int
linux_sysctl_osrelease(SYSCTL_HANDLER_ARGS)
{
	char osrelease[LINUX_MAX_UTSNAME];
	int error;

	linux_get_osrelease(req->td, osrelease);
	error = sysctl_handle_string(oidp, osrelease, LINUX_MAX_UTSNAME, req);
	if (error || req->newptr == NULL)
		return (error);
	error = linux_set_osrelease(req->td, osrelease);
	return (error);
}

SYSCTL_PROC(_compat_linux, OID_AUTO, osrelease,
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_PRISON | CTLFLAG_MPSAFE,
	    0, 0, linux_sysctl_osrelease, "A",
	    "Linux kernel OS release");

static int
linux_sysctl_oss_version(SYSCTL_HANDLER_ARGS)
{
	int oss_version;
	int error;

	oss_version = linux_get_oss_version(req->td);
	error = sysctl_handle_int(oidp, &oss_version, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	error = linux_set_oss_version(req->td, oss_version);
	return (error);
}

SYSCTL_PROC(_compat_linux, OID_AUTO, oss_version,
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_PRISON | CTLFLAG_MPSAFE,
	    0, 0, linux_sysctl_oss_version, "I",
	    "Linux OSS version");

/*
 * Map the osrelease into integer
 */
static int
linux_map_osrel(char *osrelease, int *osrel)
{
	char *sep, *eosrelease;
	int len, v0, v1, v2, v;

	len = strlen(osrelease);
	eosrelease = osrelease + len;
	v0 = strtol(osrelease, &sep, 10);
	if (osrelease == sep || sep + 1 >= eosrelease || *sep != '.')
		return (EINVAL);
	osrelease = sep + 1;
	v1 = strtol(osrelease, &sep, 10);
	if (osrelease == sep || sep + 1 >= eosrelease || *sep != '.')
		return (EINVAL);
	osrelease = sep + 1;
	v2 = strtol(osrelease, &sep, 10);
	if (osrelease == sep || sep != eosrelease)
		return (EINVAL);

	v = v0 * 1000000 + v1 * 1000 + v2;
	if (v < 1000000)
		return (EINVAL);

	*osrel = v;
	return (0);
}

/*
 * Find a prison with Linux info.
 * Return the Linux info and the (locked) prison.
 */
static struct linux_prison *
linux_find_prison(struct prison *spr, struct prison **prp)
{
	struct prison *pr;
	struct linux_prison *lpr;

	if (!linux_osd_jail_slot)
		/* In case osd_register failed. */
		spr = &prison0;
	for (pr = spr;; pr = pr->pr_parent) {
		mtx_lock(&pr->pr_mtx);
		lpr = (pr == &prison0)
		    ? &lprison0
		    : osd_jail_get(pr, linux_osd_jail_slot);
		if (lpr != NULL)
			break;
		mtx_unlock(&pr->pr_mtx);
	}
	*prp = pr;
	return (lpr);
}

/*
 * Ensure a prison has its own Linux info.  If lprp is non-null, point it to
 * the Linux info and lock the prison.
 */
static int
linux_alloc_prison(struct prison *pr, struct linux_prison **lprp)
{
	struct prison *ppr;
	struct linux_prison *lpr, *nlpr;
	int error;

	/* If this prison already has Linux info, return that. */
	error = 0;
	lpr = linux_find_prison(pr, &ppr);
	if (ppr == pr)
		goto done;
	/*
	 * Allocate a new info record.  Then check again, in case something
	 * changed during the allocation.
	 */
	mtx_unlock(&ppr->pr_mtx);
	nlpr = malloc(sizeof(struct linux_prison), M_PRISON, M_WAITOK);
	lpr = linux_find_prison(pr, &ppr);
	if (ppr == pr) {
		free(nlpr, M_PRISON);
		goto done;
	}
	/* Inherit the initial values from the ancestor. */
	mtx_lock(&pr->pr_mtx);
	error = osd_jail_set(pr, linux_osd_jail_slot, nlpr);
	if (error == 0) {
		bcopy(lpr, nlpr, sizeof(*lpr));
		lpr = nlpr;
	} else {
		free(nlpr, M_PRISON);
		lpr = NULL;
	}
	mtx_unlock(&ppr->pr_mtx);
 done:
	if (lprp != NULL)
		*lprp = lpr;
	else
		mtx_unlock(&pr->pr_mtx);
	return (error);
}

/*
 * Jail OSD methods for Linux prison data.
 */
static int
linux_prison_create(void *obj, void *data)
{
	struct prison *pr = obj;
	struct vfsoptlist *opts = data;
	int jsys;

	if (vfs_copyopt(opts, "linux", &jsys, sizeof(jsys)) == 0 &&
	    jsys == JAIL_SYS_INHERIT)
		return (0);
	/*
	 * Inherit a prison's initial values from its parent
	 * (different from JAIL_SYS_INHERIT which also inherits changes).
	 */
	return linux_alloc_prison(pr, NULL);
}

static int
linux_prison_check(void *obj __unused, void *data)
{
	struct vfsoptlist *opts = data;
	char *osname, *osrelease;
	int error, jsys, len, osrel, oss_version;

	/* Check that the parameters are correct. */
	error = vfs_copyopt(opts, "linux", &jsys, sizeof(jsys));
	if (error != ENOENT) {
		if (error != 0)
			return (error);
		if (jsys != JAIL_SYS_NEW && jsys != JAIL_SYS_INHERIT)
			return (EINVAL);
	}
	error = vfs_getopt(opts, "linux.osname", (void **)&osname, &len);
	if (error != ENOENT) {
		if (error != 0)
			return (error);
		if (len == 0 || osname[len - 1] != '\0')
			return (EINVAL);
		if (len > LINUX_MAX_UTSNAME) {
			vfs_opterror(opts, "linux.osname too long");
			return (ENAMETOOLONG);
		}
	}
	error = vfs_getopt(opts, "linux.osrelease", (void **)&osrelease, &len);
	if (error != ENOENT) {
		if (error != 0)
			return (error);
		if (len == 0 || osrelease[len - 1] != '\0')
			return (EINVAL);
		if (len > LINUX_MAX_UTSNAME) {
			vfs_opterror(opts, "linux.osrelease too long");
			return (ENAMETOOLONG);
		}
		error = linux_map_osrel(osrelease, &osrel);
		if (error != 0) {
			vfs_opterror(opts, "linux.osrelease format error");
			return (error);
		}
	}
	error = vfs_copyopt(opts, "linux.oss_version", &oss_version,
	    sizeof(oss_version));
	return (error == ENOENT ? 0 : error);
}

static int
linux_prison_set(void *obj, void *data)
{
	struct linux_prison *lpr;
	struct prison *pr = obj;
	struct vfsoptlist *opts = data;
	char *osname, *osrelease;
	int error, gotversion, jsys, len, oss_version;

	/* Set the parameters, which should be correct. */
	error = vfs_copyopt(opts, "linux", &jsys, sizeof(jsys));
	if (error == ENOENT)
		jsys = -1;
	error = vfs_getopt(opts, "linux.osname", (void **)&osname, &len);
	if (error == ENOENT)
		osname = NULL;
	else
		jsys = JAIL_SYS_NEW;
	error = vfs_getopt(opts, "linux.osrelease", (void **)&osrelease, &len);
	if (error == ENOENT)
		osrelease = NULL;
	else
		jsys = JAIL_SYS_NEW;
	error = vfs_copyopt(opts, "linux.oss_version", &oss_version,
	    sizeof(oss_version));
	if (error == ENOENT)
		gotversion = 0;
	else {
		gotversion = 1;
		jsys = JAIL_SYS_NEW;
	}
	switch (jsys) {
	case JAIL_SYS_INHERIT:
		/* "linux=inherit": inherit the parent's Linux info. */
		mtx_lock(&pr->pr_mtx);
		osd_jail_del(pr, linux_osd_jail_slot);
		mtx_unlock(&pr->pr_mtx);
		break;
	case JAIL_SYS_NEW:
		/*
		 * "linux=new" or "linux.*":
		 * the prison gets its own Linux info.
		 */
		error = linux_alloc_prison(pr, &lpr);
		if (error) {
			mtx_unlock(&pr->pr_mtx);
			return (error);
		}
		if (osrelease) {
			error = linux_map_osrel(osrelease, &lpr->pr_osrel);
			if (error) {
				mtx_unlock(&pr->pr_mtx);
				return (error);
			}
			strlcpy(lpr->pr_osrelease, osrelease,
			    LINUX_MAX_UTSNAME);
		}
		if (osname)
			strlcpy(lpr->pr_osname, osname, LINUX_MAX_UTSNAME);
		if (gotversion)
			lpr->pr_oss_version = oss_version;
		mtx_unlock(&pr->pr_mtx);
	}
	return (0);
}

SYSCTL_JAIL_PARAM_SYS_NODE(linux, CTLFLAG_RW, "Jail Linux parameters");
SYSCTL_JAIL_PARAM_STRING(_linux, osname, CTLFLAG_RW, LINUX_MAX_UTSNAME,
    "Jail Linux kernel OS name");
SYSCTL_JAIL_PARAM_STRING(_linux, osrelease, CTLFLAG_RW, LINUX_MAX_UTSNAME,
    "Jail Linux kernel OS release");
SYSCTL_JAIL_PARAM(_linux, oss_version, CTLTYPE_INT | CTLFLAG_RW,
    "I", "Jail Linux OSS version");

static int
linux_prison_get(void *obj, void *data)
{
	struct linux_prison *lpr;
	struct prison *ppr;
	struct prison *pr = obj;
	struct vfsoptlist *opts = data;
	int error, i;

	static int version0;

	/* See if this prison is the one with the Linux info. */
	lpr = linux_find_prison(pr, &ppr);
	i = (ppr == pr) ? JAIL_SYS_NEW : JAIL_SYS_INHERIT;
	error = vfs_setopt(opts, "linux", &i, sizeof(i));
	if (error != 0 && error != ENOENT)
		goto done;
	if (i) {
		error = vfs_setopts(opts, "linux.osname", lpr->pr_osname);
		if (error != 0 && error != ENOENT)
			goto done;
		error = vfs_setopts(opts, "linux.osrelease", lpr->pr_osrelease);
		if (error != 0 && error != ENOENT)
			goto done;
		error = vfs_setopt(opts, "linux.oss_version",
		    &lpr->pr_oss_version, sizeof(lpr->pr_oss_version));
		if (error != 0 && error != ENOENT)
			goto done;
	} else {
		/*
		 * If this prison is inheriting its Linux info, report
		 * empty/zero parameters.
		 */
		error = vfs_setopts(opts, "linux.osname", "");
		if (error != 0 && error != ENOENT)
			goto done;
		error = vfs_setopts(opts, "linux.osrelease", "");
		if (error != 0 && error != ENOENT)
			goto done;
		error = vfs_setopt(opts, "linux.oss_version", &version0,
		    sizeof(lpr->pr_oss_version));
		if (error != 0 && error != ENOENT)
			goto done;
	}
	error = 0;

 done:
	mtx_unlock(&ppr->pr_mtx);
	return (error);
}

static void
linux_prison_destructor(void *data)
{

	free(data, M_PRISON);
}

void
linux_osd_jail_register(void)
{
	struct prison *pr;
	osd_method_t methods[PR_MAXMETHOD] = {
	    [PR_METHOD_CREATE] =	linux_prison_create,
	    [PR_METHOD_GET] =		linux_prison_get,
	    [PR_METHOD_SET] =		linux_prison_set,
	    [PR_METHOD_CHECK] =		linux_prison_check
	};

	linux_osd_jail_slot =
	    osd_jail_register(linux_prison_destructor, methods);
	if (linux_osd_jail_slot > 0) {
		/* Copy the system linux info to any current prisons. */
		sx_xlock(&allprison_lock);
		TAILQ_FOREACH(pr, &allprison, pr_list)
			(void)linux_alloc_prison(pr, NULL);
		sx_xunlock(&allprison_lock);
	}
}

void
linux_osd_jail_deregister(void)
{

	if (linux_osd_jail_slot)
		osd_jail_deregister(linux_osd_jail_slot);
}

void
linux_get_osname(struct thread *td, char *dst)
{
	struct prison *pr;
	struct linux_prison *lpr;

	lpr = linux_find_prison(td->td_ucred->cr_prison, &pr);
	bcopy(lpr->pr_osname, dst, LINUX_MAX_UTSNAME);
	mtx_unlock(&pr->pr_mtx);
}

int
linux_set_osname(struct thread *td, char *osname)
{
	struct prison *pr;
	struct linux_prison *lpr;

	lpr = linux_find_prison(td->td_ucred->cr_prison, &pr);
	strlcpy(lpr->pr_osname, osname, LINUX_MAX_UTSNAME);
	mtx_unlock(&pr->pr_mtx);
	return (0);
}

void
linux_get_osrelease(struct thread *td, char *dst)
{
	struct prison *pr;
	struct linux_prison *lpr;

	lpr = linux_find_prison(td->td_ucred->cr_prison, &pr);
	bcopy(lpr->pr_osrelease, dst, LINUX_MAX_UTSNAME);
	mtx_unlock(&pr->pr_mtx);
}

int
linux_kernver(struct thread *td)
{
	struct prison *pr;
	struct linux_prison *lpr;
	int osrel;

	lpr = linux_find_prison(td->td_ucred->cr_prison, &pr);
	osrel = lpr->pr_osrel;
	mtx_unlock(&pr->pr_mtx);
	return (osrel);
}

int
linux_set_osrelease(struct thread *td, char *osrelease)
{
	struct prison *pr;
	struct linux_prison *lpr;
	int error;

	lpr = linux_find_prison(td->td_ucred->cr_prison, &pr);
	error = linux_map_osrel(osrelease, &lpr->pr_osrel);
	if (error == 0)
		strlcpy(lpr->pr_osrelease, osrelease, LINUX_MAX_UTSNAME);
	mtx_unlock(&pr->pr_mtx);
	return (error);
}

int
linux_get_oss_version(struct thread *td)
{
	struct prison *pr;
	struct linux_prison *lpr;
	int version;

	lpr = linux_find_prison(td->td_ucred->cr_prison, &pr);
	version = lpr->pr_oss_version;
	mtx_unlock(&pr->pr_mtx);
	return (version);
}

int
linux_set_oss_version(struct thread *td, int oss_version)
{
	struct prison *pr;
	struct linux_prison *lpr;

	lpr = linux_find_prison(td->td_ucred->cr_prison, &pr);
	lpr->pr_oss_version = oss_version;
	mtx_unlock(&pr->pr_mtx);
	return (0);
}

#if defined(DEBUG) || defined(KTR)

u_char linux_debug_map[howmany(LINUX_SYS_MAXSYSCALL, sizeof(u_char))];

static int
linux_debug(int syscall, int toggle, int global)
{

	if (global) {
		char c = toggle ? 0 : 0xff;

		memset(linux_debug_map, c, sizeof(linux_debug_map));
		return (0);
	}
	if (syscall < 0 || syscall >= LINUX_SYS_MAXSYSCALL)
		return (EINVAL);
	if (toggle)
		clrbit(linux_debug_map, syscall);
	else
		setbit(linux_debug_map, syscall);
	return (0);
}

/*
 * Usage: sysctl linux.debug=<syscall_nr>.<0/1>
 *
 *    E.g.: sysctl linux.debug=21.0
 *
 * As a special case, syscall "all" will apply to all syscalls globally.
 */
#define LINUX_MAX_DEBUGSTR	16
static int
linux_sysctl_debug(SYSCTL_HANDLER_ARGS)
{
	char value[LINUX_MAX_DEBUGSTR], *p;
	int error, sysc, toggle;
	int global = 0;

	value[0] = '\0';
	error = sysctl_handle_string(oidp, value, LINUX_MAX_DEBUGSTR, req);
	if (error || req->newptr == NULL)
		return (error);
	for (p = value; *p != '\0' && *p != '.'; p++);
	if (*p == '\0')
		return (EINVAL);
	*p++ = '\0';
	sysc = strtol(value, NULL, 0);
	toggle = strtol(p, NULL, 0);
	if (strcmp(value, "all") == 0)
		global = 1;
	error = linux_debug(sysc, toggle, global);
	return (error);
}

SYSCTL_PROC(_compat_linux, OID_AUTO, debug,
            CTLTYPE_STRING | CTLFLAG_RW,
            0, 0, linux_sysctl_debug, "A",
            "Linux debugging control");

#endif /* DEBUG || KTR */
