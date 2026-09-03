/* xy.c -- the Kosterlitz-Thouless transition in the 2D XY model.
 *
 * The Ising study in ../ising/ found a critical temperature and matched
 * Onsager. This is the stranger transition next door, and the one that won the
 * 2016 Nobel Prize.
 *
 * Replace each +/-1 Ising spin with a continuous angle and something odd
 * happens: the Mermin-Wagner theorem forbids any ordered phase at all in two
 * dimensions, so the magnetisation is zero at every non-zero temperature. And
 * yet there is still a sharp transition. It is driven not by symmetry breaking
 * but by topology -- by vortices, points where the angle field winds through a
 * full turn. Below the transition vortices exist only in tightly bound
 * vortex-antivortex pairs; above it the pairs unbind and the free vortices
 * destroy the quasi-order.
 *
 * WHAT THIS MEASURES, AND WHY IT IS A HARDER TEST THAN ONSAGER
 *
 * The helicity modulus Upsilon is the stiffness of the spin field against a
 * twist -- how much energy it costs to rotate one edge of the lattice relative
 * to the other. Kosterlitz and Thouless predict that it does not fall smoothly
 * to zero. It drops discontinuously, and the size of the jump is universal:
 *
 *     Upsilon(T_KT) / T_KT = 2 / pi
 *
 * That is a pure number. No coupling constant, no lattice detail, no fitted
 * parameter -- the same 2/pi for every system in this universality class. So
 * the test is not "did we land near a published number" but "does the curve
 * cross a line that theory fixes exactly". T_KT is then wherever the crossing
 * happens; high-precision Monte Carlo puts it at about 0.8929.
 *
 * Finite lattices cross ABOVE that, and converge only logarithmically, which
 * is a known and slow approach rather than an error to be tuned away. This
 * program reports the crossing it measures and lets it be off.
 *
 * NO MATH LIBRARY
 *
 * There is none in a freestanding unikernel, so sin, cos and exp are
 * hand-rolled below. ../ising/ising.c already had to do this for exp and sqrt;
 * the XY model needs trigonometry too, since its energy is a cosine rather
 * than a product of signs.
 *
 * DETERMINISM
 *
 * The generator is a seeded xoshiro256++, not RDRAND. ../ising/ uses hardware
 * randomness and that was the point there. Here a seed means the whole run
 * replays exactly -- and this repo has already shown (docs/aco-r1/) that the
 * same seed gives bit-identical results on BareMetal and on Linux, so a
 * published seed is a result anyone can reproduce rather than merely repeat.
 *
 *   linux:     gcc -O2 -o xy xy.c && ./xy
 *   baremetal: cp xy.c BareMetal-App/ && ./1-build.sh xy.c && ./baremetal.sh start
 */

#include <stdio.h>
#include <stdint.h>

/* ------------------------------------------------------------- parameters */

#ifndef XY_L
#define XY_L 64                 /* lattice is L x L */
#endif
#ifndef XY_SEED
#define XY_SEED 20260903ULL
#endif
#ifndef XY_WARMUP
#define XY_WARMUP 20000         /* sweeps discarded before measuring */
#endif
#ifndef XY_SWEEPS
#define XY_SWEEPS 60000         /* measurement sweeps per temperature */
#endif
#ifndef XY_TMIN
#define XY_TMIN 0.40
#endif
#ifndef XY_TMAX
#define XY_TMAX 1.30
#endif
#ifndef XY_TSTEP
#define XY_TSTEP 0.05
#endif
#ifndef XY_DELTA
#define XY_DELTA 1.2            /* Metropolis proposal width, radians */
#endif
#ifndef XY_DUMP_FIELD
#define XY_DUMP_FIELD 0         /* 1 = print the angle field for plotting */
#endif

#define PI     3.14159265358979323846
#define TWOPI  6.28318530717958647693
#define N      (XY_L * XY_L)

/* ------------------------------------------------- hand-rolled transcendentals */

/* sin on the reduced range, Taylor to x^15.
 *
 * Measured against libm over [-20, 20]: max absolute error 6.0e-12 for both
 * a_sin and a_cos. An earlier version stopped at x^11, which sounds like
 * plenty and is not -- the next term alone is (pi/2)^13/13! = 5.7e-08, and
 * that is exactly the error it showed. Two more terms cost two multiplies per
 * call and buy four orders of magnitude, which is worth having when every
 * energy difference in the Metropolis test is a sum of four of these. */
