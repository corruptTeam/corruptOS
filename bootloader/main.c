#include <efi.h>
#include <efilib.h>
#include <elf.h>

typedef unsigned long long size_t;

// --------------------------------------------
// GOP Structures
typedef struct {
	void* BaseAddress;
	size_t BufferSize;
	unsigned int Width;
	unsigned int Height;
	unsigned int PixelsPerScanLine;
} Framebuffer;

// --------------------------------------------
// Initialize GOP
Framebuffer framebuffer;
Framebuffer* InitializeGOP(){
	EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
	EFI_STATUS status;

	status = uefi_call_wrapper(BS->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);
	if(EFI_ERROR(status)){
		Print(L"Unable to locate GOP!\n\r");
		return NULL;
	}

	Print(L"GOP located!\n\r");

	framebuffer.BaseAddress = (void*)gop->Mode->FrameBufferBase;
	framebuffer.BufferSize = gop->Mode->FrameBufferSize;
	framebuffer.Width = gop->Mode->Info->HorizontalResolution;
	framebuffer.Height = gop->Mode->Info->VerticalResolution;
	framebuffer.PixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;

	return &framebuffer;
}

// --------------------------------------------
// Load file or directory
EFI_FILE* LoadFile(EFI_FILE* Directory, CHAR16* Path, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable){
	EFI_FILE* LoadedFile;

	EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
	SystemTable->BootServices->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (void**)&LoadedImage);

	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
	SystemTable->BootServices->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&FileSystem);

	if (Directory == NULL){
		FileSystem->OpenVolume(FileSystem, &Directory);
	}

	EFI_STATUS s = Directory->Open(Directory, &LoadedFile, Path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
	if (s != EFI_SUCCESS){
		return NULL;
	}
	return LoadedFile;

}

// --------------------------------------------
// PSF1 Font Structures and Defines
#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04

typedef struct {
	unsigned char magic[2];
	unsigned char mode;
	unsigned char charsize;
} PSF1_HEADER;

typedef struct {
	PSF1_HEADER* psf1_Header;
	void* glyphBuffer;
} PSF1_FONT;

// --------------------------------------------
// Load PSF1 Font
PSF1_FONT* LoadPSF1Font(EFI_FILE* Directory, CHAR16* Path, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
	EFI_FILE* font = LoadFile(Directory, Path, ImageHandle, SystemTable);
	if (font == NULL) return NULL;

	PSF1_HEADER* fontHeader;
	SystemTable->BootServices->AllocatePool(EfiLoaderData, sizeof(PSF1_HEADER), (void**)&fontHeader);
	UINTN size = sizeof(PSF1_HEADER);
	font->Read(font, &size, fontHeader);

	if (fontHeader->magic[0] != PSF1_MAGIC0 || fontHeader->magic[1] != PSF1_MAGIC1){
		return NULL;
	}

	UINTN glyphBufferSize = fontHeader->charsize * 256;
	if (fontHeader->mode == 1) { //512 glyph mode
		glyphBufferSize = fontHeader->charsize * 512;
	}

	void* glyphBuffer;
	{
		font->SetPosition(font, sizeof(PSF1_HEADER));
		SystemTable->BootServices->AllocatePool(EfiLoaderData, glyphBufferSize, (void**)&glyphBuffer);
		font->Read(font, &glyphBufferSize, glyphBuffer);
	}

	PSF1_FONT* finishedFont;
	SystemTable->BootServices->AllocatePool(EfiLoaderData, sizeof(PSF1_FONT), (void**)&finishedFont);
	finishedFont->psf1_Header = fontHeader;
	finishedFont->glyphBuffer = glyphBuffer;
	return finishedFont;
}

// --------------------------------------------
// Copy memory from aptr to bptr
int memcmp(const void* aptr, const void* bptr, size_t n){
	const unsigned char* a = aptr, *b = bptr;
	for (size_t i = 0; i < n; i++){
		if (a[i] < b[i]) return -1;
		else if (a[i] > b[i]) return 1;
	}
	return 0;
}

// --------------------------------------------
// Kernel structures
typedef struct {
    Framebuffer* framebuffer;
    PSF1_FONT* font;
    UINTN kernelSize;
    void* kernelStart;
    void* kernelEnd;
} KernelData;

