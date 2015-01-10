// Virtual.cpp
// Copyright (c) 2013 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.


// Sets up the paging structures left by our bootstrap in (boot.s).

#include <Kernel.hpp>
#include <StandardIO.hpp>
#include <stddef.h>

using namespace Kernel;
using namespace Kernel::HardwareAbstraction::MemoryManager;
using namespace Kernel::HardwareAbstraction::MemoryManager::Virtual;

namespace Kernel {
namespace HardwareAbstraction {
namespace MemoryManager {
namespace Virtual
{
	static PageMapStructure* CurrentPML4T;
	static bool IsPaging;

	#define I_RECURSIVE_SLOT	510

	// Convert an address into array index of a structure
	// E.G. int index = I_PML4_INDEX(0xFFFFFFFFFFFFFFFF); // index = 511
	#define I_PML4_INDEX(addr)		((((uintptr_t)(addr)) >> 39) & 0x1FF)
	#define I_PDPT_INDEX(addr)		((((uintptr_t)(addr)) >> 30) & 0x1FF)
	#define I_PD_INDEX(addr)		((((uintptr_t)(addr)) >> 21) & 0x1FF)
	#define I_PT_INDEX(addr)		((((uintptr_t)(addr)) >> 12) & 0x1FF)


	void Initialise()
	{
		PageMapStructure* OriginalPml4 = (PageMapStructure*) GetKernelCR3();

		OriginalPml4->Entry[I_RECURSIVE_SLOT] = (uint64_t) OriginalPml4 | I_Present | I_ReadWrite;
		CurrentPML4T = OriginalPml4;
		IsPaging = true;
	}



	// this models (exactly) the FPL pair system of the physical allocator.
	uint64_t AllocateVirtual(uint64_t size, uint64_t addr, VirtualAddressSpace* v, uint64_t phys)
	{
		Multitasking::Process* proc = Multitasking::GetCurrentProcess();
		assert(proc);

		VirtualAddressSpace* vas = v ? v : &proc->VAS;
		assert(vas);

		if(vas->pairs.size() == 0)
		{
			Log(3, "Virtual address space exhausted, WTF are you doing?!");
			Multitasking::Kill(proc);
			UHALT();
			return 0;
		}

		// look for address first.
		if(addr > 0)
		{
			uint64_t end = addr + (size * 0x1000);
			AddressLengthPair* found = 0;
			for(auto pair : vas->pairs)
			{
				if(pair->start <= addr && pair->start + (pair->length * 0x1000) >= end)
				{
					found = pair;
					break;
				}
			}

			if(found)
			{
				if(found->start == addr && found->length == size)
				{
					vas->pairs.remove(found);
					delete found;
				}
				else if(found->start == addr)
				{
					AddressLengthPair* np = new AddressLengthPair(end, found->length - size);
					vas->pairs.remove(found);
					vas->pairs.push_back(np);
					delete found;
				}
				else
				{
					// make a new pair.
					AddressLengthPair* np = new AddressLengthPair(found->start, (addr - found->start) / 0x1000);
					vas->pairs.push_back(np);

					found->start = end;
					found->length = found->length - size;
				}

				vas->used.push_back(new ALPPair(addr, size, phys));
				return addr;
			}

			// else we just continue as normal.
			Log(1, "Couldn't satisfy request to allocate virtual address at %x, return address(0) = %x", addr, __builtin_return_address(0));
		}














		AddressLengthPair* pair = vas->pairs.front();
		if(pair->length > size)
		{
			uint64_t ret = pair->start;
			pair->start += (size * 0x1000);
			pair->length -= size;
			vas->used.push_back(new ALPPair(ret, size, phys));
			return ret;
		}
		else if(pair->length == size)
		{
			uint64_t ret = pair->start;
			delete vas->pairs.front();
			vas->pairs.erase(vas->pairs.begin());

			vas->used.push_back(new ALPPair(ret, size, phys));
			return ret;
		}
		else
		{
			// we need to use the next pair.
			AddressLengthPair* found = 0;
			for(AddressLengthPair* p : vas->pairs)
			{
				if(p->length >= size)
				{
					found = p;
					break;
				}
			}

			if(!found)
			{
				Log(3, "Virtual address space exhausted, WTF are you doing?! HUH?");
				Multitasking::Kill(proc);
				UHALT();
			}
			else
			{
				if(found->length > size)
				{
					uint64_t ret = found->start;
					found->length -= size;
					found->start += (size * 0x1000);

					vas->used.push_back(new ALPPair(ret, size, phys));
					return ret;
				}
				else
				{
					uint64_t ret = found->start;
					delete found;

					vas->used.push_back(new ALPPair(ret, size, phys));
					return ret;
				}
			}

			HALT("something went wrong");
			return 0;
		}
	}

