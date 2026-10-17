#include "dialogs.hpp"
namespace aether::win {
namespace {
struct NoteDialog {std::string initial;std::optional<std::string> result;const wchar_t* title;const wchar_t* hint;int limit;};
INT_PTR CALLBACK noteProcedure(HWND dialog,UINT message,WPARAM w,LPARAM l){
 auto* data=reinterpret_cast<NoteDialog*>(GetWindowLongPtrW(dialog,DWLP_USER));
 if(message==WM_INITDIALOG){data=reinterpret_cast<NoteDialog*>(l);SetWindowLongPtrW(dialog,DWLP_USER,l);SetWindowTextW(dialog,data->title);
  const int dpi=dpiFor(dialog);auto px=[&](int value){return MulDiv(value,dpi,96);};RECT owner{};GetWindowRect(GetWindow(dialog,GW_OWNER),&owner);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(dialog,MONITOR_DEFAULTTONEAREST),&monitor);
  const int width=px(510),height=px(180);SetWindowPos(dialog,nullptr,std::clamp(owner.left,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-width)),std::clamp(owner.top,monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-height)),width,height,SWP_NOZORDER);
  auto control=[&](const wchar_t* klass,const wchar_t* text,DWORD style,int id,int x,int y,int widthValue,int heightValue){auto child=CreateWindowExW(wcscmp(klass,L"EDIT")==0?WS_EX_CLIENTEDGE:0,klass,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(widthValue),px(heightValue),dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return child;};
  control(L"STATIC",data->hint,0,100,14,12,470,24);auto edit=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,101,14,42,470,30);
  control(L"BUTTON",L"Save",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,292,88,90,30);control(L"BUTTON",L"Cancel",WS_TABSTOP|BS_PUSHBUTTON,IDCANCEL,394,88,90,30);
  SendMessageW(edit,EM_SETLIMITTEXT,data->limit,0);SetWindowTextW(edit,wide(data->initial).c_str());SendMessageW(edit,EM_SETSEL,0,-1);SendMessageW(dialog,DM_SETDEFID,IDOK,0);SetFocus(edit);return FALSE;
 }
 if(message==WM_COMMAND&&LOWORD(w)==IDOK&&data){data->result=utf8(windowText(GetDlgItem(dialog,101)));EndDialog(dialog,IDOK);return TRUE;}
 if(message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(w)==IDCANCEL)){EndDialog(dialog,IDCANCEL);return TRUE;}return FALSE;
}
}
std::optional<std::string> editNote(HWND owner,std::string_view initial,const wchar_t* title,const wchar_t* hint,int limit){struct Definition {DLGTEMPLATE dialog;WORD menu=0,klass=0,title=0;} definition{};definition.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;definition.dialog.cx=340;definition.dialog.cy=100;NoteDialog data{std::string(initial),{},title,hint,limit};if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&definition.dialog,owner,noteProcedure,reinterpret_cast<LPARAM>(&data))==-1)message(owner,L"Windows could not open the note editor.",MB_OK|MB_ICONERROR);return data.result;}
std::optional<std::filesystem::path> chooseFile(HWND owner,bool save,const wchar_t* title,
 const COMDLG_FILTERSPEC* filters,UINT count,const wchar_t* name,const wchar_t* extension){
 ComPtr<IFileDialog> dialog;
 HRESULT hr=save?CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)):
  CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
 if(FAILED(hr)){message(owner,L"Windows could not open the file picker.",MB_OK|MB_ICONERROR);return {};}
 DWORD flags=0;dialog->GetOptions(&flags);
 dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|(save?FOS_OVERWRITEPROMPT:FOS_FILEMUSTEXIST));
 dialog->SetTitle(title);dialog->SetFileTypes(count,filters);
 if(name)dialog->SetFileName(name);if(extension)dialog->SetDefaultExtension(extension);
 hr=dialog->Show(owner);if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};
 if(FAILED(hr)){message(owner,L"The file picker failed.",MB_OK|MB_ICONERROR);return {};}
 ComPtr<IShellItem> item;PWSTR path=nullptr;
 if(FAILED(dialog->GetResult(&item))||FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path)))return {};
 std::filesystem::path result(path);CoTaskMemFree(path);return result;
}
}
