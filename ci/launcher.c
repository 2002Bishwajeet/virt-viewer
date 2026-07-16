/* Portable launcher for the bundled remote-viewer.exe.
 *
 * The real exe (bin/remote-viewer.exe) is relocatable for GTK's own resources,
 * but a few things don't self-relocate on Windows: OpenSSL's CA bundle (TLS
 * cert verification) and GStreamer's plugin/scanner paths. Double-clicking the
 * raw exe therefore connects with no CA bundle and fails cert verification.
 *
 * This launcher lives at the bundle ROOT, sets those env vars relative to its
 * own location, then spawns bin/remote-viewer.exe, forwarding any CLI args.
 * Built as a GUI-subsystem app (-mwindows) so there is no console flash.
 */
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>

/* Skip argv[0] in a raw command line, return the rest (the user's args). */
static wchar_t *skip_arg0(wchar_t *c) {
    if (*c == L'"') { c++; while (*c && *c != L'"') c++; if (*c) c++; }
    else            { while (*c && *c != L' ' && *c != L'\t') c++; }
    while (*c == L' ' || *c == L'\t') c++;
    return c;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR a, int show) {
    (void)hi; (void)hp; (void)a; (void)show;

    static wchar_t here[4096];
    GetModuleFileNameW(NULL, here, 4096);
    wchar_t *slash = wcsrchr(here, L'\\');
    if (slash) *slash = 0;                    /* here = launcher's directory */

    static wchar_t buf[8192];
    #define SETENV(name, sub) do { \
        _snwprintf(buf, 8192, L"%s\\%s", here, sub); \
        SetEnvironmentVariableW(name, buf); \
    } while (0)

    SETENV(L"SSL_CERT_FILE",          L"ssl\\certs\\ca-bundle.crt");        /* required for TLS */
    SETENV(L"GST_PLUGIN_PATH",        L"lib\\gstreamer-1.0");
    SETENV(L"GST_PLUGIN_SYSTEM_PATH", L"lib\\gstreamer-1.0");
    SETENV(L"GST_PLUGIN_SCANNER",     L"bin\\gst-plugin-scanner.exe");
    SETENV(L"GDK_PIXBUF_MODULE_FILE", L"lib\\gdk-pixbuf-2.0\\2.10.0\\loaders.cache");
    SETENV(L"GSETTINGS_SCHEMA_DIR",   L"share\\glib-2.0\\schemas");
    SETENV(L"XDG_DATA_DIRS",          L"share");

    /* Prepend bin/ to PATH so the exe's DLLs resolve regardless of cwd. */
    wchar_t *oldpath = _wgetenv(L"PATH");
    static wchar_t newpath[32768];
    _snwprintf(newpath, 32768, L"%s\\bin;%s", here, oldpath ? oldpath : L"");
    SetEnvironmentVariableW(L"PATH", newpath);

    /* child = "<here>\bin\remote-viewer.exe" <forwarded args> */
    static wchar_t cmd[32768];
    _snwprintf(cmd, 32768, L"\"%s\\bin\\remote-viewer.exe\" %s",
               here, skip_arg0(GetCommandLineW()));

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxW(NULL, L"Failed to launch bin\\remote-viewer.exe",
                    L"Remote Viewer", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
