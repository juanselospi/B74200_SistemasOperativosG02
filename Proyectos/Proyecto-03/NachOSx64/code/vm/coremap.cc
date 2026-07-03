
#include "copyright.h"
#include "coremap.h"
#include "addrspace.h"
#include "system.h"

CoreMapEntry *coreMap = NULL;
Lock *coreMapLock = NULL;

void
CoreMapInit()
{
    coreMap = new CoreMapEntry[NumPhysPages];
    coreMapLock = new Lock("core map lock");

    for(int i = 0; i < NumPhysPages; i++) {
        coreMap[i].space = NULL;
        coreMap[i].vpn = -1;
        coreMap[i].inUse = false;
    }
}

void
CoreMapDone()
{
    delete[] coreMap;
    delete coreMapLock;
    coreMap = NULL;
    coreMapLock = NULL;
}

void
CoreMapSet(int physPage, AddrSpace *space, int vpn)
{
    coreMapLock->Acquire();
    coreMap[physPage].space = space;
    coreMap[physPage].vpn = vpn;
    coreMap[physPage].inUse = true;
    coreMapLock->Release();
}

void
CoreMapClear(int physPage)
{
    coreMapLock->Acquire();
    coreMap[physPage].space = NULL;
    coreMap[physPage].vpn = -1;
    coreMap[physPage].inUse = false;
    coreMapLock->Release();
}

CoreMapEntry *
CoreMapLookup(int physPage)
{
    return &coreMap[physPage];
}