	void FreeVirtual(uint64_t page, uint64_t size, VirtualAddressSpace* v)
	{
		Multitasking::Process* proc = Multitasking::GetCurrentProcess();
		assert(proc);

		VirtualAddressSpace* vas = v ? v : &proc->VAS;
		assert(vas);

		uint64_t end = page + (size * 0x1000);

		for(AddressLengthPair* pair : vas->pairs)
		{
			// 3 basic conditions
			// 1. we find a match below a pair's baseaddr
			// 2. we find a match above a pair's baseaddr
			// 3. we don't find a match

			if(end == pair->start)
			{
				pair->start = page;
				pair->length += size;
				return;
			}
			else if(pair->start + (pair->length * 0x1000) == page)
			{
				pair->length += (size * 0x1000);
				return;
			}
		}

		// if we reach here, we haven't found a match.
		vas->pairs.push_back(new AddressLengthPair(page, size));

		// TODO: remove matching 'used' pair
	}

	uint64_t GetVirtualPhysical(uint64_t virt, VirtualAddressSpace* v)
	{
		VirtualAddressSpace* vas = v ? v : &Multitasking::GetCurrentProcess()->VAS;
		assert(vas);

		for(ALPPair* pair : vas->used)
		{
			uint64_t end = pair->start + pair->length * 0x1000;
			if(pair->start >= virt && virt <= end)
			{
				return pair->phys + (virt - pair->start);
			}
		}

		return 0;
	}

	uint64_t AllocatePage(uint64_t size, uint64_t addr, uint64_t flags)
	{
		uint64_t phys = Physical::AllocatePage(size);
		uint64_t virt = AllocateVirtual(size, addr, 0, phys);

		MapRegion(virt, phys, size, flags);

		return virt;
	}

	void FreePage(uint64_t addr, uint64_t size)
	{
		// freepage should only be called with a return from allocatepage
		// we got to do some work here
		// look through the vas pairs, looking for one that fits our description.
		ALPPair* pair = 0;

		Multitasking::Process* proc = Multitasking::GetCurrentProcess();
		assert(proc);

		VirtualAddressSpace* vas = &proc->VAS;
		assert(vas);

		for(ALPPair* p : vas->used)
		{
			if(p->start == addr && p->length == size)
			{
				pair = p;
				break;
			}
		}

		assert(pair);

		uint64_t phys = pair->phys;
		assert(phys > 0);

		Physical::FreePage(phys, size);
		Virtual::FreeVirtual(addr, size);

		UnmapRegion(addr, size);
	}






	VirtualAddressSpace* SetupVAS(VirtualAddressSpace* vas)
	{
		// Max 48-bit virtual address space (current implementations)
		vas->pairs.push_back(new AddressLengthPair(0x01000000, 0xFF000));
		vas->pairs.push_back(new AddressLengthPair(0xFFFFF00000000000, 0x100000));

		return vas;
	}

