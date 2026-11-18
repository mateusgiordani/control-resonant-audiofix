// Control Resonant AudioFix - proxy version.dll
//
// O jogo abre seus pacotes .rmdblob com FILE_FLAG_NO_BUFFERING, o que faz o
// Windows ignorar o cache de arquivos em RAM. Como o jogo relê os mesmos dados
// sem parar, o SSD satura e o streaming de áudio perde a corrida (stutter/desync).
//
// Esta DLL é carregada pelo jogo no lugar da version.dll do sistema, encaminha
// todas as funções reais para C:\Windows\System32\version.dll e intercepta
// (via IAT) CreateFileW/CreateFileA do executável para remover o
// FILE_FLAG_NO_BUFFERING nas aberturas somente-leitura dos arquivos de dados.
// Resultado: as releituras passam a vir do cache de arquivos do Windows (RAM).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

// ---------------------------------------------------------------------------
// Encaminhamento das exportações da version.dll real
// ---------------------------------------------------------------------------
#define FWD(name) __pragma(comment(linker, "/export:" #name "=C:\\Windows\\System32\\version." #name))
FWD(GetFileVersionInfoA)
FWD(GetFileVersionInfoByHandle)
FWD(GetFileVersionInfoExA)
FWD(GetFileVersionInfoExW)
FWD(GetFileVersionInfoSizeA)
FWD(GetFileVersionInfoSizeExA)
FWD(GetFileVersionInfoSizeExW)
FWD(GetFileVersionInfoSizeW)
FWD(GetFileVersionInfoW)
FWD(VerFindFileA)
FWD(VerFindFileW)
FWD(VerInstallFileA)
FWD(VerInstallFileW)
FWD(VerLanguageNameA)
FWD(VerLanguageNameW)
FWD(VerQueryValueA)
FWD(VerQueryValueW)

// ---------------------------------------------------------------------------
// Configuração (audiofix.ini ao lado da DLL)
// ---------------------------------------------------------------------------
static struct {
    BOOL  enabled;        // remove NO_BUFFERING
    BOOL  randomAccess;   // adiciona FILE_FLAG_RANDOM_ACCESS (cache mantém views)
    BOOL  log;            // grava audiofix.log
    DWORD statsSeconds;   // intervalo do log de estatísticas (0 = desliga)
    WCHAR exts[512];      // extensões afetadas, separadas por ';' (vazio = todas)
} g_cfg = { TRUE, TRUE, TRUE, 60, L".rmdblob" };

static WCHAR g_dir[MAX_PATH];

typedef HANDLE (WINAPI *CreateFileW_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL   (WINAPI *ReadFile_t)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);

static CreateFileW_t real_CreateFileW;
static CreateFileA_t real_CreateFileA;
static ReadFile_t    real_ReadFile;

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------
static HANDLE           g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logLock;

