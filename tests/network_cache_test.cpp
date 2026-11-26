#include "network_cache.hpp"
#include <cstdlib>
#include <iostream>
using lcd::NetRow;
void require(bool ok){if(!ok)std::exit(1);}
int main(){
    lcd::NetworkCache cache;
    std::vector<NetRow> live{{1,100,200,true,true},{2,50,50,true,false},{3,1000,1000,false,true}};
    int scans=0,reads=0;bool scan_ok=true,entry_ok=true;
    auto scan=[&](std::vector<NetRow>& out){++scans;if(!scan_ok)return false;out=live;return true;};
    auto read=[&](uint64_t id,NetRow& out){
        ++reads;if(!entry_ok)return false;
        for(auto r:live)if(r.id==id){out=r;return true;}return false;
    };
    auto sample=[&](uint64_t t){return cache.sample(t,scan,read);};
    require(sample(100).rx==-1&&scans==1&&reads==0);
    live[0].rx+=100;live[0].tx+=200;live[2].rx+=9999;
    auto r=sample(600);require(r.rx==200&&r.tx==400&&scans==1&&reads==2);
    live[1].up=true;live[1].rx+=100;live[0].rx+=50;
    require(sample(1100).rx==100); // first up sample is a baseline
    live[0].up=false;live[1].rx+=75;
    require(sample(1600).rx==150); // down interface excluded immediately
    live[1].rx=1;require(sample(2100).rx==-1); // counter reset
    live[1].rx+=100;live.push_back({4,200,200,true,true});
    require(sample(2600).rx==200&&scans==1);
    require(sample(5100).rx==0&&scans==2); // new interface discovered, baseline only
    live[3].rx+=50;require(sample(5600).rx==100);
    live.erase(live.begin()); // disappeared cached ID: full same-cycle fallback
    live[2].rx+=50;require(sample(6100).rx==100&&scans==3);
    entry_ok=false;scan_ok=false;require(sample(6600).rx==-1&&scans==4);
    entry_ok=scan_ok=true;require(sample(7100).rx==-1&&scans==5);
    live.clear();require(sample(12100).rx==-1&&scans==6);
    const int before=reads;require(sample(12600).rx==-1&&reads==before&&scans==6);
    live.push_back({5,100,100,true,true});require(sample(17100).rx==-1&&scans==7);
    live[0].rx+=10;require(sample(17600).rx==20);
    std::cout<<"Network cache tests passed\n";
}
