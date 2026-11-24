#include "desktop/Autostart.h"

#include "util/Log.h"
#include "util/Win32.h"

#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <taskschd.h>
#include <wrl/client.h>

#include <format>

namespace ap::desktop {

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kName[] = L"OpenASCII"; // valor do Run e nome da tarefa

std::wstring exePath() {
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return buf;
}

bool runValueExists() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof(buf);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kName, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

bool setRunValue(bool on) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    LSTATUS st = ERROR_SUCCESS;
    if (on) {
        const std::wstring cmd = L"\"" + exePath() + L"\"";
        st = RegSetValueExW(key, kName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(key, kName);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

// A pasta raiz do Agendador de Tarefas (as tarefas do próprio utilizador ficam lá).
ComPtr<ITaskFolder> rootFolder(HRESULT& hr) {
    ComPtr<ITaskService> service;
    hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
    if (FAILED(hr)) return nullptr;
    VARIANT empty{};
    if (FAILED(hr = service->Connect(empty, empty, empty, empty))) return nullptr;
    ComPtr<ITaskFolder> folder;
    BSTR root = SysAllocString(L"\\");
    hr = service->GetFolder(root, &folder);
    SysFreeString(root);
    return SUCCEEDED(hr) ? folder : nullptr;
}

bool taskExists() {
    HRESULT hr = S_OK;
    ComPtr<ITaskFolder> folder = rootFolder(hr);
    if (!folder) return false;
    ComPtr<IRegisteredTask> task;
    BSTR name = SysAllocString(kName);
    hr = folder->GetTask(name, &task);
    SysFreeString(name);
    VARIANT_BOOL enabled = VARIANT_FALSE;
    return SUCCEEDED(hr) && task && SUCCEEDED(task->get_Enabled(&enabled)) && enabled == VARIANT_TRUE;
}

std::wstring xmlEscape(std::wstring_view s) {
    std::wstring out;
    for (wchar_t c : s) {
        switch (c) {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'"': out += L"&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

bool registerTask(std::wstring& error) {
    wchar_t user[256] = {};
    ULONG userLen = static_cast<ULONG>(std::size(user));
    if (!GetUserNameExW(NameSamCompatible, user, &userLen)) {
        error = L"não consegui saber o nome do utilizador";
        return false;
    }
    // Ao iniciar sessão (só esta conta), sem atraso; prioridade 4 = normal (o padrão das tarefas,
    // 7, é "abaixo do normal"); sem limite de tempo (o padrão mata a tarefa ao fim de 3 dias);
    // também na bateria; uma instância só.
    const std::wstring xml = std::format(
        LR"(<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo><Description>Arranca o OpenASCII (wallpapers ASCII) logo que inicias sessão.</Description></RegistrationInfo>
  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>{0}</UserId></LogonTrigger></Triggers>
  <Principals><Principal id="Author"><UserId>{0}</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <AllowHardTerminate>true</AllowHardTerminate>
    <StartWhenAvailable>false</StartWhenAvailable>
    <AllowStartOnDemand>true</AllowStartOnDemand>
    <Enabled>true</Enabled>
    <Hidden>false</Hidden>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <Priority>4</Priority>
    <IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>
  </Settings>
  <Actions Context="Author"><Exec><Command>{1}</Command></Exec></Actions>
</Task>)",
        xmlEscape(user), xmlEscape(exePath()));

    HRESULT hr = S_OK;
    ComPtr<ITaskFolder> folder = rootFolder(hr);
    if (!folder) {
        error = L"Agendador de Tarefas indisponível (" + win::hrToString(hr) + L")";
        return false;
    }
    BSTR name = SysAllocString(kName);
    BSTR text = SysAllocString(xml.c_str());
    VARIANT empty{};
    ComPtr<IRegisteredTask> task;
    hr = folder->RegisterTask(name, text, TASK_CREATE_OR_UPDATE, empty, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty,
                              &task);
    SysFreeString(name);
    SysFreeString(text);
    if (FAILED(hr)) {
        error = L"não consegui criar a tarefa agendada (" + win::hrToString(hr) + L")";
        return false;
    }
    return true;
}

void deleteTask() {
    HRESULT hr = S_OK;
    if (ComPtr<ITaskFolder> folder = rootFolder(hr)) {
        BSTR name = SysAllocString(kName);
        folder->DeleteTask(name, 0); // não existir não é erro
        SysFreeString(name);
    }
}

} // namespace

AutostartMode queryAutostart() {
    if (taskExists()) return AutostartMode::Fast;
    if (runValueExists()) return AutostartMode::Normal;
    return AutostartMode::Off;
}

bool setAutostart(AutostartMode mode, std::wstring& error) {
    switch (mode) {
    case AutostartMode::Off:
        deleteTask();
        if (!setRunValue(false)) error = L"não consegui apagar a entrada de arranque";
        break;
    case AutostartMode::Normal:
        deleteTask();
        if (!setRunValue(true)) error = L"não consegui escrever a entrada de arranque";
        break;
    case AutostartMode::Fast:
        if (registerTask(error)) {
            setRunValue(false); // as duas ao mesmo tempo arrancariam duas vezes (a 2.ª sai logo)
        } else {
            setRunValue(true); // ao menos o arranque normal
        }
        break;
    }
    if (!error.empty()) log::warn(L"Arranque com o Windows: {}", error);
    else log::info(L"Arranque com o Windows: {}", mode == AutostartMode::Off ? L"desligado"
                                                   : mode == AutostartMode::Fast ? L"rápido (tarefa agendada)"
                                                                                 : L"normal (Run)");
    return error.empty();
}

} // namespace ap::desktop
