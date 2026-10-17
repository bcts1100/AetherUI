#include "platform/provider_sources.hpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return 2;
 int result=2;
 try{result=aether::win::runProviderHost(argc,argv,[](aether::win::ProviderOperation operation,const aether::win::ProviderBytes& payload,std::wstring_view){return aether::win::runProviderOperation(operation,payload);});}catch(...){}
 LocalFree(argv);return result;
}
