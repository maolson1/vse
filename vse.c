#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <shellapi.h>

#define VERSION "2.0.0"
#define VSE_SERVICE_NAME L"vse"

#define USAGE \
"\nvse " VERSION "\n" \
"\n" \
"Usage:\n" \
"   vse -cmd <command> [-delay <seconds>]\n" \
"\n" \
"Logs:\n" \
"   C:\\vse-cmd-log.txt - <command> output.\n" \
"   C:\\vse-svc-log.txt - vse output.\n" \
"\n"

SERVICE_STATUS_HANDLE vse_svc_status_handle;
SERVICE_STATUS vse_svc_status;

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
    wchar_t* quoted_cmd = NULL;

    // Modify args in the following ways to pass on to SCM:
    // -ensure first arg is an absolute path to vse.exe.
    // -insert "-insvc" as second arg so vse.exe can tell
    //       when it's run as a service.
    // -restore the quotes of the command passed in so that
    //       the syntax of the command will be identical to
    //       a command run plainly.

    abspath[0] = L'\"'; // wrap in quotes in case of spaces in path.
    if (!GetModuleFileName(NULL, abspath + 1, MAX_PATH)) {
        err = GetLastError();
        printf("GetModuleFileName failed with %d\n", err);
        goto exit;
    }
    wcscat_s(abspath, MAX_PATH + 2, L"\"");

    size_t quoted_cmd_len = wcslen(argv[2]) + 3;
    quoted_cmd = malloc(quoted_cmd_len * sizeof(wchar_t));
    if (quoted_cmd == NULL) {
        printf("out of memory\n");
        err = ERROR_NOT_ENOUGH_MEMORY;
        goto exit;
    }
    quoted_cmd[0] = L'\"';
    quoted_cmd[1] = L'\0';
    wcscat_s(quoted_cmd, quoted_cmd_len, argv[2]);
    wcscat_s(quoted_cmd, quoted_cmd_len, L"\"");

    argv_cooked = malloc(sizeof(wchar_t*) * (argc + 1));
    if (argv_cooked == NULL) {
        printf("out of memory\n");
        goto exit;
    }
    // Ensure first arg is an absolute path.
    argv_cooked[0] = abspath;
    // Insert "-insvc" arg so vse knows when it's launched
    // as a service.
    argv_cooked[1] = L"-insvc";
    argv_cooked[2] = L"-cmd";
    argv_cooked[3] = quoted_cmd;
    argc_cooked = 4;
    for (int i = 3; i < argc; i++) {
        argv_cooked[argc_cooked++] = argv[i];
    }

    // Also serialize the modified argv to give to CreateService.
    // Nastily, the serialized args given to CreateService are
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
    if (quoted_cmd != NULL) {
        free(quoted_cmd);
    }
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
    wchar_t* quoted_cmd = NULL;

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

    wchar_t* command = NULL;
    ULONG delay_sec = 0;

    int ac = 1;
    wchar_t** av = my_argv + 1;
    while (ac < my_argc) {
        wchar_t** name = av++; ac++;
        int argsleft = my_argc - ac;
        if (!wcscmp(*name, L"-insvc")) {

        } else if (argsleft >= 1 && !wcscmp(*name, L"-cmd")) {
            command = *av;
            av++; ac++;
        } else if (argsleft >= 1 && !wcscmp(*name, L"-delay")) {
            delay_sec = _wtoi(*av);
            av++; ac++;
        } else {
            printf("Invalid parameter %ws\n", *name);
            goto exit;
        }
    }

    if (command == NULL) {
        printf("missing command\n");
        goto exit;
    }

    // Restore quotes which were stripped
    size_t quoted_cmd_len = wcslen(command) + 3;
    quoted_cmd = malloc(quoted_cmd_len * sizeof(wchar_t));
    if (quoted_cmd == NULL) {
        printf("out of memory\n");
        goto exit;
    }
    quoted_cmd[0] = L'\"';
    quoted_cmd[1] = L'\0';
    wcscat_s(quoted_cmd, quoted_cmd_len, command);
    wcscat_s(quoted_cmd, quoted_cmd_len, L"\"");

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

    wchar_t cmd_command[512];
    wcscpy_s(cmd_command, 512, L"cmd /c ");
    wcscat_s(cmd_command, 512, quoted_cmd);

    printf("running: %ws\n", cmd_command);
    fflush(stdout);

    PROCESS_INFORMATION pi = {0};
    STARTUPINFO si = {0};
    si.cb = sizeof(si);
    si.hStdOutput = outfile;
    si.hStdError = outfile;
    si.dwFlags |= STARTF_USESTDHANDLES;
    if (!CreateProcessW(
            NULL, cmd_command, NULL, NULL, TRUE, 0, NULL,
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

    if (quoted_cmd != NULL) {
        free(quoted_cmd);
    }

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
    } else if (argc >= 3 && !wcscmp(argv[1], L"-cmd")) {
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
