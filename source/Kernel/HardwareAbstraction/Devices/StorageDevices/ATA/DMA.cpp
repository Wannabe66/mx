// DMA.cpp
// Copyright (c) 2013 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include <Kernel.hpp>
#include <HardwareAbstraction/Devices/StorageDevice.hpp>
#include <HardwareAbstraction/Interrupts.hpp>
#include <HardwareAbstraction/Devices/IOPort.hpp>
#include <Memory.hpp>
#include <StandardIO.hpp>

using namespace Library::StandardIO;
using namespace Kernel::HardwareAbstraction::MemoryManager;

namespace Kernel {
namespace HardwareAbstraction {
namespace Devices {
namespace Storage {
namespace ATA {
namespace DMA
{
	const uint8_t DMACommandRead		= 0x8;
	const uint8_t DMACommandStart		= 0x1;
	const uint8_t DMACommandStop		= 0x0;
	const uint8_t DMACommandWrite		= 0x0;

	const uint8_t ATA_ReadSectors28DMA	= 0xC8;
	const uint8_t ATA_ReadSectors48DMA	= 0x25;
	const uint8_t ATA_WriteSectors28DMA	= 0xCA;
	const uint8_t ATA_WriteSectors48DMA	= 0x35;

	static volatile bool _WaitingDMA14	= false;
	static volatile bool _WaitingDMA15	= false;

	static ATADrive* PreviousDevice = 0;

	struct PRDEntry
	{
		uint32_t bufferPhysAddr;
		uint16_t byteCount;
		uint16_t lastEntry;

	} __attribute__((packed));

	struct PRDTableCache
	{
		DMAAddr address;
		uint32_t length;
		uint32_t used;
	};

	#define MaxCachedTables 2
	static rde::vector<PRDTableCache>* cachedPRDTables;

	void Initialise()
	{
		using namespace Kernel::HardwareAbstraction::Devices::PCI;
		PCIDevice* ide = PCI::GetDeviceByClassSubclass(01, 01);

		assert(ide);

		// enable bus mastering
		uint32_t f = ide->GetRegisterData(0x4, 0, 2);
		ide->WriteRegisterData(0x4, 0, 2, f | 0x4);

		uint32_t mmio = (uint32_t) ide->GetBAR(4);
		assert(ide->IsBARIOPort(4));

		cachedPRDTables = new rde::vector<PRDTableCache>();
		for(int i = 0; i < MaxCachedTables; i++)
		{
			// precreate these
			PRDTableCache tcache;
			tcache.address = Physical::AllocateDMA(1);
			tcache.length = 0x1000;
			tcache.used = 0;

			cachedPRDTables->push_back(tcache);
		}

		IOPort::WriteByte((uint16_t) mmio + 2, 0x4);
		IOPort::WriteByte((uint16_t) mmio + 10, 0x4);

		IOPort::WriteByte(0x3F6, 0);
		IOPort::WriteByte(0x376, 0);

		Log("Initialised Busmastering DMA with BaseIO %x", mmio);
	}





	IOResult ReadBytes(ATADrive* dev, uint64_t Buffer, uint64_t Sector, uint64_t Bytes)
	{
		(void) Buffer;

		uint32_t mmio = (uint32_t) dev->ParentPCI->GetBAR(4);

		// get a prd
		bool found = false;
		PRDTableCache prdCache;
		for(PRDTableCache& cache : *cachedPRDTables)
		{
			if(!cache.used)
			{
				prdCache = cache;
				cache.used = 1;
				found = true;
				break;
			}
		}

		if(!found)
		{
			prdCache.address = Physical::AllocateDMA(1);
			prdCache.length = 0x1000;
			prdCache.used = 1;

			cachedPRDTables->push_back(prdCache);
		}


		PRDEntry* prd = (PRDEntry*) prdCache.address.virt;

		// allocate a buffer that we know is a good deal
		DMAAddr paddr = Physical::AllocateDMA((Bytes + 0xFFF) / 0x1000);

		prd->bufferPhysAddr = (uint32_t) paddr.phys;
		prd->byteCount = (uint16_t) Bytes;
		prd->lastEntry = 0x8000;

		// write the bytes of address into register
		IOPort::Write32((uint16_t) (mmio + (dev->GetBus() ? 8 : 0) + 4), (uint32_t) prdCache.address.phys);

		// todo
		PreviousDevice = dev;
		PIO::SendCommandData(dev, Sector, (uint8_t) (Bytes / dev->GetSectorSize()));

		IOPort::WriteByte(dev->GetBaseIO() + 7, Sector > 0x0FFFFFFF ? ATA_ReadSectors48DMA : ATA_ReadSectors28DMA);
		IOPort::WriteByte((uint16_t)(mmio + (dev->GetBus() ? 8 : 0) + 0), DMA::DMACommandRead | DMA::DMACommandStart);

		_WaitingDMA14 = !dev->GetBus();
		_WaitingDMA15 = dev->GetBus();


		uint64_t no = Time::Now();
		while((_WaitingDMA14 || _WaitingDMA15) && Time::Now() < no + 1000);

		_WaitingDMA14 = false;
		_WaitingDMA15 = false;

		// stop
		IOPort::WriteByte((uint16_t) (mmio + (dev->GetBus() ? 8 : 0) + 0), DMA::DMACommandRead | DMA::DMACommandStop);


		// todo: release cache

		// copy over
		return IOResult(Bytes, paddr, (Bytes + 0xFFF) / 0x1000);
	}

























