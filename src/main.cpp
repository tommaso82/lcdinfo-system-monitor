#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include "usb.hpp"
#include "metrics.hpp"
#include <thread>
#include <memory>

namespace {
constexpr UINT TrayMsg=WM_APP+1;
constexpr UINT ExitId=100;
constexpr UINT_PTR TrayRetry=1;
HANDLE stop_event=nullptr;
UINT taskbar_created=0;
NOTIFYICONDATAW tray{};
HICON owned_icon=nullptr;
void add_tray(HWND hwnd){
    tray={};tray.cbSize=sizeof tray;tray.hWnd=hwnd;tray.uID=1;
    tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=TrayMsg;
    tray.hIcon=owned_icon;wcscpy_s(tray.szTip,L"LCDInfo System Monitor");
    if(Shell_NotifyIconW(NIM_ADD,&tray)) KillTimer(hwnd,TrayRetry);
    else SetTimer(hwnd,TrayRetry,2000,nullptr);
}
HICON make_icon(){
    // Opaque blue monitor with a light border: visible on light and dark taskbars.
    BITMAPINFO bi{};
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=16;
    bi.bmiHeader.biHeight=-16;
    bi.bmiHeader.biPlanes=1;
    bi.bmiHeader.biBitCount=32;
    bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP color=CreateDIBSection(nullptr,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    BYTE mask_bits[32]{};
    HBITMAP mask=CreateBitmap(16,16,1,1,mask_bits);
    HICON icon=nullptr;
    if(color&&bits&&mask){
        auto* pixels=static_cast<uint32_t*>(bits);
        std::fill_n(pixels,256,0u);
        auto point=[&](int x,int y,uint32_t c){pixels[y*16+x]=c;};
        for(int y=1;y<=12;++y)for(int x=0;x<=15;++x)
            point(x,y,(y==1||y==12||x==0||x==15)?0xff182b48u:0xff1764b0u);
        for(int y=2;y<=11;++y)for(int x=1;x<=14;++x)
            if(y==2||y==11||x==1||x==14)point(x,y,0xff9bcaffu);
        for(int x=3;x<=4;++x)for(int y=8;y<=10;++y)point(x,y,0xffffffffu);
        for(int x=7;x<=8;++x)for(int y=4;y<=10;++y)point(x,y,0xffffffffu);
        for(int x=11;x<=12;++x)for(int y=6;y<=10;++y)point(x,y,0xffffffffu);
        for(int x=7;x<=8;++x)for(int y=13;y<=14;++y)point(x,y,0xff315c91u);
        for(int x=4;x<=11;++x)point(x,15,0xff315c91u);
        ICONINFO info{};info.fIcon=TRUE;info.hbmMask=mask;info.hbmColor=color;
        icon=CreateIconIndirect(&info);
    }
    if(mask)DeleteObject(mask);
    if(color)DeleteObject(color);
    return icon;
}
LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    if(taskbar_created&&msg==taskbar_created){add_tray(hwnd);return 0;}
    switch(msg){
    case WM_TIMER:
        if(w==TrayRetry){add_tray(hwnd);return 0;}
        return DefWindowProcW(hwnd,msg,w,l);
    case TrayMsg:
        if(l==WM_RBUTTONUP||l==WM_CONTEXTMENU){
            POINT p{};GetCursorPos(&p);HMENU menu=CreatePopupMenu();
            if(menu){AppendMenuW(menu,MF_STRING,ExitId,L"Exit");SetForegroundWindow(hwnd);
                UINT cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_NONOTIFY,p.x,p.y,0,hwnd,nullptr);
                DestroyMenu(menu);PostMessageW(hwnd,WM_NULL,0,0);if(cmd==ExitId)DestroyWindow(hwnd);}
        }return 0;
    case WM_QUERYENDSESSION:return TRUE;
    case WM_ENDSESSION:if(w)SetEvent(stop_event);return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:KillTimer(hwnd,TrayRetry);Shell_NotifyIconW(NIM_DELETE,&tray);SetEvent(stop_event);PostQuitMessage(0);return 0;
    default:return DefWindowProcW(hwnd,msg,w,l);
    }
}
void worker(){
    Display display;
    lcd::Frame previous{};bool synchronized=false;
    ULONGLONG reconnect_at=0;
    // No metric polling while the display is absent.
    std::unique_ptr<Sampler> sampler;
    while(WaitForSingleObject(stop_event,0)==WAIT_TIMEOUT){
        ULONGLONG start=GetTickCount64();
        if(!display.connected()&&start>=reconnect_at){
            synchronized=false;
            if(display.open())sampler=std::make_unique<Sampler>();
            else reconnect_at=GetTickCount64()+3000;
        }
        if(display.connected()&&sampler){
            auto frame=lcd::render(sampler->sample());
            auto packet=lcd::encode(frame,synchronized?&previous:nullptr);
            if(display.update(packet)){previous=frame;synchronized=true;}
            else {display.close();sampler.reset();synchronized=false;reconnect_at=GetTickCount64()+3000;}
        }
        ULONGLONG now=GetTickCount64();
        DWORD delay=display.connected()?DWORD(now-start>=500?1:500-(now-start)):DWORD(reconnect_at>now?reconnect_at-now:1);
        if(WaitForSingleObject(stop_event,delay)!=WAIT_TIMEOUT)break;
    }
}
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int){
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\LCDInfoSystemMonitor");
    if(!mutex)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mutex);return 0;}
    stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!stop_event){CloseHandle(mutex);return 1;}
    taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc{};wc.lpfnWndProc=wndproc;wc.hInstance=h;wc.lpszClassName=L"LCDInfoSystemMonitorTray";
    if(!RegisterClassW(&wc)){CloseHandle(stop_event);CloseHandle(mutex);return 1;}
    HWND hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"LCDInfo System Monitor",WS_POPUP,0,0,0,0,nullptr,nullptr,h,nullptr);
    if(!hwnd){CloseHandle(stop_event);CloseHandle(mutex);return 1;}
    owned_icon=make_icon();if(!owned_icon)owned_icon=CopyIcon(LoadIconW(nullptr,IDI_APPLICATION));
    add_tray(hwnd);
    std::thread thread(worker);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    SetEvent(stop_event);thread.join();Shell_NotifyIconW(NIM_DELETE,&tray);
    if(owned_icon)DestroyIcon(owned_icon);
    CloseHandle(stop_event);CloseHandle(mutex);return 0;
}
