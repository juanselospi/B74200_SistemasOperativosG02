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
#include "noff.h"
#include "synch.h"
#include "bitmap.h"

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

static unsigned int
AddrToPhys(TranslationEntry *pageTable, unsigned int virtAddr)
{
    unsigned int vpn = virtAddr / PageSize;
    unsigned int offset = virtAddr % PageSize;
    return pageTable[vpn].physicalPage * PageSize + offset;
}


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

        int physPage = AllocatePhysicalPage();
        ASSERT(physPage != -1);

        pageTable[i].virtualPage = i;
        pageTable[i].physicalPage = physPage;
        pageTable[i].valid = true;
        pageTable[i].use = false;
        pageTable[i].dirty = false;
        pageTable[i].readOnly = false;
    }
}


void
AddrSpace::LoadSegment(OpenFile * executable, int segmentSize, unsigned int virtualAddr, int inFileAddr)
{
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

    NoffHeader noffH;
    unsigned int size;

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);

    if((noffH.noffMagic != NOFFMAGIC) && (WordToHost(noffH.noffMagic) == NOFFMAGIC)) {

        SwapHeader(&noffH);
    }

    ASSERT(noffH.noffMagic == NOFFMAGIC);

    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size + UserStackSize;
    unsigned int nPages = divRoundUp(size, PageSize);

    ASSERT(nPages <= NumPhysPages);

    DEBUG('a', "Initializing address space, num pages %d, size %d\n", nPages, nPages * PageSize);

    threadCount = 1;
    parentSpace = NULL;
    AllocatePageTable(nPages);

    if(noffH.code.size > 0) {

        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n", noffH.code.virtualAddr, noffH.code.size);
        LoadSegment(executable, noffH.code.size, noffH.code.virtualAddr, noffH.code.inFileAddr);
    }

    if(noffH.initData.size > 0) {

        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n", noffH.initData.virtualAddr, noffH.initData.size);
        LoadSegment(executable, noffH.initData.size, noffH.initData.virtualAddr, noffH.initData.inFileAddr);
    }
}


AddrSpace::AddrSpace(AddrSpace *parent) {

    numPages = parent->numPages;
    numStackPages = parent->numStackPages;
    stackVirtualTop = parent->stackVirtualTop;
    threadCount = 1;
    parentSpace = parent;
    parent->AddThread();

    unsigned int firstStackVpn = numPages - numStackPages;

    pageTable = new TranslationEntry[numPages];

    for(unsigned int i = 0; i < numPages; i++) {

        pageTable[i].virtualPage = i;
        pageTable[i].valid = true;
        pageTable[i].use = false;
        pageTable[i].dirty = false;
        pageTable[i].readOnly = false;

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
        
        DeallocatePhysicalPage(pageTable[i].physicalPage);
    } 

    delete[] pageTable;
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
{}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() {

    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
}
