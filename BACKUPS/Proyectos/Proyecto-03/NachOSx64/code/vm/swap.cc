// Lectura/escritura de paginas en archivo SWAP
#include "copyright.h"
#include "swap.h"
#include "system.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>

SwapManager *swapManager = NULL;

SwapManager::SwapManager()
{
    maxUsedSlot = -1;
    lock = new Lock("swap lock");
    swapMap = new BitMap(NumSwapPages);

    int fd = open("SWAP", O_RDWR | O_CREAT | O_TRUNC, 0666);
    ASSERT(fd >= 0);
    ftruncate(fd, NumSwapPages * PageSize);
    swapFile = new OpenFile(fd);
}

SwapManager::~SwapManager()
{
    delete swapMap;
    delete swapFile;
    delete lock;
    unlink("SWAP");
}

int
SwapManager::AllocateSlot()
{
    lock->Acquire();
    int slot = swapMap->Find();
    if(slot != -1 && slot > maxUsedSlot) {
        maxUsedSlot = slot;
    }
    lock->Release();
    return slot;
}

void
SwapManager::FreeSlot(int slot)
{
    if(slot < 0) {
        return;
    }
    lock->Acquire();
    swapMap->Clear(slot);
    lock->Release();
}

void
SwapManager::ReadPage(int slot, char *physAddr)
{
    lock->Acquire();
    swapFile->ReadAt(physAddr, PageSize, slot * PageSize);
    stats->numDiskReads++;
    lock->Release();
}

void
SwapManager::WritePage(int slot, char *physAddr)
{
    lock->Acquire();
    swapFile->WriteAt(physAddr, PageSize, slot * PageSize);
    stats->numDiskWrites++;
    lock->Release();
}
