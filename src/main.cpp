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
HANDLE stop_event=nullptr,power_event=nullptr,suspend_done=nullptr;
SRWLOCK power_lock=SRWLOCK_INIT;
lcd::PowerRequest power_request;
int resume_backlight=-1; // No light commands unless explicitly configured.
lcd::PowerRequest power_snapshot(){
    AcquireSRWLockShared(&power_lock);
    const auto p=power_request;
    ReleaseSRWLockShared(&power_lock);
    return p;
}
void on_suspend(){
    AcquireSRWLockExclusive(&power_lock);
    const bool changed=power_request.suspend(GetTickCount64());
    if(changed){
        ResetEvent(suspend_done);
        SetEvent(power_event);
    }
    const auto deadline=power_request.deadline;
    ReleaseSRWLockExclusive(&power_lock);
    if(changed){
        const auto now=GetTickCount64();
        if(now<deadline)WaitForSingleObject(suspend_done,static_cast<DWORD>(deadline-now));
    }
}
void on_resume(){
    AcquireSRWLockExclusive(&power_lock);
    if(power_request.resume(GetTickCount64()))SetEvent(power_event);
    ReleaseSRWLockExclusive(&power_lock);
}
void acknowledge_suspend(uint64_t generation){
    AcquireSRWLockExclusive(&power_lock);
    if(power_request.suspended&&power_request.generation==generation)SetEvent(suspend_done);
    ReleaseSRWLockExclusive(&power_lock);
}
void close_events(){
    if(suspend_done)CloseHandle(suspend_done);
    if(power_event)CloseHandle(power_event);
    if(stop_event)CloseHandle(stop_event);
}
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
    case WM_POWERBROADCAST:
        if(w==PBT_APMSUSPEND){on_suspend();return TRUE;}
        if(w==PBT_APMRESUMEAUTOMATIC){on_resume();return TRUE;}
        if(w==PBT_APMRESUMESUSPEND)return TRUE; // Automatic notification already handled.
        return DefWindowProcW(hwnd,msg,w,l);
    case WM_QUERYENDSESSION:return TRUE;
    case WM_ENDSESSION:if(w)SetEvent(stop_event);return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:KillTimer(hwnd,TrayRetry);Shell_NotifyIconW(NIM_DELETE,&tray);SetEvent(stop_event);PostQuitMessage(0);return 0;
    default:return DefWindowProcW(hwnd,msg,w,l);
    }
}
void worker(){
    Display display;
    lcd::Frame previous{};bool synchronized=false,restore_pending=false;
    ULONGLONG reconnect_at=0;
    uint64_t generation=0;
    std::unique_ptr<Sampler> sampler;
    const HANDLE events[]={stop_event,power_event};
    // Only the worker owns USB and Sampler. The UI never performs USB I/O.
    while(WaitForSingleObject(stop_event,0)==WAIT_TIMEOUT){
        const auto request=power_snapshot();
        if(request.generation!=generation){
            generation=request.generation;
            if(request.suspended&&display.connected())
                display.suspend_display(resume_backlight>=0,request.deadline);
            display.close();sampler.reset();synchronized=false;
            if(request.suspended)acknowledge_suspend(generation);
            else {
                reconnect_at=request.deadline;
                restore_pending=resume_backlight>=0;
            }
        }
        if(request.suspended){
            if(WaitForMultipleObjects(2,events,FALSE,INFINITE)!=WAIT_OBJECT_0+1)break;
            continue;
        }
        const ULONGLONG start=GetTickCount64();
        if(WaitForSingleObject(power_event,0)==WAIT_OBJECT_0)continue;
        if(!display.connected()&&start>=reconnect_at){
            synchronized=false;
            if(!display.open())reconnect_at=GetTickCount64()+3000;
        }
        // A notification during open takes priority over restoring settings.
        if(WaitForSingleObject(power_event,0)==WAIT_OBJECT_0)continue;
        if(WaitForSingleObject(stop_event,0)!=WAIT_TIMEOUT)break;
        // Keep restoration pending until it succeeds, even when a stale wake
        // signal interrupted the preceding successful open.
        if(display.connected()&&!sampler){
            if(restore_pending&&!display.restore_backlight(static_cast<uint8_t>(resume_backlight))){
                display.close();reconnect_at=GetTickCount64()+3000;
            }else{
                restore_pending=false;
                if(WaitForSingleObject(power_event,0)==WAIT_OBJECT_0)continue;
                if(WaitForSingleObject(stop_event,0)!=WAIT_TIMEOUT)break;
                sampler=std::make_unique<Sampler>();
            }
        }
        if(display.connected()&&sampler){
            auto frame=lcd::render(sampler->sample());
            auto packet=lcd::encode(frame,synchronized?&previous:nullptr);
            if(WaitForSingleObject(power_event,0)==WAIT_OBJECT_0)continue;
            if(WaitForSingleObject(stop_event,0)!=WAIT_TIMEOUT)break;
            if(display.update(packet)){previous=frame;synchronized=true;}
            else {display.close();sampler.reset();synchronized=false;reconnect_at=GetTickCount64()+3000;}
        }
        const ULONGLONG now=GetTickCount64();
        const DWORD delay=display.connected()?DWORD(now-start>=500?1:500-(now-start)):DWORD(reconnect_at>now?reconnect_at-now:1);
        const DWORD wait=WaitForMultipleObjects(2,events,FALSE,delay);
        if(wait!=WAIT_TIMEOUT&&wait!=WAIT_OBJECT_0+1)break;
    }
}
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int){
    int argc=0;
    LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv)return 1;
    const bool valid=argc==1||(argc==3&&std::wstring_view(argv[1])==L"--backlight"&&lcd::parse_backlight(argv[2],resume_backlight));
    LocalFree(argv);
    if(!valid){
        MessageBoxW(nullptr,L"Usage: LCDInfoSystemMonitor.exe [--backlight 0..255]",L"LCDInfo System Monitor",MB_OK|MB_ICONERROR);
        return 1;
    }
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\LCDInfoSystemMonitor");
    if(!mutex)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mutex);return 0;}
    stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    power_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    suspend_done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!stop_event||!power_event||!suspend_done){close_events();CloseHandle(mutex);return 1;}
    taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc{};wc.lpfnWndProc=wndproc;wc.hInstance=h;wc.lpszClassName=L"LCDInfoSystemMonitorTray";
    if(!RegisterClassW(&wc)){close_events();CloseHandle(mutex);return 1;}
    HWND hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"LCDInfo System Monitor",WS_POPUP,0,0,0,0,nullptr,nullptr,h,nullptr);
    if(!hwnd){close_events();CloseHandle(mutex);return 1;}
    owned_icon=make_icon();if(!owned_icon)owned_icon=CopyIcon(LoadIconW(nullptr,IDI_APPLICATION));
    add_tray(hwnd);
    std::thread thread(worker);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    SetEvent(stop_event);thread.join();Shell_NotifyIconW(NIM_DELETE,&tray);
    if(owned_icon)DestroyIcon(owned_icon);
    close_events();CloseHandle(mutex);return 0;
}
