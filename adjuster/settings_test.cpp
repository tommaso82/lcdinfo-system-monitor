#include "settings.hpp"
#include <cstdlib>
#include <iostream>
void require(bool b){if(!b)std::exit(1);}
int main(){
    for(int v=0;v<=255;++v){
        lcd::Settings a{v,255-v},b;
        require(lcd::parse_settings(lcd::serialize(a),b));
        require(a.contrast==b.contrast&&a.backlight==b.backlight);
        require(lcd::step(v,1)==std::min(v+1,255));
        require(lcd::step(v,-1)==std::max(v-1,0));
    }
    lcd::Settings s{23,42};
    for(auto text:{"","[Display]\nContrast=256\nBacklight=5","[Display]\nContrast=-1\nBacklight=5",
        "[Display]\nContrast=4x\nBacklight=5","[Display]\nContrast=4\nContrast=3\nBacklight=5",
        "[Display]\nContrast=4","[Other]\nContrast=4\nBacklight=5"}){
        require(!lcd::parse_settings(text,s));require(s.contrast==23&&s.backlight==42);
    }
    require(lcd::parse_settings("[Display]\nBacklight=180\nContrast=181\n",s));
    require(s.contrast==181&&s.backlight==180);
    std::cout<<"Settings tests passed\n";
}
