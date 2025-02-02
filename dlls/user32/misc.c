/*
 * Misc USER functions
 *
 * Copyright 1995 Thomas Sandford
 * Copyright 1997 Marcus Meissner
 * Copyright 1998 Turchanov Sergey
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"

#include <stdarg.h>

#include "windef.h"
#include "wine/windef16.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winreg.h"
#include "winnls.h"
#include "winternl.h"
#include "controls.h"
#include "initguid.h"
#include "devguid.h"
#include "setupapi.h"
#include "user_private.h"

#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(win);

/* Wine specific monitor properties */
DEFINE_DEVPROPKEY(WINE_DEVPROPKEY_MONITOR_STATEFLAGS, 0x233a9ef3, 0xafc4, 0x4abd, 0xb5, 0x64, 0xc3, 0x2f, 0x21, 0xf1, 0x53, 0x5b, 2);

static const WCHAR default_adapter_name[] = {'\\','\\','.','\\','D','I','S','P','L','A','Y','1',0};
static const WCHAR default_monitor_name[] =
    {'\\','\\','.','\\',
     'D','I','S','P','L','A','Y','1','\\',
     'M','o','n','i','t','o','r','0',0};
static const WCHAR default_adapter_string[] = {'W','i','n','e',' ','A','d','a','p','t','e','r',0};
static const WCHAR default_monitor_string[] =
    {'G','e','n','e','r','i','c',' ','N','o','n','-','P','n','P',' ','M','o','n','i','t','o','r',0};
static const WCHAR default_adapter_id[] =
    {'P','C','I','\\',
     'V','E','N','_','0','0','0','0','&',
     'D','E','V','_','0','0','0','0','&',
     'S','U','B','S','Y','S','_','0','0','0','0','0','0','0','0','&',
     'R','E','V','_','0','0',0};
static const WCHAR default_monitor_id[] =
    {'M','O','N','I','T','O','R','\\',
     'D','e','f','a','u','l','t','_','M','o','n','i','t','o','r','\\',
     '{','4','d','3','6','e','9','6','e','-','e','3','2','5','-','1','1','c','e','-',
     'b','f','c','1','-','0','8','0','0','2','b','e','1','0','3','1','8','}',
     '\\','0','0','0','0',0};
static const WCHAR default_monitor_interface_id[] =
    {'\\','\\','\?','\\',
     'D','I','S','P','L','A','Y','#','D','e','f','a','u','l','t','_','M','o','n','i','t','o','r','#',
     '4','&','1','7','f','0','f','f','5','4','&','0','&','U','I','D','0','#',
     '{','e','6','f','0','7','b','5','f','-','e','e','9','7','-','4','a','9','0','-',
     'b','0','7','6','-','3','3','f','5','7','b','f','4','e','a','a','7','}',0};

static const WCHAR monitor_fmtW[] =
    {'\\','\\','.','\\',
     'D','I','S','P','L','A','Y','%','d','\\',
     'M','o','n','i','t','o','r','%','d',0};
static const WCHAR adapter_fmtW[] = {'\\','\\','.','\\','D','I','S','P','L','A','Y','%','d',0};
static const WCHAR displayW[] = {'\\','\\','.','\\','D','I','S','P','L','A','Y'};
static const WCHAR video_keyW[] =
    {'H','A','R','D','W','A','R','E','\\',
     'D','E','V','I','C','E','M','A','P','\\',
     'V','I','D','E','O','\\',0};
static const WCHAR video_value_fmtW[] =
    {'\\','D','e','v','i','c','e','\\',
     'V','i','d','e','o','%','d',0};
static const WCHAR monitor_interface_prefixW[] = {'\\','\\','\?','\\',0};
static const WCHAR guid_devinterface_monitorW[] =
    {'#','{','e','6','f','0','7','b','5','f','-','e','e','9','7','-',
     '4','a','9','0','-','b','0','7','6','-','3','3','f','5','7','b','f','4','e','a','a','7','}',0};
static const WCHAR backslashW[] = {'\\',0};
static const WCHAR nt_classW[] =
    {'\\','R','e','g','i','s','t','r','y','\\',
     'M','a','c','h','i','n','e','\\',
     'S','y','s','t','e','m','\\',
     'C','u','r','r','e','n','t','C','o','n','t','r','o','l','S','e','t','\\',
     'C','o','n','t','r','o','l','\\',
     'C','l','a','s','s','\\',0};
static const WCHAR driver_descW[] = {'D','r','i','v','e','r','D','e','s','c',0};
static const WCHAR state_flagsW[] = {'S','t','a','t','e','F','l','a','g','s',0};
static const WCHAR gpu_idW[] = {'G','P','U','I','D',0};
static const WCHAR mointor_id_value_fmtW[] = {'M','o','n','i','t','o','r','I','D','%','d',0};

