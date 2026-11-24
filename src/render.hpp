#pragma once
#include "font.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdint>
namespace lcd {
using Frame = std::array<uint8_t,1024>;
struct Metrics { double cpu=-1, ram=-1, gpu=-1, rx=-1, tx=-1; uint64_t uptime=0; char clock[6]="00:00"; };
inline int rounded(double v) { return static_cast<int>(std::nearbyint(v)); }
inline double clamp(double v) { return std::max(0.0,std::min(100.0,v)); }
inline void pixel(Frame& f,int x,int y) { if(x>=0 && x<128 && y>=0 && y<64) f[(y/8)*128+x]|=uint8_t(1u<<(y%8)); }
inline void rect(Frame& f,int x,int y,int w,int h,bool fill=false) { for(int j=0;j<h;++j) for(int i=0;i<w;++i) if(fill||i==0||i==w-1||j==0||j==h-1) pixel(f,x+i,y+j); }
inline int textwidth(const char* s) { return *s?int(strlen(s))*8-2:0; }
inline void text(Frame& f,int x,int y,const char* s,int scale=1) { for(;*s;++s) { auto ch=static_cast<unsigned char>(*s); const auto& g=font[ch<128?ch:63]; for(int i=0;i<6;++i) for(int j=0;j<7;++j) if(g[i]&(1u<<j)) rect(f,x+scale*i,y+scale*j,scale,scale,true); x+=6*scale+2; } }
inline void right(Frame& f,int x,int y,const char* s) { text(f,x-textwidth(s),y,s); }
inline void rate(char (&s)[8],double v) { if(!std::isfinite(v)||v<0) { strcpy(s,"--"); return; } v*=8; const double scales[]={1e9,1e6,1e3}; const char units[]="GMK"; for(int i=0;i<3;++i) if(v>=scales[i]*.9995) { double n=v/scales[i]; if(n<9.95) { snprintf(s,sizeof s,"%.1f%c",n,units[i]); return; } if(n<999.5) { snprintf(s,sizeof s,"%.0f%c",n,units[i]); return; } } if(v<1000) snprintf(s,sizeof s,"%dB",std::min(999,rounded(v))); else strcpy(s,"999G"); }
inline Frame render(const Metrics& m) { Frame f{}; rect(f,42,1,1,44,true); rect(f,85,1,1,44,true); const char* labels[]={"CPU","RAM","GPU"}; const double vals[]={m.cpu,m.ram,m.gpu}; const int lefts[]={0,43,86}; for(int i=0;i<3;++i) { int l=lefts[i]; double v=vals[i]; bool valid=std::isfinite(v)&&v>=0; text(f,l+(42-textwidth(labels[i]))/2,2,labels[i]); char s[5]; if(valid) snprintf(s,sizeof s,"%d",rounded(clamp(v))); else strcpy(s,"--"); text(f,l+(42-(14*int(strlen(s))-2))/2,13,s,2); text(f,l+(42-6)/2,30,"%"); rect(f,l+3,39,36,5); if(valid) rect(f,l+4,40,rounded(34*clamp(v)/100),3,true); } char r[8],t[8],line[32]; rate(r,m.rx); rate(t,m.tx); snprintf(line,sizeof line,"RX %s",r); text(f,1,47,line); snprintf(line,sizeof line,"TX %s",t); right(f,127,47,line); rect(f,1,55,126,1,true); auto days=m.uptime/86400; if(days) snprintf(line,sizeof line,"UP %lluD %02lluH",(unsigned long long)days,(unsigned long long)(m.uptime/3600%24)); else snprintf(line,sizeof line,"UP %02lluH %02lluM",(unsigned long long)(m.uptime/3600),(unsigned long long)(m.uptime/60%60)); if(strlen(line)>10) snprintf(line,sizeof line,"UP %lluD",(unsigned long long)days); if(strlen(line)>10) strcpy(line,"UP ?"); text(f,1,57,line); right(f,127,57,m.clock); return f; }
struct Packet { std::array<uint8_t,1076> bytes{}; size_t len=0; void add(uint8_t b){bytes[len++]=b;} };
inline Packet encode(const Frame& f,const Frame* prev) { Packet o; for(int p=0;p<8;++p) { int a[128],b[128],n=0,cost=0; for(int x=0;x<128;++x) if(!prev||f[p*128+x]!=(*prev)[p*128+x]) { if(n&&x-b[n-1]<=7) b[n-1]=x; else {a[n]=b[n]=x;++n;} } for(int i=0;i<n;++i) cost+=b[i]-a[i]+7; if(cost>=134){n=1;a[0]=0;b[0]=127;} for(int i=0;i<n;++i){o.add(5);o.add(uint8_t(p));o.add(6);o.add(uint8_t(a[i]));o.add(8);o.add(uint8_t(b[i]-a[i]+1));for(int x=a[i];x<=b[i];++x)o.add(f[p*128+x]);} } if(o.len) for(auto b:{3,2,2,3}) o.add(uint8_t(b)); return o; }
}
