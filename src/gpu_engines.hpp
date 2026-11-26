#pragma once
#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

namespace lcd {
// Views refer to the current PDH buffer only. Clear before reuse/reallocation.
class GpuEngines {
    std::vector<std::pair<std::wstring_view,double>> engines;
public:
    void clear(){engines.clear();}
    void add(std::wstring_view name,double value){
        if(!std::isfinite(value))return;
        auto a=name.find(L"luid_"),b=name.find(L"_eng_",a);
        if(a==std::wstring_view::npos||b==std::wstring_view::npos)return;
        auto end=name.find(L'_',b+5);
        auto key=name.substr(a,end==std::wstring_view::npos?end:end-a);
        auto it=std::find_if(engines.begin(),engines.end(),[&](const auto& e){return e.first==key;});
        value=std::max(0.0,value);
        if(it==engines.end())engines.emplace_back(key,value);
        else it->second+=value;
    }
    double maximum()const{
        double result=-1;
        for(const auto& e:engines)result=std::max(result,e.second);
        return result;
    }
};
}