#define IMM_INIT_MAGIC 0x19650412
static HWND (WINAPI *imm_get_ui_window)(HKL);
BOOL (WINAPI *imm_register_window)(HWND) = NULL;
void (WINAPI *imm_unregister_window)(HWND) = NULL;

/* MSIME messages */
static UINT WM_MSIME_SERVICE;
static UINT WM_MSIME_RECONVERTOPTIONS;
static UINT WM_MSIME_MOUSE;
static UINT WM_MSIME_RECONVERTREQUEST;
static UINT WM_MSIME_RECONVERT;
static UINT WM_MSIME_QUERYPOSITION;
static UINT WM_MSIME_DOCUMENTFEED;

/* USER signal proc flags and codes */
/* See UserSignalProc for comments */
#define USIG_FLAGS_WIN32          0x0001
#define USIG_FLAGS_GUI            0x0002
#define USIG_FLAGS_FEEDBACK       0x0004
#define USIG_FLAGS_FAULT          0x0008

#define USIG_DLL_UNLOAD_WIN16     0x0001
#define USIG_DLL_UNLOAD_WIN32     0x0002
#define USIG_FAULT_DIALOG_PUSH    0x0003
#define USIG_FAULT_DIALOG_POP     0x0004
#define USIG_DLL_UNLOAD_ORPHANS   0x0005
#define USIG_THREAD_INIT          0x0010
#define USIG_THREAD_EXIT          0x0020
#define USIG_PROCESS_CREATE       0x0100
#define USIG_PROCESS_INIT         0x0200
#define USIG_PROCESS_EXIT         0x0300
#define USIG_PROCESS_DESTROY      0x0400
#define USIG_PROCESS_RUNNING      0x0500
#define USIG_PROCESS_LOADED       0x0600

/***********************************************************************
 *		SignalProc32 (USER.391)
 *		UserSignalProc (USER32.@)
 *
 * The exact meaning of the USER signals is undocumented, but this
 * should cover the basic idea:
 *
 * USIG_DLL_UNLOAD_WIN16
 *     This is sent when a 16-bit module is unloaded.
 *
 * USIG_DLL_UNLOAD_WIN32
 *     This is sent when a 32-bit module is unloaded.
 *
 * USIG_DLL_UNLOAD_ORPHANS
 *     This is sent after the last Win3.1 module is unloaded,
 *     to allow removal of orphaned menus.
 *
 * USIG_FAULT_DIALOG_PUSH
 * USIG_FAULT_DIALOG_POP
 *     These are called to allow USER to prepare for displaying a
 *     fault dialog, even though the fault might have happened while
 *     inside a USER critical section.
 *
 * USIG_THREAD_INIT
 *     This is called from the context of a new thread, as soon as it
 *     has started to run.
 *
 * USIG_THREAD_EXIT
 *     This is called, still in its context, just before a thread is
 *     about to terminate.
 *
 * USIG_PROCESS_CREATE
 *     This is called, in the parent process context, after a new process
 *     has been created.
 *
 * USIG_PROCESS_INIT
 *     This is called in the new process context, just after the main thread
 *     has started execution (after the main thread's USIG_THREAD_INIT has
 *     been sent).
 *
 * USIG_PROCESS_LOADED
 *     This is called after the executable file has been loaded into the
 *     new process context.
 *
 * USIG_PROCESS_RUNNING
 *     This is called immediately before the main entry point is called.
 *
 * USIG_PROCESS_EXIT
 *     This is called in the context of a process that is about to
 *     terminate (but before the last thread's USIG_THREAD_EXIT has
 *     been sent).
 *
 * USIG_PROCESS_DESTROY
 *     This is called after a process has terminated.
 *
 *
 * The meaning of the dwFlags bits is as follows:
 *
 * USIG_FLAGS_WIN32
 *     Current process is 32-bit.
 *
 * USIG_FLAGS_GUI
 *     Current process is a (Win32) GUI process.
 *
 * USIG_FLAGS_FEEDBACK
 *     Current process needs 'feedback' (determined from the STARTUPINFO
 *     flags STARTF_FORCEONFEEDBACK / STARTF_FORCEOFFFEEDBACK).
 *
 * USIG_FLAGS_FAULT
 *     The signal is being sent due to a fault.
 */
WORD WINAPI UserSignalProc( UINT uCode, DWORD dwThreadOrProcessID,
                            DWORD dwFlags, HMODULE16 hModule )
{
    FIXME("(%04x, %08x, %04x, %04x)\n",
          uCode, dwThreadOrProcessID, dwFlags, hModule );
    /* FIXME: Should chain to GdiSignalProc now. */
    return 0;
}


/**********************************************************************
 * SetLastErrorEx [USER32.@]
 *
 * Sets the last-error code.
 *
 * RETURNS
 *    None.
 */
