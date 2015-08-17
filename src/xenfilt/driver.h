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

#ifndef _XENFILT_DRIVER_H
#define _XENFILT_DRIVER_H

extern PDRIVER_OBJECT
DriverGetDriverObject(
    VOID
    );

extern HANDLE
DriverGetParametersKey(
    VOID
    );

extern VOID
DriverAcquireMutex(
    VOID
     );

extern VOID
DriverReleaseMutex(
    VOID
     );

typedef enum _XENFILT_FILTER_STATE {
    XENFILT_FILTER_ENABLED = 0,
    XENFILT_FILTER_PENDING,
    XENFILT_FILTER_DISABLED
} XENFILT_FILTER_STATE, *PXENFILT_FILTER_STATE;

VOID
DriverSetFilterState(
    VOID
    );

XENFILT_FILTER_STATE
DriverGetFilterState(
    VOID
    );

#include "emulated.h"

PXENFILT_EMULATED_CONTEXT
DriverGetEmulatedContext(
    VOID
    );

#include "pvdevice.h"

PXENFILT_PVDEVICE_CONTEXT
DriverGetPvdeviceContext(
    VOID
    );

typedef struct _XENFILT_FDO XENFILT_FDO, *PXENFILT_FDO;
typedef struct _XENFILT_PDO XENFILT_PDO, *PXENFILT_PDO;

#include "pdo.h"
#include "fdo.h"

#define MAX_DEVICE_ID_LEN   200

extern VOID
DriverAddFunctionDeviceObject(
    IN  PXENFILT_FDO    Fdo
    );

extern VOID
DriverRemoveFunctionDeviceObject(
    IN  PXENFILT_FDO    Fdo
    );

#pragma warning(push)
#pragma warning(disable:4201) // nonstandard extension used : nameless struct/union

typedef struct _XENFILT_DX {
    PDEVICE_OBJECT      DeviceObject;
    DEVICE_OBJECT_TYPE  Type;

    DEVICE_PNP_STATE    DevicePnpState;
    DEVICE_PNP_STATE    PreviousDevicePnpState;

    SYSTEM_POWER_STATE  SystemPowerState;
    DEVICE_POWER_STATE  DevicePowerState;

    CHAR                DeviceID[MAX_DEVICE_ID_LEN];
    CHAR                InstanceID[MAX_DEVICE_ID_LEN];

    IO_REMOVE_LOCK      RemoveLock;

    LIST_ENTRY          ListEntry;

    union {
        PXENFILT_FDO    Fdo;
        PXENFILT_PDO    Pdo;
    };
} XENFILT_DX, *PXENFILT_DX;

#pragma warning(pop)

#endif  // _XENFILT_DRIVER_H