	IOResult WriteBytes(ATADrive* dev, uint64_t Buffer, uint64_t Sector, uint64_t Bytes)
	{
		// todo: something
		HALT("DMA WRITE NOT SUPPORTED");
		return IOResult();

		uint32_t mmio = (uint32_t) dev->ParentPCI->GetBAR(4);

		// get a prd
		bool found = false;
		PRDTableCache prdCache;
		for(PRDTableCache& cache : *cachedPRDTables)
		{
			if(!cache.used)
			{
				prdCache = cache;
				cache.used = true;
				found = 1;
				break;
			}
		}

		if(!found)
		{
			prdCache.address = Physical::AllocateDMA(1);
			prdCache.length = 0x1000;
			prdCache.used = 1;

			cachedPRDTables->push_back(prdCache);
		}

		PRDEntry* prd = (PRDEntry*) prdCache.address.virt;

		// allocate a buffer that we know is a good deal
		DMAAddr paddr = Physical::AllocateDMA((Bytes + 0xFFF) / 0x1000);

		prd->bufferPhysAddr = (uint32_t) paddr.phys;
		prd->byteCount = (uint16_t) Bytes;
		prd->lastEntry = 0x8000;

		// write the bytes of address into register
		IOPort::Write32((uint16_t)(mmio + (dev->GetBus() ? 8 : 0) + 4), (uint32_t) prdCache.address.phys);

		PreviousDevice = dev;
		PIO::SendCommandData(dev, Sector, (uint8_t)(Bytes / dev->GetSectorSize()));

		IOPort::WriteByte(dev->GetBaseIO() + 7, Sector > 0x0FFFFFFF ? ATA_WriteSectors48DMA : ATA_WriteSectors28DMA);
		IOPort::WriteByte((uint16_t)(mmio + (dev->GetBus() ? 8 : 0) + 0), DMA::DMACommandWrite | DMA::DMACommandStart);

		_WaitingDMA14 = !dev->GetBus();
		_WaitingDMA15 = dev->GetBus();

		uint64_t no = Time::Now();
		uint64_t t1 = 100000000;
		while((_WaitingDMA14 || _WaitingDMA15) && Time::Now() < no + 100 && t1 > 0)
			t1--;


		_WaitingDMA14 = false;
		_WaitingDMA15 = false;

		// stop
		IOPort::WriteByte((uint16_t)(mmio + (dev->GetBus() ? 8 : 0) + 0), DMA::DMACommandWrite | DMA::DMACommandStop);


		// copy over
		Memory::Copy((void*) Buffer, (void*) paddr.virt, Bytes);
		Physical::FreeDMA(paddr, (Bytes + 0xFFF) / 0x1000);
	}



	static void HandleIRQ()
	{
		if(!PreviousDevice)
			return;

		if(PreviousDevice->ParentPCI->IsBARIOPort(4))
		{
			IOPort::ReadByte((uint16_t)(PreviousDevice->ParentPCI->GetBAR(4) + (PreviousDevice->GetBus() ? 8 : 0) + 2));
		}
		else
		{
			// wtf are we supposed to do?
		}
	}

	void HandleIRQ14()
	{
		if(_WaitingDMA14)
		{
			_WaitingDMA14 = false;
			HandleIRQ();
		}
	}

	void HandleIRQ15()
	{
		if(_WaitingDMA15)
		{
			_WaitingDMA15 = false;
			HandleIRQ();
		}
	}
}
}
}
}
}
}







