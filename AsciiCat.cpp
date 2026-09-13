/* {Nz} Ascii Cat Desktop Companion | v.1.0 | 13.09.2026
========================================================

AsciiCat v1 - Windows 10+, C++17, no third-party libraries.
BUILD: Open "x64 Native Tools Command Prompt for VS", cd to the AsciiCat.cpp folder:
cl /nologo /std:c++17 /O1 /EHsc /MT AsciiCat.cpp /link /SUBSYSTEM:WINDOWS /OUT:AsciiCat.exe
Drag glyphs to move; click to pet; right-click to close.
Middle-click cycles all demonstration states; after the last, returns to auto.

Optional AsciiCat.ini BESIDE THE EXE (restart after editing):
[cat]
IdleSeconds=300
CpuWarning=90
RamWarning=90
TempWarning=85
NightStart=23
NightEnd=7
Jobs=cl.exe;MSBuild.exe;
WorkApps=devenv.exe;Code.exe;blender.exe;sublime_text.exe;rider64.exe;jetbrains_client64.exe

Status file: asciicat-status.txt BESIDE THE EXE.
Fallback: Windows Documents\asciicat-status.txt (supports redirected Documents).
UTF-8 text, one key=value per line; updated every second.
mood=auto|normal|sleep|busy|happy|alert|offline|stretch|work
task=Rendering frame 42
temperature=72.5
ttl=60

Also accepts old one-word "happy". ttl is seconds since file modification,
defaults to 60, 0 means never expire. An expired/deleted file returns to auto.
Explicit mood overrides automation. temperature is optional external telemetry,
NOT a CPU temperature inferred from CPU usage. Keep rewriting telemetry.
A render within Blender cannot be inferred from blender.exe being open;
use mood=busy/task=Rendering, then mood=happy/task=Render complete.
Process monitoring sees only jobs lasting long enough to be sampled (1 s);
completion means all watched processes ended, NOT that they succeeded.
Automatic priority: load/temperature warning > short event > jobs > offline
> idle/night sleep > foreground work > normal. CPU warning needs 5 samples.
Night sleep requires at least 30 seconds idle; activity wakes the cat.
Hourly stretch, morning wake, connection restoration, job end, click reactions.
Network is Windows' reported connection availability, not an Internet probe.
CPU GetSystemTimes sampling has Windows processor-group limits on >64 CPUs.

Lowest priority, topmost, takes around 2MB of RAM, works on Win7 and up.
*/ 

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <wininet.h>
#include <shlobj.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <cmath>
#include <cwchar>
#include <cstdlib>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"wininet.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"uuid.lib")

