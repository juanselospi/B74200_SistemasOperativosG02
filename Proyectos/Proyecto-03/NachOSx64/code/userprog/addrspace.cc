// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#include "synch.h"
#include "bitmap.h"

#ifdef VM
#include "coremap.h"
#include "swap.h"
#include "lru.h"
#endif

#include <string.h>


#define min(a,b) ((a) < (b) ? (a) : (b))

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void 
SwapHeader (NoffHeader *noffH)
{
    noffH->noffMagic = WordToHost(noffH->noffMagic);
    noffH->code.size = WordToHost(noffH->code.size);
    noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
    noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
    noffH->initData.size = WordToHost(noffH->initData.size);
    noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
    noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
    noffH->uninitData.size = WordToHost(noffH->uninitData.size);
    noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
    noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

#ifndef VM
static unsigned int
AddrToPhys(TranslationEntry *pageTable, unsigned int virtAddr)
{
    unsigned int vpn = virtAddr / PageSize;
    unsigned int offset = virtAddr % PageSize;
    return pageTable[vpn].physicalPage * PageSize + offset;
}
#endif


void
AddrSpace::ReserveTestPhysPages()
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    physPageLock->Acquire();

    for(int p = 0; p <= 10; p += 2) {

        freePhysPages->Mark(p);
        frameRefCount[p] = 1;
    }

    physPageLock->Release();
    interrupt->SetLevel(oldLevel);
}


void
AddrSpace::ReleaseTestPhysPages()
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    physPageLock->Acquire();

    for(int p = 0; p <= 10; p += 2) {

        if(frameRefCount[p] <= 1) {

            freePhysPages->Clear(p);
            frameRefCount[p] = 0;
        }
    }

    physPageLock->Release();
    interrupt->SetLevel(oldLevel);
}


int
AddrSpace::AllocatePhysicalPage()
{
    physPageLock->Acquire();
    int physPage = freePhysPages->Find();

    if(physPage != -1) {

        frameRefCount[physPage]++;
        bzero(&(machine->mainMemory[physPage * PageSize]), PageSize);
    }

    physPageLock->Release();
    return physPage;
}

void
AddrSpace::DeallocatePhysicalPage(int physPage)
{
    if(physPage < 0 || physPage >= NumPhysPages) {

        return;
    }

    physPageLock->Acquire();
    frameRefCount[physPage]--;

    if(frameRefCount[physPage] <= 0) {

        frameRefCount[physPage] = 0;
        freePhysPages->Clear(physPage);
#ifdef VM
        CoreMapClear(physPage);
#endif
    }

    physPageLock->Release();
}


void
AddrSpace::AllocatePageTable(unsigned int nPages)
{
    numPages = nPages;
    numStackPages = divRoundUp(UserStackSize, PageSize);
    stackVirtualTop = numPages * PageSize - 16;

    pageTable = new TranslationEntry[numPages];

    for(unsigned int i = 0; i < numPages; i++) {

#ifdef VM
        pageTable[i].virtualPage = i;
        pageTable[i].physicalPage = -1;
        pageTable[i].valid = false;
        pageTable[i].use = false;
        pageTable[i].dirty = false;
        pageTable[i].readOnly = false;
        swapLocations[i] = -1;
#else
        int physPage = AllocatePhysicalPage();
        ASSERT(physPage != -1);

        pageTable[i].virtualPage = i;
        pageTable[i].physicalPage = physPage;
        pageTable[i].valid = true;
        pageTable[i].use = false;
        pageTable[i].dirty = false;
        pageTable[i].readOnly = false;
#endif
    }
}


