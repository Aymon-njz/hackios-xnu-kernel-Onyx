/**
  XNU payload handoff.

  The boot shim places an appended XNUP container in reserved RAM. This
  library starts its loader.efi entry without reading or writing Android
  partitions; a failed handoff simply cold-resets the device.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "XnuLoader.h"

STATIC
VOID
StartPayloadLoader (
  VOID
  )
{
  XNU_PAYLOAD_HEADER *Header = (XNU_PAYLOAD_HEADER *)XNU_PAYLOAD_BASE;
  UINT8              *Cursor;
  UINT8              *End;

  if (Header->Magic != XNU_PAYLOAD_MAGIC || Header->Size > XNU_PAYLOAD_MAX_SIZE) {
    return;
  }

  Cursor = (UINT8 *)(Header + 1);
  End    = Cursor + Header->Size;

  while (Cursor + sizeof (XNU_PAYLOAD_ENTRY) <= End) {
    XNU_PAYLOAD_ENTRY *Entry = (XNU_PAYLOAD_ENTRY *)Cursor;
    EFI_HANDLE         Handle;

    if (Entry->Name[0] == 0) {
      return;
    }

    if (Entry->Size > (UINTN)(End - (UINT8 *)(Entry + 1))) {
      return;
    }

    if (AsciiStrnCmp (Entry->Name, "loader.efi", sizeof (Entry->Name)) == 0) {
      if (!EFI_ERROR (gBS->LoadImage (FALSE, gImageHandle, NULL, Entry + 1, Entry->Size, &Handle))) {
        gBS->StartImage (Handle, NULL, NULL);
      }
      return;
    }

    Cursor += sizeof (XNU_PAYLOAD_ENTRY) + ALIGN_VALUE (Entry->Size, 16);
  }
}

VOID
XnuLoaderMain (
  VOID
  )
{
  StartPayloadLoader ();
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
}
