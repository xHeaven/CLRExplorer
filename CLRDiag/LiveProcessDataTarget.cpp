#include "pch.h"
#include "LiveProcessDataTarget.h"
#include <metahost.h>
#include <string>
#include "CLRDataTarget.h"
#include "CLRDiag.h"
#include "sos.h"
#include <winternl.h>

#pragma comment(lib, "mscoree")

extern "C" NTSTATUS NTAPI NtSuspendProcess(HANDLE hProcess);
extern "C" NTSTATUS NTAPI NtResumeProcess(HANDLE hProcess);

#pragma comment(lib, "ntdll")

LiveProcessDataTarget::LiveProcessDataTarget(DWORD pid) : _pid(pid) {
}

DWORD LiveProcessDataTarget::GetProcessId() const {
	return _pid;
}

bool LiveProcessDataTarget::Suspend() {
	auto hProcess = ::OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, _pid);
	if (!hProcess)
		return false;

	auto status = NtSuspendProcess(hProcess);
	::CloseHandle(hProcess);
	return NT_SUCCESS(status);
}

bool LiveProcessDataTarget::Resume() {
	auto hProcess = ::OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, _pid);
	if (!hProcess)
		return false;

	auto status = NtResumeProcess(hProcess);
	::CloseHandle(hProcess);
	return NT_SUCCESS(status);
}

HRESULT LiveProcessDataTarget::Init() {
	CString path, dacFile;
	if (FindModule(L"clr.dll", path))
		dacFile = L"mscordacwks.dll";
	else if (FindModule(L"coreclr.dll", path))
		dacFile = L"mscordaccore.dll";

	path = path.Left(path.ReverseFind(L'\\') + 1) + dacFile;

	auto hDac = ::LoadLibrary(path);
	if (!hDac)
		return HRESULT_FROM_WIN32(::GetLastError());

	auto pCLRCreateData = (PFN_CLRDataCreateInstance)::GetProcAddress(hDac, "CLRDataCreateInstance");
	if(!pCLRCreateData)
		return HRESULT_FROM_WIN32(::GetLastError());

	CComObject<CCLRDataTarget>* target;
	target->CreateInstance(&target);
	auto hr = target->Init(this);
	if (FAILED(hr))
		return hr;
	_clrTarget = target;

	CComPtr<ISosDac> spSos;
	hr = pCLRCreateData(__uuidof(ISosDac), target,  reinterpret_cast<void**>(&spSos));
	if (FAILED(hr))
		return hr;

	_spSos = spSos;
	return hr;
}

bool LiveProcessDataTarget::FindModule(PCWSTR name, CString& path) const {
	auto hProcess = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, _pid);
	if (!hProcess)
		return false;

	auto hSnapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, _pid);
	MODULEENTRY32 me;
	me.dwSize = sizeof(me);
	Module32First(hSnapshot, &me);
	do {
		auto filename = ::wcsrchr(me.szExePath, L'\\') + 1;
		if (_wcsicmp(filename, name) == 0) {
			path = me.szExePath;
			break;
		}
	} while (::Module32Next(hSnapshot, &me));
	::CloseHandle(hSnapshot);

	return !path.IsEmpty();
}