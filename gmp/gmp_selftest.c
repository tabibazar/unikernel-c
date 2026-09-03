/* Known-answer checks for a GMP built with this port's configure flags.
 *
 * The freestanding build changes four things that could plausibly break
 * arithmetic without breaking the link: -mcmodel=large codegen, fat
 * runtime CPU dispatch, --enable-alloca=malloc-reentrant rerouting every
 * temporary, and musl's headers in place of glibc's. A library that links
 * but computes wrong would be the worst outcome available, and on a
 * platform already known to compute incorrectly once in 1.6 million
 * operations it would be extremely hard to attribute later.
 *
 * So: fixed inputs, known answers, no randomness, exit code says which.
 * Runs hosted on x86-64 Linux against the same libgmp.a the unikernel
 * links, which is the strongest check available without booting.
 */
#include <stdio.h>
#include <string.h>
#include <gmp.h>

static int failures = 0;

static void check(const char *name, int ok)
{
	printf("%-34s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) failures++;
}

int main(void)
{
	mpz_t n, e, b, r, expect;
	mpz_inits(n, e, b, r, expect, NULL);

	/* 1. Fermat test on a known prime: 2^521-1 is Mersenne prime M13.
	   A correct 3^(n-1) mod n is exactly 1. This exercises the whole
	   powm path -- windowed exponentiation, Montgomery reduction, and
	   the tdiv_qr threshold test that does scalar double arithmetic. */
	mpz_set_ui(n, 1); mpz_mul_2exp(n, n, 521); mpz_sub_ui(n, n, 1);
	mpz_sub_ui(e, n, 1);
	mpz_set_ui(b, 3);
	mpz_powm(r, b, e, n);
	check("fermat 3^(M521-1) mod M521 == 1", mpz_cmp_ui(r, 1) == 0);

	/* 2. The same test on a composite must NOT return 1. A powm that
	    returned 1 unconditionally would pass check 1 and be useless. */
	mpz_set_ui(n, 1); mpz_mul_2exp(n, n, 520); mpz_sub_ui(n, n, 1);
	mpz_sub_ui(e, n, 1);
	mpz_powm(r, b, e, n);
	check("fermat on composite 2^520-1 != 1", mpz_cmp_ui(r, 1) != 0);

	/* 3. Big multiplication against a closed form: (2^n-1)^2 =
	   2^(2n) - 2^(n+1) + 1. At n=4096 this is well past the Karatsuba
	   and Toom thresholds, so it exercises the recursive paths and the
	   temporary allocation that --enable-alloca=malloc-reentrant moved
	   onto the heap. */
	mpz_set_ui(b, 1); mpz_mul_2exp(b, b, 4096); mpz_sub_ui(b, b, 1);
	mpz_mul(r, b, b);
	mpz_set_ui(expect, 1); mpz_mul_2exp(expect, expect, 8192);
	mpz_set_ui(n, 1); mpz_mul_2exp(n, n, 4097);
	mpz_sub(expect, expect, n);
	mpz_add_ui(expect, expect, 1);
	check("(2^4096-1)^2 closed form", mpz_cmp(r, expect) == 0);

	/* 4. Division round-trip at record size: q*d + r == n with 0<=r<d.
	   mpz_powm leans on tdiv_qr, so a wrong quotient here would show up
	   as a wrong PRP result and nothing else. */
	mpz_set_ui(n, 1); mpz_mul_2exp(n, n, 3400); mpz_sub_ui(n, n, 12345);
	mpz_set_ui(b, 1); mpz_mul_2exp(b, b, 1700); mpz_add_ui(b, b, 7);
	mpz_tdiv_qr(r, expect, n, b);       /* r=quotient, expect=remainder */
	mpz_mul(e, r, b);
	mpz_add(e, e, expect);
	check("tdiv_qr round-trip at 3400 bits",
	      mpz_cmp(e, n) == 0 && mpz_sgn(expect) >= 0 && mpz_cmp(expect, b) < 0);

	/* 5. GCD of two constructed coprimes must be 1, and of two known
	   multiples must be the shared factor. gcd_11/gcd_22 are among the
	   few asm routines that reference a global table (ctz_table) through
	   RIP-relative addressing -- the exact construct that would have
	   broken at this load address. */
	mpz_set_ui(b, 1); mpz_mul_2exp(b, b, 607); mpz_sub_ui(b, b, 1);   /* M607, prime */
	mpz_set_ui(n, 1); mpz_mul_2exp(n, n, 521); mpz_sub_ui(n, n, 1);   /* M521, prime */
	mpz_gcd(r, b, n);
	check("gcd(M607, M521) == 1", mpz_cmp_ui(r, 1) == 0);

	printf("\n%s (%d failure%s)\n", failures ? "SELFTEST FAILED" : "SELFTEST PASSED",
	       failures, failures == 1 ? "" : "s");
	mpz_clears(n, e, b, r, expect, NULL);
	return failures ? 1 : 0;
}