static void Log(const char *fmt, ...)
{
    if (g_log == INVALID_HANDLE_VALUE) return;
    char buf[1024];
    SYSTEMTIME t; GetLocalTime(&t);
    int n = _snprintf_s(buf, sizeof buf, _TRUNCATE, "[%02u:%02u:%02u] ", t.wHour, t.wMinute, t.wSecond);
    va_list ap; va_start(ap, fmt);
    int m = _vsnprintf_s(buf + n, sizeof buf - n - 2, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (m < 0) m = (int)strlen(buf + n);
    n += m; buf[n++] = '\r'; buf[n++] = '\n';
    EnterCriticalSection(&g_logLock);
    DWORD w; WriteFile(g_log, buf, (DWORD)n, &w, NULL);
    LeaveCriticalSection(&g_logLock);
}

// ---------------------------------------------------------------------------
// Estatísticas
// ---------------------------------------------------------------------------
#define MAX_TRACKED 4096
static volatile HANDLE g_tracked[MAX_TRACKED];   // handles de .rmdblob
static volatile LONG64 g_opens, g_patchedOpens, g_reads, g_bytes;

static void Track(HANDLE h)
{
    for (int i = 0; i < MAX_TRACKED; i++)
        if (InterlockedCompareExchangePointer((PVOID volatile *)&g_tracked[i], h, NULL) == NULL) return;
}
static BOOL IsTracked(HANDLE h)
{
    for (int i = 0; i < MAX_TRACKED; i++) {
        HANDLE x = g_tracked[i];
        if (x == h) return TRUE;
        if (x == NULL) return FALSE;
    }
    return FALSE;
}

static DWORD WINAPI StatsThread(LPVOID p)
{
    (void)p;
    LONG64 lastBytes = 0;
    for (;;) {
        Sleep(g_cfg.statsSeconds * 1000);
        LONG64 b = g_bytes;
        MEMORYSTATUSEX ms = { sizeof ms }; GlobalMemoryStatusEx(&ms);
        Log("stats: aberturas=%lld (modificadas=%lld) leituras=%lld total=%.2f GB  ultimo intervalo=%.1f MB/s  RAM livre=%.1f GB",
            g_opens, g_patchedOpens, g_reads, b / 1073741824.0,
            (b - lastBytes) / 1048576.0 / g_cfg.statsSeconds, ms.ullAvailPhys / 1073741824.0);
        lastBytes = b;
    }
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
static BOOL MatchesExt(LPCWSTR path)
{
    if (!path) return FALSE;
    if (!g_cfg.exts[0]) return TRUE;
    size_t plen = wcslen(path);
    const WCHAR *p = g_cfg.exts;
    while (*p) {
        const WCHAR *e = wcschr(p, L';');
        size_t len = e ? (size_t)(e - p) : wcslen(p);
        if (len && plen >= len && _wcsnicmp(path + plen - len, p, len) == 0) return TRUE;
        if (!e) break;
        p = e + 1;
    }
    return FALSE;
}

static DWORD AdjustFlags(LPCWSTR path, DWORD access, DWORD flags, BOOL *changed)
{
    *changed = FALSE;
    if (!g_cfg.enabled || !MatchesExt(path)) return flags;
    // Só mexe em aberturas somente-leitura (nunca em saves/configs).
    if (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA)) return flags;
    DWORD nf = flags & ~(FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH);
    if (g_cfg.randomAccess) nf = (nf & ~FILE_FLAG_SEQUENTIAL_SCAN) | FILE_FLAG_RANDOM_ACCESS;
    *changed = (nf != flags);
    return nf;
}

static HANDLE WINAPI hook_CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                      DWORD disp, DWORD flags, HANDLE tmpl)
{
    BOOL changed, match = MatchesExt(name);
    DWORD nf = AdjustFlags(name, access, flags, &changed);
    HANDLE h = real_CreateFileW(name, access, share, sa, disp, nf, tmpl);
    if (match && h != INVALID_HANDLE_VALUE) {
        LONG64 n = InterlockedIncrement64(&g_opens);
        if (changed) InterlockedIncrement64(&g_patchedOpens);
        Track(h);
        if (n <= 200)
            Log("open %ls flags=0x%08X -> 0x%08X%s", name, flags, nf,
                (flags & FILE_FLAG_NO_BUFFERING) ? "  (NO_BUFFERING removido)" : "");
    }
    return h;
}

static HANDLE WINAPI hook_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                      DWORD disp, DWORD flags, HANDLE tmpl)
{
    WCHAR w[MAX_PATH * 2];
    if (name && MultiByteToWideChar(CP_ACP, 0, name, -1, w, MAX_PATH * 2) > 0 && MatchesExt(w)) {
        BOOL changed;
        DWORD nf = AdjustFlags(w, access, flags, &changed);
        HANDLE h = real_CreateFileA(name, access, share, sa, disp, nf, tmpl);
        if (h != INVALID_HANDLE_VALUE) {
            InterlockedIncrement64(&g_opens);
            if (changed) InterlockedIncrement64(&g_patchedOpens);
            Track(h);
            Log("openA %s flags=0x%08X -> 0x%08X", name, flags, nf);
        }
        return h;
    }
    return real_CreateFileA(name, access, share, sa, disp, flags, tmpl);
}

