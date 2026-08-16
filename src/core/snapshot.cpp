#include "snapshot.h"
#include <windows.h>
#include <winternl.h>
#include <psapi.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

namespace memcore {
namespace {
constexpr ULONG kSystemProcessInformation = 5;
constexpr LONG kLengthMismatch = static_cast<LONG>(0xC0000004u);
struct ProcessRecord {
    ULONG next_entry_offset, number_of_threads;
    LARGE_INTEGER working_set_private_size;
    ULONG hard_fault_count, number_of_threads_high_watermark;
    ULONGLONG cycle_time;
    LARGE_INTEGER create_time, user_time, kernel_time;
    UNICODE_STRING image_name;
    LONG base_priority;
    HANDLE process_id, parent_process_id;
    ULONG handle_count, session_id;
    ULONG_PTR unique_process_key;
    SIZE_T peak_virtual_size, virtual_size;
    ULONG reserved_process_fault_count;
    SIZE_T peak_working_set_size, working_set_size;
    SIZE_T quota_peak_paged_pool_usage, quota_paged_pool_usage;
    SIZE_T quota_peak_non_paged_pool_usage, quota_non_paged_pool_usage;
    SIZE_T pagefile_usage, peak_pagefile_usage, private_page_count;
    LARGE_INTEGER read_operation_count, write_operation_count, other_operation_count;
    LARGE_INTEGER read_transfer_count, write_transfer_count, other_transfer_count;
};
using NtQueryFunction = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using GetPackageFunction = LONG(WINAPI*)(HANDLE, UINT32*, PWSTR);

class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE value = nullptr) : value_(value) {}
    ~UniqueHandle() { if (*this) CloseHandle(value_); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};

std::wstring ErrorText(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, nullptr, error, 0,
                                        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring result = length && buffer ? std::wstring(buffer, length) : L"unknown error";
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result;
}

std::wstring ToString(const UNICODE_STRING& value) {
    return value.Buffer && value.Length
        ? std::wstring(value.Buffer, value.Length / sizeof(wchar_t)) : std::wstring();
}

void QueryDetails(ProcessInfo& process) {
    if (process.process_id == 0) return;
    UniqueHandle handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.process_id));
    if (!handle) return;
    wchar_t path[32768]{};
    DWORD length = static_cast<DWORD>(std::size(path));
    if (QueryFullProcessImageNameW(handle.get(), 0, path, &length))
        process.exe_path.assign(path, length);

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    auto get_package = kernel32
        ? reinterpret_cast<GetPackageFunction>(GetProcAddress(kernel32, "GetPackageFamilyName"))
        : nullptr;
    if (!get_package) return;
    UINT32 package_length = 0;
    if (get_package(handle.get(), &package_length, nullptr) != ERROR_INSUFFICIENT_BUFFER ||
        package_length == 0) return;
    std::vector<wchar_t> package(package_length);
    if (get_package(handle.get(), &package_length, package.data()) == ERROR_SUCCESS) {
        if (package_length && package[package_length - 1] == L'\0') --package_length;
        process.package_family_name.assign(package.data(), package_length);
    }
}

bool ReadProcessBuffer(std::vector<std::byte>& buffer, std::wstring& error) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto query = ntdll
        ? reinterpret_cast<NtQueryFunction>(GetProcAddress(ntdll, "NtQuerySystemInformation"))
        : nullptr;
    if (!query) { error = L"NtQuerySystemInformation is unavailable."; return false; }

    constexpr std::size_t maximum = 64U * 1024U * 1024U;
    buffer.resize(1024U * 1024U);
    for (;;) {
        ULONG returned = 0;
        const LONG status = query(kSystemProcessInformation, buffer.data(),
                                  static_cast<ULONG>(buffer.size()), &returned);
        if (status >= 0) return true;
        if (status != kLengthMismatch || buffer.size() >= maximum) {
            wchar_t text[16]{};
            swprintf_s(text, L"%08lX", static_cast<unsigned long>(status));
            error = L"NtQuerySystemInformation failed with status 0x" + std::wstring(text);
            return false;
        }
        const std::size_t next = returned > buffer.size()
            ? static_cast<std::size_t>(returned) : buffer.size() * 2U;
        buffer.resize(std::min(next, maximum));
    }
}

void ReadPerformance(SystemStats& stats) {
    PERFORMANCE_INFORMATION info{};
    info.cb = sizeof(info);
    if (GetPerformanceInfo(&info, sizeof(info))) {
        const std::uint64_t page_size = info.PageSize;
        stats.commit_total = static_cast<std::uint64_t>(info.CommitTotal) * page_size;
        stats.commit_limit = static_cast<std::uint64_t>(info.CommitLimit) * page_size;
        stats.physical_total = static_cast<std::uint64_t>(info.PhysicalTotal) * page_size;
        stats.physical_available = static_cast<std::uint64_t>(info.PhysicalAvailable) * page_size;
    } else {
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        if (GlobalMemoryStatusEx(&memory)) {
            stats.physical_total = memory.ullTotalPhys;
            stats.physical_available = memory.ullAvailPhys;
        }
    }
}
}
bool CaptureSnapshot(Snapshot& snapshot, std::wstring& error_message) {
    snapshot = {};
    LARGE_INTEGER timestamp{};
    QueryPerformanceCounter(&timestamp);
    snapshot.captured_at_qpc = static_cast<std::uint64_t>(timestamp.QuadPart);

    std::vector<std::byte> buffer;
    if (!ReadProcessBuffer(buffer, error_message)) return false;
    std::byte* current = buffer.data();
    const std::byte* end = buffer.data() + buffer.size();

    for (;;) {
        if (current + sizeof(ProcessRecord) > end) {
            error_message = L"NtQuerySystemInformation returned a truncated process record.";
            return false;
        }
        const auto* record = reinterpret_cast<const ProcessRecord*>(current);
        ProcessInfo process;
        process.process_id = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(record->process_id));
        process.parent_process_id =
            static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(record->parent_process_id));
        process.session_id = record->session_id;
        process.create_time = static_cast<std::uint64_t>(record->create_time.QuadPart);
        process.working_set = record->working_set_private_size.QuadPart > 0
            ? static_cast<std::uint64_t>(record->working_set_private_size.QuadPart)
            : static_cast<std::uint64_t>(record->working_set_size);
        process.commit = static_cast<std::uint64_t>(record->pagefile_usage);
        process.hard_faults = record->hard_fault_count;
        process.image_name = ToString(record->image_name);
        QueryDetails(process);
        if (process.exe_path.empty()) process.exe_path = process.image_name;
        snapshot.system.hard_faults += process.hard_faults;
        snapshot.processes.push_back(std::move(process));

        if (!record->next_entry_offset) break;
        if (record->next_entry_offset > static_cast<ULONG>(end - current) ||
            current + record->next_entry_offset >= end) {
            error_message = L"NtQuerySystemInformation returned an invalid process offset.";
            return false;
        }
        current += record->next_entry_offset;
    }
    ReadPerformance(snapshot.system);
    return true;
}
}
