#include "Android/AdbClient.h"
#include "Device/DeviceCatalog.h"
#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>

namespace audiobridge::android {
namespace {
struct Handle {
    HANDLE value{};
    ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
bool ascii_digit(wchar_t c){return c>=L'0'&&c<=L'9';}
std::vector<wchar_t> adb_environment(){
    // The native Windows backend only enumerates the standard ADB interface
    // GUID. libusb also finds WinUSB devices exposing a vendor-specific GUID.
    struct Environment {
        wchar_t* value=GetEnvironmentStringsW();
        ~Environment(){if(value)FreeEnvironmentStringsW(value);}
    } inherited;
    if(!inherited.value)throw std::runtime_error("Cannot read adb environment");
    std::vector<std::wstring> entries;bool configured=false;
    for(const wchar_t* entry=inherited.value;*entry;){
        entries.emplace_back(entry);entry+=entries.back().size()+1;
        if(_wcsnicmp(entries.back().c_str(),L"ADB_LIBUSB=",11)==0)configured=true;
    }
    // Respect an explicit override, and do not modify the parent's environment.
    if(!configured)entries.emplace_back(L"ADB_LIBUSB=1");
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    std::vector<wchar_t> result;
    for(const auto& entry:entries){result.insert(result.end(),entry.begin(),entry.end());result.push_back(L'\0');}
    result.push_back(L'\0');return result;
}
}
std::wstring quote_argument(std::wstring_view value){
    if(value.find(L'\0')!=std::wstring_view::npos)throw std::invalid_argument("NUL in adb argument");
    std::wstring result=L"\"";size_t slashes=0;
    for(auto c:value){
        if(c==L'\\'){++slashes;continue;}
        result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;
    }
    result.append(slashes*2,L'\\');return result+L'\"';
}
AdbProcess::AdbProcess(const std::filesystem::path& executable,const std::vector<std::wstring>& arguments){
    if(!std::filesystem::is_regular_file(executable))
        throw std::runtime_error("缺少 Android/adb.exe，请完整解压 AudioBridge（构建请运行 Prepare-Scrcpy.ps1）");
    auto command=quote_argument(executable.wstring());
    for(const auto& argument:arguments)command+=L" "+quote_argument(argument);
    auto environment=adb_environment();
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    Handle reader,writer,input;
    if(!CreatePipe(&reader.value,&writer.value,&security,0) || !SetHandleInformation(reader.value,HANDLE_FLAG_INHERIT,0))
        throw std::runtime_error("Cannot create adb output pipe");
    input.value=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    if(input.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open adb input");
    SIZE_T size{};InitializeProcThreadAttributeList(nullptr,1,0,&size);
    std::vector<unsigned char> storage(size);
    auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if(!InitializeProcThreadAttributeList(attributes,1,0,&size))throw std::runtime_error("Cannot initialize adb process attributes");
    const HANDLE inherited[]{writer.value,input.value};
    const bool updated=UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        const_cast<HANDLE*>(inherited),sizeof(inherited),nullptr,nullptr)!=FALSE;
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=input.value;
    startup.StartupInfo.hStdOutput=startup.StartupInfo.hStdError=writer.value;
    PROCESS_INFORMATION info{};
    const bool launched=updated && CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT,environment.data(),executable.parent_path().c_str(),&startup.StartupInfo,&info);
    DeleteProcThreadAttributeList(attributes);
    if(!launched)throw std::runtime_error("Cannot launch adb: "+std::to_string(GetLastError()));
    process_=info.hProcess;CloseHandle(info.hThread);read_=reader.value;reader.value=nullptr;
}
AdbProcess::~AdbProcess(){terminate();if(process_)CloseHandle(process_);if(read_)CloseHandle(read_);}
void AdbProcess::terminate() noexcept {
    if(process_&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT){TerminateProcess(process_,1);WaitForSingleObject(process_,1000);}
}
void AdbProcess::drain(){
    // Bounded per tick as well as in memory, so a noisy server cannot starve stop.
    for(size_t total=0;total<65536;){
        DWORD available{};
        if(!PeekNamedPipe(read_,nullptr,0,nullptr,&available,nullptr)||!available)return;
        std::array<char,4096> buffer{};DWORD count{};
        if(!ReadFile(read_,buffer.data(),std::min<DWORD>(available,static_cast<DWORD>(buffer.size())),&count,nullptr)||!count)return;
        output_.append(buffer.data(),count);total+=count;
        if(output_.size()>65536)output_.erase(0,output_.size()-65536);
    }
}
bool AdbProcess::exited(){drain();return WaitForSingleObject(process_,0)==WAIT_OBJECT_0;}
DWORD AdbProcess::exit_code() const {DWORD code{};if(!GetExitCodeProcess(process_,&code))throw std::runtime_error("Cannot query adb exit code");return code;}
const std::string& AdbProcess::output(){drain();return output_;}
std::string AdbClient::run(const std::vector<std::wstring>& arguments,std::stop_token stop,DWORD timeout_ms) const {
    if(stop.stop_requested())throw std::runtime_error("ADB operation cancelled");
    AdbProcess child(executable_,arguments);const auto deadline=GetTickCount64()+timeout_ms;
    while(!child.exited()){
        if(stop.stop_requested())throw std::runtime_error("ADB operation cancelled");
        if(GetTickCount64()>=deadline)throw std::runtime_error("ADB 操作超时，请检查设备授权和网络连接");
        Sleep(20);
    }
    const auto result=child.output();
    if(child.exit_code()!=0)throw std::runtime_error("ADB: "+result);
    return result;
}
std::vector<AB_Device> parse_devices(std::string_view output,Transport transport){
    std::vector<AB_Device> devices;std::istringstream lines{std::string(output)};std::string line;
    while(std::getline(lines,line)){
        std::istringstream fields(line);std::string serial,state;fields>>serial>>state;
        if(state!="device"&&state!="offline"&&state!="unauthorized")continue;
        const bool wireless=serial.find(':')!=std::string::npos||serial.find("._adb-tls-connect._tcp")!=std::string::npos;
        if((transport==Transport::Wireless)!=wireless||serial.starts_with("emulator-"))continue;
        auto id=widen(serial);if(id.empty()||id.size()>=128)continue;
        std::string field,model;
        while(fields>>field)if(field.starts_with("model:"))model=field.substr(6);
        std::replace(model.begin(),model.end(),'_',' ');
        AB_Device device{};copy_text(device.id,id);copy_text(device.model,widen(model));copy_text(device.os,L"Android");
        device.paired=state!="unauthorized";device.accessible=state=="device";
        auto label=(model.empty()?id:widen(model))+L" ["+id+L"]";
        if(state=="unauthorized")label+=L" — 请在手机允许调试";
        if(state=="offline")label+=L" — 离线";
        copy_text(device.name,label);devices.push_back(device);
    }
    return devices;
}
std::vector<AB_Device> AdbClient::devices(Transport transport) const {return parse_devices(run({L"devices",L"-l"}),transport);}
void validate_endpoint(std::wstring_view endpoint){
    const auto colon=endpoint.rfind(L':');
    if(endpoint.size()>253||colon==std::wstring_view::npos||colon==0||colon+1==endpoint.size())
        throw std::invalid_argument("请输入手机无线调试页面的 IP:端口");
    const auto host=endpoint.substr(0,colon),port=endpoint.substr(colon+1);
    if(port.size()>5||!std::all_of(port.begin(),port.end(),ascii_digit))throw std::invalid_argument("端口必须是 1–65535");
    const auto number=std::stoul(std::wstring(port));
    if(number==0||number>65535)throw std::invalid_argument("端口必须是 1–65535");
    const bool ipv6=host.front()==L'['&&host.back()==L']'&&host.size()>2;
    if(host.front()==L'-'||!std::all_of(host.begin(),host.end(),[ipv6](wchar_t c){
        return ascii_digit(c)||(c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||c==L'.'||c==L'-'||(ipv6&&(c==L':'||c==L'['||c==L']'));
    }))throw std::invalid_argument("无线地址格式无效，请使用 IP:端口");
}
void AdbClient::connect(std::wstring_view endpoint) const {
    validate_endpoint(endpoint);const auto result=run({L"connect",std::wstring(endpoint)});
    if(result.find("connected to ")==std::string::npos)throw std::runtime_error("ADB 连接失败: "+result);
}
void AdbClient::pair(std::wstring_view endpoint,std::wstring_view code) const {
    validate_endpoint(endpoint);
    if(code.size()!=6||!std::all_of(code.begin(),code.end(),ascii_digit))throw std::invalid_argument("配对码必须是六位数字");
    const auto result=run({L"pair",std::wstring(endpoint),std::wstring(code)},{},30000);
    if(result.find("Successfully paired")==std::string::npos)throw std::runtime_error("ADB 配对失败: "+result);
}
}