void
AddrSpace::LoadSegment(OpenFile * executable, int segmentSize, unsigned int virtualAddr, int inFileAddr)
{
#ifndef VM
    char * pageBuffer = new char[PageSize];
    int remaining = segmentSize;
    int fileOffset = inFileAddr;
    unsigned int virtAddr = virtualAddr;

    while(remaining > 0) {

        int bytesThisPage = (remaining > PageSize) ? PageSize : remaining;
        int bytesRead = executable->ReadAt(pageBuffer, bytesThisPage, fileOffset);

        unsigned int physAddr = AddrToPhys(pageTable, virtAddr);
        memcpy(&(machine->mainMemory[physAddr]), pageBuffer, bytesRead);

        if(bytesRead < bytesThisPage) {

            bzero(&(machine->mainMemory[physAddr + bytesRead]), bytesThisPage - bytesRead);
        }

        remaining -= bytesThisPage;
        fileOffset += bytesThisPage;
        virtAddr += bytesThisPage;
    }

    delete[] pageBuffer;
#endif
}


//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable) {

    NoffHeader hdr;
    unsigned int size;

    executable->ReadAt((char *)&hdr, sizeof(hdr), 0);

    if((hdr.noffMagic != NOFFMAGIC) && (WordToHost(hdr.noffMagic) == NOFFMAGIC)) {

        SwapHeader(&hdr);
    }

    ASSERT(hdr.noffMagic == NOFFMAGIC);

    size = hdr.code.size + hdr.initData.size + hdr.uninitData.size + UserStackSize;
    unsigned int nPages = divRoundUp(size, PageSize);

#ifndef VM
    ASSERT(nPages <= NumPhysPages);
#endif

    DEBUG('a', "Initializing address space, num pages %d, size %d\n", nPages, nPages * PageSize);

    threadCount = 1;
    parentSpace = NULL;

#ifdef VM
    noffH = hdr;
    execFile = executable;
    swapLocations = new int[nPages];
#endif

    AllocatePageTable(nPages);

#ifndef VM
    if(noffH.code.size > 0) {

        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n", noffH.code.virtualAddr, noffH.code.size);
        LoadSegment(executable, noffH.code.size, noffH.code.virtualAddr, noffH.code.inFileAddr);
    }

    if(noffH.initData.size > 0) {

        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n", noffH.initData.virtualAddr, noffH.initData.size);
        LoadSegment(executable, noffH.initData.size, noffH.initData.virtualAddr, noffH.initData.inFileAddr);
    }
#endif
}


AddrSpace::AddrSpace(AddrSpace *parent) {

    numPages = parent->numPages;
    numStackPages = parent->numStackPages;
    stackVirtualTop = parent->stackVirtualTop;
    threadCount = 1;
    parentSpace = parent;
    parent->AddThread();

    unsigned int firstStackVpn = numPages - numStackPages;

#ifdef VM
    noffH = parent->noffH;
    execFile = parent->execFile;
    swapLocations = new int[numPages];
#endif

    pageTable = new TranslationEntry[numPages];

    for(unsigned int i = 0; i < numPages; i++) {

        pageTable[i].virtualPage = i;
        pageTable[i].use = false;
        pageTable[i].dirty = false;
        pageTable[i].readOnly = parent->pageTable[i].readOnly;

#ifdef VM
        swapLocations[i] = parent->swapLocations[i];

        if(i < firstStackVpn) {

            pageTable[i].valid = parent->pageTable[i].valid;
            pageTable[i].physicalPage = parent->pageTable[i].physicalPage;

            if(parent->pageTable[i].valid) {

                physPageLock->Acquire();
                frameRefCount[pageTable[i].physicalPage]++;
                physPageLock->Release();
            }

        } else {

            pageTable[i].valid = false;
            pageTable[i].physicalPage = -1;
        }
#else
        pageTable[i].valid = true;

        if(i < firstStackVpn) {

            pageTable[i].physicalPage = parent->pageTable[i].physicalPage;
            physPageLock->Acquire();
            frameRefCount[pageTable[i].physicalPage]++;
            physPageLock->Release();

        } else {

            int physPage = AllocatePhysicalPage();
            ASSERT(physPage != -1);
            pageTable[i].physicalPage = physPage;
        }
#endif
    }
}


