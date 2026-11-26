#include "profile.hpp"
#include <cstdlib>
#include <iostream>
void require(bool ok){if(!ok)std::exit(1);}
int main(){
    diag::Profile p(false);
    require(p.measure(diag::Stage::GpuCollect,[]{return 17;})==17);
    p.measure(diag::Stage::Probe,[]{});
    p.measure(diag::Stage::NetApi,[]{Sleep(20);});
    try{p.measure(diag::Stage::Render,[]{throw 42;});}catch(int x){require(x==42);}
    auto get=[&](diag::Stage s)->const diag::Stat&{return p.stats[static_cast<size_t>(s)];};
    require(get(diag::Stage::GpuCollect).calls==1&&get(diag::Stage::Probe).calls==1);
    require(get(diag::Stage::Render).calls==1&&get(diag::Stage::NetApi).wall>0);
    require(get(diag::Stage::NetApi).cpu_valid==1&&get(diag::Stage::NetApi).cycles_valid==1);
    p.note_gpu(20);p.note_gpu(30);p.note_net(5);p.note_net(3);
    require(p.gpu_samples==2&&p.gpu_items==50&&p.gpu_max_items==30);
    require(p.net_samples==2&&p.net_rows==8&&p.net_max_rows==5);
    p.report(true);
    std::cout<<"Profile accounting smoke tests passed\n";
}
