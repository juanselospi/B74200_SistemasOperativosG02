#ifndef LRU_H
#define LRU_H

#include "copyright.h"
#include "synch.h"

class LRUManager {
  public:
    LRUManager(int itemCount);
    ~LRUManager();

    void Touch(int id);
    void Clear(int id);
    int GetLastUsed(int id);
    int Oldest();

  private:
    int *lastUsed;
    int timeStamp;
    int itemCount;
    Lock *lock;
};

extern LRUManager *frameLRU;
extern LRUManager *tlbLRU;

#endif
