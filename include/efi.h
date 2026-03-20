// EntropyOS/src/include/efi.h
#ifndef __EFI_H__
#define __EFI_H__

#include <stdint.h>
#include <stddef.h>
#include "../kernel/kernel.h"

/* Basic fixed-width aliases */
typedef int32_t   INT32;
typedef uint8_t   UINT8;

typedef uint_least16_t char16_t; /* for C compilation */
typedef char16_t  CHAR16;
typedef uint64_t  UINT64;
typedef uint32_t  UINT32;
typedef uint16_t  UINT16;
typedef uint64_t  UINTN;
typedef void      VOID;

#define EFIAPI __attribute__((ms_abi))

typedef UINTN  EFI_STATUS;
typedef VOID*  EFI_HANDLE;

/* Status */
#define EFI_SUCCESS 0

/* Table header */
typedef struct {
        UINT64 Signature;
        UINT32 Revision;
        UINT32 HeaderSize;
        UINT32 CRC32;
        UINT32 Reserved;
} EFI_TABLE_HEADER;

/* Forward decls */
struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* Input key */
typedef struct {
        UINT16 ScanCode;
        CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(
        struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
        EFI_INPUT_KEY *Key
);

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
        VOID *Reset;
        EFI_INPUT_READ_KEY ReadKeyStroke;
        VOID *WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

/* Text output mode (subset) */
typedef struct {
        INT32 MaxMode;
        INT32 Mode;
        INT32 Attribute;
        INT32 CursorColumn;
        INT32 CursorRow;
        UINT8 CursorVisible;
        INT32 MaxColumn;
        INT32 MaxRow;
} SIMPLE_TEXT_OUTPUT_MODE;

/* Text output protocol (subset) */
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
        struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(
        struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
        struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_CURSOR_POSITION)(
        struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Column, UINTN Row);

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
        VOID *Reset;
        EFI_TEXT_STRING OutputString;
        VOID *TestString;
        VOID *QueryMode;
        VOID *SetMode;
        EFI_TEXT_SET_ATTRIBUTE SetAttribute;
        EFI_TEXT_CLEAR_SCREEN ClearScreen;
        EFI_TEXT_SET_CURSOR_POSITION SetCursorPosition;
        VOID *EnableCursor;
        SIMPLE_TEXT_OUTPUT_MODE *Mode; /* was VOID*; now typed so MaxColumn/MaxRow are accessible */
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* Reset types/services (subset) */
typedef enum {
        EfiResetCold = 0,
        EfiResetWarm,
        EfiResetShutdown,
        EfiResetPlatformSpecific
} EFI_RESET_TYPE;

typedef VOID (EFIAPI *EFI_RESET_SYSTEM)(
        EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus, UINTN DataSize, VOID *ResetData);

typedef struct {
        EFI_TABLE_HEADER Hdr;
        VOID *GetTime;
        VOID *SetTime;
        VOID *GetWakeupTime;
        VOID *SetWakeupTime;
        VOID *GetNextHighMonotonicCount;
        EFI_RESET_SYSTEM ResetSystem;
} EFI_RUNTIME_SERVICES;

/* Boot services (only the entries you use) */
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_HANDLE_BUFFER)(
        UINTN, const void*, void*, UINTN*, EFI_HANDLE**);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
        const void*, void*, void**);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
        EFI_HANDLE, const void*, void**);

/* Typed BootServices functions used by the kernel to avoid casting warnings */
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
        UINTN *MemoryMapSize, void *MemoryMap, UINTN *MapKey,
        UINTN *DescriptorSize, UINT32 *DescriptorVersion);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
        EFI_HANDLE ImageHandle, UINTN MapKey);

/* Stall function prototype used by BootServices */
typedef VOID (EFIAPI *EFI_STALL)(UINTN Microseconds);

