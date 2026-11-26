#pragma once
#include "render.hpp"
#include "profile.hpp"
#include "network_cache.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <map>
#include <string>
#include <vector>
#include <utility>

class Sampler {
    diag::Profile& profile;
    struct Cpu {uint64_t idle=0,kernel=0,user=0;};
    Cpu prev{};bool cpu_valid=false;
    lcd::NetworkCache net;
    PDH_HQUERY query=nullptr;PDH_HCOUNTER gpu=nullptr;
    std::vector<uint64_t> pdh_storage;
    static uint64_t ft(FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
    double cpu_load() {
        FILETIME i{},k{},u{};
        if(!GetSystemTimes(&i,&k,&u)){cpu_valid=false;return -1;}
        Cpu now{ft(i),ft(k),ft(u)};double result=-1;
        if(cpu_valid&&now.idle>=prev.idle&&now.kernel>=prev.kernel&&now.user>=prev.user) {
            uint64_t total=now.kernel-prev.kernel+now.user-prev.user,idle=now.idle-prev.idle;
            if(total&&idle<=total)result=100.0*double(total-idle)/double(total);
        }
        prev=now;cpu_valid=true;return result;
    }
    static lcd::NetRow net_row(const MIB_IF_ROW2& r){
        return {r.InterfaceLuid.Value,r.InOctets,r.OutOctets,
            (r.Type==IF_TYPE_ETHERNET_CSMACD||r.Type==IF_TYPE_IEEE80211)&&
                r.InterfaceAndOperStatusFlags.HardwareInterface&&!r.InterfaceAndOperStatusFlags.FilterInterface,
            r.OperStatus==IfOperStatusUp};
    }
    void network(lcd::Metrics& m){
        diag::Profile::Scope total(profile,diag::Stage::NetTotal);
        const auto t=GetTickCount64();
        // Measure API calls individually; net_rest is exclusive of API time.
        // Profile the surrounding work by difference in the diagnostic log.
        auto rate=net.sample(t,[&](std::vector<lcd::NetRow>& rows){
            ++profile.net_scans;
            PMIB_IF_TABLE2 table=nullptr;
            auto status=profile.measure(diag::Stage::NetApi,[&]{return GetIfTable2(&table);});
            if(status!=NO_ERROR){++profile.net_scan_errors;return false;}
            diag::Profile::Scope measured(profile,diag::Stage::NetRest);
            profile.note_net(table->NumEntries);
            for(ULONG i=0;i<table->NumEntries;++i){
                auto r=net_row(table->Table[i]);if(r.eligible)rows.push_back(r);
            }
            FreeMibTable(table);
            return true;
        },[&](uint64_t id,lcd::NetRow& out){
            ++profile.net_entries;
            MIB_IF_ROW2 row{};row.InterfaceLuid.Value=id;
            auto status=profile.measure(diag::Stage::NetApi,[&]{return GetIfEntry2(&row);});
            if(status!=NO_ERROR){++profile.net_entry_errors;return false;}
            out=net_row(row);return true;
        });
        m.rx=rate.rx;m.tx=rate.tx;
    }
    double gpu_load() {
        if(!query||profile.measure(diag::Stage::GpuCollect,[&]{return PdhCollectQueryData(query);})!=ERROR_SUCCESS)return -1;
        DWORD bytes=0,count=0;PDH_STATUS rc=profile.measure(diag::Stage::GpuFormat,[&]{return PdhGetFormattedCounterArrayW(gpu,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&bytes,&count,nullptr);});
        if(rc!=PDH_MORE_DATA||!bytes||bytes>32*1024*1024)return -1;
        for(int retry=0;retry<3;++retry){
            PDH_FMT_COUNTERVALUE_ITEM_W* items=nullptr;DWORD capacity=0;
            rc=profile.measure(diag::Stage::GpuFormat,[&]{
                pdh_storage.resize((bytes+7)/8);
                items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(pdh_storage.data());
                capacity=DWORD(pdh_storage.size()*8);
                return PdhGetFormattedCounterArrayW(gpu,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&capacity,&count,items);
            });
            if(rc==PDH_MORE_DATA&&capacity>0&&capacity<=32*1024*1024){bytes=capacity;continue;}
            if(rc!=ERROR_SUCCESS||count>pdh_storage.size()*8/sizeof(*items))return -1;
            diag::Profile::Scope measured(profile,diag::Stage::GpuParse);
            profile.note_gpu(count);
            std::map<std::wstring,double> engines;
            for(DWORD i=0;i<count;++i){
                const auto& v=items[i].FmtValue;
                if((v.CStatus!=0&&v.CStatus!=1)||!items[i].szName||!std::isfinite(v.doubleValue))continue;
                std::wstring name(items[i].szName);
                size_t a=name.find(L"luid_"),b=name.find(L"_eng_",a);
                if(a==std::wstring::npos||b==std::wstring::npos)continue;
                size_t end=name.find(L'_',b+5);
                engines[name.substr(a,end==std::wstring::npos?end:end-a)]+=std::max(0.0,v.doubleValue);
            }
            double result=-1;for(const auto& p:engines)result=std::max(result,p.second);
            return result<0?-1:lcd::clamp(result);
        }
        return -1;
    }
public:
    explicit Sampler(diag::Profile& p):profile(p){
        if(PdhOpenQueryW(nullptr,0,&query)==ERROR_SUCCESS){
            if(PdhAddEnglishCounterW(query,L"\\GPU Engine(*)\\Utilization Percentage",0,&gpu)!=ERROR_SUCCESS){PdhCloseQuery(query);query=nullptr;}
            else PdhCollectQueryData(query);
        }else query=nullptr;
    }
    ~Sampler(){if(query)PdhCloseQuery(query);}
    lcd::Metrics sample(){
        lcd::Metrics m;
        {
            diag::Profile::Scope measured(profile,diag::Stage::Core);
            m.cpu=cpu_load();
            MEMORYSTATUSEX mem{};mem.dwLength=sizeof mem;
            if(GlobalMemoryStatusEx(&mem)&&mem.ullTotalPhys)m.ram=100.0*double(mem.ullTotalPhys-mem.ullAvailPhys)/double(mem.ullTotalPhys);
        }
        network(m);m.gpu=gpu_load();
        {
            diag::Profile::Scope measured(profile,diag::Stage::Clock);
            m.uptime=GetTickCount64()/1000;
            SYSTEMTIME t{};GetLocalTime(&t);snprintf(m.clock,sizeof m.clock,"%02u:%02u",unsigned(t.wHour),unsigned(t.wMinute));
        }
        ++profile.samples;if(m.gpu<0)++profile.gpu_missing;if(m.rx<0||m.tx<0)++profile.net_missing;
        return m;
    }
};