void WINAPI SetLastErrorEx(
    DWORD error, /* [in] Per-thread error code */
    DWORD type)  /* [in] Error type */
{
    TRACE("(0x%08x, 0x%08x)\n", error,type);
    switch(type) {
        case 0:
            break;
        case SLE_ERROR:
        case SLE_MINORERROR:
        case SLE_WARNING:
            /* Fall through for now */
        default:
            FIXME("(error=%08x, type=%08x): Unhandled type\n", error,type);
            break;
    }
    SetLastError( error );
}

/******************************************************************************
 * GetAltTabInfoA [USER32.@]
 */
BOOL WINAPI GetAltTabInfoA(HWND hwnd, int iItem, PALTTABINFO pati, LPSTR pszItemText, UINT cchItemText)
{
    FIXME("(%p, 0x%08x, %p, %p, 0x%08x)\n", hwnd, iItem, pati, pszItemText, cchItemText);
    return FALSE;
}

/******************************************************************************
 * GetAltTabInfoW [USER32.@]
 */
BOOL WINAPI GetAltTabInfoW(HWND hwnd, int iItem, PALTTABINFO pati, LPWSTR pszItemText, UINT cchItemText)
{
    FIXME("(%p, 0x%08x, %p, %p, 0x%08x)\n", hwnd, iItem, pati, pszItemText, cchItemText);
    return FALSE;
}

/******************************************************************************
 * SetDebugErrorLevel [USER32.@]
 * Sets the minimum error level for generating debugging events
 *
 * PARAMS
 *    dwLevel [I] Debugging error level
 *
 * RETURNS
 *    Nothing.
 */
VOID WINAPI SetDebugErrorLevel( DWORD dwLevel )
{
    FIXME("(%d): stub\n", dwLevel);
}


/***********************************************************************
 *		SetWindowStationUser (USER32.@)
 */
DWORD WINAPI SetWindowStationUser(DWORD x1,DWORD x2)
{
    FIXME("(0x%08x,0x%08x),stub!\n",x1,x2);
    return 1;
}

/***********************************************************************
 *		RegisterLogonProcess (USER32.@)
 */
DWORD WINAPI RegisterLogonProcess(HANDLE hprocess,BOOL x)
{
    FIXME("(%p,%d),stub!\n",hprocess,x);
    return 1;
}

/***********************************************************************
 *		SetLogonNotifyWindow (USER32.@)
 */
DWORD WINAPI SetLogonNotifyWindow(HWINSTA hwinsta,HWND hwnd)
{
    FIXME("(%p,%p),stub!\n",hwinsta,hwnd);
    return 1;
}

/***********************************************************************
 *		EnumDisplayDevicesA (USER32.@)
 */
BOOL WINAPI EnumDisplayDevicesA( LPCSTR lpDevice, DWORD i, LPDISPLAY_DEVICEA lpDispDev,
                                 DWORD dwFlags )
{
    UNICODE_STRING deviceW;
    DISPLAY_DEVICEW ddW;
    BOOL ret;

    if(lpDevice)
        RtlCreateUnicodeStringFromAsciiz(&deviceW, lpDevice); 
    else
        deviceW.Buffer = NULL;

    ddW.cb = sizeof(ddW);
    ret = EnumDisplayDevicesW(deviceW.Buffer, i, &ddW, dwFlags);
    RtlFreeUnicodeString(&deviceW);

    if(!ret) return ret;

    WideCharToMultiByte(CP_ACP, 0, ddW.DeviceName, -1, lpDispDev->DeviceName, sizeof(lpDispDev->DeviceName), NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, ddW.DeviceString, -1, lpDispDev->DeviceString, sizeof(lpDispDev->DeviceString), NULL, NULL);
    lpDispDev->StateFlags = ddW.StateFlags;

    if(lpDispDev->cb >= offsetof(DISPLAY_DEVICEA, DeviceID) + sizeof(lpDispDev->DeviceID))
        WideCharToMultiByte(CP_ACP, 0, ddW.DeviceID, -1, lpDispDev->DeviceID, sizeof(lpDispDev->DeviceID), NULL, NULL);
    if(lpDispDev->cb >= offsetof(DISPLAY_DEVICEA, DeviceKey) + sizeof(lpDispDev->DeviceKey))
        WideCharToMultiByte(CP_ACP, 0, ddW.DeviceKey, -1, lpDispDev->DeviceKey, sizeof(lpDispDev->DeviceKey), NULL, NULL);

    return TRUE;
}

/***********************************************************************
 *		EnumDisplayDevicesW (USER32.@)
 */
