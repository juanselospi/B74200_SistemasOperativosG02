// addrspace.h 
//	Data structures to keep track of executing user programs 
//	(address spaces).
//
//	For now, we don't keep any information about address spaces.
//	The user level CPU state is saved and restored in the thread
//	executing the user program (see thread.h).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"
#include "noff.h"

#define UserStackSize		4096 	// increase this as necessary!
#define MAX_OPEN_FILES 128

class AddrSpace {
  public:
    AddrSpace(OpenFile *executable);	// Create an address space,
					// initializing it with the program
					// stored in the file "executable"

    AddrSpace(AddrSpace *parent);

    ~AddrSpace();			// De-allocate an address space

    void InitRegisters();		// Initialize user-level CPU registers,
					// before jumping to user code
    void InitRegistersForFork(int funcAddr);

    void SaveState();			// Save/restore address space-specific
    void RestoreState();		// info on a context switch 

    unsigned int getNumPages() { return numPages; }

    void AddThread() {threadCount++;}
    void RemoveThread() {threadCount--;}

    bool CanDelete() {return threadCount <= 0;}

    static void ReserveTestPhysPages();
    static void ReleaseTestPhysPages();

    int newStack();

#ifdef VM
    void HandlePageFault(int vpn);
#endif

    OpenFile* openFiles[MAX_OPEN_FILES];

  private:
    AddrSpace *parentSpace;
    TranslationEntry *pageTable;	// Assume linear page table translation
					// for now!
    unsigned int numPages;		// Number of pages in the virtual 
					// address space
    unsigned int numStackPages;
    unsigned int stackVirtualTop;

    int threadCount;

#ifdef VM
    OpenFile *execFile;
    NoffHeader noffH;
    int *swapLocations;
#endif

    void AllocatePageTable(unsigned int numPages);
    void LoadSegment(OpenFile *executable, int segmentSize, unsigned int virtualAddr, int inFileAddr);
    int  AllocatePhysicalPage();
    void DeallocatePhysicalPage(int physPage);

#ifdef VM
    void LoadPage(int vpn);
    int  EvictFrame();
    void LoadPageContent(int vpn, int physPage);
    void LoadPageSegment(char *mem, unsigned int vAddr, unsigned int segVA,
                         int segSize, int inFileAddr);
    bool IsCodeOnlyPage(unsigned int vpn);
    void UpdateTLB(int vpn);
    void SyncTLBToPageTable();
    void SyncTLBToFrames();
    void InvalidateTLB();
    void InvalidateTLBEntry(int vpn);
    int  FindTLBEntry(int vpn);
    int  AllocateTLBSlot();
#endif

};

#endif // ADDRSPACE_H