	VirtualAddressSpace* CopyVAS(VirtualAddressSpace* src, VirtualAddressSpace* dest)
	{
		for(auto p : src->pairs)
			dest->pairs.push_back(new AddressLengthPair(*p));

		for(auto _pair : src->used)
		{
			ALPPair* pair = new ALPPair(*_pair);
			dest->used.push_back(pair);
			uint64_t p = Physical::AllocatePage(pair->length);

			Virtual::MapRegion(pair->start, /*pair->phys*/ p, pair->length, 0x07, dest->PML4);
			Virtual::MapRegion(TemporaryVirtualMapping, p, pair->length, 0x07);
			Memory::CopyOverlap((void*) TemporaryVirtualMapping, (void*) pair->start, pair->length * 0x1000);


			Virtual::UnmapRegion(TemporaryVirtualMapping, pair->length);
			pair->phys = p;




			#if 0
				Virtual::MapRegion(pair->start, pair->phys, pair->length, 0x07, dest->PML4);
				for(uint64_t i = 0; i < pair->length; i++)
				{
					Log("Marking %x as COW", pair->start + (i * 0x1000));
					Virtual::MarkCOW(pair->start + (i * 0x1000), dest);
				}
			#endif
		}

		return dest;
	}

	void DestroyVAS(VirtualAddressSpace* vas)
	{
		// free every phys page in vas->used
		// delete pair objects from both lists
		// delete both lists

		assert(vas);
		// assert(vas->used);
		// assert(vas->pairs);

		for(ALPPair* pair : vas->used)
		{
			if(pair->phys > 0)
				Physical::FreePage(pair->phys, pair->length);

			delete pair;
		}


		for(AddressLengthPair* pair : vas->pairs)
			delete pair;

		// delete vas->pairs;
		// delete vas->used;
		delete vas;
	}






















	void ChangeRawCR3(uint64_t newval)
	{
		asm volatile("mov %[nv], %%rax; mov %%rax, %%cr3" :: [nv]"g"(newval) : "memory", "rax");
	}

	uint64_t GetRawCR3()
	{
		uint64_t ret = 0;
		asm volatile("mov %%cr3, %%rax; mov %%rax, %[r]" : [r]"=g"(ret) :: "memory", "rax");

		return ret;
	}

	void SwitchPML4T(PageMapStructure* PML4T)
	{
		CurrentPML4T = PML4T;
	}

	PageMapStructure* GetCurrentPML4T()
	{
		return CurrentPML4T;
	}

	void invlpg(PageMapStructure* p)
	{
		asm volatile("invlpg (%0)" : : "a" ((uint64_t) p));
	}




	void MapAddress(uint64_t VirtAddr, uint64_t PhysAddr, uint64_t Flags)
	{
		MapAddress(VirtAddr, PhysAddr, Flags, GetCurrentPML4T(), false);
	}

	void MapAddress(uint64_t VirtAddr, uint64_t PhysAddr, uint64_t Flags, PageMapStructure* PML4)
	{
		MapAddress(VirtAddr, PhysAddr, Flags, PML4, false);
	}

	void MapAddress(uint64_t VirtAddr, uint64_t PhysAddr, uint64_t Flags, bool DoNotUnmap)
	{
		MapAddress(VirtAddr, PhysAddr, Flags, GetCurrentPML4T(), DoNotUnmap);
	}



	void UnmapAddress(uint64_t VirtAddr)
	{
		UnmapAddress(VirtAddr, GetCurrentPML4T(), false);
	}

	void UnmapAddress(uint64_t VirtAddr, PageMapStructure* PML4)
	{
		UnmapAddress(VirtAddr, PML4, false);
	}

	void UnmapAddress(uint64_t VirtAddr, bool DoNotUnmap)
	{
		UnmapAddress(VirtAddr, GetCurrentPML4T(), DoNotUnmap);
	}