BOOL WINAPI EnumDisplayDevicesW( LPCWSTR lpDevice, DWORD i, LPDISPLAY_DEVICEW lpDisplayDevice,
                                 DWORD dwFlags )
{
    SP_DEVINFO_DATA device_data = {sizeof(device_data)};
    HDEVINFO set = INVALID_HANDLE_VALUE;
    WCHAR key_nameW[MAX_PATH];
    WCHAR instanceW[MAX_PATH];
    WCHAR bufferW[1024];
    LONG adapter_index;
    WCHAR *next_charW;
    DWORD size;
    DWORD type;
    HKEY hkey;
    BOOL ret = FALSE;

    TRACE("%s %d %p %#x\n", debugstr_w(lpDevice), i, lpDisplayDevice, dwFlags);

    /* Find adapter */
    if (!lpDevice)
    {
        sprintfW(key_nameW, video_value_fmtW, i);
        size = sizeof(bufferW);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, video_keyW, key_nameW, RRF_RT_REG_SZ, NULL, bufferW, &size))
            return FALSE;

        /* DeviceKey */
        if(lpDisplayDevice->cb >= offsetof(DISPLAY_DEVICEW, DeviceKey) + sizeof(lpDisplayDevice->DeviceKey))
            lstrcpyW(lpDisplayDevice->DeviceKey, bufferW);

        /* DeviceName */
        sprintfW(lpDisplayDevice->DeviceName, adapter_fmtW, i + 1);

        /* Strip \Registry\Machine\ */
        lstrcpyW(key_nameW, bufferW + 18);

        /* DeviceString */
        size = sizeof(lpDisplayDevice->DeviceString);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key_nameW, driver_descW, RRF_RT_REG_SZ, NULL,
                         lpDisplayDevice->DeviceString, &size))
            return FALSE;

        /* StateFlags */
        size = sizeof(lpDisplayDevice->StateFlags);
        if (RegGetValueW(HKEY_CURRENT_CONFIG, key_nameW, state_flagsW, RRF_RT_REG_DWORD, NULL,
                         &lpDisplayDevice->StateFlags, &size))
            return FALSE;

        /* DeviceID */
        if (lpDisplayDevice->cb >= offsetof(DISPLAY_DEVICEW, DeviceID) + sizeof(lpDisplayDevice->DeviceID))
        {
            if (dwFlags & EDD_GET_DEVICE_INTERFACE_NAME)
                lpDisplayDevice->DeviceID[0] = 0;
            else
            {
                size = sizeof(bufferW);
                if (RegGetValueW(HKEY_CURRENT_CONFIG, key_nameW, gpu_idW, RRF_RT_REG_SZ | RRF_ZEROONFAILURE, NULL,
                                 bufferW, &size))
                    return FALSE;
                set = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_DISPLAY, NULL);
                if (!SetupDiOpenDeviceInfoW(set, bufferW, NULL, 0, &device_data)
                    || !SetupDiGetDeviceRegistryPropertyW(set, &device_data, SPDRP_HARDWAREID, NULL, (BYTE *)bufferW,
                                                          sizeof(bufferW), NULL))
                    goto done;
                lstrcpyW(lpDisplayDevice->DeviceID, bufferW);
            }
        }
    }
    /* Find monitor */
    else
    {
        /* Check adapter name */
        if (strncmpiW(lpDevice, displayW, ARRAY_SIZE(displayW)))
            return FALSE;

        adapter_index = strtolW(lpDevice + ARRAY_SIZE(displayW), NULL, 10);
        sprintfW(key_nameW, video_value_fmtW, adapter_index - 1);

        size = sizeof(bufferW);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, video_keyW, key_nameW, RRF_RT_REG_SZ, NULL, bufferW, &size))
            return FALSE;

        /* DeviceName */
        sprintfW(lpDisplayDevice->DeviceName, monitor_fmtW, adapter_index, i);

        /* Get monitor instance */
        /* Strip \Registry\Machine\ first */
        lstrcpyW(key_nameW, bufferW + 18);
        sprintfW(bufferW, mointor_id_value_fmtW, i);

        size = sizeof(instanceW);
        if (RegGetValueW(HKEY_CURRENT_CONFIG, key_nameW, bufferW, RRF_RT_REG_SZ, NULL, instanceW, &size))
            return FALSE;

        set = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_MONITOR, NULL);
        if (!SetupDiOpenDeviceInfoW(set, instanceW, NULL, 0, &device_data))
            goto done;

        /* StateFlags */
        if (!SetupDiGetDevicePropertyW(set, &device_data, &WINE_DEVPROPKEY_MONITOR_STATEFLAGS, &type,
                                       (BYTE *)&lpDisplayDevice->StateFlags, sizeof(lpDisplayDevice->StateFlags), NULL, 0))
            goto done;

        /* DeviceString */
        if (!SetupDiGetDeviceRegistryPropertyW(set, &device_data, SPDRP_DEVICEDESC, NULL,
                                               (BYTE *)lpDisplayDevice->DeviceString,
                                               sizeof(lpDisplayDevice->DeviceString), NULL))
            goto done;

        /* DeviceKey */
        if (lpDisplayDevice->cb >= offsetof(DISPLAY_DEVICEW, DeviceKey) + sizeof(lpDisplayDevice->DeviceKey))
        {
            if (!SetupDiGetDeviceRegistryPropertyW(set, &device_data, SPDRP_DRIVER, NULL, (BYTE *)bufferW,
                                                   sizeof(bufferW), NULL))
                goto done;

            lstrcpyW(lpDisplayDevice->DeviceKey, nt_classW);
            lstrcatW(lpDisplayDevice->DeviceKey, bufferW);
        }

        /* DeviceID */
        if (lpDisplayDevice->cb >= offsetof(DISPLAY_DEVICEW, DeviceID) + sizeof(lpDisplayDevice->DeviceID))
        {
            if (dwFlags & EDD_GET_DEVICE_INTERFACE_NAME)
            {
                lstrcpyW(lpDisplayDevice->DeviceID, monitor_interface_prefixW);
                lstrcatW(lpDisplayDevice->DeviceID, instanceW);
                lstrcatW(lpDisplayDevice->DeviceID, guid_devinterface_monitorW);
                /* Replace '\\' with '#' after prefix */
                for (next_charW = lpDisplayDevice->DeviceID + strlenW(monitor_interface_prefixW); *next_charW;
                     next_charW++)
                {
                    if (*next_charW == '\\')
                        *next_charW = '#';
                }
            }
            else
            {
                if (!SetupDiGetDeviceRegistryPropertyW(set, &device_data, SPDRP_HARDWAREID, NULL, (BYTE *)bufferW,
                                                       sizeof(bufferW), NULL))
                    goto done;

                lstrcpyW(lpDisplayDevice->DeviceID, bufferW);
                lstrcatW(lpDisplayDevice->DeviceID, backslashW);

                if (!SetupDiGetDeviceRegistryPropertyW(set, &device_data, SPDRP_DRIVER, NULL, (BYTE *)bufferW,
                                                       sizeof(bufferW), NULL))
                    goto done;

                lstrcatW(lpDisplayDevice->DeviceID, bufferW);
            }
        }
    }

    ret = TRUE;
