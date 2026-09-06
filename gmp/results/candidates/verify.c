/* Independent check of a reported chain: rebuild the primorial from scratch,
   construct each term directly, and test at far higher strength than the hunt
   used. Deliberately not sharing code with cc_hunt. */
#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
int main(int argc,char**argv){
    unsigned long m=strtoul(argv[1],0,10), lim=2357; int j=1,k=6,reps=256;
    mpz_t P,N; mpz_inits(P,N,NULL); mpz_set_ui(P,1);
    for(unsigned long n=2;n<=lim;n++){int pr=1;
        for(unsigned long d=2;d*d<=n;d++) if(n%d==0){pr=0;break;}
        if(pr) mpz_mul_ui(P,P,n);}
    printf("primorial 2357# has %zu digits\n", mpz_sizeinbase(P,10));
    int len=0;
    for(int i=0;i<k;i++){
        mpz_mul_ui(N,P,m); mpz_mul_2exp(N,N,j+i); mpz_sub_ui(N,N,1);
        int r=mpz_probab_prime_p(N,reps);
        printf("  N_%d (%zu digits): %s\n", i, mpz_sizeinbase(N,10),
               r==2?"definitely prime":r==1?"probably prime":"COMPOSITE");
        if(!r) break; len++;
    }
    printf("\nindependently confirmed length: %d (reps=%d)\n",len,reps);
    return 0;
}