	void MapAddress(uint64_t VirtAddr, uint64_t PhysAddr, uint64_t Flags, PageMapStructure* PML4, bool DoNotUnmap)
	{
		uint64_t PageTableIndex					= I_PT_INDEX(VirtAddr);
		uint64_t PageDirectoryIndex				= I_PD_INDEX(VirtAddr);
		uint64_t PageDirectoryPointerTableIndex	= I_PDPT_INDEX(VirtAddr);
		uint64_t PML4TIndex						= I_PML4_INDEX(VirtAddr);

		assert(PageTableIndex < 512);
		assert(PageDirectoryIndex < 512);
		assert(PageDirectoryPointerTableIndex < 512);
		assert(PML4TIndex < 512);

		if(PML4TIndex == I_RECURSIVE_SLOT)
		{
			// We can't map 510, we need that for our recursive mapping.
			HALT("Tried to map to PML4[510]! (RESTRICTED, KERNEL USE)");
		}


		(void) DoNotUnmap;


		PageMapStructure* PML = (PML4 == 0 ? GetCurrentPML4T() : PML4);
		bool other = (PML != GetCurrentPML4T());

		assert(PML);

		if(other)
			Virtual::MapAddress((uint64_t) PML, (uint64_t) PML, 0x7);



		if(!(PML->Entry[PML4TIndex] & I_Present))
		{
			PML->Entry[PML4TIndex] = Physical::AllocateFromReserved() | (Flags | 0x1);
			invlpg(PML);
		}
		PageMapStructure* PDPT = (PageMapStructure*) (PML->Entry[PML4TIndex] & I_AlignMask);
		if(other)
			Virtual::MapAddress((uint64_t) PDPT, (uint64_t) PDPT, 0x7);



		if(!(PDPT->Entry[PageDirectoryPointerTableIndex] & I_Present))
		{
			PDPT->Entry[PageDirectoryPointerTableIndex] = Physical::AllocateFromReserved() | (Flags | 0x1);
			invlpg(PML);
			invlpg(PDPT);
		}
		PageMapStructure* PageDirectory = (PageMapStructure*) (PDPT->Entry[PageDirectoryPointerTableIndex] & I_AlignMask);
		if(other)
			Virtual::MapAddress((uint64_t) PageDirectory, (uint64_t) PageDirectory, 0x7);



		if(!(PageDirectory->Entry[PageDirectoryIndex] & I_Present))
		{
			PageDirectory->Entry[PageDirectoryIndex] = Physical::AllocateFromReserved() | (Flags | 0x1);
			invlpg(PML);
			invlpg(PDPT);
			invlpg(PageDirectory);
		}
		PageMapStructure* PageTable = (PageMapStructure*) (PageDirectory->Entry[PageDirectoryIndex] & I_AlignMask);
		if(other)
			Virtual::MapAddress((uint64_t) PageTable, (uint64_t) PageTable, 0x7);


		PageTable->Entry[PageTableIndex] = PhysAddr | Flags;

		if(other)
		{
			Virtual::UnmapAddress((uint64_t) PageTable);
			Virtual::UnmapAddress((uint64_t) PageDirectory);
			Virtual::UnmapAddress((uint64_t) PDPT);
			Virtual::UnmapAddress((uint64_t) PML);
		}
	}





	void UnmapAddress(uint64_t VirtAddr, PageMapStructure* PML4, bool DoNotUnmap)
	{
		bool DidMapPML4 = false;

		if(PML4 == 0)
			PML4 = GetCurrentPML4T();

		else if(PML4 != GetCurrentPML4T())
		{
			DidMapPML4 = true;
			MapAddress((uint64_t) PML4, (uint64_t) PML4, 0x03);
		}

		uint64_t PageTableIndex					= I_PT_INDEX(VirtAddr);
		uint64_t PageDirectoryIndex				= I_PD_INDEX(VirtAddr);
		uint64_t PageDirectoryPointerTableIndex	= I_PDPT_INDEX(VirtAddr);
		uint64_t PML4TIndex						= I_PML4_INDEX(VirtAddr);


		PageMapStructure* PML = (PageMapStructure*)(((PageMapStructure*) PML4)->Entry[I_RECURSIVE_SLOT] & I_AlignMask);

		if(PML)
		{
			PageMapStructure* PDPT = (PageMapStructure*)(PML->Entry[PML4TIndex] & I_AlignMask);

			if(PDPT)
			{
				PageMapStructure* PageDirectory = (PageMapStructure*)(PDPT->Entry[PageDirectoryPointerTableIndex] & I_AlignMask);

				if(PageDirectory)
				{
					PageMapStructure* PageTable = (PageMapStructure*)(PageDirectory->Entry[PageDirectoryIndex] & I_AlignMask);

					PageTable->Entry[PageTableIndex] = 0;
					invlpg((PageMapStructure*) VirtAddr);
				}
			}
		}


		if(DidMapPML4 && !DoNotUnmap)
		{
			DidMapPML4 = false;
			if((uint64_t) PML4 != GetKernelCR3()){ UnmapAddress((uint64_t) PML4); }
		}
	}