done:
    SetupDiDestroyDeviceInfoList(set);
    if (ret)
        return ret;

    /* Fallback to report at least one adapter and monitor, if user driver didn't initialize display device registry */
    if (i)
        return FALSE;

    /* If user driver did initialize the registry, then exit */
    if (!RegOpenKeyW(HKEY_LOCAL_MACHINE, video_keyW, &hkey))
    {
        RegCloseKey(hkey);
        return FALSE;
    }
    WARN("Reporting fallback display devices\n");

    /* Adapter */
    if (!lpDevice)
    {
        memcpy(lpDisplayDevice->DeviceName, default_adapter_name, sizeof(default_adapter_name));
        memcpy(lpDisplayDevice->DeviceString, default_adapter_string, sizeof(default_adapter_string));
        lpDisplayDevice->StateFlags =
            DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE | DISPLAY_DEVICE_VGA_COMPATIBLE;
        if(lpDisplayDevice->cb >= offsetof(DISPLAY_DEVICEW, DeviceID) + sizeof(lpDisplayDevice->DeviceID))
        {
            if (dwFlags & EDD_GET_DEVICE_INTERFACE_NAME)
                lpDisplayDevice->DeviceID[0] = 0;
            else
                memcpy(lpDisplayDevice->DeviceID, default_adapter_id, sizeof(default_adapter_id));
        }
    }
    /* Monitor */
    else
    {
        if (lstrcmpiW(default_adapter_name, lpDevice))
            return FALSE;

        memcpy(lpDisplayDevice->DeviceName, default_monitor_name, sizeof(default_monitor_name));
        memcpy(lpDisplayDevice->DeviceString, default_monitor_string, sizeof(default_monitor_string));
        lpDisplayDevice->StateFlags = DISPLAY_DEVICE_ACTIVE | DISPLAY_DEVICE_ATTACHED;
        if (lpDisplayDevice->cb >= offsetof(DISPLAY_DEVICEW, DeviceID) + sizeof(lpDisplayDevice->DeviceID))
        {
            if (dwFlags & EDD_GET_DEVICE_INTERFACE_NAME)
                memcpy(lpDisplayDevice->DeviceID, default_monitor_interface_id, sizeof(default_monitor_interface_id));
            else
                memcpy(lpDisplayDevice->DeviceID, default_monitor_id, sizeof(default_monitor_id));
        }
    }

    if(lpDisplayDevice->cb >= offsetof(DISPLAY_DEVICEW, DeviceKey) + sizeof(lpDisplayDevice->DeviceKey))
        lpDisplayDevice->DeviceKey[0] = 0;

    return TRUE;
}

