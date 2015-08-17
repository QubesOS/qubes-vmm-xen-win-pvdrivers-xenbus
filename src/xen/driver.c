/* Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, 
 * with or without modification, are permitted provided 
 * that the following conditions are met:
 * 
 * *   Redistributions of source code must retain the above 
 *     copyright notice, this list of conditions and the 
 *     following disclaimer.
 * *   Redistributions in binary form must reproduce the above 
 *     copyright notice, this list of conditions and the 
 *     following disclaimer in the documentation and/or other 
 *     materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND 
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 */

#define XEN_API __declspec(dllexport)

#include <ntddk.h>
#include <procgrp.h>
#include <xen.h>

#include "registry.h"
#include "driver.h"
#include "hypercall.h"
#include "log.h"
#include "module.h"
#include "process.h"
#include "system.h"
#include "acpi.h"
#include "unplug.h"
#include "bug_check.h"
#include "dbg_print.h"
#include "assert.h"
#include "version.h"

extern PULONG   InitSafeBootMode;

typedef struct _XEN_DRIVER {
    PLOG_DISPOSITION    TraceDisposition;
    PLOG_DISPOSITION    InfoDisposition;
    HANDLE              UnplugKey;
    HANDLE              StatusKey;
} XEN_DRIVER, *PXEN_DRIVER;

static XEN_DRIVER   Driver;

static FORCEINLINE VOID
__DriverSetUnplugKey(
    IN  HANDLE  Key
    )
{
    Driver.UnplugKey = Key;
}

static FORCEINLINE HANDLE
__DriverGetUnplugKey(
    VOID
    )
{
    return Driver.UnplugKey;
}

HANDLE
DriverGetUnplugKey(
    VOID
    )
{
    return __DriverGetUnplugKey();
}

static FORCEINLINE VOID
__DriverSetStatusKey(
    IN  HANDLE  Key
    )
{
    Driver.StatusKey = Key;
}

static FORCEINLINE HANDLE
__DriverGetStatusKey(
    VOID
    )
{
    return Driver.StatusKey;
}

HANDLE
DriverGetStatusKey(
    VOID
    )
{
    return __DriverGetStatusKey();
}

static FORCEINLINE VOID
__DriverRequestReboot(
    VOID
    )
{
    Info("<===>\n");

    ASSERT3U(KeGetCurrentIrql(), ==, PASSIVE_LEVEL);

    (VOID) RegistryUpdateDwordValue(__DriverGetStatusKey(),
                                    "NeedReboot",
                                    1);
}

XEN_API
NTSTATUS
XenTouch(
    IN  const CHAR  *Name,
    IN  ULONG       MajorVersion,
    IN  ULONG       MinorVersion,
    IN  ULONG       MicroVersion,
    IN  ULONG       BuildNumber
   )
{
    static ULONG    Reference;
    ULONG           Major;
    ULONG           Minor;
    CHAR            Extra[XEN_EXTRAVERSION_LEN];
    NTSTATUS        status;

    if (MajorVersion != MAJOR_VERSION ||
        MinorVersion != MINOR_VERSION ||
        MicroVersion != MICRO_VERSION ||
        BuildNumber != BUILD_NUMBER)
        goto fail1;

    if (Reference++ != 0)
        goto done;

    status = XenVersion(&Major, &Minor);
    ASSERT(NT_SUCCESS(status));

    status = XenVersionExtra(Extra);
    ASSERT(NT_SUCCESS(status));

    LogPrintf(LOG_LEVEL_INFO,
              "XEN: %u.%u%s (__XEN_INTERFACE_VERSION__ = %08x)\n",
              Major,
              Minor,
              Extra,
              __XEN_INTERFACE_VERSION__);

done:
    return STATUS_SUCCESS;

fail1:
    Info("MODULE '%s' NOT COMPATIBLE (REBOOT REQUIRED)\n", Name);
    __DriverRequestReboot();

    return STATUS_INCOMPATIBLE_DRIVER_BLOCKED;
}

static VOID
DriverOutputBuffer(
    IN  PVOID   Argument,
    IN  PCHAR   Buffer,
    IN  ULONG   Length
    )
{
    ULONG_PTR   Port = (ULONG_PTR)Argument;

    __outbytestring((USHORT)Port, (PUCHAR)Buffer, Length);
}

#define XEN_PORT    0xE9
#define QEMU_PORT   0x12

