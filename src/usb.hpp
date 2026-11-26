#pragma once
#include "render.hpp"
#include "profile.hpp"
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winusb.h>
#include <objbase.h>
#include <vector>
#include <cwchar>

class Display {
    diag::Profile& profile;
    HANDLE file=INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE usb=nullptr;
    bool exchange(const uint8_t* data,size_t n,const uint8_t* expected,size_t want) {
        ULONG sent=0;
        ++profile.usb_writes;profile.usb_out_requested+=n;
        const BOOL written=WinUsb_WritePipe(usb,0x02,const_cast<PUCHAR>(data),ULONG(n),&sent,nullptr);
        if(written)profile.usb_out_ok+=sent;
        if(!written||sent!=n){++profile.usb_write_fail;return false;}
        size_t got=0;
        ULONGLONG deadline=GetTickCount64()+750;
        while(GetTickCount64()<deadline) {
            UCHAR buf[64]; ULONG count=0;
            ++profile.usb_reads;
            if(!WinUsb_ReadPipe(usb,0x81,buf,sizeof buf,&count,nullptr)) {
                const DWORD error=GetLastError();
                if(error==ERROR_SEM_TIMEOUT){++profile.usb_read_timeout;continue;}
                ++profile.usb_read_fail;return false;
            }
            profile.usb_in_ok+=count;
            if(!count) continue;
            if(count<2||count-2>want-got) return false;
            for(ULONG i=2;i<count;++i) if(buf[i]!=expected[got++]) return false;
            if(got==want) return true;
        }
        return false;
    }
    bool open_path(const wchar_t* path) {
        close();
        file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(file==INVALID_HANDLE_VALUE) return false;
        if(!WinUsb_Initialize(file,&usb)) { close(); return false; }
        USB_INTERFACE_DESCRIPTOR d{};
        if(!WinUsb_QueryInterfaceSettings(usb,0,&d)||d.bInterfaceNumber!=0||d.bAlternateSetting!=0||d.bNumEndpoints!=2) {close(); return false;}
        bool in=false,out=false;
        for(UCHAR i=0;i<d.bNumEndpoints;++i) {
            WINUSB_PIPE_INFORMATION p{};
            if(!WinUsb_QueryPipe(usb,0,i,&p)||p.PipeType!=UsbdPipeTypeBulk||p.MaximumPacketSize!=64) {close();return false;}
            in|=p.PipeId==0x81;out|=p.PipeId==0x02;
        }
        ULONG read_ms=120,write_ms=1000;
        if(!in||!out||!WinUsb_SetPipePolicy(usb,0x81,PIPE_TRANSFER_TIMEOUT,sizeof read_ms,&read_ms)||!WinUsb_SetPipePolicy(usb,0x02,PIPE_TRANSFER_TIMEOUT,sizeof write_ms,&write_ms)) {close();return false;}
        const uint8_t query[]={1,2,3};
        const uint8_t signature[]={'C','F','A','G','1','2','8','6','4','B',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',0x80,0x40};
        if(!exchange(query,sizeof query,signature,sizeof signature)) {close();return false;}
        return true;
    }
    bool try_guid(const GUID& g,wchar_t* instance) {
        ULONG len=0;
        GUID guid=g;
        if(CM_Get_Device_Interface_List_SizeW(&len,&guid,instance,CM_GET_DEVICE_INTERFACE_LIST_PRESENT)!=CR_SUCCESS||len<2||len>65536) return false;
        std::vector<wchar_t> paths(len);
        if(CM_Get_Device_Interface_ListW(&guid,instance,paths.data(),len,CM_GET_DEVICE_INTERFACE_LIST_PRESENT)!=CR_SUCCESS) return false;
        for(const wchar_t* p=paths.data();*p;p+=wcslen(p)+1) if(open_path(p)) return true;
        return false;
    }
public:
    explicit Display(diag::Profile& p):profile(p){}
    Display(const Display&)=delete;
    Display& operator=(const Display&)=delete;
    ~Display(){close();}
    bool connected() const {return usb!=nullptr;}
    void close(){if(usb){WinUsb_Free(usb);usb=nullptr;}if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);file=INVALID_HANDLE_VALUE;}}
    bool open() {
        close();
        HDEVINFO devices=SetupDiGetClassDevsW(nullptr,L"USB",nullptr,DIGCF_ALLCLASSES|DIGCF_PRESENT);
        if(devices==INVALID_HANDLE_VALUE) return false;
        bool found=false;
        const wchar_t prefix[]=L"USB\\VID_0403&PID_E300\\";
        const GUID generic={0xa5dcbf10,0x6530,0x11d2,{0x90,0x1f,0,0xc0,0x4f,0xb9,0x51,0xed}};
        for(DWORD i=0;!found;++i) {
            SP_DEVINFO_DATA info{};info.cbSize=sizeof info;
            if(!SetupDiEnumDeviceInfo(devices,i,&info)) break;
            wchar_t id[512]{};
            if(!SetupDiGetDeviceInstanceIdW(devices,&info,id,512,nullptr)||_wcsnicmp(id,prefix,_countof(prefix)-1)!=0) continue;
            HKEY key=SetupDiOpenDevRegKey(devices,&info,DICS_FLAG_GLOBAL,0,DIREG_DEV,KEY_QUERY_VALUE);
            if(key!=INVALID_HANDLE_VALUE) {
                for(auto name:{L"DeviceInterfaceGUIDs",L"DeviceInterfaceGUID"}) {
                    wchar_t buf[4096]{};DWORD type=0,bytes=sizeof(buf)-2*sizeof(wchar_t);
                    if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(buf),&bytes)!=ERROR_SUCCESS||(type!=REG_SZ&&type!=REG_MULTI_SZ)) continue;
                    for(wchar_t* p=buf;*p;p+=wcslen(p)+1) { GUID g{};if(SUCCEEDED(CLSIDFromString(p,&g))&&try_guid(g,id)){found=true;break;} }
                    if(found) break;
                }
                RegCloseKey(key);
            }
            if(!found) found=try_guid(generic,id);
        }
        SetupDiDestroyDeviceInfoList(devices);
        return found;
    }
    bool update(const lcd::Packet& p) {
        if(!p.len) return true;
        const uint8_t reply[]={0x40,0x80,0x80,0x40};
        return exchange(p.bytes.data(),p.len,reply,sizeof reply);
    }
};