/***********************************************************************
 *              QueryDisplayConfig (USER32.@)
 */
LONG WINAPI QueryDisplayConfig(UINT32 flags, UINT32 *numpathelements, DISPLAYCONFIG_PATH_INFO *pathinfo,
                               UINT32 *numinfoelements, DISPLAYCONFIG_MODE_INFO *modeinfo,
                               DISPLAYCONFIG_TOPOLOGY_ID *topologyid)
{
   FIXME("(%08x %p %p %p %p %p)\n", flags, numpathelements, pathinfo, numinfoelements, modeinfo, topologyid);
   return ERROR_CALL_NOT_IMPLEMENTED;
}

/***********************************************************************
 *		RegisterSystemThread (USER32.@)
 */
void WINAPI RegisterSystemThread(DWORD flags, DWORD reserved)
{
    FIXME("(%08x, %08x)\n", flags, reserved);
}

/***********************************************************************
 *           RegisterShellHookWindow			[USER32.@]
 */
BOOL WINAPI RegisterShellHookWindow(HWND hWnd)
{
    FIXME("(%p): stub\n", hWnd);
    return FALSE;
}


/***********************************************************************
 *           DeregisterShellHookWindow			[USER32.@]
 */
BOOL WINAPI DeregisterShellHookWindow(HWND hWnd)
{
    FIXME("(%p): stub\n", hWnd);
    return FALSE;
}


/***********************************************************************
 *           RegisterTasklist   			[USER32.@]
 */
DWORD WINAPI RegisterTasklist (DWORD x)
{
    FIXME("0x%08x\n",x);
    return TRUE;
}


/***********************************************************************
 *		RegisterDeviceNotificationA (USER32.@)
 *
 * See RegisterDeviceNotificationW.
 */
HDEVNOTIFY WINAPI RegisterDeviceNotificationA(HANDLE hnd, LPVOID notifyfilter, DWORD flags)
{
    FIXME("(hwnd=%p, filter=%p,flags=0x%08x) returns a fake device notification handle!\n",
          hnd,notifyfilter,flags );
    return (HDEVNOTIFY) 0xcafecafe;
}

/***********************************************************************
 *		RegisterDeviceNotificationW (USER32.@)
 *
 * Registers a window with the system so that it will receive
 * notifications about a device.
 *
 * PARAMS
 *     hRecipient           [I] Window or service status handle that
 *                              will receive notifications.
 *     pNotificationFilter  [I] DEV_BROADCAST_HDR followed by some
 *                              type-specific data.
 *     dwFlags              [I] See notes
 *
 * RETURNS
 *
 * A handle to the device notification.
 *
 * NOTES
 *
 * The dwFlags parameter can be one of two values:
 *| DEVICE_NOTIFY_WINDOW_HANDLE  - hRecipient is a window handle
 *| DEVICE_NOTIFY_SERVICE_HANDLE - hRecipient is a service status handle
 */
HDEVNOTIFY WINAPI RegisterDeviceNotificationW(HANDLE hRecipient, LPVOID pNotificationFilter, DWORD dwFlags)
{
    FIXME("(hwnd=%p, filter=%p,flags=0x%08x) returns a fake device notification handle!\n",
          hRecipient,pNotificationFilter,dwFlags );
    return (HDEVNOTIFY) 0xcafeaffe;
}

/***********************************************************************
 *		UnregisterDeviceNotification (USER32.@)
 *
 */
BOOL  WINAPI UnregisterDeviceNotification(HDEVNOTIFY hnd)
{
    FIXME("(handle=%p), STUB!\n", hnd);
    return TRUE;
}

/***********************************************************************
 *           GetAppCompatFlags   (USER32.@)
 */
DWORD WINAPI GetAppCompatFlags( HTASK hTask )
{
    FIXME("(%p) stub\n", hTask);
    return 0;
}

/***********************************************************************
 *           GetAppCompatFlags2   (USER32.@)
 */
DWORD WINAPI GetAppCompatFlags2( HTASK hTask )
{
    FIXME("(%p) stub\n", hTask);
    return 0;
}


/***********************************************************************
 *           AlignRects   (USER32.@)
 */
BOOL WINAPI AlignRects(LPRECT rect, DWORD b, DWORD c, DWORD d)
{
    FIXME("(%p, %d, %d, %d): stub\n", rect, b, c, d);
    if (rect)
        FIXME("rect: %s\n", wine_dbgstr_rect(rect));
    /* Calls OffsetRect */
    return FALSE;
}


/***********************************************************************
 *		LoadLocalFonts (USER32.@)
 */
