#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <string.h>

#include "wc_platform.h"

/* Native "pick a folder" dialog. Returns false on cancel or failure.
 * COM is initialized for the duration of the dialog; if the UI thread already
 * has a different apartment model, the dialog still runs with the existing one. */
bool wc_plat_pick_folder(char *out, size_t cap) {
    if (!out || cap == 0) return false;
    out[0] = '\0';

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool initialized_here = SUCCEEDED(hr);

    IFileOpenDialog *dialog = NULL;
    hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IFileOpenDialog, (void **)&dialog);

    bool ok = false;
    if (SUCCEEDED(hr) && dialog) {
        dialog->lpVtbl->SetOptions(dialog, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                               FOS_PATHMUSTEXIST);
        hr = dialog->lpVtbl->Show(dialog, NULL);
        if (SUCCEEDED(hr)) {
            IShellItem *item = NULL;
            if (SUCCEEDED(dialog->lpVtbl->GetResult(dialog, &item)) && item) {
                PWSTR wide_path = NULL;
                if (SUCCEEDED(item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &wide_path)) &&
                    wide_path) {
                    int n = WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, out, (int)cap, NULL,
                                                NULL);
                    ok = n > 0;
                    CoTaskMemFree(wide_path);
                }
                item->lpVtbl->Release(item);
            }
        }
        dialog->lpVtbl->Release(dialog);
    }

    if (initialized_here) CoUninitialize();
    if (!ok) out[0] = '\0';
    return ok;
}