	void MapRegion(uint64_t VirtAddr, uint64_t PhysAddr, uint64_t LengthInPages, uint64_t Flags, PageMapStructure* PML4)
	{
		for(uint64_t i = 0; i < LengthInPages; i++)
			MapAddress(VirtAddr + (i * 0x1000), PhysAddr + (i * 0x1000), Flags, PML4);
	}

	void UnmapRegion(uint64_t VirtAddr, uint64_t LengthInPages, PageMapStructure* PML4)
	{
		for(uint64_t i = 0; i < LengthInPages; i++)
			UnmapAddress(VirtAddr + (i * 0x1000), PML4);
	}


	static uint64_t* GetPageTableEntry(uint64_t VirtAddr, PageMapStructure* VAS, PageMapStructure** pdpt, PageMapStructure** pd, PageMapStructure** pt)
	{

		uint64_t PageTableIndex					= I_PT_INDEX(VirtAddr);
		uint64_t PageDirectoryIndex				= I_PD_INDEX(VirtAddr);
		uint64_t PageDirectoryPointerTableIndex	= I_PDPT_INDEX(VirtAddr);
		uint64_t PML4TIndex						= I_PML4_INDEX(VirtAddr);



		// Log("virt: %x, indices: pt %d, pd %d, pdpt %d, pml4 %d", VirtAddr, PageTableIndex, PageDirectoryIndex, PageDirectoryPointerTableIndex, PML4TIndex);


		PageMapStructure* PML = (VAS == 0 ? GetCurrentPML4T() : VAS);
		bool other = (PML != GetCurrentPML4T());

		assert(PML);
		{
			if(other)
				Virtual::MapAddress((uint64_t) PML, (uint64_t) PML, 0x7);

			PageMapStructure* PDPT = (PageMapStructure*) (PML->Entry[PML4TIndex] & I_AlignMask);

			assert(PDPT);
			{
				if(other)
					Virtual::MapAddress((uint64_t) PDPT, (uint64_t) PDPT, 0x7);

				PageMapStructure* PageDirectory = (PageMapStructure*) (PDPT->Entry[PageDirectoryPointerTableIndex] & I_AlignMask);

				assert(PageDirectory);
				{
					if(other)
						Virtual::MapAddress((uint64_t) PageDirectory, (uint64_t) PageDirectory, 0x7);

					PageMapStructure* PageTable = (PageMapStructure*) (PageDirectory->Entry[PageDirectoryIndex] & I_AlignMask);

					assert(PageTable);
					{
						if(other)
							Virtual::MapAddress((uint64_t) PageTable, (uint64_t) PageTable, 0x7);

						*pdpt = PDPT;
						*pd = PageDirectory;
						*pt = PageTable;

						uint64_t* ret = &PageTable->Entry[PageTableIndex];

						// if(other)
						// {
						// 	Virtual::UnmapAddress((uint64_t) PageDirectory);
						// 	Virtual::UnmapAddress((uint64_t) PDPT);
						// 	Virtual::UnmapAddress((uint64_t) PML);
						// }

						return ret;
					}
				}
			}
		}
					// else
					// {
					// 	if(other)
					// 	{
					// 		Virtual::UnmapAddress((uint64_t) PageDirectory);
					// 		Virtual::UnmapAddress((uint64_t) PDPT);
					// 		Virtual::UnmapAddress((uint64_t) PML);
					// 	}
					// 	return 0;
					// }
		// 		}
		// 		else
		// 		{
		// 			if(other)
		// 			{
		// 				Virtual::UnmapAddress((uint64_t) PDPT);
		// 				Virtual::UnmapAddress((uint64_t) PML);
		// 			}
		// 			return 0;
		// 		}
		// 	}
		// 	else
		// 	{
		// 		if(other)
		// 		{
		// 			Virtual::UnmapAddress((uint64_t) PML);
		// 		}
		// 		return 0;
		// 	}
		// }
		// else
		// {
		// 	return 0;
		// }
	}