typedef struct {
        EFI_TABLE_HEADER Hdr;
        VOID *RaiseTPL;
        VOID *RestoreTPL;
        VOID *AllocatePages;
        VOID *FreePages;
        EFI_GET_MEMORY_MAP GetMemoryMap;
        VOID *AllocatePool;
        VOID *FreePool;
        VOID *CreateEvent;
        VOID *SetTimer;
        VOID *WaitForEvent;
        VOID *SignalEvent;
        VOID *CloseEvent;
        VOID *CheckEvent;
        VOID *InstallProtocolInterface;
        VOID *ReinstallProtocolInterface;
        VOID *UninstallProtocolInterface;
        EFI_HANDLE_PROTOCOL HandleProtocol;     /* used */
        VOID *Reserved;
        VOID *RegisterProtocolNotify;
        VOID *LocateHandle;
        VOID *LocateDevicePath;
        VOID *InstallConfigurationTable;
        VOID *LoadImage;
        VOID *StartImage;
        VOID *Exit;
        VOID *UnloadImage;
        EFI_EXIT_BOOT_SERVICES ExitBootServices;
        VOID *GetNextMonotonicCount;
        EFI_STALL Stall;
        VOID *SetWatchdogTimer;
        VOID *ConnectController;
        VOID *DisconnectController;
        VOID *OpenProtocol;
        VOID *CloseProtocol;
        VOID *OpenProtocolInformation;
        VOID *ProtocolsPerHandle;
        EFI_LOCATE_HANDLE_BUFFER LocateHandleBuffer; /* used */
        EFI_LOCATE_PROTOCOL      LocateProtocol;     /* used */
} EFI_BOOT_SERVICES;

/* System table (subset) */
typedef struct {
        EFI_TABLE_HEADER Hdr;
        CHAR16 *FirmwareVendor;
        UINT32 FirmwareRevision;
        VOID *ConsoleInHandle;
        EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
        VOID *ConsoleOutHandle;
        EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
        VOID *StandardErrorHandle;
        VOID *StdErr;
        EFI_RUNTIME_SERVICES *RuntimeServices;
        EFI_BOOT_SERVICES    *BootServices;
        UINTN NumberOfTableEntries;
        VOID *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* GOP types (subset) */
typedef enum {
        PixelRedGreenBlueReserved8BitPerColor = 0,
        PixelBlueGreenRedReserved8BitPerColor = 1,
        PixelBitMask = 2,
        PixelBltOnly = 3
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
        UINT32 RedMask;
        UINT32 GreenMask;
        UINT32 BlueMask;
        UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
        UINT32 Version;
        UINT32 HorizontalResolution;
        UINT32 VerticalResolution;
        EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
        EFI_PIXEL_BITMASK PixelInformation;
        UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
        UINT32 MaxMode;
        UINT32 Mode;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
        UINTN SizeOfInfo;
        UINTN FrameBufferBase;
        UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
        VOID *QueryMode;
        VOID *SetMode;
        VOID *Blt;
        EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* GOP GUID (canonical) */
static const struct { uint32_t a; uint16_t b; uint16_t c; uint8_t d[8]; }
EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {0x9042a9de, 0x23dc, 0x4a38,
                                     {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};

/* Text attribute colors (4-bit values, not RGB) */
#define EFI_BLACK          0x0
#define EFI_BLUE           0x1
#define EFI_GREEN          0x2
#define EFI_CYAN           0x3
#define EFI_RED            0x4
#define EFI_MAGENTA        0x5
#define EFI_BROWN          0x6
#define EFI_LIGHTGRAY      0x7
#define EFI_DARKGRAY       0x8
#define EFI_LIGHTBLUE      0x9
#define EFI_LIGHTGREEN     0xA
#define EFI_LIGHTCYAN      0xB
#define EFI_LIGHTRED       0xC
#define EFI_LIGHTMAGENTA   0xD
#define EFI_YELLOW         0xE
#define EFI_WHITE          0xF

/* Compose attribute: low 4 bits = FG, high 4 bits = BG */
#define EFI_TEXT_ATTR(FG,BG) ((UINTN)((FG) | ((BG) << 4)))

#endif
