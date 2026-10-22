#ifndef _XNU_LOADER_H_
#define _XNU_LOADER_H_

//
// The boot shim copies everything appended after the UEFI FD to this address.
// The region is marked reserved in the device memory map.
//
#ifndef XNU_PAYLOAD_BASE
#define XNU_PAYLOAD_BASE      0xA8000000ULL
#endif
#define XNU_PAYLOAD_MAX_SIZE  0x04000000ULL
#define XNU_PAYLOAD_MAGIC     0x50554E58 // "XNUP"

typedef struct {
  UINT32 Magic;
  UINT32 Size;
} XNU_PAYLOAD_HEADER;

// The header is followed by entries; an empty name ends the list.
typedef struct {
  CHAR8  Name[16];
  UINT32 Size;
  UINT32 Reserved;
  // UINT8 Data[Size], padded to 16 bytes
} XNU_PAYLOAD_ENTRY;

VOID
XnuLoaderMain ();

#endif /* _XNU_LOADER_H_ */
