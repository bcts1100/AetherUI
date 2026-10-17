#pragma once
#include "platform/windows.hpp"
#include <wincodec.h>
namespace aether::test {
using namespace aether;
inline bool captureOwnedClient(HWND window,const std::filesystem::path& path){
 RECT client{};GetClientRect(window,&client);POINT origin{};ClientToScreen(window,&origin);const int width=client.right,height=client.bottom;if(width<1||height<1)return false;
 for(int y:{height/4,height/2,3*height/4})for(int x:{width/4,width/2,3*width/4}){auto top=WindowFromPoint({origin.x+x,origin.y+y});if(top!=window&&GetAncestor(top,GA_ROOT)!=window)return false;}
 DwmFlush();HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);if(!screen||!memory){if(memory)DeleteDC(memory);if(screen)ReleaseDC(nullptr,screen);return false;}
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap){DeleteDC(memory);ReleaseDC(nullptr,screen);return false;}
 auto previous=SelectObject(memory,bitmap);const bool copied=BitBlt(memory,0,0,width,height,screen,origin.x,origin.y,SRCCOPY|CAPTUREBLT)!=FALSE;
 bool saved=false;if(copied){auto* data=static_cast<BYTE*>(pixels);for(size_t i=3;i<static_cast<size_t>(width)*height*4;i+=4)data[i]=255;
  win::ComPtr<IWICImagingFactory> factory;win::ComPtr<IWICStream> stream;win::ComPtr<IWICBitmapEncoder> encoder;win::ComPtr<IWICBitmapFrameEncode> frame;win::ComPtr<IPropertyBag2> properties;
  if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE))&&SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))&&SUCCEEDED(encoder->CreateNewFrame(&frame,&properties))&&SUCCEEDED(frame->Initialize(properties.Get()))&&SUCCEEDED(frame->SetSize(static_cast<UINT>(width),static_cast<UINT>(height)))){
   auto format=GUID_WICPixelFormat32bppBGRA;if(SUCCEEDED(frame->SetPixelFormat(&format))&&IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA)&&SUCCEEDED(frame->WritePixels(static_cast<UINT>(height),static_cast<UINT>(width*4),static_cast<UINT>(width*height*4),data))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()))saved=true;
  }
 }
 SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);return saved;
}
}
