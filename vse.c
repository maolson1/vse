#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <shellapi.h>

// C:\vse-cmd-log.txt
// C:\vse-svc-log.txt
// hklm\system\currentcontrolset\services\vse

#define VERSION "0.0.0"

#define USAGE \
"\nvse version " VERSION "\n" \
"\n" \
"Usage:\n" \
"   vse -exec <script_abspath> [-delay <seconds>]\n" \
"\n"

SERVICE_STATUS_HANDLE vse_svc_status_handle;
SERVICE_STATUS vse_svc_status;

#define VSE_SERVICE_NAME L"vse"

int start_svc(int argc, wchar_t** argv)
{
    int err = NO_ERROR;
    SC_HANDLE svc_handle = NULL;
    SC_HANDLE scm_handle = NULL;
    SERVICE_STATUS svc_status = {0};
    wchar_t abspath[MAX_PATH + 2];
    wchar_t** argv_cooked = NULL;
    int argc_cooked = 0;
    wchar_t* argv_serial = NULL;

    // Insert "-insvc" as argv[1], and modify argv[0] to absolute path.

    abspath[0] = L'\"'; // wrap in quotes in case of spaces in path.
    if (!GetModuleFileName(NULL, abspath + 1, MAX_PATH)) {
        err = GetLastError();
        printf("GetModuleFileName failed with %d\n", err);
        goto exit;
    }
    wcscat_s(abspath, MAX_PATH + 2, L"\"");

    argv_cooked = malloc(sizeof(wchar_t*) * (argc + 1));
    if (argv_cooked == NULL) {
        printf("out of memory\n");
        goto exit;
    }
    argv_cooked[0] = abspath;
    argv_cooked[1] = L"-insvc";
    argc_cooked = 2;
    for (int i = 1; i < argc; i++) {
        argv_cooked[argc_cooked++] = argv[i];
    }

    // Also serialize the modified argv to give to CreateService.
    // Nastily, the serialized args given to CreateService are what
    // passed to ServiceMain when the service is started with
    // "net start" or (when set to auto start) by the SCM during
    // boot, but then if you start the service with "sc start"
    // or with StartService, the command line args passed to
    // sc start or the arg vector passed to StartService is
    // what gets passed.

    size_t argv_serial_len = 0; // in characters
    for (int i = 0; i < argc_cooked; i++) {
        argv_serial_len += wcslen(argv_cooked[i]) + 1;
    }
    argv_serial = malloc(argv_serial_len * sizeof(wchar_t));
    if (argv_serial == NULL) {
        printf("Could not allocate cmdline for CreateService\n");
        err = ERROR_NOT_ENOUGH_MEMORY;
        goto exit;
    }
    argv_serial[0] = L'\0';
    for (int i = 0; i < argc_cooked - 1; i++) {
        wcscat_s(argv_serial, argv_serial_len, argv_cooked[i]);
        wcscat_s(argv_serial, argv_serial_len, L" ");
    }
    wcscat_s(argv_serial, argv_serial_len, argv_cooked[argc_cooked - 1]);

    // Now, finally, the service manager calls.

    scm_handle = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm_handle == NULL) {
        err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            printf("ERROR_ACCESS_DENIED- must start service as admin.\n");
        } else {
            printf("OpenSCManager failed with %d\n", err);
        }
        goto exit;
    }

    svc_handle = OpenService(scm_handle, VSE_SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (svc_handle == NULL) {
        err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            svc_handle = CreateService(
                scm_handle, VSE_SERVICE_NAME, VSE_SERVICE_NAME,
                SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, argv_serial,
                NULL, NULL, NULL, NULL, NULL);
            if (svc_handle == NULL) {
                err = GetLastError();
                printf("CreateService failed with %d\n", err);
                goto exit;
            }
        } else {
            printf("OpenService failed with %d\n", err);
            goto exit;
        }
    }

    if (!QueryServiceStatus(svc_handle, &svc_status)) {
        err = GetLastError();
        printf("QueryServiceStatus failed with %d\n", err);
        goto exit;
    }

    if (svc_status.dwCurrentState == SERVICE_STOPPED) {
        printf("Starting service.\n");
        if (!StartService(svc_handle, argc_cooked, argv_cooked)) {
            err = GetLastError();
            printf("StartService failed with %d\n", err);
            goto exit;
        }
    } else if (svc_status.dwCurrentState == SERVICE_RUNNING) {
        printf("Service is already running.\n");
        printf("Run -delsvc first if you want to start with a new configuration.\n");
        err = 1;
        goto exit;
    }

exit:
    if (argv_serial != NULL) {
        free(argv_serial);
    }
    if (argv_cooked != NULL) {
        free(argv_cooked);
    }
    if (svc_handle != NULL) {
        CloseServiceHandle(svc_handle);
    }
    if (scm_handle != NULL) {
        CloseServiceHandle(scm_handle);
    }
    return err;
}

