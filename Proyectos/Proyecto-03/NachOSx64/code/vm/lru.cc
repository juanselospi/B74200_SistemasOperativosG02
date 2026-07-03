// LRU implementacion
#include "copyright.h"
#include "lru.h"
#include "system.h"

LRUManager *frameLRU = NULL;
LRUManager *tlbLRU = NULL;

LRUManager::LRUManager(int itemCount)
{
    this->itemCount = itemCount;
    timeStamp = 0;
    lastUsed = new int[itemCount];
    lock = new Lock("lru lock");

    for(int i = 0; i < itemCount; i++) {
        lastUsed[i] = 0;
    }
}

LRUManager::~LRUManager()
{
    delete[] lastUsed;
    delete lock;
}

void
LRUManager::Touch(int id)
{
    if(id < 0 || id >= itemCount) {
        return;
    }
    lock->Acquire();
    lastUsed[id] = ++timeStamp;
    lock->Release();
}

void
LRUManager::Clear(int id)
{
    if(id < 0 || id >= itemCount) {
        return;
    }
    lock->Acquire();
    lastUsed[id] = 0;
    lock->Release();
}

int
LRUManager::GetLastUsed(int id)
{
    if(id < 0 || id >= itemCount) {
        return 0;
    }
    lock->Acquire();
    int value = lastUsed[id];
    lock->Release();
    return value;
}

int
LRUManager::Oldest()
{
    lock->Acquire();
    int oldestId = -1;
    int oldestTime = timeStamp + 1;

    for(int i = 0; i < itemCount; i++) {
        if(lastUsed[i] > 0 && lastUsed[i] < oldestTime) {
            oldestTime = lastUsed[i];
            oldestId = i;
        }
    }
    lock->Release();
    return oldestId;
}
