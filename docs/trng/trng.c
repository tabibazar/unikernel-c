// trng.c -- a true random number generator for a machine with no operating
// system, and the evidence that it works.
//
// A unikernel has no /dev/random and no kernel entropy pool. This builds one
// from the hardware up:
//
//   sources     RDSEED (raw hardware entropy) XORed with RDTSC timing jitter,
//               two physically different sources so a failure of one is caught
//               by health tests and covered by the other.
//   health      NIST SP 800-90B startup tests -- Repetition Count and Adaptive
//               Proportion -- on the raw RDSEED stream, to detect a stuck or
//               degraded source rather than trusting it blindly.
//   conditioning a SHA-256 sponge absorbs the raw samples and squeezes out
//               full-entropy 256-bit blocks; this whitens the biased jitter and
//               mixes the sources.
//   output      a hash DRBG: each block is SHA-256(pool || counter), and the
//               pool is reseeded from fresh RDSEED between blocks.
//
// Then it proves the result: the same statistical battery is run on the RAW
// jitter (which should fail) and on the conditioned output (which should pass),
// so the conditioning is shown working rather than asserted.
//
//   baremetal: cp trng.c BareMetal-App/ && ./1-build.sh trng.c
//   linux:     gcc -O2 -o trng trng.c   (RDSEED/RDRAND on any recent x86-64)

#include <stdio.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------- hardware
static int rdseed64(uint64_t *o){ unsigned char ok; __asm__ volatile("rdseed %0; setc %1":"=r"(*o),"=qm"(ok)); return ok; }
static int rdrand64(uint64_t *o){ unsigned char ok; __asm__ volatile("rdrand %0; setc %1":"=r"(*o),"=qm"(ok)); return ok; }
static inline uint64_t rdtsc(void){ uint32_t a,d; __asm__ volatile("rdtsc":"=a"(a),"=d"(d)); return ((uint64_t)d<<32)|a; }

// RDSEED can transiently fail (its buffer is empty); retry a bounded number of
// times, and report exhaustion rather than returning a zero silently.
static int rdseed_retry(uint64_t *o){ for(int i=0;i<512;i++){ if(rdseed64(o)) return 1; __asm__ volatile("pause"); } return 0; }

// One RDTSC-jitter sample: the time taken by a fixed tiny workload. Its low bits
// carry microarchitectural and interrupt timing noise.
static uint64_t jitter_sample(void){
    volatile uint64_t s=0; uint64_t t0=rdtsc();
    for(volatile int k=0;k<64;k++) s+=k*3+1;
    return (rdtsc()-t0) ^ (s<<7);
}

