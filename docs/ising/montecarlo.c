// montecarlo.c -- volumes by Monte Carlo, using the machine's own hardware
// randomness, on a unikernel with no operating system.
//
// The random numbers come from RDRAND, the CPU's hardware generator, which was
// shown clean on this platform in the entropy study (50.04% ones, no stuck
// values, passes ent / FIPS 140-2). Each 53-bit draw becomes a double in [0,1).
//
// Three targets, chosen so the method can be checked before it is trusted:
//   pi           2D: the fraction of a square covered by its inscribed disk.
//   unit sphere  3D: known volume 4/3 pi, so the estimate can be graded.
//   metaball     3D: an irregular blob with no closed-form volume -- the actual
//                point of Monte Carlo, reported with a statistical error bar.
//
// A Monte Carlo estimate of a fraction p over N samples has standard error
// sqrt(p(1-p)/N); the volume error is the box volume times that. It shrinks
// like 1/sqrt(N), which is why the counts below climb into the hundreds of
// millions.

#include <stdio.h>
#include <stdint.h>

static inline int rdrand64(uint64_t *o){ unsigned char ok; __asm__ volatile("rdrand %0; setc %1":"=r"(*o),"=qm"(ok)); return ok; }
static inline uint64_t rnd(void){ uint64_t v; while(!rdrand64(&v)){} return v; }
// 53 random bits -> a double uniformly in [0,1)
static inline double urand(void){ return (rnd() >> 11) * (1.0/9007199254740992.0); }
static double dsqrt(double x){ if(x<=0)return 0; double r=x; for(int i=0;i<40;i++) r=0.5*(r+x/r); return r; }

// An irregular solid: four metaball centres with different weights, inside
// where the summed 1/r^2 field exceeds a threshold. No symmetry, no formula.
static int in_metaball(double x, double y, double z){
    static const double cx[4]={ 0.35,-0.40, 0.10,-0.20};
    static const double cy[4]={ 0.30, 0.25,-0.45, 0.05};
    static const double cz[4]={-0.15, 0.20, 0.30,-0.35};
    static const double w [4]={ 0.55, 0.50, 0.45, 0.40};
    double f=0;
    for(int i=0;i<4;i++){ double dx=x-cx[i],dy=y-cy[i],dz=z-cz[i]; f += w[i]/(dx*dx+dy*dy+dz*dz+0.02); }
    return f > 6.0;
}

int main(void){
    setvbuf(stdout, NULL, _IOLBF, 0);
    const double PI_TRUE = 3.14159265358979324;
    const double SPH_TRUE = 4.18879020478639098;   // 4/3 pi
    uint64_t pin=0, sin_=0, min_=0;                 // hit counts
    uint64_t N = 400000000ULL;                      // 400M samples

    printf("MC_START samples=%llu source=rdrand\n", (unsigned long long)N);
    for(uint64_t i=1;i<=N;i++){
        // one point for pi (unit square -> disk), one for the 3D box [-1,1]^3
        double a=urand(), b=urand();
        if(a*a+b*b <= 1.0) pin++;

        double x=2*urand()-1, y=2*urand()-1, z=2*urand()-1;
        if(x*x+y*y+z*z <= 1.0) sin_++;
        if(in_metaball(x,y,z)) min_++;

        if(i==1000000ULL || i==10000000ULL || i==100000000ULL || i==N){
            double pi = 4.0*(double)pin/(double)i;
            double sph = 8.0*(double)sin_/(double)i;
            double mv  = 8.0*(double)min_/(double)i;
            double ps=(double)sin_/i, pm=(double)min_/i;
            double sph_se = 8.0*dsqrt(ps*(1-ps)/i);
            double mv_se  = 8.0*dsqrt(pm*(1-pm)/i);
            printf("MC n=%llu  pi=%.6f (err %+.2e)  sphere=%.6f+-%.6f (true %.6f)  metaball=%.6f+-%.6f\n",
                   (unsigned long long)i, pi, pi-PI_TRUE, sph, sph_se, SPH_TRUE, mv, mv_se);
        }
    }
    printf("MC_DONE\n");
    return 0;
}
