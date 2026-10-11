// tablaprocesos.cc
//	Ver tablaprocesos.h
//
// Proyecto 2 - programas de usuario

#include "tablaprocesos.h"
#include <string.h>

TablaProcesos::TablaProcesos() {
    ocupados = new BitMap( MaxProcesos );
    for ( int i = 0; i < MaxProcesos; i++ ) {
        entradas[ i ].esperaFin = NULL;
        entradas[ i ].termino = false;
        entradas[ i ].estado = 0;
        entradas[ i ].nombre[ 0 ] = 0;
    }
}


TablaProcesos::~TablaProcesos() {
    for ( int i = 0; i < MaxProcesos; i++ ) {
        if ( entradas[ i ].esperaFin != NULL ) {
            delete entradas[ i ].esperaFin;
        }
    }
    delete ocupados;
}


int TablaProcesos::registrar( const char * nombreProceso ) {
    int id = ocupados->Find();
    if ( id == -1 ) {
        return -1;
    }
    strncpy( entradas[ id ].nombre, nombreProceso, sizeof( entradas[ id ].nombre ) - 1 );
    entradas[ id ].nombre[ sizeof( entradas[ id ].nombre ) - 1 ] = 0;
    entradas[ id ].estado = 0;
    entradas[ id ].termino = false;
    // El semaforo arranca en 0: Join hace P y se duerme hasta el V de Exit
    entradas[ id ].esperaFin = new Semaphore( entradas[ id ].nombre, 0 );
    return id;
}


bool TablaProcesos::existe( int id ) {
    if ( id < 0 || id >= MaxProcesos ) {
        return false;
    }
    return ocupados->Test( id );
}


const char * TablaProcesos::nombre( int id ) {
    return existe( id ) ? entradas[ id ].nombre : "?";
}


void TablaProcesos::termino( int id, int estado ) {
    if ( !existe( id ) ) {
        return;
    }
    entradas[ id ].estado = estado;
    entradas[ id ].termino = true;
    entradas[ id ].esperaFin->V();
}


int TablaProcesos::esperar( int id ) {
    if ( !existe( id ) ) {
        return -1;
    }
    entradas[ id ].esperaFin->P();
    int estado = entradas[ id ].estado;

    // Ya nadie mas va a preguntar por este proceso, se libera la entrada
    delete entradas[ id ].esperaFin;
    entradas[ id ].esperaFin = NULL;
    ocupados->Clear( id );

    return estado;
}


void TablaProcesos::imprimir() {
    printf( "Procesos de usuario:\n" );
    for ( int i = 0; i < MaxProcesos; i++ ) {
        if ( ocupados->Test( i ) ) {
            printf( "  %2d %-30s %s\n", i, entradas[ i ].nombre,
                    entradas[ i ].termino ? "termino" : "corriendo" );
        }
    }
}
