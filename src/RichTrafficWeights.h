#pragma once

#include <windows.h>

void SetPluginModule(HMODULE module);
void ScriptMain();
void OnKeyboard(DWORD key, WORD repeats, BYTE scanCode, BOOL isExtended,
                BOOL isWithAlt, BOOL wasDownBefore, BOOL isUpNow);
