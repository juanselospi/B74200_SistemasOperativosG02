#ifndef SWAP_H
#define SWAP_H

#include "copyright.h"
#include "bitmap.h"
#include "openfile.h"
#include "synch.h"
#include "machine.h"

const int SwapFloorPages = 64;
const int NumSwapPages = (2 * NumPhysPages > SwapFloorPages) ? (2 * NumPhysPages) : SwapFloorPages;

class SwapManager {
  public:
    SwapManager();
    ~SwapManager();

    int AllocateSlot();
    void FreeSlot(int slot);
    void ReadPage(int slot, char *physAddr);
    void WritePage(int slot, char *physAddr);
    int MaxUsedSlot() { return maxUsedSlot; }

  private:
    OpenFile *swapFile;
    BitMap *swapMap;
    Lock *lock;
    int maxUsedSlot;
};

extern SwapManager *swapManager;

#endif
