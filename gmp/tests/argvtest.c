/* argvtest.c -- print exactly what the app receives, so the boot path can be
   checked end to end rather than inferred from the parser. */
#include <stdio.h>
int main(int argc, char **argv)
{
	printf("ARGV_ARGC %d\n", argc);
	for (int i = 0; i < argc; i++)
		printf("ARGV_%d [%s]\n", i, argv[i] ? argv[i] : "(null)");
	printf("ARGV_DONE\n");
	fflush(stdout);
	return 0;
}
