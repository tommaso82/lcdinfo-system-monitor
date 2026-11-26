#include <windows.h>
#include "usb.hpp"
#include "settings.hpp"
#include <string>

namespace {
enum : int { ContrastDown=101,ContrastUp,LightDown,LightUp,Apply,Restore,Save,Connect };
Display display;
lcd::Settings saved{},current{};
bool have_saved=false,c_sent=false,b_sent=false,dirty=false;
std::wstring ini,load_note;
HWND window=nullptr,c_value=nullptr,b_value=nullptr,status_label=nullptr,ref_label=nullptr;
HWND buttons[8]{};

std::wstring ini_path(){
    wchar_t path[32768]{};
    const DWORD n=GetModuleFileNameW(nullptr,path,_countof(path));
    if(!n||n>=_countof(path))return {};
    std::wstring p(path,n);const auto slash=p.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return {};
    p.resize(slash+1);return p+L"LCDInfoDisplayAdjuster.ini";
}
void load_settings(){
    ini=ini_path();
    load_note=L"No saved INI; starting from reference values.";
    if(ini.empty()){load_note=L"INI path unavailable; reference values only.";return;}
    HANDLE f=CreateFileW(ini.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE){
        auto e=GetLastError();
        if(e!=ERROR_FILE_NOT_FOUND&&e!=ERROR_PATH_NOT_FOUND)load_note=L"Cannot read INI; reference values only.";
        return;
    }
    char data[256]{};DWORD n=0;
    bool ok=ReadFile(f,data,sizeof data,&n,nullptr)&&n<sizeof data;
    CloseHandle(f);
    if(ok&&lcd::parse_settings(std::string_view(data,n),saved)){
        current=saved;have_saved=true;load_note=L"INI loaded; device settings cannot be read.";
    }else load_note=L"Invalid INI; reference values only.";
}
void status(const wchar_t* s){SetWindowTextW(status_label,s);}
void refresh(){
    auto value=[](int n,bool sent){return std::to_wstring(n)+(sent?L" / 255  (sent)":L" / 255  (reference)");};
    SetWindowTextW(c_value,value(current.contrast,c_sent).c_str());
    SetWindowTextW(b_value,value(current.backlight,b_sent).c_str());
    std::wstring ref=have_saved?L"Last saved locally: ":L"Initial reference: ";
    ref+=std::to_wstring(saved.contrast)+L" / "+std::to_wstring(saved.backlight);
    if(dirty)ref+=L"   |   Temporary changes";
    SetWindowTextW(ref_label,ref.c_str());
    for(int i=0;i<6;++i)EnableWindow(buttons[i],display.connected());
    EnableWindow(buttons[6],display.connected()&&c_sent&&b_sent&&dirty);
    EnableWindow(buttons[7],!display.connected());
}
bool send(uint8_t op,int value){
    if(display.adjust(op,static_cast<uint8_t>(value)))return true;
    display.close();c_sent=b_sent=false;
    status(L"USB response failed; state unknown. Reconnect before continuing.");
    refresh();return false;
}
void connect(){
    c_sent=b_sent=false;
    if(display.open())status(load_note.c_str());
    else status(L"Cannot open LCD. Exit System Monitor, then click Connect.");
    refresh();
}
void change(bool contrast,int delta){
    if(!display.connected())return;
    int& v=contrast?current.contrast:current.backlight;
    const int n=lcd::step(v,delta);
    if(n==v)return;
    if(!send(contrast?0x0c:0x0d,n))return;
    v=n;(contrast?c_sent:b_sent)=true;dirty=true;
    status(L"Temporary setting. Apply both values before Save.");
    refresh();
}
bool apply(lcd::Settings target){
    if(!display.connected())return false;
    if(!send(0x0c,target.contrast))return false;
    current.contrast=target.contrast;c_sent=true;dirty=true;
    if(!send(0x0d,target.backlight))return false;
    current.backlight=target.backlight;b_sent=true;refresh();return true;
}
bool stage_ini(std::wstring& temp){
    if(ini.empty())return false;
    temp=ini+L".tmp."+std::to_wstring(GetCurrentProcessId());
    HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    auto data=lcd::serialize(current);DWORD n=0;
    bool ok=WriteFile(f,data.data(),static_cast<DWORD>(data.size()),&n,nullptr)&&n==data.size()&&FlushFileBuffers(f);
    CloseHandle(f);
    if(!ok)DeleteFileW(temp.c_str());
    return ok;
}
void save(){
    if(!display.connected()||!c_sent||!b_sent||!dirty)return;
    std::wstring prompt=L"Save contrast "+std::to_wstring(current.contrast)+L" and backlight "+std::to_wstring(current.backlight)+L" to the display and INI?";
    if(MessageBoxW(window,prompt.c_str(),L"Confirm persistent settings",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
    std::wstring temp;
    if(!stage_ini(temp)){
        status(L"Cannot write INI beside EXE. No persistent commands sent.");
        return;
    }
    status(L"Saving to display...");UpdateWindow(status_label);
    if(!send(0x0e,current.contrast)||!send(0x0f,current.backlight)){
        DeleteFileW(temp.c_str());
        status(L"Save incomplete; device may be partly changed. Previous INI retained.");
        MessageBoxW(window,L"Persistent settings may have been partially changed. The previous INI was retained. Reconnect and check the display before trying again.",L"Save incomplete",MB_OK|MB_ICONWARNING);
        return;
    }
    if(!MoveFileExW(temp.c_str(),ini.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        // Retain staged values for manual recovery.
        std::wstring msg=L"Commands completed, but INI replacement failed. Recover values from:\n"+temp;
        status(L"Device commands completed; INI replacement failed.");
        MessageBoxW(window,msg.c_str(),L"INI error",MB_OK|MB_ICONWARNING);
        return;
    }
    saved=current;have_saved=true;dirty=false;
    load_note=L"INI loaded; device settings cannot be read.";
    status(L"Saved to device and INI. Verify persistence after power cycle.");
    refresh();
}
HWND child(const wchar_t* cls,const wchar_t* title,int x,int y,int w,int h,int id=0,DWORD extra=0){
    HWND c=CreateWindowExW(0,cls,title,WS_CHILD|WS_VISIBLE|extra,x,y,w,h,window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    if(c)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
    return c;
}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    switch(msg){
    case WM_CREATE:
        window=hwnd;
        child(L"STATIC",L"Contrast  (Up / Down)",16,16,205,22);
        c_value=child(L"STATIC",L"",16,42,220,22);
        buttons[0]=child(L"BUTTON",L"-",252,34,55,32,ContrastDown);
        buttons[1]=child(L"BUTTON",L"+",315,34,55,32,ContrastUp);
        child(L"STATIC",L"Backlight  (Left / Right)",16,86,220,22);
        b_value=child(L"STATIC",L"",16,112,220,22);
        buttons[2]=child(L"BUTTON",L"-",252,104,55,32,LightDown);
        buttons[3]=child(L"BUTTON",L"+",315,104,55,32,LightUp);
        ref_label=child(L"STATIC",L"",16,151,360,22);
        buttons[4]=child(L"BUTTON",L"Apply both",16,181,105,30,Apply);
        buttons[5]=child(L"BUTTON",L"Restore",129,181,105,30,Restore);
        buttons[6]=child(L"BUTTON",L"Save",242,181,128,30,Save);
        buttons[7]=child(L"BUTTON",L"Connect",16,224,105,30,Connect);
        status_label=child(L"STATIC",L"",16,267,354,58);
        if(!c_value||!b_value||!ref_label||!status_label)return -1;
        for(auto b:buttons)if(!b)return -1;
        refresh();
        return 0;
    case WM_COMMAND:
        if(HIWORD(w)==BN_CLICKED)switch(LOWORD(w)){
        case ContrastDown:change(true,-1);break;
        case ContrastUp:change(true,1);break;
        case LightDown:change(false,-1);break;
        case LightUp:change(false,1);break;
        case Apply:if(apply(current))status(L"Both values applied temporarily. Save if satisfied.");break;
        case Restore:if(apply(saved))status(L"Reference restored temporarily. Save if satisfied.");break;
        case Save:save();break;
        case Connect:connect();break;
        }
        return 0;
    case WM_DESTROY:display.close();PostQuitMessage(0);return 0;
    default:return DefWindowProcW(hwnd,msg,w,l);
    }
}
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int show){
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\LCDInfoDisplayAdjuster");
    if(!mutex)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mutex);return 0;}
    load_settings();
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=h;wc.lpszClassName=L"LCDInfoDisplayAdjuster";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
    if(!RegisterClassW(&wc)){CloseHandle(mutex);return 1;}
    RECT r{0,0,386,330};constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    AdjustWindowRect(&r,style,FALSE);
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"LCDInfo Display Adjuster",style,
        CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,h,nullptr);
    if(!hwnd){CloseHandle(mutex);return 1;}
    ShowWindow(hwnd,show);UpdateWindow(hwnd);connect();
    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0){
        if(msg.message==WM_KEYDOWN&&(msg.hwnd==hwnd||IsChild(hwnd,msg.hwnd))){
            bool c=msg.wParam==VK_UP||msg.wParam==VK_DOWN;
            bool b=msg.wParam==VK_LEFT||msg.wParam==VK_RIGHT;
            if(c||b){
                // Win32 repeat count is normally one; preserve each step.
                unsigned repeats=LOWORD(msg.lParam);
                int delta=(msg.wParam==VK_UP||msg.wParam==VK_RIGHT)?1:-1;
                while(repeats--&&display.connected())change(c,delta);
                continue;
            }
        }
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    CloseHandle(mutex);return 0;
}