	uint64_t GetMapping(uint64_t VirtAddr, PageMapStructure* VAS)
	{
		PageMapStructure* pdpt = 0;
		PageMapStructure* pd = 0;
		PageMapStructure* pt = 0;

		uint64_t ret = *GetPageTableEntry(VirtAddr, VAS, &pdpt, &pd, &pt);

		if(VAS != GetCurrentPML4T())
		{
			UnmapAddress((uintptr_t) pdpt);
			UnmapAddress((uintptr_t) pd);
			UnmapAddress((uintptr_t) pt);
		}

		return ret;
	}


	static void ChangeCOWFlag(uint64_t virt, PageMapStructure* pml, bool cow)
	{
		PageMapStructure* pdpt = 0;
		PageMapStructure* pd = 0;
		PageMapStructure* pt = 0;


		uint64_t* pg = GetPageTableEntry(virt, pml, &pdpt, &pd, &pt);

		uint64_t* pdptv = (uint64_t*) pdpt;
		uint64_t* pdv = (uint64_t*) pd;
		uint64_t* ptv = (uint64_t*) pt;

		// Log("virt: %x, pdpt: %x, pd: %x, pt: %x, pg: %x", virt, pdptv, pdv, ptv, pg);
		// UHALT();

		if(cow)
		{
			*pdptv |= I_CopyOnWrite;
			*pdptv &= ~I_ReadWrite;

			*pdv |= I_CopyOnWrite;
			*pdv &= ~I_ReadWrite;

			*ptv |= I_CopyOnWrite;
			*ptv &= ~I_ReadWrite;

			*pg |= I_CopyOnWrite;
			*pg &= ~I_ReadWrite;
		}
		else
		{
			*pdptv &= ~I_CopyOnWrite;
			*pdptv |= I_ReadWrite;

			*pdv &= ~I_CopyOnWrite;
			*pdv |= I_ReadWrite;

			*ptv &= ~I_CopyOnWrite;
			*ptv |= I_ReadWrite;

			*pg &= ~I_CopyOnWrite;
			*pg |= I_ReadWrite;
		}


		if(pml != GetCurrentPML4T())
		{
			UnmapAddress((uintptr_t) pdpt);
			UnmapAddress((uintptr_t) pd);
			UnmapAddress((uintptr_t) pt);
		}
	}

	void MarkCOW(uint64_t virt, PageMapStructure* pml)
	{
		ChangeCOWFlag(virt, pml, 1);
	}

	void UnmarkCOW(uint64_t virt, PageMapStructure* pml)
	{
		ChangeCOWFlag(virt, pml, 0);
	}

	void MarkCOW(uint64_t virt, VirtualAddressSpace* vas)
	{
		MarkCOW(virt, vas ? vas->PML4 : GetCurrentPML4T());
	}

	void UnmarkCOW(uint64_t virt, VirtualAddressSpace* vas)
	{
		MarkCOW(virt, vas ? vas->PML4 : GetCurrentPML4T());
	}


	bool HandlePageFault(uint64_t cr2, uint64_t cr3, uint64_t errorcode)
	{
		(void) cr3;
		(void) errorcode;

		PageMapStructure* pdpt = 0;
		PageMapStructure* pd = 0;
		PageMapStructure* pt = 0;

		// HALT("PF");

		// check if cow.
		uint64_t* value = GetPageTableEntry(cr2, Multitasking::GetCurrentProcess()->VAS.PML4, &pdpt, &pd, &pt);

		// conditions for cow:
		// bit 11 (0x800) for COW set, bit 1 (0x2, R/W bit) not set.
		if(value && (*value & I_CopyOnWrite) && !(*value & I_ReadWrite))
		{
			// allocate a page, copy existing data, then return.
			uint64_t np = Physical::AllocatePage();
			uint64_t old = *value & I_AlignMask;
			uint64_t oldflags = *value & ~I_AlignMask;

			*value = np | (oldflags | I_ReadWrite | I_CopyOnWrite);

			// we need to copy the old bytes to the new page.
			// _old_ should contain the virtual address of the parent
			Virtual::MapAddress(cr2 & I_AlignMask, np, 0x07);
			Virtual::MapAddress(TemporaryVirtualMapping, old, 0x07);

			Log(3, "Copying 0x1000 bytes from phys %x to %x (error: %x)", old, cr2 & I_AlignMask, errorcode);
			Memory::CopyOverlap((void*) (cr2 & I_AlignMask) , (void*) TemporaryVirtualMapping, 0x1000);
			Virtual::UnmapAddress(TemporaryVirtualMapping);

			Log("Successfully allocated new physical page at %x for COW purposes mapped to virtual page %x", np, cr2 & I_AlignMask);
			return true;
		}

		Log("Invalid access (%x:%x)", value, value ? *value : 0);
		return false;
	}






