#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include <utility>

namespace lcd {
struct NetRow { uint64_t id=0,rx=0,tx=0; bool eligible=false,up=false; };
struct NetRate { double rx=-1,tx=-1; };
class NetworkCache {
    using Net=std::map<uint64_t,std::pair<uint64_t,uint64_t>>;
    Net old;
    uint64_t last=0,next_scan=0;
    std::vector<uint64_t> ids;
    std::vector<NetRow> rows;
public:
    // Read all known physical adapters, including down ones, on the fast path.
    // A failed direct read discards the partial sample and rescans immediately.
    template<class Scan,class Read>
    NetRate sample(uint64_t t,Scan scan,Read read){
        Net now;
        bool refresh=t>=next_scan||t<last;
        if(!refresh){
            for(auto id:ids){
                NetRow r{};
                if(!read(id,r)||r.id!=id){refresh=true;break;}
                if(r.eligible&&r.up)now[r.id]={r.rx,r.tx};
            }
        }
        if(refresh){
            now.clear();rows.clear();
            if(!scan(rows)){
                old.clear();ids.clear();last=next_scan=0;return {};
            }
            ids.clear();
            for(const auto& r:rows)if(r.eligible){
                ids.push_back(r.id);
                if(r.up)now[r.id]={r.rx,r.tx};
            }
            next_scan=t+5000;
        }
        NetRate result;
        if(last&&t>last){
            double rx=0,tx=0;bool any=false;
            for(const auto& p:now){
                auto it=old.find(p.first);if(it==old.end())continue;
                if(p.second.first<it->second.first||p.second.second<it->second.second)continue;
                rx+=double(p.second.first-it->second.first);
                tx+=double(p.second.second-it->second.second);any=true;
            }
            if(any){result.rx=rx*1000.0/double(t-last);result.tx=tx*1000.0/double(t-last);}
        }
        old.swap(now);last=t;return result;
    }
};
}