int del_svc(void)
{
    int err = NO_ERROR;
    SC_HANDLE svc_handle = NULL;
    SC_HANDLE scm_handle = NULL;

    scm_handle = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm_handle == NULL) {
        err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            printf("ERROR_ACCESS_DENIED- must delete service as admin.\n");
        } else {
            printf("OpenSCManager failed with %d\n", err);
        }
        goto exit;
    }

    svc_handle = OpenService(scm_handle, VSE_SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (svc_handle == NULL) {
        err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            printf("Service is not running.\n");
            err = NO_ERROR;
        } else {
            printf("OpenService failed with %d\n", err);
        }
        goto exit;
    }

    if (!DeleteService(svc_handle)) {
        err = GetLastError();
        printf("DeleteService failed with %d\n", err);
        goto exit;
    }

exit:
    if (svc_handle != NULL) {
        CloseServiceHandle(svc_handle);
    }
    if (scm_handle != NULL) {
        CloseServiceHandle(scm_handle);
    }
    return err;
}

void WINAPI svc_ctrl(DWORD code)
{
    printf("svc_ctrl, code = %d\n", code);
    if (code == SERVICE_CONTROL_STOP) {
        vse_svc_status.dwCurrentState = SERVICE_STOP_PENDING;
        vse_svc_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
        vse_svc_status.dwWin32ExitCode = NO_ERROR;
        SetServiceStatus(vse_svc_status_handle, &vse_svc_status);
    }
    fflush(stdout);
}

void WINAPI svc_main(DWORD argc, wchar_t** argv)
{
    vse_svc_status_handle =
        RegisterServiceCtrlHandlerW(VSE_SERVICE_NAME, svc_ctrl);
    vse_svc_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    vse_svc_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    vse_svc_status.dwCurrentState = SERVICE_RUNNING;
    vse_svc_status.dwWin32ExitCode = NO_ERROR;
    SetServiceStatus(vse_svc_status_handle, &vse_svc_status);

    FILE* f;
    freopen_s(&f, "C:\\vse-svc-log.txt", "w", stdout);

    printf("vse start\n");

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);
    int my_argc = 0;
    wchar_t** my_argv = CommandLineToArgvW(GetCommandLine(), &my_argc);

    wchar_t* script_path = NULL;
    ULONG delay_sec = 0;

    int ac = 1;
    wchar_t** av = my_argv + 1;
    while (ac < my_argc) {
        wchar_t** name = av++; ac++;
        int argsleft = my_argc - ac;
        if (!wcscmp(*name, L"-insvc")) {
            
        } else if (argsleft >= 1 && !wcscmp(*name, L"-exec")) {
            script_path = *av;
            av++; ac++;
        } else if (argsleft >= 1 && !wcscmp(*name, L"-delay")) {
            delay_sec = _wtoi(*av);
            av++; ac++;
        } else {
            printf("Invalid parameter %ws\n", *name);
            goto exit;
        }
    }

    if (script_path == NULL) {
        printf("missing script path\n");
        goto exit;
    }

    if (delay_sec > 0) {
        printf("sleeping for %lu sec\n", delay_sec);
        Sleep(delay_sec * 1000);
    }

    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    HANDLE outfile = CreateFile(
        L"C:\\vse-cmd-log.txt",
        GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (outfile == INVALID_HANDLE_VALUE) {
        printf("Failed to create output file.\n");
        goto exit;
    }

    wchar_t command[512];
    wcscpy_s(command, 512, L"cmd /c ");
    wcscat_s(command, 512, script_path);

    printf("running: %ws\n", command);
    fflush(stdout);

    PROCESS_INFORMATION pi = {0};
    STARTUPINFO si = {0};
    si.cb = sizeof(si);
    si.hStdOutput = outfile;
    si.hStdError = outfile;
    si.dwFlags |= STARTF_USESTDHANDLES;
    if (!CreateProcessW(
            NULL, command, NULL, NULL, TRUE, 0, NULL,
            NULL, &si, &pi)) {
        printf("CreateProcess failed with %lu\n", GetLastError());
        CloseHandle(outfile);
        goto exit;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(outfile);

exit:
    printf("vse end\n");
    fflush(stdout);

    del_svc();

    vse_svc_status.dwCurrentState = SERVICE_STOPPED;
    vse_svc_status.dwWin32ExitCode = NO_ERROR;
    SetServiceStatus(vse_svc_status_handle, &vse_svc_status);
}

int __cdecl wmain(int argc, wchar_t** argv)
{
    int err = NO_ERROR;
    if (argc == 1) {
        printf(USAGE);
    } else if (argc >= 3 && !wcscmp(argv[1], L"-exec")) {
        err = start_svc(argc, argv);
    } else if (argc >= 3 && !wcscmp(argv[1], L"-insvc")) {
        SERVICE_TABLE_ENTRYW svctable[] = {{L"", svc_main}, {NULL, NULL}};
        if (!StartServiceCtrlDispatcherW(svctable)) {
            err = GetLastError();
            if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
                printf("Don't pass -insvc from command line!\n");
            }
        }
    } else {
        err = ERROR_INVALID_PARAMETER;
        printf(USAGE);
    }
    return err;
}