	uint64_t CreateVAS()
	{
		/*
			256 TB
			512 GB
			1 GB
			2 MB

			bottom 1gb is kernel owned.
			top 1024gb is also kernel owned.
		*/

		PageMapStructure* _kernelpml4 = (PageMapStructure*) Kernel::GetKernelCR3();
		Virtual::MapAddress(TemporaryVirtualMapping, (uint64_t) _kernelpml4, 0x07);
		PageMapStructure* kernelpml4 = (PageMapStructure*) TemporaryVirtualMapping;

		PageMapStructure* _pdpt = (PageMapStructure*) kernelpml4->Entry[0];

		// bottom 1gb
		Virtual::MapAddress(TemporaryVirtualMapping + 0x1000, (uint64_t) _pdpt, 0x07);
		PageMapStructure* pdpt = (PageMapStructure*) (TemporaryVirtualMapping + 0x1000);
		PageMapStructure* pt = (PageMapStructure*) pdpt->Entry[0];

		// we need to throw this address (aka pointer) into the created things.
		PageMapStructure* PML4 = (PageMapStructure*) Physical::AllocateFromReserved();
		Memory::Set(PML4, 0, 0x1000);

		Virtual::MapAddress((uint64_t) PML4, (uint64_t) PML4, 0x03);
		PML4->Entry[I_RECURSIVE_SLOT] = (uint64_t) PML4 | I_Present | I_ReadWrite;

		// do it here.
		{
			if(!(PML4->Entry[0] & I_Present))
				PML4->Entry[0] = Physical::AllocateFromReserved() | I_Present | I_ReadWrite | I_UserAccess;

			else
				Log(3, "what: %x", PML4->Entry[0]);

			((PageMapStructure*) (PML4->Entry[0]))->Entry[0] = (uint64_t) pt;
			// that just gives us 1gb lower, now we need 512gb upper.

			PML4->Entry[511] = (uint64_t) kernelpml4->Entry[511] | 0x6;
		}

		Virtual::MapAddress((uint64_t) PML4, (uint64_t) PML4, 0x07, PML4);

		// Map 16 MB, includes the kernel.
		Virtual::MapAddress(0x1000, 0x1000, 0x03, PML4);

		// map 0x2000 as supervisor only, since we store sensitive stuff there
		// namely TSS stuff.
		Virtual::MapRegion(0x2000, 0x2000, 0x2, 0x03, PML4);

		// todo: make it more robust, right now it's always 16mb identity mapped.
		for(uint64_t i = 0x4000; i < 0x01000000; i += 0x1000)
		{
			Virtual::MapAddress(i, i, 0x07, PML4);
			// MarkCOW(i, PML4);
		}

		// Map the LFB.
		for(uint64_t i = 0; i < Kernel::GetLFBLengthInPages(); i++)
			Virtual::MapAddress(Kernel::GetFramebufferAddress() + (i * 0x1000), Kernel::GetFramebufferAddress() + (i * 0x1000), 0x07, PML4);


		Virtual::UnmapAddress(TemporaryVirtualMapping);
		Virtual::UnmapAddress(TemporaryVirtualMapping + 0x1000);
		return (uint64_t) PML4;
	}
}
}
}
}






















