/* $Id: linux_genassym.c,v 1.5 1997/07/20 16:05:56 bde Exp $ */

#include <stdio.h>

#include <sys/param.h>

#include <i386/linux/linux.h>

int	main __P((void));

int
main()
{
	struct linux_sigframe *linux_sigf = (struct linux_sigframe *)0;
	struct linux_sigcontext *linux_sc = (struct linux_sigcontext *)0;

	printf("#define\tLINUX_SIGF_HANDLER %d\n", &linux_sigf->sf_handler);
	printf("#define\tLINUX_SIGF_SC %d\n", &linux_sigf->sf_sc);
	printf("#define\tLINUX_SC_FS %d\n", &linux_sc->sc_fs);
	printf("#define\tLINUX_SC_GS %d\n", &linux_sc->sc_gs);
	printf("#define\tLINUX_SC_EFLAGS %d\n", &linux_sc->sc_eflags);

	return (0);
}
