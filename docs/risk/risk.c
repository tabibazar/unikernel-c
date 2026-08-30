// risk.c -- a Monte Carlo derivatives risk engine, on a unikernel, driven by
// the machine's own hardware randomness.
//
// It prices a European call option by simulating thousands of random price
// paths and averaging the payoff -- and checks itself against the Black-Scholes
// closed form, which is the exact answer for this instrument. If the simulation
// lands on the analytic price within its error bar, the engine is correct.
//
// Then it does what closed forms cannot: the Value-at-Risk and Expected
// Shortfall of a position, straight from the simulated loss distribution.
//
// Two quant techniques are shown honestly:
//   - antithetic variates: pairing each random draw Z with -Z halves the
//     variance for free, so the error bar shrinks without more samples.
//   - pathwise Greeks: the option's delta comes out of the same simulation.
//
// Every Gaussian shock is built from RDRAND, the hardware source proven clean
// in the entropy study. exp, ln, sqrt and erf are hand-rolled; a freestanding
// unikernel has no math library.

#include <stdio.h>
#include <stdint.h>

static inline int rdrand64(uint64_t *o){ unsigned char ok; __asm__ volatile("rdrand %0; setc %1":"=r"(*o),"=qm"(ok)); return ok; }
static inline uint64_t rnd(void){ uint64_t v; while(!rdrand64(&v)){} return v; }
static inline double urand(void){ return (rnd() >> 11) * (1.0/9007199254740992.0); }

static double my_sqrt(double x){ if(x<=0)return 0; double r=x; for(int i=0;i<50;i++) r=0.5*(r+x/r); return r; }
static double my_exp(double x){ const double ln2=0.6931471805599453; int k=(int)(x/ln2+(x<0?-0.5:0.5)); double r=x-k*ln2;
    double e=1+r*(1+r*(0.5+r*(1/6.0+r*(1/24.0+r*(1/120.0+r*(1/720.0+r/5040.0)))))); double p=1;
    if(k>=0) for(int i=0;i<k;i++)p*=2; else for(int i=0;i<-k;i++)p*=0.5; return e*p; }
static double my_ln(double x){ if(x<=0)return -1e300; int e=0; while(x>=2){x*=0.5;e++;} while(x<1){x*=2;e--;}
    double y=(x-1)/(x+1), y2=y*y, s=0, t=y; for(int k=1;k<60;k+=2){ s+=t/k; t*=y2; } return e*0.6931471805599453 + 2*s; }
// standard normal CDF via erf (Abramowitz-Stegun 7.1.26)
static double norm_cdf(double x){ double s=x<0?-1:1; x=(x<0?-x:x)/1.4142135623730951;
    double t=1/(1+0.3275911*x);
    double y=1-(((((1.061405429*t-1.453152027)*t)+1.421413741)*t-0.284496736)*t+0.254829592)*t*my_exp(-x*x);
    return 0.5*(1+s*y); }

// One standard normal via Marsaglia's polar method -- no trig needed.
static double g_spare; static int g_have;
static double gauss(void){ if(g_have){ g_have=0; return g_spare; }
    double u,v,s; do{ u=2*urand()-1; v=2*urand()-1; s=u*u+v*v; }while(s>=1||s==0);
    double f=my_sqrt(-2*my_ln(s)/s); g_spare=v*f; g_have=1; return u*f; }

// numeric comparison for the VaR percentile
static void isort(double *a, int n){ for(int i=1;i<n;i++){ double k=a[i]; int j=i-1; while(j>=0&&a[j]>k){a[j+1]=a[j];j--;} a[j+1]=k; } }

