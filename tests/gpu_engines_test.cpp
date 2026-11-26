#include "gpu_engines.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <string>
void require(bool ok){if(!ok)std::exit(1);}
double reference(const std::vector<std::pair<std::wstring,double>>& input){
    std::map<std::wstring,double> engines;
    for(const auto& item:input){
        const auto& name=item.first;
        if(!std::isfinite(item.second))continue;
        size_t a=name.find(L"luid_"),b=name.find(L"_eng_",a);
        if(a==std::wstring::npos||b==std::wstring::npos)continue;
        size_t end=name.find(L'_',b+5);
        engines[name.substr(a,end==std::wstring::npos?end:end-a)]+=std::max(0.0,item.second);
    }
    double result=-1;for(const auto& p:engines)result=std::max(result,p.second);
    return result;
}
int main(){
    lcd::GpuEngines engines;
    auto check=[&](const std::vector<std::pair<std::wstring,double>>& input){
        engines.clear();
        for(const auto& p:input)engines.add(p.first,p.second);
        require(engines.maximum()==reference(input));
        engines.clear();require(engines.maximum()==-1);
    };
    check({});
    check({{L"invalid",50},{L"luid_x_phys_0_eng_1",10},{L"luid_x_phys_0_eng_1_type",15},
        {L"pid_2_luid_x_phys_0_eng_1_other",20},{L"luid_x_phys_0_eng_2",30},
        {L"luid_y_phys_0_eng_1",44},{L"luid_x_phys_1_eng_1",43},
        {L"luid_x_phys_0_eng_1",-1},{L"luid_x_phys_0_eng_1",std::numeric_limits<double>::infinity()},
        {L"luid_x_phys_0_eng_1",std::numeric_limits<double>::quiet_NaN()}});
    std::mt19937 rng(108);
    for(int iteration=0;iteration<500;++iteration){
        std::vector<std::pair<std::wstring,double>> input;
        for(unsigned j=0,n=rng()%700;j<n;++j){
            std::wstring name=L"pid_"+std::to_wstring(rng()%100)+L"_luid_"+std::to_wstring(rng()%4)+
                L"_phys_"+std::to_wstring(rng()%2)+L"_eng_"+std::to_wstring(rng()%12)+L"_engtype_3D";
            if(rng()%20==0)name=L"invalid";
            input.emplace_back(std::move(name),double(int(rng()%13000)-2000)/100.);
        }
        check(input);
    }
    std::cout<<"GPU aggregation equivalence tests passed\n";
}