VOID WINAPI LoadLocalFonts(VOID)
{
    /* are loaded. */
    return;
}


/***********************************************************************
 *		User32InitializeImmEntryTable
 */
BOOL WINAPI User32InitializeImmEntryTable(DWORD magic)
{
    static const WCHAR imm32_dllW[] = {'i','m','m','3','2','.','d','l','l',0};
    HMODULE imm32 = GetModuleHandleW(imm32_dllW);

    TRACE("(%x)\n", magic);

    if (!imm32 || magic != IMM_INIT_MAGIC)
        return FALSE;

    if (imm_get_ui_window)
        return TRUE;

    WM_MSIME_SERVICE = RegisterWindowMessageA("MSIMEService");
    WM_MSIME_RECONVERTOPTIONS = RegisterWindowMessageA("MSIMEReconvertOptions");
    WM_MSIME_MOUSE = RegisterWindowMessageA("MSIMEMouseOperation");
    WM_MSIME_RECONVERTREQUEST = RegisterWindowMessageA("MSIMEReconvertRequest");
    WM_MSIME_RECONVERT = RegisterWindowMessageA("MSIMEReconvert");
    WM_MSIME_QUERYPOSITION = RegisterWindowMessageA("MSIMEQueryPosition");
    WM_MSIME_DOCUMENTFEED = RegisterWindowMessageA("MSIMEDocumentFeed");

    /* this part is not compatible with native imm32.dll */
    imm_get_ui_window = (void*)GetProcAddress(imm32, "__wine_get_ui_window");
    imm_register_window = (void*)GetProcAddress(imm32, "__wine_register_window");
    imm_unregister_window = (void*)GetProcAddress(imm32, "__wine_unregister_window");
    if (!imm_get_ui_window)
        FIXME("native imm32.dll not supported\n");
    return TRUE;
}

/**********************************************************************
 * WINNLSGetIMEHotkey [USER32.@]
 *
 */
UINT WINAPI WINNLSGetIMEHotkey(HWND hwnd)
{
    FIXME("hwnd %p: stub!\n", hwnd);
    return 0; /* unknown */
}

/**********************************************************************
 * WINNLSEnableIME [USER32.@]
 *
 */
BOOL WINAPI WINNLSEnableIME(HWND hwnd, BOOL enable)
{
    FIXME("hwnd %p enable %d: stub!\n", hwnd, enable);
    return TRUE; /* success (?) */
}

/**********************************************************************
 * WINNLSGetEnableStatus [USER32.@]
 *
 */
BOOL WINAPI WINNLSGetEnableStatus(HWND hwnd)
{
    FIXME("hwnd %p: stub!\n", hwnd);
    return TRUE; /* success (?) */
}

/**********************************************************************
 * SendIMEMessageExA [USER32.@]
 *
 */
LRESULT WINAPI SendIMEMessageExA(HWND hwnd, LPARAM lparam)
{
  FIXME("(%p,%lx): stub\n", hwnd, lparam);
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return 0;
}

/**********************************************************************
 * SendIMEMessageExW [USER32.@]
 *
 */
LRESULT WINAPI SendIMEMessageExW(HWND hwnd, LPARAM lparam)
{
  FIXME("(%p,%lx): stub\n", hwnd, lparam);
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return 0;
}

/**********************************************************************
 * DisableProcessWindowsGhosting [USER32.@]
 *
 */
VOID WINAPI DisableProcessWindowsGhosting(VOID)
{
  FIXME(": stub\n");
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return;
}

/**********************************************************************
 * UserHandleGrantAccess [USER32.@]
 *
 */
BOOL WINAPI UserHandleGrantAccess(HANDLE handle, HANDLE job, BOOL grant)
{
    FIXME("(%p,%p,%d): stub\n", handle, job, grant);
    return TRUE;
}

/**********************************************************************
 * RegisterPowerSettingNotification [USER32.@]
 */
HPOWERNOTIFY WINAPI RegisterPowerSettingNotification(HANDLE recipient, const GUID *guid, DWORD flags)
{
    FIXME("(%p,%s,%x): stub\n", recipient, debugstr_guid(guid), flags);
    return (HPOWERNOTIFY)0xdeadbeef;
}

/**********************************************************************
 * UnregisterPowerSettingNotification [USER32.@]
 */
BOOL WINAPI UnregisterPowerSettingNotification(HPOWERNOTIFY handle)
{
    FIXME("(%p): stub\n", handle);
    return TRUE;
}

/*****************************************************************************
 * GetGestureConfig (USER32.@)
 */
