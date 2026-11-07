#include "../Source/Dsp.h"
#include <cstdio>
using namespace lr;
static double cascadeDb(bool hi,int type,int slope,double fs,double fc,double f){Coef c[2];int n=designPass(hi,type,slope,fs,fc,c);double d=0;for(int i=0;i<n;++i)d+=magnitudeDb(c[i],fs,f);return d;}
int main(){
  const double fs=48000, fc=1000; const char* tn[]={"BW","LR","BS"}; int bad=0;
  for(int hi=0;hi<2;++hi) for(int t=0;t<3;++t) for(int s=0;s<3;++s){
    double at=cascadeDb(hi,t,s,fs,fc,fc), dec=cascadeDb(hi,t,s,fs,fc,hi?fc/4:fc*4)-cascadeDb(hi,t,s,fs,fc,hi?fc/8:fc*8);
    double slope=-dec/1.0; // dB per octave measured between 2 octaves-ish points (1 octave apart)
    double pass=cascadeDb(hi,t,s,fs,fc,hi?fc*16:fc/16);
    printf("%s %s slope%d: @fc=%.2f dB  passband=%.3f dB  asympt=%.1f dB/oct\n",hi?"HPF":"LPF",tn[t],(s+2)*6,at,pass,hi?-dec*-1:dec*-1);
    double expect=(t==1)?-6.02:-3.01; if(fabs(at-expect)>0.45){printf("  ** corner mismatch (expect %.2f)\n",expect);bad++;}
    if(fabs(pass)>0.2){printf("  ** passband not flat\n");bad++;}
  }
  // peaking check
  Coef p=peaking(fs,2000,6,1.0); printf("peak 2k +6: %.3f dB (expect 6), at 20Hz %.3f\n",magnitudeDb(p,fs,2000),magnitudeDb(p,fs,20));
  // comp curve: T=-24 R=4 K=6
  printf("comp out(-60)=%.2f out(-24)=%.2f out(0)=%.2f (expect -60,~-25.1? ,-18)\n",Compressor::outDb(-60,-24,4,6),Compressor::outDb(-24,-24,4,6),Compressor::outDb(0,-24,4,6));
  // dynamic: 1kHz sine at -6 dBFS steady -> GR converge
  Compressor c; c.setup(fs,5,100); double g=0; for(int i=0;i<48000;++i) g=c.process(0.5,-24,4,6);
  double exp_gr=(-6)-Compressor::outDb(-6.0206,-24,4,6); printf("steady GR=%.3f dB expect %.3f\n",g,exp_gr);
  // IIR stability / impulse
  Coef cc[2]; int n=designPass(false,2,2,fs,20000,cc); BiquadState st[2]; double y=0,acc=0; for(int i=0;i<4000;++i){y=i==0?1:0;for(int k=0;k<n;++k)y=run(cc[k],st[k],y);acc+=fabs(y);} printf("stable tail |y|=%g\n",y);
  printf("%s\n",bad?"FAIL":"ALL OK"); return bad;}
