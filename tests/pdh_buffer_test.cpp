#include "pdh_buffer.hpp"
#include <cstdlib>
#include <iostream>
void require(bool ok){if(!ok)std::exit(1);}
int main(){
    std::vector<uint64_t> storage;
    DWORD count=0,required=1024;int calls=0,probes=0;
    PDH_STATUS short_status=PDH_MORE_DATA;
    auto fake=[&](DWORD* bytes,DWORD* n,PDH_FMT_COUNTERVALUE_ITEM_W* items)->PDH_STATUS{
        ++calls;
        if(!items){++probes;require(*bytes==0);*bytes=required;return PDH_MORE_DATA;}
        if(*bytes<required){*bytes=1;return short_status;} // deliberately misleading
        *bytes=required;*n=2;return ERROR_SUCCESS;
    };
    require(lcd::formatted_array(storage,count,fake)&&calls==2&&probes==1&&count==2);
    require(lcd::formatted_array(storage,count,fake)&&calls==3&&probes==1);
    required=2048;require(lcd::formatted_array(storage,count,fake)&&calls==6&&probes==2);
    short_status=PDH_INVALID_ARGUMENT;required=4096;
    require(lcd::formatted_array(storage,count,fake)&&calls==9&&probes==3);
    required=1024;require(lcd::formatted_array(storage,count,fake)&&storage.size()*8==4096);
    required=33u*1024u*1024u;require(!lcd::formatted_array(storage,count,fake));
    auto invalid=[](DWORD*,DWORD*,PDH_FMT_COUNTERVALUE_ITEM_W*)->PDH_STATUS{return PDH_INVALID_HANDLE;};
    require(!lcd::formatted_array(storage,count,invalid));
    auto bad_count=[](DWORD*,DWORD* n,PDH_FMT_COUNTERVALUE_ITEM_W*)->PDH_STATUS{*n=0xffffffffu;return ERROR_SUCCESS;};
    require(!lcd::formatted_array(storage,count,bad_count));
    int attempts=0;
    auto unstable=[&](DWORD* bytes,DWORD*,PDH_FMT_COUNTERVALUE_ITEM_W* items)->PDH_STATUS{
        ++attempts;if(!items)*bytes=4096;return PDH_MORE_DATA;
    };
    require(!lcd::formatted_array(storage,count,unstable)&&attempts==5);
    std::cout<<"PDH buffer tests passed\n";
}
