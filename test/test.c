// Simula o jogo: importa version.dll e abre um .rmdblob com NO_BUFFERING.
// Uma leitura com tamanho/offset desalinhado FALHA com NO_BUFFERING (ERROR_INVALID_PARAMETER)
// e só funciona se a DLL removeu a flag.
#include <windows.h>
#include <stdio.h>
#pragma comment(lib, "version.lib")

int main(int argc, char **argv)
{
    const wchar_t *path = L"test.rmdblob";
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    char data[8192]; memset(data, 'x', sizeof data);
    DWORD w; WriteFile(h, data, sizeof data, &w, NULL); CloseHandle(h);

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    char buf[100]; DWORD r = 0;
    BOOL ok = ReadFile(h, buf + 1, 37, &r, NULL);
    printf("leitura desalinhada: %s (lidos=%lu, err=%lu)\n", ok ? "OK -> flag removida" : "FALHOU -> flag ativa", r, ok ? 0 : GetLastError());
    CloseHandle(h);

    DWORD dummy;
    DWORD sz = GetFileVersionInfoSizeW(L"C:\\Windows\\System32\\kernel32.dll", &dummy);
    printf("GetFileVersionInfoSizeW (encaminhado) = %lu\n", sz);
    DeleteFileW(path);
    int expectPatched = argc > 1;
    return (ok == expectPatched && sz > 0) ? 0 : 1;
}
