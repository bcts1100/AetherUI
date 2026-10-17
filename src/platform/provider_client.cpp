#include "provider_client.hpp"
#include <array>
#include <limits>
#include <cstring>
namespace aether::win {
namespace {
struct Packet {std::uint32_t magic,version,operation,sequence,size,status;};
constexpr std::size_t ChannelSize=sizeof(Packet)+ProviderPayloadLimit;
struct View {
 void* data=nullptr;
 ~View(){if(data)UnmapViewOfFile(data);}
 void reset(){if(data){UnmapViewOfFile(data);data=nullptr;}}
};
std::wstring quote(std::wstring_view value){
 std::wstring result=L"\"";std::size_t slashes=0;
 for(wchar_t c:value){
  if(c==L'\\'){++slashes;continue;}
  if(c==L'"'){result.append(slashes*2+1,L'\\');result+=c;}
  else{result.append(slashes,L'\\');result+=c;}
  slashes=0;
 }
 result.append(slashes*2,L'\\');result+=L'"';return result;
}
bool decimal(std::wstring_view value,std::uintptr_t& result){
 if(value.empty())return false;result=0;
 for(wchar_t c:value){if(c<L'0'||c>L'9')return false;const auto digit=static_cast<std::uintptr_t>(c-L'0');
  if(result>(std::numeric_limits<std::uintptr_t>::max()-digit)/10)return false;
  result=result*10+digit;}
 return result!=0;
}
bool operationValid(std::uint32_t value){return value>=1&&value<=14;}
struct Attributes {
 std::vector<std::uint8_t> bytes;LPPROC_THREAD_ATTRIBUTE_LIST list=nullptr;std::array<HANDLE,1> jobs{};
 ~Attributes(){if(list)DeleteProcThreadAttributeList(list);}
 bool initialize(const std::array<HANDLE,4>& handles,HANDLE job){
  jobs[0]=job;SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,2,0,&size);
  if(!size)return false;bytes.resize(size);list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes.data());
  if(!InitializeProcThreadAttributeList(list,2,0,&size)){list=nullptr;return false;}
  return UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,const_cast<HANDLE*>(handles.data()),sizeof(handles),nullptr,nullptr)
   &&UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,jobs.data(),sizeof(jobs),nullptr,nullptr);
 }
};
}
struct ProviderClient::Impl {
 ProviderProcessOptions options;Handle mapping,request,response,parent,process,job;View view;
 DWORD pid=0;std::uint32_t sequence=0;
 explicit Impl(ProviderProcessOptions value):options(std::move(value)){}
 void retire(){
  if(process&&WaitForSingleObject(process.value,0)!=WAIT_OBJECT_0){
   if(job)TerminateJobObject(job.value,1);
   WaitForSingleObject(process.value,1000);
  }
  view.reset();process.reset();job.reset();parent.reset();response.reset();request.reset();mapping.reset();pid=0;
 }
 bool start(){
  if(process)return true;
  auto executable=options.executable.empty()?executablePath().parent_path()/L"AetherProvider.exe":options.executable;
  if(!executable.is_absolute())return false;
  SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
  mapping.reset(CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,static_cast<DWORD>(ChannelSize),nullptr));
  request.reset(CreateEventW(&security,FALSE,FALSE,nullptr));response.reset(CreateEventW(&security,FALSE,FALSE,nullptr));
  HANDLE parentCopy=nullptr;
  if(!DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&parentCopy,SYNCHRONIZE,TRUE,0)){retire();return false;}
  parent.reset(parentCopy);
  if(!mapping||!request||!response||!parent){retire();return false;}
  view.data=MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,ChannelSize);
  job.reset(CreateJobObjectW(nullptr,nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
  limits.BasicLimitInformation.ActiveProcessLimit=1;
  if(options.launchBreakaway)limits.BasicLimitInformation.LimitFlags|=JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
  if(!view.data||!job||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){retire();return false;}
  const std::array<HANDLE,4> inherited{mapping.value,request.value,response.value,parent.value};
  Attributes attributes;if(!attributes.initialize(inherited,job.value)){retire();return false;}
  STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes.list;
  std::wstring command=quote(executable.native())+L" --provider-channel";
  for(HANDLE handle:inherited)command+=L" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(handle));
  command+=L" "+quote(options.mode);
  PROCESS_INFORMATION created{};
  BOOL success=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
   CREATE_SUSPENDED|CREATE_NO_WINDOW|(options.launchBreakaway?NORMAL_PRIORITY_CLASS:BELOW_NORMAL_PRIORITY_CLASS)|EXTENDED_STARTUPINFO_PRESENT,
   nullptr,nullptr,&startup.StartupInfo,&created);
  for(HANDLE handle:inherited)SetHandleInformation(handle,HANDLE_FLAG_INHERIT,0);
  if(!success){retire();return false;}
  process.reset(created.hProcess);Handle mainThread(created.hThread);pid=created.dwProcessId;

  if(ResumeThread(mainThread.value)==static_cast<DWORD>(-1)){retire();return false;}
  return true;
 }
};
ProviderClient::ProviderClient(ProviderProcessOptions options):impl_(std::make_unique<Impl>(std::move(options))){}
ProviderClient::~ProviderClient(){close();}
DWORD ProviderClient::processId()const{return impl_->pid;}
bool ProviderClient::prepare(){if(impl_->process&&WaitForSingleObject(impl_->process.value,0)!=WAIT_TIMEOUT)impl_->retire();const bool running=impl_->process.value!=nullptr;if(!impl_->start()){++statistics_.failed;return false;}if(!running)++statistics_.started;return true;}
ProviderResult ProviderClient::call(ProviderOperation operation,const ProviderBytes& data,HANDLE stop,DWORD timeout){
 if(stop&&WaitForSingleObject(stop,0)==WAIT_OBJECT_0){++statistics_.cancelled;impl_->retire();return {ProviderOutcome::Cancelled,{}};}
 if(data.size()>ProviderPayloadLimit||!operationValid(static_cast<std::uint32_t>(operation))){++statistics_.failed;return {ProviderOutcome::Malformed,{}};}
 if(impl_->process&&WaitForSingleObject(impl_->process.value,0)!=WAIT_TIMEOUT)impl_->retire();
 const bool wasRunning=impl_->process.value!=nullptr;
 if(!impl_->start()){++statistics_.failed;return {ProviderOutcome::Unavailable,{}};}
 if(!wasRunning)++statistics_.started;
 auto* packet=static_cast<Packet*>(impl_->view.data);
 ResetEvent(impl_->response.value);
 if(++impl_->sequence==0)++impl_->sequence;
 *packet={ProviderMagic,ProviderVersion,static_cast<std::uint32_t>(operation),impl_->sequence,static_cast<std::uint32_t>(data.size()),0};
 if(!data.empty())memcpy(packet+1,data.data(),data.size());
 MemoryBarrier();
 if(!SetEvent(impl_->request.value)){impl_->retire();++statistics_.failed;return {ProviderOutcome::Exited,{}};}
 std::array<HANDLE,3> waiting{impl_->response.value,impl_->process.value,stop};
 const DWORD count=stop?3:2;
 const DWORD result=WaitForMultipleObjects(count,waiting.data(),FALSE,timeout);
 if(result!=WAIT_OBJECT_0){
  ProviderOutcome outcome=ProviderOutcome::Exited;
  if(result==WAIT_TIMEOUT){outcome=ProviderOutcome::Timeout;++statistics_.timeouts;}
  else if(stop&&result==WAIT_OBJECT_0+2){outcome=ProviderOutcome::Cancelled;++statistics_.cancelled;}
  else ++statistics_.failed;
  impl_->retire();return {outcome,{}};
 }
 MemoryBarrier();
 const Packet reply=*packet;
 if(reply.magic!=ProviderMagic||reply.version!=ProviderVersion||reply.operation!=static_cast<std::uint32_t>(operation)
    ||reply.sequence!=impl_->sequence||reply.size>ProviderPayloadLimit||reply.status>1){
  impl_->retire();++statistics_.failed;return {ProviderOutcome::Malformed,{}};
 }
 if(reply.status){++statistics_.failed;return {ProviderOutcome::Failed,{}};}
 ProviderBytes bytes(reply.size);if(reply.size)memcpy(bytes.data(),packet+1,reply.size);
 ++statistics_.completed;return {ProviderOutcome::Ok,std::move(bytes)};
}
void ProviderClient::close(DWORD grace){
 if(impl_->process&&WaitForSingleObject(impl_->process.value,0)==WAIT_TIMEOUT){
  auto* packet=static_cast<Packet*>(impl_->view.data);
  *packet={ProviderMagic,ProviderVersion,3,++impl_->sequence,0,0};MemoryBarrier();
  SetEvent(impl_->request.value);WaitForSingleObject(impl_->process.value,grace);
 }
 impl_->retire();
}
int runProviderHost(int argc,wchar_t** argv,const ProviderHandler& handler){
 if(argc!=7||std::wstring_view(argv[1])!=L"--provider-channel")return 2;
 std::array<std::uintptr_t,4> raw{};
 for(std::size_t i=0;i<raw.size();++i)if(!decimal(argv[i+2],raw[i]))return 2;
 Handle mapping(reinterpret_cast<HANDLE>(raw[0])),request(reinterpret_cast<HANDLE>(raw[1])),
  response(reinterpret_cast<HANDLE>(raw[2])),parent(reinterpret_cast<HANDLE>(raw[3]));
 View view;view.data=MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,ChannelSize);
 if(!view.data)return 2;
 const std::array<HANDLE,2> waiting{request.value,parent.value};
 std::uint32_t previous=0;
 for(;;){
  const DWORD result=WaitForMultipleObjects(2,waiting.data(),FALSE,INFINITE);
  if(result==WAIT_OBJECT_0+1)return 0;if(result!=WAIT_OBJECT_0)return 2;
  MemoryBarrier();auto* packet=static_cast<Packet*>(view.data);const Packet input=*packet;
  if(input.magic!=ProviderMagic||input.version!=ProviderVersion||!operationValid(input.operation)
   ||!input.sequence||input.sequence==previous||input.size>ProviderPayloadLimit)return 2;
  previous=input.sequence;
  if(input.operation==3)return 0;
  ProviderBytes data(input.size);if(input.size)memcpy(data.data(),packet+1,input.size);
  ProviderWireReply reply;
  try{reply=handler(static_cast<ProviderOperation>(input.operation),data,argv[6]);}
  catch(...){reply={1,{}};}
  if(reply.bytes.size()>ProviderPayloadLimit)reply={1,{}};
  *packet={ProviderMagic,ProviderVersion,input.operation,input.sequence,static_cast<std::uint32_t>(reply.bytes.size()),reply.status};
  if(!reply.bytes.empty())memcpy(packet+1,reply.bytes.data(),reply.bytes.size());
  MemoryBarrier();if(!SetEvent(response.value))return 2;
 }
}
}


