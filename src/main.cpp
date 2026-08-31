#include "RichTrafficWeights.h"
#include "main.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        SetPluginModule(module);
        keyboardHandlerRegister(OnKeyboard);
        scriptRegister(module, ScriptMain);
        break;
    case DLL_PROCESS_DETACH:
        keyboardHandlerUnregister(OnKeyboard);
        scriptUnregister(module);
        break;
    default:
        break;
    }
    return TRUE;
}
