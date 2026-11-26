#pragma once
#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>
namespace lcd {
struct Settings {int contrast=175,backlight=127;};
inline int step(int value,int delta){return std::clamp(value+delta,0,255);}
inline bool parse_byte(std::string_view v,int& out){
    if(v.empty())return false;
    int n=0;auto r=std::from_chars(v.data(),v.data()+v.size(),n);
    if(r.ec!=std::errc{}||r.ptr!=v.data()+v.size()||n<0||n>255)return false;
    out=n;return true;
}
inline bool parse_settings(std::string_view in,Settings& out){
    Settings s;bool section=false,c=false,b=false;
    while(!in.empty()){
        auto pos=in.find('\n');auto line=in.substr(0,pos);
        if(pos==std::string_view::npos)in={};else in.remove_prefix(pos+1);
        if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
        if(line.empty())continue;
        if(!section){if(line!="[Display]")return false;section=true;continue;}
        constexpr std::string_view ck="Contrast=",bk="Backlight=";
        if(line.substr(0,ck.size())==ck&&!c){if(!parse_byte(line.substr(ck.size()),s.contrast))return false;c=true;}
        else if(line.substr(0,bk.size())==bk&&!b){if(!parse_byte(line.substr(bk.size()),s.backlight))return false;b=true;}
        else return false;
    }
    if(!section||!c||!b)return false;
    out=s;return true;
}
inline std::string serialize(Settings s){
    return "[Display]\r\nContrast="+std::to_string(s.contrast)+"\r\nBacklight="+std::to_string(s.backlight)+"\r\n";
}
}
