// risk.c -- Monte Carlo derivatives pricing and risk, on a unikernel, with the
// full teaching set: sample price paths, convergence to the exact price, the
// Greeks, and the loss distribution behind Value-at-Risk.
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
static double norm_pdf(double x){ return 0.3989422804014327*my_exp(-0.5*x*x); }
static double norm_cdf(double x){ double s=x<0?-1:1; x=(x<0?-x:x)/1.4142135623730951; double t=1/(1+0.3275911*x);
    double y=1-(((((1.061405429*t-1.453152027)*t)+1.421413741)*t-0.284496736)*t+0.254829592)*t*my_exp(-x*x);
    return 0.5*(1+s*y); }
static double g_spare; static int g_have;
static double gauss(void){ if(g_have){ g_have=0; return g_spare; }
    double u,v,s; do{ u=2*urand()-1; v=2*urand()-1; s=u*u+v*v; }while(s>=1||s==0);
    double f=my_sqrt(-2*my_ln(s)/s); g_spare=v*f; g_have=1; return u*f; }
static void isort(double *a,int n){ for(int i=1;i<n;i++){ double k=a[i]; int j=i-1; while(j>=0&&a[j]>k){a[j+1]=a[j];j--;} a[j+1]=k; } }

int main(void){
    setvbuf(stdout, NULL, _IOLBF, 0);
    double S0=100,K=100,r=0.05,sig=0.20,T=1.0;
    double srt=my_sqrt(T), d1=(my_ln(S0/K)+(r+0.5*sig*sig)*T)/(sig*srt), d2=d1-sig*srt;
    double bs=S0*norm_cdf(d1)-K*my_exp(-r*T)*norm_cdf(d2);
    printf("RISK_START S0=%.0f K=%.0f r=%.2f vol=%.2f T=%.1f\n",S0,K,r,sig,T);
    // full Black-Scholes Greeks (exact)
    double delta=norm_cdf(d1), gamma=norm_pdf(d1)/(S0*sig*srt), vega=S0*norm_pdf(d1)*srt/100.0;
    double theta=(-(S0*norm_pdf(d1)*sig)/(2*srt)-r*K*my_exp(-r*T)*norm_cdf(d2))/365.0;
    double rho=K*T*my_exp(-r*T)*norm_cdf(d2)/100.0;
    printf("BS call=%.4f delta=%.4f gamma=%.4f vega=%.4f theta=%.4f rho=%.4f\n",bs,delta,gamma,vega,theta,rho);

    // sample GBM paths (8 paths, 52 weekly steps) -- for the illustration
    double dt=T/52, mu=(r-0.5*sig*sig)*dt, vd=sig*my_sqrt(dt);
    for(int p=0;p<8;p++){ double S=S0; printf("PATH"); printf(" %.2f",S);
        for(int w=0;w<52;w++){ S*=my_exp(mu+vd*gauss()); printf(" %.2f",S); } printf("\n"); }

    // convergence of MC price to Black-Scholes as samples grow
    double drift=(r-0.5*sig*sig)*T, vol=sig*srt, disc=my_exp(-r*T);
    long chk[5]={1000,10000,100000,1000000,8000000}; int ci=0;
    double sum=0,sum2=0, dsum=0, vsum=0;
    long Nmax=8000000;
    for(long i=1;i<=Nmax;i++){
        double z=gauss(), sp=S0*my_exp(drift+vol*z), sm=S0*my_exp(drift-vol*z);
        double pp=(sp>K?sp-K:0), pm=(sm>K?sm-K:0), est=disc*0.5*(pp+pm);
        sum+=est; sum2+=est*est;
        dsum+=disc*0.5*((sp>K?sp/S0:0)+(sm>K?sm/S0:0));            // pathwise delta
        vsum+=disc*0.5*((sp>K?sp*(srt*z - sig*T):0)+(sm>K?sm*(srt*(-z) - sig*T):0)); // pathwise vega (per 1.0 vol)
        if(ci<5 && i==chk[ci]){ double m=sum/i, se=my_sqrt((sum2/i-m*m)/i);
            printf("CONV n=%ld price=%.4f se=%.4f err=%+.4f\n", i, m, se, m-bs); ci++; }
    }
    double m=sum/Nmax; printf("MC_FINAL price=%.4f delta=%.4f vega=%.4f (BS delta=%.4f vega=%.4f)\n",
        m, dsum/Nmax, vsum/Nmax/100.0, delta, vega);

    // VaR / ES from the 10-day loss distribution, $1M underlying position
    int M=200000; static double pnl[200000]; double h=10.0/252,hd=(r-0.5*sig*sig)*h,hv=sig*my_sqrt(h),nz=1000000;
    for(int i=0;i<M;i++){ double ST=S0*my_exp(hd+hv*gauss()); pnl[i]=nz*(ST/S0-1); }
    isort(pnl,M);
    double v95=-pnl[(int)(0.05*M)], v99=-pnl[(int)(0.01*M)], es=0; int ne=(int)(0.01*M);
    for(int i=0;i<ne;i++) es-=pnl[i]; es/=ne;
    printf("VAR VaR95=%.0f VaR99=%.0f ES99=%.0f\n",v95,v99,es);
    double lo=-4*hv*nz,hi=4*hv*nz; int B=40; long hist[40]={0};
    for(int i=0;i<M;i++){ int b=(int)((pnl[i]-lo)/(hi-lo)*B); if(b<0)b=0; if(b>=B)b=B-1; hist[b]++; }
    printf("HIST lo=%.0f hi=%.0f:",lo,hi); for(int i=0;i<B;i++) printf(" %ld",hist[i]); printf("\n");
    printf("RISK_DONE\n");
    return 0;
}
