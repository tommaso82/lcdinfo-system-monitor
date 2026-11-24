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
HANDLE stop_event=nullptr;
UINT taskbar_created=0;
NOTIFYICONDATAW tray{};
HICON owned_icon=nullptr;
void add_tray(HWND hwnd){
    tray={};tray.cbSize=sizeof tray;tray.hWnd=hwnd;tray.uID=1;
    tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=TrayMsg;
    tray.hIcon=owned_icon;wcscpy_s(tray.szTip,L"LCDInfo System Monitor");
    Shell_NotifyIconW(NIM_ADD,&tray);
}
HICON make_icon(){
    // Monochrome 16x16 monitor and bars, no external icon dependency.
    BYTE and_mask[32],xor_mask[32]{};
    memset(and_mask,0xff,sizeof and_mask);
    auto point=[&](int x,int y){int i=y*2+x/8;BYTE bit=BYTE(0x80>>(x%8));and_mask[i]&=BYTE(~bit);xor_mask[i]|=bit;};
    for(int x=1;x<=14;++x){point(x,1);point(x,12);}
    for(int y=1;y<=12;++y){point(1,y);point(14,y);}
    for(int x=5;x<=10;++x)point(x,15);
    point(7,13);point(8,13);point(7,14);point(8,14);
    for(int x=3;x<=4;++x)for(int y=8;y<=10;++y)point(x,y);
    for(int x=7;x<=8;++x)for(int y=4;y<=10;++y)point(x,y);
    for(int x=11;x<=12;++x)for(int y=6;y<=10;++y)point(x,y);
    return CreateIcon(GetModuleHandleW(nullptr),16,16,1,1,and_mask,xor_mask);
}
LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    if(taskbar_created&&msg==taskbar_created){add_tray(hwnd);return 0;}
    switch(msg){
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
    case WM_DESTROY:Shell_NotifyIconW(NIM_DELETE,&tray);SetEvent(stop_event);PostQuitMessage(0);return 0;
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