namespace {
enum class Mood { Normal, Sleep, Busy, Happy, Alert, Offline, Stretch, Work };
struct View { Mood mood; std::wstring label; };
std::wstring directory, statusPath, fallbackPath;
int idleSeconds=300, cpuWarning=90, ramWarning=90, tempWarning=85;
int nightStart=23, nightEnd=7;
std::vector<std::wstring> jobs, workApps;
HFONT catFont=nullptr, labelFont=nullptr;
unsigned frame=0;
bool dragging=false, moved=false;
POINT dragOrigin{}, windowOrigin{};
double cpu=-1;
DWORD ram=0;
int hotSamples=0, demo=-1;
ULONGLONG eventUntil=0;
View eventView{Mood::Happy,L"Hello! AsciiCat v1"};
View current{Mood::Happy,L"Hello! AsciiCat v1"};
std::wstring metrics;
ULONGLONG cpuIdle=0,cpuTotal=0;
bool cpuPrimed=false, networkPrimed=false, wasOnline=false;
bool wasBusy=false, clockPrimed=false, wasNight=false;
int lastHour=-1;
std::wstring jobName;
bool SetLowPriority() {
    return SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS) != 0;
}
ULONGLONG number(FILETIME f) { return (ULONGLONG(f.dwHighDateTime)<<32)|f.dwLowDateTime; }
std::wstring trim(std::wstring s) {
    auto a=s.find_first_not_of(L" \t\r\n");
    return a==s.npos?L"":s.substr(a,s.find_last_not_of(L" \t\r\n")-a+1);
}
std::wstring lower(std::wstring s) {
    for(auto& c:s) c=static_cast<wchar_t>(towlower(c));
    return s;
}
std::vector<std::wstring> split(std::wstring s) {
    std::vector<std::wstring> out; std::wistringstream in(s); std::wstring x;
    while(std::getline(in,x,L';')) if(!trim(x).empty())out.push_back(lower(trim(x)));
    return out;
}
bool contains(const std::vector<std::wstring>& list,const std::wstring& name) {
    return std::find(list.begin(),list.end(),name)!=list.end();
}
void configure() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr,path,32768);
    directory=path; directory=directory.substr(0,directory.find_last_of(L"\\/")+1);
    statusPath=directory+L"asciicat-status.txt";
    PWSTR doc=nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&doc))) {
        fallbackPath=std::wstring(doc)+L"\\asciicat-status.txt"; CoTaskMemFree(doc);
    }
    const auto ini=directory+L"AsciiCat.ini";
    auto setting=[&](const wchar_t* key,int def,int lo,int hi) {
        return std::clamp(static_cast<int>(GetPrivateProfileIntW(L"cat",key,def,ini.c_str())),lo,hi);
    };
    idleSeconds=setting(L"IdleSeconds",300,1,86400);
    cpuWarning=setting(L"CpuWarning",90,1,100);
    ramWarning=setting(L"RamWarning",90,1,100);
    tempWarning=setting(L"TempWarning",85,1,150);
    nightStart=setting(L"NightStart",23,0,23); nightEnd=setting(L"NightEnd",7,0,23);
    wchar_t buf[4096]{};
    GetPrivateProfileStringW(L"cat",L"Jobs",L"cl.exe;MSBuild.exe",buf,4096,ini.c_str());jobs=split(buf);
    GetPrivateProfileStringW(L"cat",L"WorkApps",L"devenv.exe;Code.exe;blender.exe;sublime_text.exe;rider64.exe;jetbrains_client64.exe",buf,4096,ini.c_str());workApps=split(buf);
}
void signal(Mood mood,const std::wstring& label,int seconds=8) {
    eventView={mood,label}; eventUntil=GetTickCount64()+seconds*1000ULL;
}
struct Status {
    std::wstring mood,task;
    double temperature=-1;
};
Status readStatus() {
    Status result;
    HANDLE file=CreateFileW(statusPath.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE && GetLastError()==ERROR_FILE_NOT_FOUND && !fallbackPath.empty())
        file=CreateFileW(fallbackPath.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return result;
    FILETIME modified{}; LARGE_INTEGER size{}; char bytes[8193]{}; DWORD count=0;
    bool ok=GetFileTime(file,nullptr,nullptr,&modified) && GetFileSizeEx(file,&size) &&
        size.QuadPart<=8192 && ReadFile(file,bytes,8192,&count,nullptr);
    CloseHandle(file);
    if(!ok || !count) return result;
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,count,nullptr,0);
    if(!n)return result;
    std::wstring text(n,L'\0'); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,count,&text[0],n);
    if(text[0]==0xFEFF)text.erase(0,1);
    std::map<std::wstring,std::wstring> fields;
    std::wistringstream in(text); std::wstring line;
    while(std::getline(in,line)) {
        auto eq=line.find(L'=');
        if(eq!=line.npos) fields[lower(trim(line.substr(0,eq)))]=trim(line.substr(eq+1));
        else if(!trim(line).empty())fields[L"mood"]=trim(line);
    }
    double ttl=60;
    try { if(fields.count(L"ttl")) ttl=std::stod(fields[L"ttl"]); }catch(...) {}
    if(!std::isfinite(ttl)||ttl<0)ttl=60;
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    double age=number(now)>=number(modified)?double(number(now)-number(modified))/10000000.:0;
    if(ttl>0 && age>ttl)return result;
    result.mood=lower(fields[L"mood"]); result.task=fields[L"task"].substr(0,44);
    try { if(fields.count(L"temperature")) result.temperature=std::stod(fields[L"temperature"]); }catch(...) {}
    if(!std::isfinite(result.temperature))result.temperature=-1;
    return result;
}
void sampleCPU() {
    FILETIME i{},k{},u{};
    if(!GetSystemTimes(&i,&k,&u)) {cpu=-1;return;}
    auto idle=number(i),total=number(k)+number(u);
    if(cpuPrimed && total>cpuTotal && idle>=cpuIdle)
        cpu=std::clamp(100.*(1.-double(idle-cpuIdle)/double(total-cpuTotal)),0.,100.);
    cpuIdle=idle;cpuTotal=total;cpuPrimed=true;
    hotSamples=cpu>=cpuWarning?std::min(hotSamples+1,5):0;
}
std::wstring processState() {
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return L"";
    PROCESSENTRY32W p{};p.dwSize=sizeof(p);
    bool busy=false;std::wstring active,name;
    if(!Process32FirstW(snapshot,&p)){CloseHandle(snapshot);return L"";}
    do {
        auto exe=lower(p.szExeFile);
        if(contains(jobs,exe)){busy=true;name=exe;}
        if(p.th32ProcessID==foreground && contains(workApps,exe))active=exe;
    }while(Process32NextW(snapshot,&p));
    CloseHandle(snapshot);
    if(wasBusy&&!busy)signal(Mood::Happy,L"Watched jobs finished",12);
    wasBusy=busy; jobName=name;
    return active;
}
bool decodeMood(const std::wstring& s,Mood& m) {
    const wchar_t* names[]={L"normal",L"sleep",L"busy",L"happy",L"alert",L"offline",L"stretch",L"work"};
    for(int i=0;i<8;++i)if(s==names[i]){m=static_cast<Mood>(i);return true;}
    return false;
}
void update() {
    sampleCPU(); MEMORYSTATUSEX mem{};mem.dwLength=sizeof(mem);
    if(GlobalMemoryStatusEx(&mem))ram=mem.dwMemoryLoad;
    DWORD flags=0;bool online=InternetGetConnectedState(&flags,0)!=FALSE;
    if(networkPrimed&&!wasOnline&&online)signal(Mood::Happy,L"Connection restored");
    networkPrimed=true;wasOnline=online;
    auto active=processState();
    LASTINPUTINFO input{};input.cbSize=sizeof(input);
    DWORD idle=GetLastInputInfo(&input)?DWORD(GetTickCount()-input.dwTime)/1000:0;
    SYSTEMTIME clock{};GetLocalTime(&clock);
    bool night=nightStart==nightEnd?false:(nightStart>nightEnd?
        clock.wHour>=nightStart||clock.wHour<nightEnd:clock.wHour>=nightStart&&clock.wHour<nightEnd);
    int hourKey=clock.wDay*24+clock.wHour;
    if(clockPrimed && wasNight&&!night)signal(Mood::Stretch,L"Good morning!",10);
    else if(clockPrimed && lastHour!=hourKey)signal(Mood::Stretch,L"Hourly stretch",10);
    clockPrimed=true;lastHour=hourKey;wasNight=night;
    auto status=readStatus();
    Mood forced{};
    if(decodeMood(status.mood,forced))current={forced,status.task.empty()?L"Status file: "+status.mood:status.task};
    else if(status.temperature>=tempWarning)current={Mood::Alert,L"Temperature warning"};
    else if(hotSamples>=5)current={Mood::Alert,L"Sustained CPU load"};
    else if(ram>=DWORD(ramWarning))current={Mood::Alert,L"Memory pressure"};
    else if(GetTickCount64()<eventUntil)current=eventView;
    else if(wasBusy)current={Mood::Busy,L"Running: "+jobName};
    else if(!online)current={Mood::Offline,L"No connection reported"};
    else if(idle>=DWORD(idleSeconds))current={Mood::Sleep,L"Idle - resting"};
    else if(night&&idle>=30)current={Mood::Sleep,L"Night time"};
    else if(!active.empty())current={Mood::Work,L"Working: "+active};
    else current={Mood::Normal,L"Keeping you company"};
    if(!status.task.empty() && !decodeMood(status.mood,forced))current.label+=L" | "+status.task;
    wchar_t time[16]{};swprintf_s(time,L"%02u:%02u",clock.wHour,clock.wMinute);
    metrics=std::wstring(time)+L"  CPU "+(cpu<0?L"--":std::to_wstring(int(cpu)))+
        L"%  RAM "+std::to_wstring(ram)+L"%";
    if(status.temperature>=0)metrics+=L"  "+std::to_wstring(int(std::min(status.temperature,999.)))+L"C";
    if(demo>=0)current={static_cast<Mood>(demo),L"Demo "+std::to_wstring(demo+1)+L"/8 - middle-click next"};
}
const wchar_t* art() {
    bool b=(frame%4)<2;
    switch(current.mood) {
    case Mood::Sleep:return b?L"       z\n  /\\_/\\  z\n ( -.- )__\n /       _)\n(_______/":L"        Z\n  /\\_/\\ z\n ( -.- )__\n /       _)\n(_______/";
    case Mood::Busy:return b?L"  /\\_/\\\n ( o.o )  /\n /|___|\\ /\n  /===\\\n /_|_|_\\":L"  /\\_/\\\n ( o.o )  -\n /|___|\\ -\n  /===\\\n /_|_|_\\";
    case Mood::Happy:return b?L" \\ /\\_/\\ /\n  ( ^.^ )\n   > w <\n  /|   |\\\n   U   U":L"   /\\_/\\\n \\( ^.^ )/\n   > w <\n   |   |\n  /     \\";
    case Mood::Alert:return b?L" ! /\\_/\\ !\n  ( >.< )\n   > O <\n  /|   |\\\n   U   U":L"   /\\_/\\\n !( >.< )!\n   > O <\n  /|   |\\\n   U   U";
    case Mood::Offline:return b?L"   /\\_/\\  ?\n  ( ._. )\n   > ~ <\n  /|   |\\\n (_|___|_)":L" ? /\\_/\\\n  ( ._. )\n   > ~ <\n  /|   |\\\n (_|___|_)";
    case Mood::Stretch:return b?L"   /\\_/\\\n  ( -.- )\n   > o <\n  /     \\\n /       \\":L"   /\\_/\\\n  ( ^.^ )\n \\ > o < /\n  \\     /\n   U   U";
    case Mood::Work:return b?L"   /\\_/\\\n  ( o.o )\n   > ^ <\n  /|___|\\\n [_______]":L"   /\\_/\\\n  ( o.o )\n   > ^ <\n  \\|___|/\n [_______]";
    default:return frame%24==0?L"   /\\_/\\\n  ( -.- )\n   > ^ <\n  /|   |\\\n (_|___|_)":L"   /\\_/\\\n  ( o.o )\n   > ^ <\n  /|   |\\\n (_|___|_)";
    }
}
void paint(HWND window,HDC target) {
    RECT rect{};GetClientRect(window,&rect);
    HDC dc=CreateCompatibleDC(target);
    HBITMAP bitmap=CreateCompatibleBitmap(target,rect.right,rect.bottom);
    if(!dc||!bitmap){if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);return;}
    auto oldBitmap=SelectObject(dc,bitmap);
    FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(184,255,196));
    auto oldFont=SelectObject(dc,catFont);
    RECT body{8,4,rect.right,144};
    DrawTextW(dc,art(),-1,&body,DT_LEFT|DT_TOP|DT_NOPREFIX);
    SelectObject(dc,labelFont);
    RECT label{8,148,rect.right-8,174},detail{8,176,rect.right-8,198};
    DrawTextW(dc,current.label.c_str(),-1,&label,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    DrawTextW(dc,metrics.c_str(),-1,&detail,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    BitBlt(target,0,0,rect.right,rect.bottom,dc,0,0,SRCCOPY);
    SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);
    DeleteObject(bitmap);DeleteDC(dc);
}
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(window,&ps);paint(window,dc);EndPaint(window,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_TIMER:
        if(wp==1){++frame;InvalidateRect(window,nullptr,FALSE);}
        if(wp==2){update();InvalidateRect(window,nullptr,FALSE);}
        return 0;
    case WM_LBUTTONDOWN:{
        dragging=true;moved=false;GetCursorPos(&dragOrigin);
        RECT r{};GetWindowRect(window,&r);windowOrigin={r.left,r.top};SetCapture(window);return 0;}
    case WM_MOUSEMOVE:
        if(dragging) {
            POINT p{};GetCursorPos(&p);
            if(abs(p.x-dragOrigin.x)>3||abs(p.y-dragOrigin.y)>3)moved=true;
            if(moved)SetWindowPos(window,nullptr,windowOrigin.x+p.x-dragOrigin.x,
                windowOrigin.y+p.y-dragOrigin.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        }return 0;
    case WM_LBUTTONUP:
        if(dragging&&!moved){signal(Mood::Happy,L"Purr...",5);update();}
        dragging=false;ReleaseCapture();return 0;
    case WM_CAPTURECHANGED:dragging=false;return 0;
    case WM_MBUTTONUP:demo=demo==7?-1:demo+1;update();return 0;
    case WM_RBUTTONUP:DestroyWindow(window);return 0;
    case WM_DESTROY:KillTimer(window,1);KillTimer(window,2);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    SetLowPriority();
    configure();
    catFont=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");
    labelFont=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=windowProc;
    wc.lpszClassName=L"AsciiCatV1";wc.hCursor=LoadCursorW(nullptr,IDC_HAND);
    if(!RegisterClassW(&wc))return 1;
    RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);
    HWND window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,
        wc.lpszClassName,L"AsciiCat v1",WS_POPUP,area.left+32,area.top+32,430,204,nullptr,nullptr,instance,nullptr);
    if(!window)return 1;
    if(!SetLayeredWindowAttributes(window,RGB(0,0,0),0,LWA_COLORKEY))return 1;
    signal(Mood::Happy,L"AsciiCat v1 - middle-click for demos",8);update();
    ShowWindow(window,SW_SHOWNOACTIVATE);
    if(!SetTimer(window,1,250,nullptr)||!SetTimer(window,2,1000,nullptr)){
        MessageBoxW(nullptr,L"Could not start cat timers.",L"AsciiCat",MB_OK);DestroyWindow(window);return 1;
    }
    MSG message{};BOOL result;
    while((result=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}
    DeleteObject(catFont);DeleteObject(labelFont);
    return result==-1?1:0;
}