static double sin_core(double x)
{
	double x2 = x * x;
	return x * (1.0
	     + x2 * (-1.0 / 6.0
	     + x2 * (1.0 / 120.0
	     + x2 * (-1.0 / 5040.0
	     + x2 * (1.0 / 362880.0
	     + x2 * (-1.0 / 39916800.0
	     + x2 * (1.0 / 6227020800.0
	     + x2 * (-1.0 / 1307674368000.0))))))));
}

/* Reduce to [-pi, pi], then fold into [-pi/2, pi/2] using sin(pi - x) = sin x.
   The reduction is done by subtracting whole turns rather than by fmod, which
   would be another libc call this platform does not have. */
static double a_sin(double x)
{
	while (x >  PI) x -= TWOPI;
	while (x < -PI) x += TWOPI;
	if (x >  PI / 2) x =  PI - x;
	if (x < -PI / 2) x = -PI - x;
	return sin_core(x);
}

static double a_cos(double x) { return a_sin(x + PI / 2); }

/* exp(-y) for y >= 0, the only form Metropolis needs.
   Split y = n*ln2 + r with r in [0, ln2), evaluate exp(-r) by series, then
   halve n times. n stays small because anything past y = 40 underflows the
   acceptance test anyway and is returned as a flat zero. */
static double a_exp_neg(double y)
{
	if (y <= 0.0) return 1.0;
	if (y > 40.0) return 0.0;

	const double LN2 = 0.69314718055994530942;
	int n = (int)(y / LN2);
	double r = y - n * LN2;

	double term = 1.0, sum = 1.0;
	for (int k = 1; k <= 14; k++) {
		term *= -r / (double)k;
		sum += term;
	}
	while (n-- > 0) sum *= 0.5;
	return sum;
}

/* Wrap an angle difference into (-pi, pi]. Used by the vortex counter, where
   getting the branch wrong would silently miscount the winding. */
static double wrap_pi(double d)
{
	while (d >  PI) d -= TWOPI;
	while (d <= -PI) d += TWOPI;
	return d;
}

/* ------------------------------------------------------------------- rng */

static uint64_t rng_s[4];

static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t next_u64(void)
{
	uint64_t r = rotl(rng_s[0] + rng_s[3], 23) + rng_s[0];
	uint64_t t = rng_s[1] << 17;
	rng_s[2] ^= rng_s[0];
	rng_s[3] ^= rng_s[1];
	rng_s[1] ^= rng_s[2];
	rng_s[0] ^= rng_s[3];
	rng_s[2] ^= t;
	rng_s[3] = rotl(rng_s[3], 45);
	return r;
}

static double next_unit(void) { return (double)(next_u64() >> 11) * 0x1.0p-53; }

static void seed_rng(uint64_t seed)
{
	/* splitmix64, so that a small seed still fills the state well */
	for (int i = 0; i < 4; i++) {
		seed += 0x9E3779B97F4A7C15ULL;
		uint64_t z = seed;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		rng_s[i] = z ^ (z >> 31);
	}
}

/* ---------------------------------------------------------------- lattice */

static double th[N];

#define IDX(i, j) ((i) * XY_L + (j))
#define UP(i)     (((i) + 1) % XY_L)
#define DN(i)     (((i) + XY_L - 1) % XY_L)

/* One sweep = N attempted single-spin rotations, sites picked at random.
   Returns the number accepted, so the proposal width can be sanity-checked:
   an acceptance rate near 0 or 1 means the walk is barely moving or barely
   discriminating, and either way the averages are worth less than they look. */
static long sweep(double T)
{
	long accepted = 0;
	double inv_T = 1.0 / T;

	for (int s = 0; s < N; s++) {
		int i = (int)(next_u64() % XY_L);
		int j = (int)(next_u64() % XY_L);
		double old = th[IDX(i, j)];
		double neu = old + (2.0 * next_unit() - 1.0) * XY_DELTA;

		/* Only the four bonds touching this site change. */
		double n0 = th[IDX(UP(i), j)], n1 = th[IDX(DN(i), j)];
		double n2 = th[IDX(i, UP(j))], n3 = th[IDX(i, DN(j))];

		double e_old = -(a_cos(old - n0) + a_cos(old - n1)
		               + a_cos(old - n2) + a_cos(old - n3));
		double e_new = -(a_cos(neu - n0) + a_cos(neu - n1)
		               + a_cos(neu - n2) + a_cos(neu - n3));
		double dE = e_new - e_old;

		if (dE <= 0.0 || next_unit() < a_exp_neg(dE * inv_T)) {
			th[IDX(i, j)] = neu;
			accepted++;
		}
	}
	return accepted;
}

