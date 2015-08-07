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

#include <ntddk.h>
#include <procgrp.h>
#include <ntstrsafe.h>

#include "registry.h"
#include "fdo.h"
#include "pdo.h"
#include "driver.h"
#include "names.h"
#include "dbg_print.h"
#include "assert.h"
#include "util.h"
#include "version.h"

extern PULONG       InitSafeBootMode;

typedef struct _XENBUS_DRIVER {
    PDRIVER_OBJECT      DriverObject;
    HANDLE              ParametersKey;
} XENBUS_DRIVER, *PXENBUS_DRIVER;

static XENBUS_DRIVER    Driver;

#define XENBUS_DRIVER_TAG   'VIRD'

static FORCEINLINE PVOID
__DriverAllocate(
    IN  ULONG   Length
    )
{
    return __AllocatePoolWithTag(NonPagedPool, Length, XENBUS_DRIVER_TAG);
}

static FORCEINLINE VOID
__DriverFree(
    IN  PVOID   Buffer
    )
{
    ExFreePoolWithTag(Buffer, XENBUS_DRIVER_TAG);
}

static FORCEINLINE VOID
__DriverSetDriverObject(
    IN  PDRIVER_OBJECT  DriverObject
    )
{
    Driver.DriverObject = DriverObject;
}

static FORCEINLINE PDRIVER_OBJECT
__DriverGetDriverObject(
    VOID
    )
{
    return Driver.DriverObject;
}

PDRIVER_OBJECT
DriverGetDriverObject(
    VOID
    )
{
    return __DriverGetDriverObject();
}

static FORCEINLINE VOID
__DriverSetParametersKey(
    IN  HANDLE  Key
    )
{
    Driver.ParametersKey = Key;
}

static FORCEINLINE HANDLE
__DriverGetParametersKey(
    VOID
    )
{
    return Driver.ParametersKey;
}

HANDLE
DriverGetParametersKey(
    VOID
    )
{
    return __DriverGetParametersKey();
}

DRIVER_UNLOAD       DriverUnload;

VOID
DriverUnload(
    IN  PDRIVER_OBJECT  DriverObject
    )
{
    HANDLE              ParametersKey;

    ASSERT3P(DriverObject, ==, __DriverGetDriverObject());

    Trace("====>\n");

    if (*InitSafeBootMode > 0)
        goto done;

    ParametersKey = __DriverGetParametersKey();

    RegistryCloseKey(ParametersKey);
    __DriverSetParametersKey(NULL);

    RegistryTeardown();

    Info("XENBUS %d.%d.%d (%d) (%02d.%02d.%04d)\n",
         MAJOR_VERSION,
         MINOR_VERSION,
         MICRO_VERSION,
         BUILD_NUMBER,
         DAY,
         MONTH,
         YEAR);

done:
    __DriverSetDriverObject(NULL);

    ASSERT(IsZeroMemory(&Driver, sizeof (XENBUS_DRIVER)));

    Trace("<====\n");
}

