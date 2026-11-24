#include "render.hpp"
#include <iostream>
#include <random>
#include <cstdlib>
using namespace lcd;
void check(bool v){if(!v){std::cerr<<"test failed\n";std::exit(1);}}
void apply_packet(Frame& f,const Packet& p){if(!p.len)return;check(p.len>=4&&p.len<=1076);size_t i=0;while(i<p.len-4){check(i+6<=p.len-4);check(p.bytes[i]==5&&p.bytes[i+2]==6&&p.bytes[i+4]==8);int pg=p.bytes[i+1],x=p.bytes[i+3],n=p.bytes[i+5];i+=6;check(pg<8&&x<128&&n>0&&x+n<=128&&i+n<=p.len-4);for(int j=0;j<n;++j)f[pg*128+x+j]=p.bytes[i++];}check(i==p.len-4&&p.bytes[i]==3&&p.bytes[i+1]==2&&p.bytes[i+2]==2&&p.bytes[i+3]==3);}
int main(){Metrics m{18,43,27,300000,37500,12*86400+4*3600,"18:42"};auto f=render(m);Frame display{};auto full=encode(f,nullptr);check(full.len==1076);apply_packet(display,full);check(display==f);check(encode(f,&f).len==0);std::mt19937 rng(42);for(int i=0;i<3000;++i){Frame next{};if(i<101){m.cpu=m.ram=m.gpu=i;next=render(m);}else if(i<1000){m.cpu=int(rng()%102)-1;m.ram=int(rng()%102)-1;m.gpu=int(rng()%102)-1;m.rx=rng();m.tx=rng();next=render(m);}else {next=display;unsigned changes=rng()%1025;for(unsigned j=0;j<changes;++j)next[rng()%1024]=uint8_t(rng());}apply_packet(display,encode(next,&f));check(display==next);f=next;}std::cout<<"3000 differential frames passed\n";}