//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
    if(parentSpace != NULL) {

        parentSpace->RemoveThread();
    }

    for(unsigned int i = 0; i < numPages; i++) {

#ifdef VM
        if(pageTable[i].valid) {

            DeallocatePhysicalPage(pageTable[i].physicalPage);
        }

        if(swapLocations[i] >= 0) {

            swapManager->FreeSlot(swapLocations[i]);
        }
#else
        DeallocatePhysicalPage(pageTable[i].physicalPage);
#endif
    } 

    delete[] pageTable;

#ifdef VM
    delete[] swapLocations;
    if(parentSpace == NULL) {
        delete execFile;
    }
#endif
}


//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()
{
    int i;

    for(i = 0; i < NumTotalRegs; i++) {

        machine->WriteRegister(i, 0);
    }

    machine->WriteRegister(PCReg, 0);
    machine->WriteRegister(NextPCReg, 4);
    machine->WriteRegister(StackReg, stackVirtualTop);
    DEBUG('a', "Initializing stack register to %d\n", stackVirtualTop);
}


void AddrSpace::InitRegistersForFork(int funcAddr) {
    
    int i;

    for(i = 0; i < NumTotalRegs; i++) {

        machine->WriteRegister(i, 0);
    }

    machine->WriteRegister(PCReg, funcAddr);
    machine->WriteRegister(NextPCReg, funcAddr + 4);
    machine->WriteRegister(RetAddrReg, 4);
    machine->WriteRegister(StackReg, stackVirtualTop);
}


//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState() 
{
#ifdef VM
    // Sincronizar bits use/dirty de TLB a page table e invalidar TLB
    SyncTLBToPageTable();
    InvalidateTLB();
#endif
}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() {

#ifdef VM
    machine->pageTable = NULL;
    machine->pageTableSize = 0;
    InvalidateTLB();
#else
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
#endif
}

#ifdef VM

void
AddrSpace::SyncTLBToPageTable()
{
    for(int i = 0; i < TLBSize; i++) {

        if(!machine->tlb[i].valid) {
            continue;
        }

        int vpn = machine->tlb[i].virtualPage;

        if(vpn >= 0 && (unsigned)vpn < numPages && pageTable[vpn].valid) {

            pageTable[vpn].use = pageTable[vpn].use || machine->tlb[i].use;
            pageTable[vpn].dirty = pageTable[vpn].dirty || machine->tlb[i].dirty;
        }
    }
}

void
AddrSpace::SyncTLBToFrames()
{
    for(int i = 0; i < TLBSize; i++) {

        if(!machine->tlb[i].valid) {
            continue;
        }

        int vpn = machine->tlb[i].virtualPage;

        if(vpn >= 0 && (unsigned)vpn < numPages && pageTable[vpn].valid) {

            pageTable[vpn].use = pageTable[vpn].use || machine->tlb[i].use;
            pageTable[vpn].dirty = pageTable[vpn].dirty || machine->tlb[i].dirty;

            if(machine->tlb[i].use) {
                frameLRU->Touch(pageTable[vpn].physicalPage);
            }
        }
    }
}

void
AddrSpace::InvalidateTLB()
{
    for(int i = 0; i < TLBSize; i++) {
        machine->tlb[i].valid = false;
    }
}

void
AddrSpace::InvalidateTLBEntry(int vpn)
{
    for(int i = 0; i < TLBSize; i++) {

        if(machine->tlb[i].valid && machine->tlb[i].virtualPage == vpn) {

            if(vpn >= 0 && (unsigned)vpn < numPages && pageTable[vpn].valid) {

                pageTable[vpn].use = pageTable[vpn].use || machine->tlb[i].use;
                pageTable[vpn].dirty = pageTable[vpn].dirty || machine->tlb[i].dirty;
            }
            machine->tlb[i].valid = false;
        }
    }
}