NTSTATUS
DllInitialize(
    IN  PUNICODE_STRING RegistryPath
    )
{
    HANDLE              ServiceKey;
    HANDLE              UnplugKey;
    HANDLE              StatusKey;
    NTSTATUS            status;

    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
    WdmlibProcgrpInitialize();

    Trace("====>\n");

    if (*InitSafeBootMode > 0)
        goto done;

    status = LogInitialize();
    if (!NT_SUCCESS(status))
        goto fail1;

    status = LogAddDisposition(LOG_LEVEL_TRACE |
                               LOG_LEVEL_CRITICAL,
                               DriverOutputBuffer,
                               (PVOID)XEN_PORT,
                               &Driver.TraceDisposition);
    ASSERT(NT_SUCCESS(status));

    status = LogAddDisposition(LOG_LEVEL_INFO |
                               LOG_LEVEL_WARNING |
                               LOG_LEVEL_ERROR |
                               LOG_LEVEL_CRITICAL,
                               DriverOutputBuffer,
                               (PVOID)QEMU_PORT,
                               &Driver.InfoDisposition);
    ASSERT(NT_SUCCESS(status));

    Info("%d.%d.%d (%d) (%02d.%02d.%04d)\n",
         MAJOR_VERSION,
         MINOR_VERSION,
         MICRO_VERSION,
         BUILD_NUMBER,
         DAY,
         MONTH,
         YEAR);

    status = RegistryInitialize(RegistryPath);
    if (!NT_SUCCESS(status))
        goto fail2;

    status = RegistryCreateServiceKey(&ServiceKey);
    if (!NT_SUCCESS(status))
        goto fail3;

    status = RegistryCreateSubKey(ServiceKey,
                                  "Unplug",
                                  REG_OPTION_NON_VOLATILE,
                                  &UnplugKey);
    if (!NT_SUCCESS(status))
        goto fail4;

    __DriverSetUnplugKey(UnplugKey);

    status = RegistryCreateSubKey(ServiceKey,
                                  "Status",
                                  REG_OPTION_VOLATILE,
                                  &StatusKey);
    if (!NT_SUCCESS(status))
        goto fail5;

    __DriverSetStatusKey(StatusKey);

    status = AcpiInitialize();
    if (!NT_SUCCESS(status))
        goto fail6;

    status = SystemInitialize();
    if (!NT_SUCCESS(status))
        goto fail7;

    status = HypercallInitialize();
    if (!NT_SUCCESS(status))
        goto fail8;

    status = BugCheckInitialize();
    if (!NT_SUCCESS(status))
        goto fail9;

    status = ModuleInitialize();
    if (!NT_SUCCESS(status))
        goto fail10;

    status = ProcessInitialize();
    if (!NT_SUCCESS(status))
        goto fail11;

    status = UnplugInitialize();
    if (!NT_SUCCESS(status))
        goto fail12;

    RegistryCloseKey(ServiceKey);

done:
    Trace("<====\n");

    return STATUS_SUCCESS;

fail12:
    Error("fail12\n");

    ProcessTeardown();

fail11:
    Error("fail11\n");

    ModuleTeardown();

fail10:
    Error("fail10\n");

    BugCheckTeardown();

fail9:
    Error("fail9\n");

    HypercallTeardown();

fail8:
    Error("fail8\n");

    SystemTeardown();

fail7:
    Error("fail7\n");

    AcpiTeardown();

fail6:
    Error("fail6\n");

    RegistryCloseKey(StatusKey);
    __DriverSetStatusKey(NULL);

fail5:
    Error("fail5\n");

    RegistryCloseKey(UnplugKey);
    __DriverSetUnplugKey(NULL);

fail4:
    Error("fail4\n");

    RegistryCloseKey(ServiceKey);

fail3:
    Error("fail3\n");

    RegistryTeardown();

fail2:
    Error("fail2\n");

    LogRemoveDisposition(Driver.InfoDisposition);
    Driver.InfoDisposition = NULL;

    LogRemoveDisposition(Driver.TraceDisposition);
    Driver.TraceDisposition = NULL;

    LogTeardown();

fail1:
    Error("fail1 (%08x)\n", status);

    ASSERT(IsZeroMemory(&Driver, sizeof (XEN_DRIVER)));

    return status;
}

NTSTATUS
DllUnload(
    VOID
    )
{
    HANDLE  StatusKey;
    HANDLE  UnplugKey;

    Trace("====>\n");

    if (*InitSafeBootMode > 0)
        goto done;

    UnplugTeardown();

    ProcessTeardown();

    ModuleTeardown();

    BugCheckTeardown();

    HypercallTeardown();

    SystemTeardown();

    StatusKey = __DriverGetStatusKey();

    RegistryCloseKey(StatusKey);
    __DriverSetStatusKey(NULL);

    UnplugKey = __DriverGetUnplugKey();

    RegistryCloseKey(UnplugKey);
    __DriverSetUnplugKey(NULL);

    RegistryTeardown();

    Info("XEN %d.%d.%d (%d) (%02d.%02d.%04d)\n",
         MAJOR_VERSION,
         MINOR_VERSION,
         MICRO_VERSION,
         BUILD_NUMBER,
         DAY,
         MONTH,
         YEAR);

    LogRemoveDisposition(Driver.InfoDisposition);
    Driver.InfoDisposition = NULL;

    LogRemoveDisposition(Driver.TraceDisposition);
    Driver.TraceDisposition = NULL;

    LogTeardown();

done:
    ASSERT(IsZeroMemory(&Driver, sizeof (XEN_DRIVER)));

    Trace("<====\n");

    return STATUS_SUCCESS;
}

DRIVER_INITIALIZE   DriverEntry;

NTSTATUS
DriverEntry(
    IN  PDRIVER_OBJECT  DriverObject,
    IN  PUNICODE_STRING RegistryPath
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    return STATUS_SUCCESS;
}
