#include "platform/wallpaper_provider.hpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return 2;int result=2;
 try{result=aether::win::runProviderHost(argc,argv,[](aether::win::ProviderOperation operation,const aether::win::ProviderBytes& bytes,std::wstring_view){return aether::win::runWallpaperOperation(operation,bytes);});}catch(...){}
 LocalFree(argv);return result;
}
