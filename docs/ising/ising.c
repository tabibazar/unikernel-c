// ising.c -- the 2D Ising model by Markov Chain Monte Carlo, on a unikernel,
// driven by the CPU's hardware randomness.
//
// A grid of magnetic spins, each +1 or -1, each wanting to align with its four
// neighbours. At low temperature they order into one direction (magnetised); at
// high temperature thermal noise wins and they disorder. In between is a phase
// transition -- and Lars Onsager solved this exactly in 1944, placing it at
//
//     Tc = 2 / ln(1 + sqrt(2)) = 2.269185...
//
// That exact number is the ground truth. Plain Monte Carlo cannot do this: you
// cannot sample spin configurations uniformly, because the physical ones are
// exponentially weighted by energy. Metropolis-Hastings can -- it walks a
// Markov chain that visits each configuration in proportion to its Boltzmann
// weight, proposing a spin flip and accepting it with probability
// min(1, exp(-dE/T)). Every one of those accept/reject coin flips, and every
// site it visits, comes from RDRAND.

#include <stdio.h>
#include <stdint.h>

#define L    64
#define N    (L*L)
#define MASK (N-1)          // N is a power of two, so site = rand & MASK
#define LMASK (L-1)

static signed char s[N];

static inline int rdrand64(uint64_t *o){ unsigned char ok; __asm__ volatile("rdrand %0; setc %1":"=r"(*o),"=qm"(ok)); return ok; }
static inline uint64_t rnd(void){ uint64_t v; while(!rdrand64(&v)){} return v; }
static inline double urand(void){ return (rnd() >> 11) * (1.0/9007199254740992.0); }

static double my_exp(double x){
    const double ln2=0.6931471805599453;
    int k=(int)(x/ln2 + (x<0?-0.5:0.5));
    double r=x-k*ln2;
    double e=1+r*(1+r*(0.5+r*(1/6.0+r*(1/24.0+r*(1/120.0+r/720.0)))));
    double p=1; if(k>=0) for(int i=0;i<k;i++) p*=2; else for(int i=0;i<-k;i++) p*=0.5;
    return e*p;
}
static double dabs(double x){ return x<0?-x:x; }
static double dsqrt(double x){ if(x<=0)return 0; double r=x; for(int i=0;i<40;i++) r=0.5*(r+x/r); return r; }

// One Metropolis sweep: N attempted flips. Only dE in {+4,+8} need a random
// accept; dE<=0 always flips. The two Boltzmann factors are precomputed per T.
static void sweep(double e4, double e8){
    for(int a=0;a<N;a++){
        int i = (int)(rnd() & MASK);
        int r = i>>6, c = i&LMASK;
        int nb = s[((r-1)&LMASK)*L+c] + s[((r+1)&LMASK)*L+c]
               + s[r*L+((c-1)&LMASK)] + s[r*L+((c+1)&LMASK)];
        int dE = 2*s[i]*nb;                 // in {-8,-4,0,4,8}
        if(dE<=0) s[i]=-s[i];
        else { double p=(dE==4)?e4:e8; if(urand()<p) s[i]=-s[i]; }
    }
}

static double magnetisation(void){ long m=0; for(int i=0;i<N;i++) m+=s[i]; return dabs((double)m)/N; }

// Onsager's exact magnetisation, for the overlay. Zero above Tc.
static double onsager_M(double T){
    double Tc=2.269185; if(T>=Tc) return 0;
    double sh=0.5*(my_exp(2.0/T)-my_exp(-2.0/T));   // sinh(2/T)
    double x=1.0/(sh*sh*sh*sh);
    double v=1.0-x; if(v<0) v=0;
    // v^(1/8)
    double r=v; for(int i=0;i<3;i++){ double g=r; for(int j=0;j<40;j++) g=g-(g*g-r)/(2*g+1e-30); r=dsqrt(dsqrt(dsqrt(v))); break; }
    return r;
}

int main(void){
    setvbuf(stdout, NULL, _IOLBF, 0);
    for(int i=0;i<N;i++) s[i]=1;             // cold start, all aligned
    printf("ISING_START L=%d Tc_exact=2.269185\n", L);

    // Sweep temperature through the transition.
    for(int t=0;t<=44;t++){
        double T=1.2 + t*0.05;               // 1.2 .. 3.4
        double e4=my_exp(-4.0/T), e8=my_exp(-8.0/T);
        for(int w=0;w<1500;w++) sweep(e4,e8);            // thermalise
        double M=0, M2=0; int samp=2500;
        for(int w=0;w<samp;w++){ sweep(e4,e8); double m=magnetisation(); M+=m; M2+=m*m; }
        M/=samp; M2/=samp;
        double chi = N*(M2-M*M)/T;           // magnetic susceptibility (peaks at Tc)
        printf("ISING T=%.3f M=%.4f onsager=%.4f chi=%.2f\n", T, M, onsager_M(T), chi);
    }

    // Snapshot the lattice at three temperatures for the pictures.
    int temps3[3] = {0,0,0}; double Ts[3]={1.6,2.27,3.2};
    for(int k=0;k<3;k++){
        for(int i=0;i<N;i++) s[i]= (urand()<0.5)?1:-1;   // fresh disordered start
        double T=Ts[k], e4=my_exp(-4.0/T), e8=my_exp(-8.0/T);
        for(int w=0;w<4000;w++) sweep(e4,e8);
        printf("GRID T=%.2f\n", T);
        for(int r=0;r<L;r++){ for(int c=0;c<L;c++) putchar(s[r*L+c]>0?'1':'0'); putchar('\n'); }
        (void)temps3;
    }
    printf("ISING_DONE\n");
    return 0;
}