int main(void){
    setvbuf(stdout, NULL, _IOLBF, 0);
    // Instrument: 1y at-the-money European call. (In the full engine these come
    // from live market data fetched over HTTPS and parsed by the LLM.)
    double S0=100, K=100, r=0.05, sigma=0.20, T=1.0;

    // Black-Scholes closed form -- the exact answer to validate against.
    double d1=(my_ln(S0/K)+(r+0.5*sigma*sigma)*T)/(sigma*my_sqrt(T));
    double d2=d1-sigma*my_sqrt(T);
    double bs = S0*norm_cdf(d1) - K*my_exp(-r*T)*norm_cdf(d2);
    double bs_delta = norm_cdf(d1);
    printf("RISK_START S0=%.0f K=%.0f r=%.2f vol=%.2f T=%.1f\n", S0,K,r,sigma,T);
    printf("BLACK_SCHOLES call=%.4f delta=%.4f\n", bs, bs_delta);

    long N=8000000;                 // 8M path-pairs
    double drift=(r-0.5*sigma*sigma)*T, vol=sigma*my_sqrt(T), disc=my_exp(-r*T);

    // --- plain MC vs antithetic MC, same number of random draws
    double sum_p=0,sum_p2=0, sum_a=0,sum_a2=0, sum_delta=0;
    for(long i=0;i<N;i++){
        double z=gauss();
        double sp=S0*my_exp(drift+vol*z), sm=S0*my_exp(drift-vol*z);
        double pp=(sp>K?sp-K:0), pm=(sm>K?sm-K:0);
        double plain=disc*pp;                        // one estimate, no antithetic
        double anti =disc*0.5*(pp+pm);               // antithetic pair
        sum_p+=plain; sum_p2+=plain*plain;
        sum_a+=anti;  sum_a2+=anti*anti;
        sum_delta += disc*0.5*((sp>K?sp/S0:0)+(sm>K?sm/S0:0));   // pathwise delta
    }
    double mp=sum_p/N, sep=my_sqrt((sum_p2/N-mp*mp)/N);
    double ma=sum_a/N, sea=my_sqrt((sum_a2/N-ma*ma)/N);
    printf("MC_PLAIN      call=%.4f +- %.4f  (err vs BS %+.4f)\n", mp, sep, mp-bs);
    printf("MC_ANTITHETIC call=%.4f +- %.4f  (err vs BS %+.4f)  variance_cut=%.1fx\n",
           ma, sea, ma-bs, (sep*sep)/(sea*sea));
    printf("MC_DELTA      delta=%.4f (BS %.4f)\n", sum_delta/N, bs_delta);

    // --- Value-at-Risk / Expected Shortfall of holding the underlying,
    // 10-day horizon, from simulated returns. The textbook risk number.
    int M=200000; static double pnl[200000];
    double h=10.0/252.0, hdrift=(r-0.5*sigma*sigma)*h, hvol=sigma*my_sqrt(h), notional=1000000;
    for(int i=0;i<M;i++){ double z=gauss(); double ST=S0*my_exp(hdrift+hvol*z); pnl[i]=notional*(ST/S0-1.0); }
    isort(pnl,M);
    double var95=-pnl[(int)(0.05*M)], var99=-pnl[(int)(0.01*M)];
    double es99=0; int ne=(int)(0.01*M); for(int i=0;i<ne;i++) es99-=pnl[i]; es99/=ne;
    printf("VAR notional=%.0f horizon=10d  VaR95=%.0f  VaR99=%.0f  ES99=%.0f\n", notional, var95, var99, es99);

    // --- histogram of the 10-day P&L for the chart (40 bins, +-4 sigma)
    double lo=-4*hvol*notional, hi=4*hvol*notional; int B=40; long hist[40]={0};
    for(int i=0;i<M;i++){ int b=(int)((pnl[i]-lo)/(hi-lo)*B); if(b<0)b=0; if(b>=B)b=B-1; hist[b]++; }
    printf("HIST lo=%.0f hi=%.0f bins=%d:", lo, hi, B);
    for(int i=0;i<B;i++) printf(" %ld", hist[i]);
    printf("\n");
    printf("RISK_DONE\n");
    return 0;
}
