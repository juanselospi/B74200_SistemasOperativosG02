// Tabla de paginas invertidas para marcos fisicos
#ifndef COREMAP_H
#define COREMAP_H

#include "copyright.h"
#include "synch.h"

class AddrSpace;

struct CoreMapEntry {
    AddrSpace *space;
    int vpn;
    bool inUse;
};

extern CoreMapEntry *coreMap;
extern Lock *coreMapLock;

void CoreMapInit();
void CoreMapDone();
void CoreMapSet(int physPage, AddrSpace *space, int vpn);
void CoreMapClear(int physPage);
CoreMapEntry *CoreMapLookup(int physPage);

#endif
