#include "nachostabla.h"
#include "system.h"

#include <unistd.h>
#include <stdio.h>


NachosOpenFilesTable::NachosOpenFilesTable() {

    openFiles = new int[MAX_OPEN_FILES ];
    openFilesMap = new BitMap(MAX_OPEN_FILES);
    usage = 0;


    for(int i = 0; i < MAX_OPEN_FILES; i++) {

        openFiles[i] = -1;
    }

    // reservar los descriptores 0 -> consoleinput, 1 -> consoleoutput, 2 -> consolerror.
    openFilesMap->Mark(0); // consoleinput
    openFilesMap->Mark(1); // consoleoutput
    openFilesMap->Mark(2); // consoleerror
}


NachosOpenFilesTable::~NachosOpenFilesTable() {

    // cerrar archivos abiertos
    for(int i = 3; i < MAX_OPEN_FILES; i++) {

        if(openFilesMap->Test(i)) {

            close(openFiles[i]);
        }
    }

    delete[] openFiles;
    delete openFilesMap;
}


int NachosOpenFilesTable::Open( int UnixHandle ) {
    
    int NachosHandle = openFilesMap->Find(); // esto ncuentra primer espacio libre y lo marca

    if(NachosHandle == -1) {

        return -1; // la tabla esta llena
    }

    openFiles[NachosHandle] = UnixHandle;

    return NachosHandle;
}


int NachosOpenFilesTable::Close( int NachosHandle ) {

    if(NachosHandle < 0 || NachosHandle >= MAX_OPEN_FILES) {

        return -1;
    }

    if(!openFilesMap->Test(NachosHandle)) {

        return -1;   // No estaba abierto
    }

    int UnixHandle = openFiles[NachosHandle];
    openFilesMap->Clear(NachosHandle);
    openFiles[NachosHandle] = -1;

    return UnixHandle;
}


bool NachosOpenFilesTable::isOpened( int NachosHandle ) {

    if(NachosHandle < 0 || NachosHandle >= MAX_OPEN_FILES) {

        return false;
    }

    return openFilesMap->Test(NachosHandle);
}


int NachosOpenFilesTable::getUnixHandle( int NachosHandle ) {

    if(!isOpened(NachosHandle)) {

        return -1;
    }

    return openFiles[NachosHandle];
}


void NachosOpenFilesTable::addThread() {

    usage++;
}


void NachosOpenFilesTable::delThread() {

    usage--;
}


void NachosOpenFilesTable::Print() {

    printf("NachosOpenFilesTable: uso actual de la tabla = %d\n", usage);

    for(int i = 0; i < MAX_OPEN_FILES; i++ ) {

        if(openFilesMap->Test(i)) {

            printf("  ...[%d] -> unix fd %d\n", i, openFiles[i]);
        }
    }
}