/* Helicity modulus, measured along x only (the lattice is isotropic, so one
   direction is enough and averaging both would only halve the variance).
	   Upsilon = <sum cos(dtheta)>/N  -  <(sum sin(dtheta))^2>/(N*T)
   The second term is a fluctuation, so it needs the SQUARE of a per-sample
   sum -- accumulating the sum first and squaring the average would give a
   different and wrong answer. */
static void measure_helicity(double *out_cos_sum, double *out_sin_sq)
{
	double c = 0.0, s = 0.0;
	for (int i = 0; i < XY_L; i++)
		for (int j = 0; j < XY_L; j++) {
			double d = th[IDX(i, j)] - th[IDX(UP(i), j)];
			c += a_cos(d);
			s += a_sin(d);
		}
	*out_cos_sum = c;
	*out_sin_sq  = s * s;
}

/* Count plaquettes whose phase winds by a full turn. This is the mechanism
   the whole transition is about, so it is measured directly rather than
   inferred from the thermodynamics. */
static long count_vortices(void)
{
	long v = 0;
	for (int i = 0; i < XY_L; i++)
		for (int j = 0; j < XY_L; j++) {
			double a = th[IDX(i, j)];
			double b = th[IDX(UP(i), j)];
			double c = th[IDX(UP(i), UP(j))];
			double d = th[IDX(i, UP(j))];
			double sum = wrap_pi(b - a) + wrap_pi(c - b)
			           + wrap_pi(d - c) + wrap_pi(a - d);
			int w = (int)(sum / TWOPI + (sum > 0 ? 0.5 : -0.5));
			if (w != 0) v++;
		}
	return v;
}

static double energy_per_site(void)
{
	double e = 0.0;
	for (int i = 0; i < XY_L; i++)
		for (int j = 0; j < XY_L; j++) {
			e -= a_cos(th[IDX(i, j)] - th[IDX(UP(i), j)]);
			e -= a_cos(th[IDX(i, j)] - th[IDX(i, UP(j))]);
		}
	return e / (double)N;
}

/* ------------------------------------------------------------------- main */

int main(void)
{
	seed_rng(XY_SEED);

	/* Start from a cold (aligned) state and walk up in temperature, carrying
	   the configuration forward. Each temperature therefore begins near
	   equilibrium for the one below it, which is much cheaper than restarting
	   from random every time -- and it is the direction that matters, since
	   heating through a KT transition does not get stuck the way cooling into
	   one can. */
	for (int k = 0; k < N; k++) th[k] = 0.0;

	printf("XY_START L=%d seed=%llu warmup=%d sweeps=%d delta=%.2f\n",
	       XY_L, (unsigned long long)XY_SEED, XY_WARMUP, XY_SWEEPS, XY_DELTA);
	printf("XY_NOTE the KT prediction is that Upsilon crosses 2T/pi; "
	       "the crossing temperature is T_KT (~0.8929 for L -> infinity)\n");
	printf("#%7s %10s %10s %10s %10s %8s\n",
	       "T", "energy", "helicity", "2T/pi", "vortex_den", "accept");

	int nT = (int)((XY_TMAX - XY_TMIN) / XY_TSTEP + 0.5) + 1;

	for (int ti = 0; ti < nT; ti++) {
		double T = XY_TMIN + ti * XY_TSTEP;

		for (int w = 0; w < XY_WARMUP; w++) sweep(T);

		double acc_cos = 0.0, acc_sinsq = 0.0, acc_e = 0.0;
		long acc_vort = 0, accepted = 0;

		for (int m = 0; m < XY_SWEEPS; m++) {
			accepted += sweep(T);
			double c, s2;
			measure_helicity(&c, &s2);
			acc_cos   += c;
			acc_sinsq += s2;
			acc_e     += energy_per_site();
			acc_vort  += count_vortices();
		}

		double inv_m = 1.0 / (double)XY_SWEEPS;
		double helicity = (acc_cos * inv_m) / (double)N
		                - (acc_sinsq * inv_m) / ((double)N * T);
		double line     = 2.0 * T / PI;

		printf("XY_T %7.4f %10.5f %10.5f %10.5f %10.6f %8.4f\n",
		       T, acc_e * inv_m, helicity, line,
		       (double)acc_vort * inv_m / (double)N,
		       (double)accepted / ((double)XY_SWEEPS * (double)N));
		fflush(stdout);
	}

#if XY_DUMP_FIELD
	/* Final configuration, for plotting the vortices. */
	printf("XY_FIELD L=%d\n", XY_L);
	for (int i = 0; i < XY_L; i++) {
		for (int j = 0; j < XY_L; j++) printf("%.4f ", th[IDX(i, j)]);
		printf("\n");
	}
#endif

	printf("XY_DONE\n");
	return 0;
}
