#pragma once
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <cstdint>
#include <vector>

namespace lcd {
// Retain the allocated buffer. If too small, use a zero-size query;
// Microsoft does not guarantee the returned size from a nonzero short buffer.
template<class Call>
bool formatted_array(std::vector<uint64_t>& storage,DWORD& count,Call call){
    constexpr DWORD limit=32u*1024u*1024u;
    bool resize=storage.empty();
    for(int attempt=0;attempt<3;++attempt){
        if(resize){
            DWORD bytes=0;count=0;
            if(call(&bytes,&count,nullptr)!=PDH_MORE_DATA||!bytes||bytes>limit)return false;
            const size_t words=(size_t(bytes)+7)/8;
            if(words>storage.size())storage.resize(words);
        }
        DWORD capacity=static_cast<DWORD>(storage.size()*sizeof(uint64_t));count=0;
        auto* items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
        auto rc=call(&capacity,&count,items);
        if(rc==ERROR_SUCCESS)return count<=storage.size()*sizeof(uint64_t)/sizeof(*items);
        if(rc!=PDH_MORE_DATA&&rc!=PDH_INVALID_ARGUMENT)return false;
        resize=true;
    }
    return false;
}
}
