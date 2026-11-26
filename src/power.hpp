#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <initializer_list>

namespace lcd {
constexpr uint64_t suspend_budget_ms=1000;
constexpr uint64_t resume_delay_ms=2000;

// UI publishes the latest desired state. A generation distinguishes rapid
// suspend/resume cycles even if a busy worker misses intermediate notifications.
struct PowerRequest {
    uint64_t generation=0,deadline=0;
    bool suspended=false;
    bool suspend(uint64_t now){
        if(suspended)return false;
        suspended=true;++generation;deadline=now+suspend_budget_ms;return true;
    }
    bool resume(uint64_t now){
        if(!suspended)return false;
        suspended=false;++generation;deadline=now+resume_delay_ms;return true;
    }
};
inline bool parse_backlight(std::wstring_view s,int& value){
    if(s.empty())return false;
    int n=0;
    for(wchar_t c:s){
        if(c<L'0'||c>L'9')return false;
        n=n*10+int(c-L'0');
        if(n>255)return false;
    }
    value=n;return true;
}
struct PowerPacket {
    std::array<uint8_t,7> bytes{};
    size_t len=0;
};
inline PowerPacket sleep_packet(bool darken){
    PowerPacket p;
    p.bytes[p.len++]=0x04;
    if(darken){p.bytes[p.len++]=0x0d;p.bytes[p.len++]=0;}
    for(uint8_t b:std::array<uint8_t,4>{0x03,0x02,0x02,0x03})p.bytes[p.len++]=b;
    return p;
}
inline PowerPacket wake_packet(uint8_t value){
    return {{{0x0d,value,0x03,0x02,0x02,0x03,0}},6};
}
}