__drv_functionClass(IO_COMPLETION_ROUTINE)
__drv_sameIRQL
static NTSTATUS
DriverQueryIdCompletion(
    IN  PDEVICE_OBJECT  DeviceObject,
    IN  PIRP            Irp,
    IN  PVOID           Context
    )
{
    PKEVENT             Event = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
DriverQueryId(
    IN  PDEVICE_OBJECT      PhysicalDeviceObject,
    IN  BUS_QUERY_ID_TYPE   Type,
    OUT PVOID               *Information
    )
{
    PDEVICE_OBJECT          DeviceObject;
    PIRP                    Irp;
    KEVENT                  Event;
    PIO_STACK_LOCATION      StackLocation;
    NTSTATUS                status;

    ASSERT3U(KeGetCurrentIrql(), ==, PASSIVE_LEVEL);

    Trace("====> %s\n", BusQueryIdTypeName(Type));

    DeviceObject = IoGetAttachedDeviceReference(PhysicalDeviceObject);

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);

    status = STATUS_INSUFFICIENT_RESOURCES;
    if (Irp == NULL)
        goto fail1;

    StackLocation = IoGetNextIrpStackLocation(Irp);

    StackLocation->MajorFunction = IRP_MJ_PNP;
    StackLocation->MinorFunction = IRP_MN_QUERY_ID;
    StackLocation->Flags = 0;
    StackLocation->Parameters.QueryId.IdType = Type;
    StackLocation->DeviceObject = DeviceObject;
    StackLocation->FileObject = NULL;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoSetCompletionRoutine(Irp,
                           DriverQueryIdCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    // Default completion status
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    status = IoCallDriver(DeviceObject, Irp);
    if (status == STATUS_PENDING) {
        (VOID) KeWaitForSingleObject(&Event,
                                     Executive,
                                     KernelMode,
                                     FALSE,
                                     NULL);
        status = Irp->IoStatus.Status;
    } else {
        ASSERT3U(status, ==, Irp->IoStatus.Status);
    }

    if (!NT_SUCCESS(status))
        goto fail2;

    *Information = (PVOID)Irp->IoStatus.Information;

    IoFreeIrp(Irp);
    ObDereferenceObject(DeviceObject);

    Trace("<====\n");

    return STATUS_SUCCESS;

fail2:
    Error("fail2\n");

    IoFreeIrp(Irp);

fail1:
    Error("fail1 (%08x)\n", status);

    ObDereferenceObject(DeviceObject);

    return status;
}

static NTSTATUS
DriverGetActiveDeviceInstance(
    OUT PWCHAR      *DeviceID,
    OUT PWCHAR      *InstanceID
    )
{
    HANDLE          ParametersKey;
    PANSI_STRING    Ansi;
    UNICODE_STRING  Unicode;
    NTSTATUS        status;

    ParametersKey = __DriverGetParametersKey();

    *DeviceID = NULL;
    *InstanceID = NULL;

    status = RegistryQuerySzValue(ParametersKey,
                                  "ActiveDeviceID",
                                  &Ansi);
    if (!NT_SUCCESS(status)) {
        if (status != STATUS_OBJECT_NAME_NOT_FOUND)
            goto fail1;

        // The active device is not yet set
        goto done;
    }

    Unicode.MaximumLength = (USHORT)(Ansi[0].MaximumLength / sizeof (CHAR) * sizeof (WCHAR));
    Unicode.Buffer = __DriverAllocate(Unicode.MaximumLength);

    status = STATUS_NO_MEMORY;
    if (Unicode.Buffer == NULL)
        goto fail2;

    status = RtlAnsiStringToUnicodeString(&Unicode,
                                          &Ansi[0],
                                          FALSE);
    ASSERT(NT_SUCCESS(status));

    RegistryFreeSzValue(Ansi);

    *DeviceID = Unicode.Buffer;
        
    status = RegistryQuerySzValue(ParametersKey,
                                  "ActiveInstanceID",
                                  &Ansi);
    if (!NT_SUCCESS(status))
        goto fail3;

    Unicode.MaximumLength = (USHORT)(Ansi[0].MaximumLength / sizeof (CHAR) * sizeof (WCHAR));
    Unicode.Buffer = __DriverAllocate(Unicode.MaximumLength);

    status = STATUS_NO_MEMORY;
    if (Unicode.Buffer == NULL)
        goto fail4;

    status = RtlAnsiStringToUnicodeString(&Unicode,
                                          &Ansi[0],
                                          FALSE);
    ASSERT(NT_SUCCESS(status));

    RegistryFreeSzValue(Ansi);

    *InstanceID = Unicode.Buffer;

done:        
    Trace("DeviceID = %ws\n", (*DeviceID != NULL) ? *DeviceID : L"NOT SET");
    Trace("InstanceID = %ws\n", (*InstanceID != NULL) ? *InstanceID : L"NOT SET");

    return STATUS_SUCCESS;

fail4:
    Error("fail4\n");

    RegistryFreeSzValue(Ansi);

fail3:
    Error("fail3\n");

    __DriverFree(*DeviceID);
    *DeviceID = NULL;

    goto fail1;

fail2:
    Error("fail2\n");

    RegistryFreeSzValue(Ansi);

fail1:
    Error("fail1 (%08x)\n", status);

    return status;
}

DRIVER_ADD_DEVICE   DriverAddDevice;

NTSTATUS
#pragma prefast(suppress:28152) // Does not clear DO_DEVICE_INITIALIZING
DriverAddDevice(
    IN  PDRIVER_OBJECT  DriverObject,
    IN  PDEVICE_OBJECT  DeviceObject
    )
{
    PWCHAR              ActiveDeviceID;
    PWCHAR              ActiveInstanceID;
    PWCHAR              DeviceID;
    PWCHAR              InstanceID;
    BOOLEAN             Active;
    NTSTATUS            status;

    ASSERT3P(DriverObject, ==, __DriverGetDriverObject());

    Trace("====>\n");

    status = DriverGetActiveDeviceInstance(&ActiveDeviceID, &ActiveInstanceID);
    if (!NT_SUCCESS(status))
        goto fail1;

    Active = FALSE;

    if (ActiveDeviceID == NULL) {
        ASSERT3P(ActiveInstanceID, ==, NULL);
        goto done;
    }

    status = DriverQueryId(DeviceObject, BusQueryDeviceID, &DeviceID);
    if (!NT_SUCCESS(status))
        goto fail2;

    Trace("DeviceID = %ws\n", DeviceID);

    status = DriverQueryId(DeviceObject, BusQueryInstanceID, &InstanceID);
    if (!NT_SUCCESS(status))
        goto fail3;

    Trace("InstanceID = %ws\n", InstanceID);

    if (_wcsicmp(DeviceID, ActiveDeviceID) == 0 &&
        _wcsicmp(InstanceID, ActiveInstanceID) == 0)
        Active = TRUE;

    ExFreePool(InstanceID);

    ExFreePool(DeviceID);

    __DriverFree(ActiveInstanceID);
    __DriverFree(ActiveDeviceID);

done:
    status = FdoCreate(DeviceObject, Active);
    if (!NT_SUCCESS(status))
        goto fail4;

    Trace("<====\n");

    return STATUS_SUCCESS;

fail4:
    Error("fail4\n");

    goto fail1;

fail3:
    Error("fail3\n");

    ExFreePool(DeviceID);

fail2:
    Error("fail2\n");

    __DriverFree(ActiveInstanceID);
    __DriverFree(ActiveDeviceID);

fail1:
    Error("fail1 (%08x)\n", status);

    return status;
}

DRIVER_DISPATCH DriverDispatch;

NTSTATUS 
DriverDispatch(
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    )
{
    PXENBUS_DX          Dx;
    NTSTATUS            status;

    Dx = (PXENBUS_DX)DeviceObject->DeviceExtension;
    ASSERT3P(Dx->DeviceObject, ==, DeviceObject);

    if (Dx->DevicePnpState == Deleted) {
        status = STATUS_NO_SUCH_DEVICE;

        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        goto done;
    }

    status = STATUS_NOT_SUPPORTED;
    switch (Dx->Type) {
    case PHYSICAL_DEVICE_OBJECT: {
        PXENBUS_PDO Pdo = Dx->Pdo;

        status = PdoDispatch(Pdo, Irp);
        break;
    }
    case FUNCTION_DEVICE_OBJECT: {
        PXENBUS_FDO Fdo = Dx->Fdo;

        status = FdoDispatch(Fdo, Irp);
        break;
    }
    default:
        ASSERT(FALSE);
        break;
    }

done:
    return status;
}

DRIVER_INITIALIZE   DriverEntry;

NTSTATUS
DriverEntry(
    IN  PDRIVER_OBJECT  DriverObject,
    IN  PUNICODE_STRING RegistryPath
    )
{
    HANDLE              ServiceKey;
    HANDLE              ParametersKey;
    ULONG               Index;
    NTSTATUS            status;

    ASSERT3P(__DriverGetDriverObject(), ==, NULL);

    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
    WdmlibProcgrpInitialize();

    Trace("====>\n");

    __DriverSetDriverObject(DriverObject);

    Driver.DriverObject->DriverUnload = DriverUnload;

    if (*InitSafeBootMode > 0)
        goto done;

    Info("%d.%d.%d (%d) (%02d.%02d.%04d)\n",
         MAJOR_VERSION,
         MINOR_VERSION,
         MICRO_VERSION,
         BUILD_NUMBER,
         DAY,
         MONTH,
         YEAR);

    status = XenTouch(__MODULE__,
                      MAJOR_VERSION,
                      MINOR_VERSION,
                      MICRO_VERSION,
                      BUILD_NUMBER);
    if (!NT_SUCCESS(status))
        goto done;

    status = RegistryInitialize(RegistryPath);
    if (!NT_SUCCESS(status))
        goto fail1;

    status = RegistryOpenServiceKey(KEY_READ, &ServiceKey);
    if (!NT_SUCCESS(status))
        goto fail2;

    status = RegistryOpenSubKey(ServiceKey, "Parameters", KEY_READ, &ParametersKey);
    if (!NT_SUCCESS(status))
        goto fail3;

    __DriverSetParametersKey(ParametersKey);

    RegistryCloseKey(ServiceKey);

    DriverObject->DriverExtension->AddDevice = DriverAddDevice;

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++) {
#pragma prefast(suppress:28169) // No __drv_dispatchType annotation
#pragma prefast(suppress:28168) // No matching __drv_dispatchType annotation for IRP_MJ_CREATE
       DriverObject->MajorFunction[Index] = DriverDispatch;
    }

done:
    Trace("<====\n");

    return STATUS_SUCCESS;

fail3:
    Error("fail3\n");

    RegistryCloseKey(ServiceKey);

fail2:
    Error("fail2\n");

    RegistryTeardown();

fail1:
    Error("fail1 (%08x)\n", status);

    __DriverSetDriverObject(NULL);

    ASSERT(IsZeroMemory(&Driver, sizeof (XENBUS_DRIVER)));

    return status;
}
