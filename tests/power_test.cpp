#include "power.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <initializer_list>
void require(bool b){if(!b)std::exit(1);}
void packet(lcd::PowerPacket p,std::initializer_list<uint8_t> expected){
    require(p.len==expected.size());
    require(std::equal(expected.begin(),expected.end(),p.bytes.begin()));
}
int main(){
    lcd::PowerRequest p;
    require(!p.suspended&&p.generation==0);
    require(!p.resume(100)&&p.generation==0);
    require(p.suspend(1000)&&p.suspended&&p.generation==1&&p.deadline==2000);
    require(!p.suspend(1500)&&p.deadline==2000);
    require(p.resume(10000)&&!p.suspended&&p.generation==2&&p.deadline==12000);
    require(!p.resume(11000)&&p.deadline==12000);
    require(p.suspend(11001)&&p.suspended&&p.generation==3&&p.deadline==12001);
    require(p.resume(20000)&&p.generation==4&&p.deadline==22000);
    int n=-1;
    for(int i=0;i<=255;++i)require(lcd::parse_backlight(std::to_wstring(i),n)&&n==i);
    for(auto s:{L"",L"-1",L"+1",L"256",L"999999999999999999999",L"1x",L" 1",L"1 ",L"1.5",L"0x10"})require(!lcd::parse_backlight(s,n)&&n==255);
    packet(lcd::sleep_packet(false),{0x04,0x03,0x02,0x02,0x03});
    packet(lcd::sleep_packet(true),{0x04,0x0d,0,0x03,0x02,0x02,0x03});
    for(int i=0;i<=255;++i)packet(lcd::wake_packet(static_cast<uint8_t>(i)),{0x0d,static_cast<uint8_t>(i),0x03,0x02,0x02,0x03});
    std::cout<<"Power request, argument and packet tests passed\n";
}