// --------------------------------------------
// Entry method
EFI_STATUS efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
	InitializeLib(ImageHandle, SystemTable);

    // --------------------------------------------
    // Loading kernel
	Print(L"corruptOS Bootloader b1.0\n\r");
	Print(L"Locating kernel file...\n\r");
	EFI_FILE* Kernel = LoadFile(LoadFile(NULL, L"kernel", ImageHandle, SystemTable), L"kernel.elf", ImageHandle, SystemTable);
	if (Kernel == NULL){
		Print(L"Unable to locate kernel file!\n\r");
        return 1;
	}

	Print(L"Loading header structure...\n\r");

    // Loading header
	Elf64_Ehdr header;
	{
		UINTN FileInfoSize;
		EFI_FILE_INFO* FileInfo;
		Kernel->GetInfo(Kernel, &gEfiFileInfoGuid, &FileInfoSize, NULL);
		SystemTable->BootServices->AllocatePool(EfiLoaderData, FileInfoSize, (void**)&FileInfo);
		Kernel->GetInfo(Kernel, &gEfiFileInfoGuid, &FileInfoSize, (void**)&FileInfo);

		UINTN size = sizeof(header);
		Kernel->Read(Kernel, &size, &header);
	}

	Print(L"Verifying header...\n\r");

    // Verifying header
	if (
		memcmp(&header.e_ident[EI_MAG0], ELFMAG, SELFMAG) != 0 ||
		header.e_ident[EI_CLASS] != ELFCLASS64 ||
		header.e_ident[EI_DATA] != ELFDATA2LSB ||
		header.e_type != ET_EXEC ||
		header.e_machine != EM_X86_64 ||
		header.e_version != EV_CURRENT
	) {
		Print(L"Header is invalid!\r\n");
        return 1;
	}

	Print(L"Kernel header successfully verified!\r\n");
    Print(L"Loading kernel kernel data...\n\r");

    // Loading kernel data
    UINTN kernelSize;
	Elf64_Phdr* phdrs;
	{
		Kernel->SetPosition(Kernel, header.e_phoff);
		kernelSize = header.e_phnum * header.e_phentsize;
		SystemTable->BootServices->AllocatePool(EfiLoaderData, kernelSize, (void**)&phdrs);
		Kernel->Read(Kernel, &kernelSize, phdrs);
	}

	// Memory allocation
	for (
		Elf64_Phdr* phdr = phdrs;
		(char*)phdr < (char*)phdrs + header.e_phnum * header.e_phentsize;
		phdr = (Elf64_Phdr*)((char*)phdr + header.e_phentsize)
	)
	{
		switch (phdr->p_type){
			case PT_LOAD:
			{
				int pages = (phdr->p_memsz + 0x1000 - 1) / 0x1000;
				Elf64_Addr segment = phdr->p_paddr;
				SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, pages, &segment);

				Kernel->SetPosition(Kernel, phdr->p_offset);
				UINTN size = phdr->p_filesz;
				Kernel->Read(Kernel, &size, (void*)segment);
				break;
			}
		}
	}

	Print(L"Kernel loaded successfully!\n\r");

    // --------------------------------------------
    // Loading files
	PSF1_FONT* zapFont = LoadPSF1Font(LoadFile(NULL, L"files", ImageHandle, SystemTable), L"zap-light16.psf", ImageHandle, SystemTable);
	if (zapFont == NULL){
		Print(L"Unable to locate font file \"files\\zap-light16.psf\"!\n\r");
		return 1;
	}

	Print(L"Font file \"files\\zap-light16.psf\" found and loaded!\n\r");

    // --------------------------------------------
    // Required steps for kernel loading
	Framebuffer* framebuffer = InitializeGOP();
    if (framebuffer == NULL) return 1;

    // Pass everything in one struct
	KernelData kernelData;
	kernelData.framebuffer = framebuffer;
    kernelData.kernelSize = kernelSize;
    kernelData.kernelStart = phdrs;
    kernelData.kernelEnd = phdrs + kernelSize;
	kernelData.font = zapFont;

    // Clear screen to avoid problems
	uefi_call_wrapper(SystemTable->ConOut->ClearScreen, 1, SystemTable->ConOut);

    // Execute kernel entry method
    void (*RunKernel)(KernelData) = ((__attribute__((sysv_abi)) void (*)(KernelData)) header.e_entry);
	RunKernel(kernelData);
	return EFI_SUCCESS;
}