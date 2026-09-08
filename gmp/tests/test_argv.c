/* Tests the UPSTREAM parser verbatim, with only the cmdline address redirected
   at a test buffer. Extracted from crt0.c rather than retyped, so a divergence
   between what I read and what ships cannot hide here. */
#include <stdio.h>
#include <string.h>
#include <stddef.h>

static char fake_cmdline[4096];
#define FC_ARGS_PARAM_ADDR   ((const char *)fake_cmdline)
#define FC_ARGS_PARAM_MAXLEN 256
#define FC_ARGS_MAX_ARGC     32

#include "argv_parser.inc"

static int fails = 0;
static void check(const char *cmdline, const char *want)
{
    memset(fake_cmdline, 0, sizeof fake_cmdline);
    strncpy(fake_cmdline, cmdline, sizeof(fake_cmdline) - 1);
    char *argv[FC_ARGS_MAX_ARGC];
    int argc = fc_parse_args_param(argv, FC_ARGS_MAX_ARGC);
    char got[512] = "";
    for (int i = 0; i < argc; i++) {
        if (i) strcat(got, "|");
        strcat(got, argv[i]);
    }
    int ok = !strcmp(got, want);
    if (!ok) fails++;
    printf("  %-46.46s -> argc=%-2d %-28s %s\n",
           cmdline[0] ? cmdline : "(empty)", argc, got, ok ? "ok" : "MISMATCH");
}

int main(void)
{
    printf("upstream fc_parse_args_param, cmdline -> argv\n");
    check("args=`Test1 Test2`",                    "main|Test1|Test2");
    check("",                                      "main");
    check("root=/dev/vda rw",                      "main");
    check("ip=1.2.3.4 args=`a b` root=/dev/vda",   "main|a|b");
    check("args=`solo`",                           "main|solo");
    check("args=``",                               "main");
    check("args=`  spaced   out  `",               "main|spaced|out");
    check("args=`unterminated",                    "main");
    check("args=notquoted",                        "main");
    check("myargs=`x y`",                          "main");
    check("args=`103780000000 105000000`",         "main|103780000000|105000000");

    /* Boundary 1: more args than argv can hold. FC_ARGS_MAX_ARGC is 32, so
       argv[0]="main" plus at most 31 arguments; the 32nd must be dropped
       rather than written past the end of the caller's array. */
    {
        char big[600] = "args=`";
        for (int i = 1; i <= 40; i++) { char t[8]; snprintf(t, sizeof t, "%d ", i); strcat(big, t); }
        strcat(big, "`");
        memset(fake_cmdline, 0, sizeof fake_cmdline);
        strncpy(fake_cmdline, big, sizeof(fake_cmdline) - 1);
        char *argv[FC_ARGS_MAX_ARGC];
        int argc = fc_parse_args_param(argv, FC_ARGS_MAX_ARGC);
        int ok = (argc == FC_ARGS_MAX_ARGC);
        if (!ok) fails++;
        printf("  %-46.46s -> argc=%-2d %-28s %s\n",
               "40 args (cap is 32)", argc, "capped", ok ? "ok" : "MISMATCH");
    }

    /* Boundary 2: uninitialised memory with no NUL inside the 256-byte window.
       The copy loop is bounded by sizeof(buf)-1, so this must terminate and
       must not read past the reserved region. */
    {
        memset(fake_cmdline, 'A', 400);
        fake_cmdline[400] = '\0';
        char *argv[FC_ARGS_MAX_ARGC];
        int argc = fc_parse_args_param(argv, FC_ARGS_MAX_ARGC);
        int ok = (argc == 1 && !strcmp(argv[0], "main"));
        if (!ok) fails++;
        printf("  %-46.46s -> argc=%-2d %-28s %s\n",
               "400 bytes of 'A', no args= token", argc, argv[0], ok ? "ok" : "MISMATCH");
    }

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
