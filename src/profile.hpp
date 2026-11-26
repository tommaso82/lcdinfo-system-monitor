#pragma once
#include <windows.h>
#include <psapi.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <algorithm>
#include <utility>

namespace diag {
enum class Stage { Core, Clock, NetApi, NetRest, GpuCollect, GpuFormat, GpuParse,
                   Render, Encode, Usb, Connect, Probe, NetTotal, Count };
inline constexpr const char* names[]={"cpu_ram","clock_uptime","net_api","net_rest",
    "gpu_collect","gpu_format","gpu_parse","render","encode","usb_update","connect","probe","net_total"};
struct CpuTime { uint64_t ticks=0; bool valid=false; };
inline uint64_t ft(FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
inline CpuTime cpu_time(bool process){
    FILETIME c{},e{},k{},u{};
    BOOL ok=process?GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u):
                     GetThreadTimes(GetCurrentThread(),&c,&e,&k,&u);
    return {ft(k)+ft(u),ok!=FALSE};
}
inline int64_t qpc(){LARGE_INTEGER t{};QueryPerformanceCounter(&t);return t.QuadPart;}
inline int64_t frequency(){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return f.QuadPart;}
struct Stat {
    uint64_t calls=0,cpu_ticks=0,cpu_valid=0,cycles=0,cycles_valid=0;
    int64_t wall=0,max_wall=0;
};
class Profile {
    FILE* file=nullptr;
    int64_t hz=frequency(),start=0,last_report=0;
    CpuTime process_start{},worker_start{};
    DWORD processors=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    static FILE* open_log(){
        SYSTEMTIME t{};GetLocalTime(&t);
        wchar_t name[128]{};
        swprintf_s(name,L"lcdinfo-pdhbuffer-%04u%02u%02u-%02u%02u%02u-%lu.log",
            unsigned(t.wYear),unsigned(t.wMonth),unsigned(t.wDay),unsigned(t.wHour),
            unsigned(t.wMinute),unsigned(t.wSecond),GetCurrentProcessId());
        wchar_t path[32768]{};
        DWORD n=GetModuleFileNameW(nullptr,path,_countof(path));
        FILE* out=nullptr;
        if(n&&n<_countof(path)){
            std::wstring p(path,n);auto slash=p.find_last_of(L"\\/");
            if(slash!=std::wstring::npos){
                p.resize(slash+1);p+=name;
                if(_wfopen_s(&out,p.c_str(),L"wx")==0&&out)return out;
            }
        }
        n=GetTempPathW(_countof(path),path);
        if(n&&n<_countof(path)){
            std::wstring p(path,n);p+=name;
            if(_wfopen_s(&out,p.c_str(),L"wx")==0&&out)return out;
        }
        OutputDebugStringW(L"LCDInfo profile: unable to create log\n");
        return nullptr;
    }
    double ms(int64_t ticks)const{return hz>0?1000.*double(ticks)/double(hz):0.;}
    static double delta_ms(CpuTime a,CpuTime b){
        return a.valid&&b.valid&&b.ticks>=a.ticks?double(b.ticks-a.ticks)/10000.:-1.;
    }
public:
    std::array<Stat,static_cast<size_t>(Stage::Count)> stats{};
    uint64_t samples=0,gpu_missing=0,net_missing=0,frames_changed=0,frames_unchanged=0,
        update_failures=0,connect_attempts=0,connections=0,overruns=0,
        gpu_items=0,gpu_samples=0,gpu_max_items=0,net_rows=0,net_samples=0,net_max_rows=0,
        net_scans=0,net_entries=0,net_scan_errors=0,net_entry_errors=0,
        usb_writes=0,usb_reads=0,usb_out_requested=0,usb_out_ok=0,usb_in_ok=0,
        usb_write_fail=0,usb_read_fail=0,usb_read_timeout=0;
    explicit Profile(bool enable_log=true){
        if(enable_log)file=open_log();
        start=last_report=qpc();process_start=cpu_time(true);worker_start=cpu_time(false);
        if(file){
            fprintf(file,"LCDInfo 1.0.0-pdhbuffer base=4387f1c logical_processors=%lu qpc_hz=%lld\n",
                    processors,(long long)hz);
            fprintf(file,"Cumulative totals; wall_ms and CPU_ms differ. Stage CPU accounting is coarse; cycles are not time. Probe measures empty instrumentation, not subtracted. USB bytes are WinUSB API counts, not on-wire framing. Startup included; subtract snapshots for steady state. No device data or process names logged.\n");
            fflush(file);
        }
    }
    ~Profile(){if(file)fclose(file);}
    Profile(const Profile&)=delete;
    Profile& operator=(const Profile&)=delete;
    class Scope {
        Profile& p;Stage stage;int64_t begin;CpuTime cpu;
        ULONG64 cycles=0;bool cycles_ok=false;
    public:
        Scope(Profile& profile,Stage s):p(profile),stage(s){
            cpu=cpu_time(false);cycles_ok=QueryThreadCycleTime(GetCurrentThread(),&cycles)!=FALSE;
            begin=qpc();
        }
        ~Scope(){
            auto end=qpc();ULONG64 after=0;
            bool ok=QueryThreadCycleTime(GetCurrentThread(),&after)!=FALSE;
            auto last=cpu_time(false);
            auto& a=p.stats[static_cast<size_t>(stage)];++a.calls;
            auto dt=std::max<int64_t>(0,end-begin);a.wall+=dt;a.max_wall=std::max(a.max_wall,dt);
            if(cpu.valid&&last.valid&&last.ticks>=cpu.ticks){a.cpu_ticks+=last.ticks-cpu.ticks;++a.cpu_valid;}
            if(cycles_ok&&ok&&after>=cycles){a.cycles+=after-cycles;++a.cycles_valid;}
        }
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    };
    template<class F> decltype(auto) measure(Stage s,F&& fn){
        Scope scope(*this,s);return std::forward<F>(fn)();
    }
    void note_gpu(DWORD count){++gpu_samples;gpu_items+=count;gpu_max_items=std::max(gpu_max_items,uint64_t(count));}
    void note_net(ULONG count){++net_samples;net_rows+=count;net_max_rows=std::max(net_max_rows,uint64_t(count));}
    void report(bool final=false){
        if(!file)return;
        const auto now=qpc();
        if(!final&&ms(now-last_report)<10000.)return;
        last_report=now;
        const auto elapsed=ms(now-start);
        const double process_ms=delta_ms(process_start,cpu_time(true));
        const double worker_ms=delta_ms(worker_start,cpu_time(false));
        const double cpu_pct=elapsed>0&&processors&&process_ms>=0?100.*process_ms/elapsed/processors:-1.;
        PROCESS_MEMORY_COUNTERS_EX mem{};mem.cb=sizeof mem;
        BOOL mem_ok=GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),sizeof mem);
        DWORD handles=0;BOOL handles_ok=GetProcessHandleCount(GetCurrentProcess(),&handles);
        fprintf(file,"%s elapsed_ms=%.3f process_cpu_ms=%.3f worker_cpu_ms=%.3f cpu_pct=%.6f ws_bytes=%lld private_bytes=%lld handles=%lld\n",
            final?"FINAL":"PROFILE",elapsed,process_ms,worker_ms,cpu_pct,
            mem_ok?static_cast<long long>(mem.WorkingSetSize):-1LL,
            mem_ok?static_cast<long long>(mem.PrivateUsage):-1LL,handles_ok?static_cast<long long>(handles):-1LL);
        for(size_t i=0;i<stats.size();++i){
            const auto& a=stats[i];
            fprintf(file,"STAGE %s calls=%llu wall_ms=%.3f max_ms=%.3f cpu_ms=%.3f cpu_valid=%llu cycles=%llu cycles_valid=%llu\n",
                names[i],(unsigned long long)a.calls,ms(a.wall),ms(a.max_wall),
                double(a.cpu_ticks)/10000.,(unsigned long long)a.cpu_valid,
                (unsigned long long)a.cycles,(unsigned long long)a.cycles_valid);
        }
        fprintf(file,"COUNTS samples=%llu gpu_missing=%llu net_missing=%llu changed=%llu unchanged=%llu update_failures=%llu connect_attempts=%llu connections=%llu overruns=%llu\n",
            (unsigned long long)samples,(unsigned long long)gpu_missing,(unsigned long long)net_missing,
            (unsigned long long)frames_changed,(unsigned long long)frames_unchanged,(unsigned long long)update_failures,
            (unsigned long long)connect_attempts,(unsigned long long)connections,(unsigned long long)overruns);
        fprintf(file,"ITEMS gpu_samples=%llu gpu_items=%llu gpu_max_items=%llu net_samples=%llu net_rows=%llu net_max_rows=%llu\n",
            (unsigned long long)gpu_samples,(unsigned long long)gpu_items,(unsigned long long)gpu_max_items,
            (unsigned long long)net_samples,(unsigned long long)net_rows,(unsigned long long)net_max_rows);
        fprintf(file,"NET scans=%llu entries=%llu scan_errors=%llu entry_errors=%llu\n",
            (unsigned long long)net_scans,(unsigned long long)net_entries,
            (unsigned long long)net_scan_errors,(unsigned long long)net_entry_errors);
        fprintf(file,"USB write_calls=%llu read_calls=%llu out_requested=%llu out_success_bytes=%llu in_success_bytes=%llu write_fail=%llu read_fail=%llu read_timeout=%llu\n\n",
            (unsigned long long)usb_writes,(unsigned long long)usb_reads,(unsigned long long)usb_out_requested,
            (unsigned long long)usb_out_ok,(unsigned long long)usb_in_ok,(unsigned long long)usb_write_fail,
            (unsigned long long)usb_read_fail,(unsigned long long)usb_read_timeout);
        fflush(file);
    }
};
static_assert(sizeof(names)/sizeof(names[0])==static_cast<size_t>(Stage::Count));
}