int
AddrSpace::FindTLBEntry(int vpn)
{
    for(int i = 0; i < TLBSize; i++) {

        if(machine->tlb[i].valid && machine->tlb[i].virtualPage == vpn) {
            return i;
        }
    }
    return -1;
}

int
AddrSpace::AllocateTLBSlot()
{
    for(int i = 0; i < TLBSize; i++) {

        if(!machine->tlb[i].valid) {
            return i;
        }
    }

    int victim = tlbLRU->Oldest();
    if(victim < 0) {
        victim = 0;
    }

    if(machine->tlb[victim].valid) {

        int vpn = machine->tlb[victim].virtualPage;

        if(vpn >= 0 && (unsigned)vpn < numPages && pageTable[vpn].valid) {

            pageTable[vpn].use = machine->tlb[victim].use;
            pageTable[vpn].dirty = pageTable[vpn].dirty || machine->tlb[victim].dirty;
        }
    }

    return victim;
}

void
AddrSpace::UpdateTLB(int vpn)
{
    int slot = FindTLBEntry(vpn);

    if(slot < 0) {
        slot = AllocateTLBSlot();
    }

    machine->tlb[slot] = pageTable[vpn];
    machine->tlb[slot].virtualPage = vpn;
    machine->tlb[slot].valid = true;
    tlbLRU->Touch(slot);
}

bool
AddrSpace::IsCodeOnlyPage(unsigned int vpn)
{
    unsigned vAddr = vpn * PageSize;
    unsigned vEnd = vAddr + PageSize;

    if(noffH.code.size <= 0) {
        return false;
    }

    unsigned codeStart = (unsigned)noffH.code.virtualAddr;
    unsigned codeEnd = codeStart + (unsigned)noffH.code.size;

    if(vAddr < codeStart || vAddr >= codeEnd) {
        return false;
    }

    if(vEnd > codeEnd) {
        return false;
    }

    if(noffH.initData.size > 0) {

        unsigned dataStart = (unsigned)noffH.initData.virtualAddr;
        unsigned dataEnd = dataStart + (unsigned)noffH.initData.size;

        if(vEnd > dataStart && vAddr < dataEnd) {
            return false;
        }
    }

    return true;
}

void
AddrSpace::LoadPageSegment(char *mem, unsigned int vAddr, unsigned int segVA,
                           int segSize, int inFileAddr)
{
    unsigned segEnd = segVA + (unsigned)segSize;
    unsigned pageEnd = vAddr + PageSize;

    if(vAddr >= segEnd || pageEnd <= segVA) {
        return;
    }

    unsigned copyStart = (vAddr > segVA) ? vAddr : segVA;
    unsigned copyEnd = (pageEnd < segEnd) ? pageEnd : segEnd;
    int copySize = (int)(copyEnd - copyStart);

    if(copySize <= 0) {
        return;
    }

    int pageOffset = (int)(copyStart - vAddr);
    int fileOffset = inFileAddr + (int)(copyStart - segVA);

    execFile->ReadAt(mem + pageOffset, copySize, fileOffset);
}

void
AddrSpace::LoadPageContent(int vpn, int physPage)
{
    char *mem = &(machine->mainMemory[physPage * PageSize]);
    unsigned vAddr = vpn * PageSize;

    bzero(mem, PageSize);

    if(swapLocations[vpn] >= 0) {

        swapManager->ReadPage(swapLocations[vpn], mem);
        return;
    }

    if(noffH.code.size > 0) {

        LoadPageSegment(mem, vAddr, (unsigned)noffH.code.virtualAddr,
                        noffH.code.size, noffH.code.inFileAddr);
    }

    if(noffH.initData.size > 0) {

        LoadPageSegment(mem, vAddr, (unsigned)noffH.initData.virtualAddr,
                        noffH.initData.size, noffH.initData.inFileAddr);
    }
}