BOOL WINAPI GetGestureConfig( HWND hwnd, DWORD reserved, DWORD flags, UINT *count, GESTURECONFIG *config, UINT size )
{
    FIXME("(%p %08x %08x %p %p %u): stub\n", hwnd, reserved, flags, count, config, size);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/**********************************************************************
 * SetGestureConfig [USER32.@]
 */
BOOL WINAPI SetGestureConfig( HWND hwnd, DWORD reserved, UINT id, PGESTURECONFIG config, UINT size )
{
    FIXME("(%p %08x %u %p %u): stub\n", hwnd, reserved, id, config, size);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/**********************************************************************
 * IsTouchWindow [USER32.@]
 */
BOOL WINAPI IsTouchWindow( HWND hwnd, PULONG flags )
{
    FIXME("(%p %p): stub\n", hwnd, flags);
    return FALSE;
}

/**********************************************************************
 * IsWindowRedirectedForPrint [USER32.@]
 */
BOOL WINAPI IsWindowRedirectedForPrint( HWND hwnd )
{
    FIXME("(%p): stub\n", hwnd);
    return FALSE;
}

/**********************************************************************
 * GetDisplayConfigBufferSizes [USER32.@]
 */
LONG WINAPI GetDisplayConfigBufferSizes(UINT32 flags, UINT32 *num_path_info, UINT32 *num_mode_info)
{
    FIXME("(0x%x %p %p): stub\n", flags, num_path_info, num_mode_info);

    if (!num_path_info || !num_mode_info)
        return ERROR_INVALID_PARAMETER;

    *num_path_info = 0;
    *num_mode_info = 0;
    return ERROR_NOT_SUPPORTED;
}

/**********************************************************************
 * RegisterPointerDeviceNotifications [USER32.@]
 */
BOOL WINAPI RegisterPointerDeviceNotifications(HWND hwnd, BOOL notifyrange)
{
    FIXME("(%p %d): stub\n", hwnd, notifyrange);
    return TRUE;
}

/**********************************************************************
 * GetPointerDevices [USER32.@]
 */
BOOL WINAPI GetPointerDevices(UINT32 *device_count, POINTER_DEVICE_INFO *devices)
{
    FIXME("(%p %p): partial stub\n", device_count, devices);

    if (!device_count)
        return FALSE;

    if (devices)
        return FALSE;

    *device_count = 0;
    return TRUE;
}

/**********************************************************************
 * RegisterTouchHitTestingWindow [USER32.@]
 */
BOOL WINAPI RegisterTouchHitTestingWindow(HWND hwnd, ULONG value)
{
    FIXME("(%p %d): stub\n", hwnd, value);
    return TRUE;
}

/**********************************************************************
 * GetPointerType [USER32.@]
 */
BOOL WINAPI GetPointerType(UINT32 id, POINTER_INPUT_TYPE *type)
{
    FIXME("(%d %p): stub\n", id, type);

    if(!id || !type)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *type = PT_MOUSE;
    return TRUE;
}

static const WCHAR imeW[] = {'I','M','E',0};
const struct builtin_class_descr IME_builtin_class =
{
    imeW,               /* name */
    0,                  /* style  */
    WINPROC_IME,        /* proc */
    2*sizeof(LONG_PTR), /* extra */
    IDC_ARROW,          /* cursor */
    0                   /* brush */
};

static BOOL is_ime_ui_msg( UINT msg )
{
    switch(msg) {
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_SETCONTEXT:
    case WM_IME_NOTIFY:
    case WM_IME_CONTROL:
    case WM_IME_COMPOSITIONFULL:
    case WM_IME_SELECT:
    case WM_IME_CHAR:
    case WM_IME_REQUEST:
    case WM_IME_KEYDOWN:
    case WM_IME_KEYUP:
        return TRUE;
    default:
        if ((msg == WM_MSIME_RECONVERTOPTIONS) ||
                (msg == WM_MSIME_SERVICE) ||
                (msg == WM_MSIME_MOUSE) ||
                (msg == WM_MSIME_RECONVERTREQUEST) ||
                (msg == WM_MSIME_RECONVERT) ||
                (msg == WM_MSIME_QUERYPOSITION) ||
                (msg == WM_MSIME_DOCUMENTFEED))
            return TRUE;

        return FALSE;
    }
}

LRESULT WINAPI ImeWndProcA( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    HWND uiwnd;

    if (msg==WM_CREATE)
        return TRUE;

    if (imm_get_ui_window && is_ime_ui_msg(msg))
    {
        if ((uiwnd = imm_get_ui_window(GetKeyboardLayout(0))))
            return SendMessageA(uiwnd, msg, wParam, lParam);
        return FALSE;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT WINAPI ImeWndProcW( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    HWND uiwnd;

    if (msg==WM_CREATE)
        return TRUE;

    if (imm_get_ui_window && is_ime_ui_msg(msg))
    {
        if ((uiwnd = imm_get_ui_window(GetKeyboardLayout(0))))
            return SendMessageW(uiwnd, msg, wParam, lParam);
        return FALSE;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