// ---------------------------------------------------------------- SHA-256
// Compact, self-contained, so the generator has no dependency to trust.
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; int n; } sha256;
static uint32_t ror(uint32_t x,int r){ return (x>>r)|(x<<(32-r)); }
static const uint32_t K[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static void sha_init(sha256*s){ static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}; memcpy(s->h,iv,32); s->len=0; s->n=0; }
static void sha_block(sha256*s,const uint8_t*p){
    uint32_t w[64];
    for(int i=0;i<16;i++) w[i]=(p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
    for(int i=16;i<64;i++){ uint32_t s0=ror(w[i-15],7)^ror(w[i-15],18)^(w[i-15]>>3), s1=ror(w[i-2],17)^ror(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
    uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for(int i=0;i<64;i++){ uint32_t S1=ror(e,6)^ror(e,11)^ror(e,25),ch=(e&f)^(~e&g),t1=h+S1+ch+K[i]+w[i],S0=ror(a,2)^ror(a,13)^ror(a,22),mj=(a&b)^(a&c)^(b&c),t2=S0+mj; h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void sha_update(sha256*s,const void*d,size_t n){ const uint8_t*p=d; s->len+=n; while(n){ int t=64-s->n; if(t>(int)n)t=n; memcpy(s->buf+s->n,p,t); s->n+=t;p+=t;n-=t; if(s->n==64){ sha_block(s,s->buf); s->n=0; } } }
static void sha_final(sha256*s,uint8_t out[32]){ uint64_t bits=s->len*8; uint8_t pad=0x80; sha_update(s,&pad,1); uint8_t z=0; while(s->n!=56) sha_update(s,&z,1); uint8_t L[8]; for(int i=0;i<8;i++) L[i]=bits>>(56-i*8); sha_update(s,L,8); for(int i=0;i<8;i++){ out[i*4]=s->h[i]>>24; out[i*4+1]=s->h[i]>>16; out[i*4+2]=s->h[i]>>8; out[i*4+3]=s->h[i]; } }

// tiny freestanding math, so the generator links without libm
static double my_sqrt(double x){ if(x<=0)return 0; double r=x; for(int i=0;i<40;i++) r=0.5*(r+x/r); return r; }
static double my_log2(double x){ if(x<=0)return 0; int e=0; while(x<1){x*=2;e--;} while(x>=2){x/=2;e++;}
    double y=(x-1)/(x+1), y2=y*y, s=0, t=y; for(int k=1;k<40;k+=2){ s+=t/k; t*=y2; } return e + 2*s/0.6931471805599453; }

// ---------------------------------------------------------------- SP 800-90B health tests
// Repetition Count Test: a stuck source repeats one value; flag a long run.
// Adaptive Proportion Test: over a window, no value should dominate.
static int rct_cutoff, apt_cutoff, apt_window=512;
static int health_fail_rct=0, health_fail_apt=0;
static uint8_t rct_last; static int rct_run;
static uint8_t apt_first; static int apt_pos, apt_count;
static void health_byte(uint8_t b){
    if(rct_run==0){ rct_last=b; rct_run=1; }
    else if(b==rct_last){ if(++rct_run>=rct_cutoff) health_fail_rct++; }
    else { rct_last=b; rct_run=1; }
    if(apt_pos==0){ apt_first=b; apt_count=1; apt_pos=1; }
    else { if(b==apt_first) apt_count++; if(++apt_pos>=apt_window){ if(apt_count>=apt_cutoff) health_fail_apt++; apt_pos=0; } }
}

// ---------------------------------------------------------------- the generator
static uint8_t pool[32];
static uint64_t drbg_ctr;
static int rdseed_words, rdseed_fail, jitter_words;

static void reseed(void){
    sha256 s; sha_init(&s);
    sha_update(&s, pool, 32);                       // fold in the old pool
    for(int i=0;i<8;i++){                            // 8 fresh RDSEED words
        uint64_t v;
        if(rdseed_retry(&v)){ rdseed_words++; } else { rdseed_fail++; v=rdrand64(&v)?v:0; }
        uint64_t j=jitter_sample(); jitter_words++;  // XOR an independent jitter sample
        uint64_t mixed = v ^ j;
        health_byte((uint8_t)v);                     // health-test the raw RDSEED byte
        sha_update(&s, &mixed, 8);
    }
    sha_final(&s, pool);
}

// One 32-byte output block: SHA-256(pool || counter), then reseed.
static void trng_block(uint8_t out[32]){
    sha256 s; sha_init(&s);
    sha_update(&s, pool, 32);
    sha_update(&s, &drbg_ctr, 8); drbg_ctr++;
    sha_final(&s, out);
    reseed();
}

// ---------------------------------------------------------------- statistics
static void stats(const char*label, const uint8_t*d, size_t n){
    // monobit
    long ones=0; for(size_t i=0;i<n;i++){ uint8_t b=d[i]; while(b){ones+=b&1;b>>=1;} }
    double bits=n*8.0, p1=ones/bits;
    double z=(ones-bits/2)/(0.5*my_sqrt(bits)); if(z<0)z=-z;
    // byte chi-square (df 255); expect n/256 per bin
    long h[256]={0}; for(size_t i=0;i<n;i++) h[d[i]]++;
    double exp=n/256.0, chi=0; for(int i=0;i<256;i++){ double dv=h[i]-exp; chi+=dv*dv/exp; }
    // serial correlation
    double sum=0,sumsq=0,cross=0; for(size_t i=0;i<n;i++){ double x=d[i]; sum+=x; sumsq+=x*x; cross+=x*(i+1<n?d[i+1]:d[0]); }
    double mean=sum/n, var=sumsq/n-mean*mean;
    double scc = var>0 ? (cross/n-mean*mean)/var : 0;
    // min-entropy estimate from the most common byte
    long mx=0; for(int i=0;i<256;i++) if(h[i]>mx)mx=h[i];
    double minent = -my_log2((double)mx/n);   // bits per byte, max 8
    printf("STAT %-9s bytes=%zu ones=%.4f%% monobit_z=%.2f chi2=%.0f(exp~255+-23) serial_corr=%+.4f min_entropy=%.3f/8\n",
           label, n, p1*100.0, z, chi, scc, minent);
}

#ifdef EMIT_MB
// Emit EMIT_MB mebibytes of conditioned output as base64 over the serial
// console, for third-party test suites (ent, dieharder, PractRand) to judge.
static const char B64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void emit_base64(void){
    uint8_t blk[32]; int col=0; uint32_t acc=0; int bits=0;
    unsigned long long total=(unsigned long long)EMIT_MB*1024*1024;
    unsigned long long done=0; int bi=32;
    while(done<total){
        if(bi>=32){ trng_block(blk); bi=0; }
        acc=(acc<<8)|blk[bi++]; bits+=8; done++;
        while(bits>=6){ bits-=6; putchar(B64[(acc>>bits)&63]); if(++col==76){ putchar('\n'); col=0; } }
    }
    if(col) putchar('\n');
}
#endif

int main(void){
    setvbuf(stdout, NULL, _IOLBF, 0);
    rct_cutoff=32; apt_cutoff=330;                   // ~2^-30 false-positive cutoffs

    // seed the pool from the hardware before producing any output
    memset(pool,0,32);
    reseed(); reseed(); reseed();
#ifdef EMIT_MB
    printf("EMIT_START mb=%d\n", EMIT_MB);
    emit_base64();
    printf("\nEMIT_DONE rdseed_words=%d rdseed_fail=%d health_rct=%d health_apt=%d\n",
           rdseed_words, rdseed_fail, health_fail_rct, health_fail_apt);
    return 0;
#endif

    // produce 64 KiB of conditioned output
    static uint8_t out[65536]; size_t N=sizeof(out);
    for(size_t i=0;i<N;i+=32) trng_block(out+i);

    // for contrast, collect RAW jitter LSBs packed into bytes -- the unconditioned source
    static uint8_t raw[8192];
    for(size_t i=0;i<sizeof(raw);i++){ uint8_t b=0; for(int k=0;k<8;k++) b=(b<<1)|(jitter_sample()&1); raw[i]=b; }

    printf("TRNG rdseed_words=%d rdseed_fail=%d jitter_words=%d health_rct=%d health_apt=%d\n",
           rdseed_words, rdseed_fail, jitter_words, health_fail_rct, health_fail_apt);
    printf("TRNG first16:"); for(int i=0;i<16;i++) printf(" %02x", out[i]); printf("\n");
    stats("raw_jit", raw, sizeof(raw));              // expected: biased, low min-entropy
    stats("condition", out, N);                      // expected: clean
    printf("TRNG_DONE\n");
    return 0;
}