int
AddrSpace::EvictFrame()
{
    int bestFrame = -1;
    int oldestTime = -1;

    if(currentThread->space != NULL) {
        currentThread->space->SyncTLBToFrames();
    }

    AddrSpace *curSpace = currentThread->space;
    int pcVpn = (curSpace != NULL) ? ((unsigned)machine->ReadRegister(PCReg) / PageSize) : -1;

    physPageLock->Acquire();

    for(int f = 0; f < NumPhysPages; f++) {

        if(!coreMap[f].inUse) {
            continue;
        }

        if(frameRefCount[f] > 1) {
            continue;
        }

        CoreMapEntry *entry = CoreMapLookup(f);
        AddrSpace *owner = entry->space;
        int ownerVpn = entry->vpn;

        if(owner == NULL || ownerVpn < 0) {
            continue;
        }

        if(owner == curSpace && ownerVpn == pcVpn) {
            continue;   // Protege la pagina de codigo de la instruccion actual
        }

        TranslationEntry *victimEntry = &owner->pageTable[ownerVpn];

        if(!victimEntry->valid) {
            continue;
        }

        int usedTime = frameLRU->GetLastUsed(f);

        if(usedTime <= 0) {
            continue;
        }

        if(bestFrame == -1 || usedTime < oldestTime) {
            oldestTime = usedTime;
            bestFrame = f;
        }
    }

    if(bestFrame == -1) {

        physPageLock->Release();
        return -1;
    }

    CoreMapEntry *entry = CoreMapLookup(bestFrame);
    AddrSpace *owner = entry->space;
    int ownerVpn = entry->vpn;
    TranslationEntry *victimEntry = &owner->pageTable[ownerVpn];
    char *mem = &(machine->mainMemory[bestFrame * PageSize]);

    if(victimEntry->dirty && !victimEntry->readOnly) {

        if(owner->swapLocations[ownerVpn] < 0) {
            owner->swapLocations[ownerVpn] = swapManager->AllocateSlot();
            ASSERT(owner->swapLocations[ownerVpn] >= 0);
        }
        swapManager->WritePage(owner->swapLocations[ownerVpn], mem);
    }

    victimEntry->valid = false;
    victimEntry->physicalPage = -1;
    owner->InvalidateTLBEntry(ownerVpn);
    CoreMapClear(bestFrame);
    frameRefCount[bestFrame] = 0;
    freePhysPages->Clear(bestFrame);
    frameLRU->Clear(bestFrame);

    physPageLock->Release();
    return bestFrame;
}

void
AddrSpace::LoadPage(int vpn)
{
    ASSERT(!pageTable[vpn].valid);

    int physPage = AllocatePhysicalPage();

    if(physPage == -1) {
        physPage = EvictFrame();
        ASSERT(physPage >= 0);
        frameRefCount[physPage] = 1;
        freePhysPages->Mark(physPage);
    }

    LoadPageContent(vpn, physPage);

    pageTable[vpn].physicalPage = physPage;
    pageTable[vpn].valid = true;
    pageTable[vpn].use = true;
    pageTable[vpn].dirty = false;
    pageTable[vpn].readOnly = IsCodeOnlyPage(vpn);

    CoreMapSet(physPage, this, vpn);
    frameLRU->Touch(physPage);
}

void
AddrSpace::HandlePageFault(int vpn)
{
    if(vpn < 0 || (unsigned)vpn >= numPages) {
        printf("Invalid page fault at vpn %d\n", vpn);
        interrupt->Halt();
        return;
    }

    SyncTLBToFrames();

    if(pageTable[vpn].valid) {

        frameLRU->Touch(pageTable[vpn].physicalPage);
        UpdateTLB(vpn);
        return;
    }

    stats->numPageFaults++;
    LoadPage(vpn);
    UpdateTLB(vpn);
}

#endif
