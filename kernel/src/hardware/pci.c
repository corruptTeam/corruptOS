#include <hardware/pci.h>
#include <video/gop.h>
#include <memory/heap.h>

const char* DeviceClasses[] = {
    "Unclassified",
    "Mass Storage Controller",
    "Network Controller",
    "Display Controller",
    "Multimedia Controller",
    "Memory Controller",
    "Bridge Device",
    "Simple Communication Controller",
    "Base System Peripheral",
    "Input Device Controller",
    "Docking Station", 
    "Processor",
    "Serial Bus Controller",
    "Wireless Controller",
    "Intelligent Controller",
    "Satellite Communication Controller",
    "Encryption Controller",
    "Signal Processing Controller",
    "Processing Accelerator",
    "Non Essential Instrumentation"
};

const char* GetVendorName(uint16_t vendorID){
    switch (vendorID){
        case 0x8086:
            return "Intel Corp.";
        case 0x1022:
            return "AMD";
        case 0x10DE:
            return "NVIDIA Corporation";
        case 0x1234:
            return "QEMU Emulator";
    }
    return to_hstring16(vendorID);
}

pci_device **pci_devices = 0;
uint32_t devs = 0;

pci_driver **pci_drivers = 0;
uint32_t drivs = 0;

void add_pci_device(pci_device *pdev)
{
	pci_devices[devs] = pdev;
	devs++;
	return;
}

void outportl(uint16_t portid, uint32_t value)
{
	asm volatile("outl %%eax, %%dx": :"d" (portid), "a" (value));
}

uint32_t inportl(uint16_t portid)
{
	uint32_t ret;
	asm volatile("inl %%dx, %%eax":"=a"(ret):"d"(portid));
	return ret;
}

uint16_t pci_read_word(uint16_t bus, uint16_t slot, uint16_t func, uint16_t offset)
{
	uint64_t address;
    uint64_t lbus = (uint64_t)bus;
    uint64_t lslot = (uint64_t)slot;
    uint64_t lfunc = (uint64_t)func;
    uint16_t tmp = 0;
    address = (uint64_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outportl (0xCF8, address);
    tmp = (uint16_t)((inportl(0xCFC) >> ((offset & 2) * 8)) & 0xffff);
    return (tmp);
}

uint16_t getVendorID(uint16_t bus, uint16_t device, uint16_t function)
{
        uint32_t r0 = pci_read_word(bus,device,function,0);
        return r0;
}

uint16_t getDeviceID(uint16_t bus, uint16_t device, uint16_t function)
{
        uint32_t r0 = pci_read_word(bus,device,function,2);
        return r0;
}

uint16_t getClassId(uint16_t bus, uint16_t device, uint16_t function)
{
        uint32_t r0 = pci_read_word(bus,device,function,0xA);
        return (r0 & ~0x00FF) >> 8;
}

uint16_t getSubClassId(uint16_t bus, uint16_t device, uint16_t function)
{
        uint32_t r0 = pci_read_word(bus,device,function,0xA);
        return (r0 & ~0xFF00);
}

void pci_init()
{
	devs = 0;
    drivs = 0;
	pci_devices = (pci_device**)malloc(32 * sizeof(pci_device));
	pci_drivers = (pci_driver**)malloc(32 * sizeof(pci_driver));
    print("[PCI] Initialized!\n", 0xFFFFFF);
}

void pci_probe()
{
	for(uint32_t bus = 0; bus < 256; bus++)
    {
        for(uint32_t slot = 0; slot < 32; slot++)
        {
            for(uint32_t function = 0; function < 8; function++)
            {
                    uint16_t vendor = getVendorID(bus, slot, function);
                    if(vendor == 0xffff) continue;
                    uint16_t device = getDeviceID(bus, slot, function);
                    uint16_t class = getClassId(bus, slot, function);
                    pci_device *pdev = (pci_device *)malloc(sizeof(pci_device));
                    pdev->vendor = vendor;
                    pdev->device = device;
                    pdev->func = function;
                    pdev->driver = 0;
                    add_pci_device(pdev);

                    print("[PCI] Device found! ID:", 0xFFFFFF);
                    print(to_hstring16(device), 0xFFFFFF);
                    print(", Vendor:", 0xFFFFFF);
                    print(GetVendorName(vendor), 0xFFFFFF);
                    print(", ClassName:", 0xFFFFFF);
                    print(DeviceClasses[class], 0xFFFFFF);
                    print(", Class:", 0xFFFFFF);
                    print(to_hstring16(class), 0xFFFFFF);
                    newline();
            }
        }
    }
}

uint16_t pciCheckVendor(uint16_t bus, uint16_t slot)
{
   uint16_t vendor,device;
   if ((vendor = pci_read_word(bus,slot,0,0)) != 0xFFFF) {
      device = pci_read_word(bus,slot,0,2);
   } return (vendor);
}

void pci_register_driver(pci_driver *driv)
{
	pci_drivers[drivs] = driv;
	drivs++;
	return;
}

void pci_proc_dump(uint8_t *buffer)
{
	for(int i = 0; i < devs; i++)
	{
		pci_device *pci_dev = pci_devices[i];
	    if(pci_dev->driver) {
            print("ProcDump Driver Name: ", 0xFFFFFF);
            print(pci_dev->driver->name, 0xFFFFFF);
            newline();
        } else {
            print("ProcDump Vendor: ", 0xFFFFFF);
            print(pci_dev->vendor, 0xFFFFFF);
            newline();
        }
			
	}
}