static BOOL WINAPI hook_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD read, LPOVERLAPPED ov)
{
    if (IsTracked(h)) {
        InterlockedIncrement64(&g_reads);
        InterlockedAdd64(&g_bytes, n);
    }
    return real_ReadFile(h, buf, n, read, ov);
}

// ---------------------------------------------------------------------------
// IAT patching
// ---------------------------------------------------------------------------
static int PatchIAT(HMODULE mod, const char *dllName, const char *func, void *hook, void **orig)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int patched = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp((char *)(base + d->Name), dllName) != 0) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *iat   = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, func) != 0) continue;
            DWORD old;
            if (VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) {
                if (!*orig) *orig = (void *)iat->u1.Function;
                iat->u1.Function = (ULONG_PTR)hook;
                VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
                patched++;
            }
        }
    }
    return patched;
}

static void LoadConfig(void)
{
    WCHAR ini[MAX_PATH];
    swprintf_s(ini, MAX_PATH, L"%ls\\audiofix.ini", g_dir);
    g_cfg.enabled      = GetPrivateProfileIntW(L"AudioFix", L"Enabled", 1, ini);
    g_cfg.randomAccess = GetPrivateProfileIntW(L"AudioFix", L"RandomAccess", 1, ini);
    g_cfg.log          = GetPrivateProfileIntW(L"AudioFix", L"Log", 1, ini);
    g_cfg.statsSeconds = GetPrivateProfileIntW(L"AudioFix", L"StatsSeconds", 60, ini);
    GetPrivateProfileStringW(L"AudioFix", L"Extensions", L".rmdblob", g_cfg.exts, 512, ini);
}

static void Init(HMODULE self)
{
    GetModuleFileNameW(self, g_dir, MAX_PATH);
    WCHAR *slash = wcsrchr(g_dir, L'\\'); if (slash) *slash = 0;
    LoadConfig();

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    real_CreateFileW = (CreateFileW_t)GetProcAddress(k32, "CreateFileW");
    real_CreateFileA = (CreateFileA_t)GetProcAddress(k32, "CreateFileA");
    real_ReadFile    = (ReadFile_t)GetProcAddress(k32, "ReadFile");

    InitializeCriticalSection(&g_logLock);
    if (g_cfg.log) {
        WCHAR lp[MAX_PATH];
        swprintf_s(lp, MAX_PATH, L"%ls\\audiofix.log", g_dir);
        g_log = real_CreateFileW(lp, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    HMODULE exe = GetModuleHandleW(NULL);
    WCHAR exePath[MAX_PATH]; GetModuleFileNameW(exe, exePath, MAX_PATH);
    Log("AudioFix carregado em %ls", exePath);
    Log("config: Enabled=%d RandomAccess=%d StatsSeconds=%u Extensions=%ls",
        g_cfg.enabled, g_cfg.randomAccess, g_cfg.statsSeconds, g_cfg.exts);

    void *dummy;
    int a = PatchIAT(exe, "KERNEL32.dll", "CreateFileW", hook_CreateFileW, (dummy = NULL, &dummy));
    int b = PatchIAT(exe, "KERNEL32.dll", "CreateFileA", hook_CreateFileA, (dummy = NULL, &dummy));
    int c = PatchIAT(exe, "KERNEL32.dll", "ReadFile",    hook_ReadFile,    (dummy = NULL, &dummy));
    Log("IAT: CreateFileW=%d CreateFileA=%d ReadFile=%d entradas interceptadas", a, b, c);

    if (g_cfg.log && g_cfg.statsSeconds) {
        HANDLE t = CreateThread(NULL, 0, StatsThread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        Init(inst);
    }
    return TRUE;
}
