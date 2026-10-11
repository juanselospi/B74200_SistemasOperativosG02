// tablainvertida.cc
//	Ver tablainvertida.h
//
// Proyecto 3 - memoria virtual

#include "tablainvertida.h"

TablaInvertida::TablaInvertida() {
    for ( int i = 0; i < NumPhysPages; i++ ) {
        liberar( i );
    }
}


TablaInvertida::~TablaInvertida() {
}


void TablaInvertida::asignar( int marco, TranslationEntry * entrada, int * cupoSwap, unsigned long ahora ) {
    ASSERT( marco >= 0 && marco < NumPhysPages );
    marcos[ marco ].ocupado = true;
    marcos[ marco ].entrada = entrada;
    marcos[ marco ].cupoSwap = cupoSwap;
    marcos[ marco ].ultimoUso = ahora;
}


void TablaInvertida::liberar( int marco ) {
    ASSERT( marco >= 0 && marco < NumPhysPages );
    marcos[ marco ].ocupado = false;
    marcos[ marco ].entrada = NULL;
    marcos[ marco ].cupoSwap = NULL;
    marcos[ marco ].ultimoUso = 0;
}


void TablaInvertida::tocar( int marco, unsigned long ahora ) {
    ASSERT( marco >= 0 && marco < NumPhysPages );
    marcos[ marco ].ultimoUso = ahora;
}


bool TablaInvertida::ocupado( int marco ) {
    return marcos[ marco ].ocupado;
}


TranslationEntry * TablaInvertida::entrada( int marco ) {
    return marcos[ marco ].entrada;
}


int * TablaInvertida::cupoSwap( int marco ) {
    return marcos[ marco ].cupoSwap;
}


int TablaInvertida::elegirVictima( bool * excluidos ) {
    int victima = -1;
    for ( int i = 0; i < NumPhysPages; i++ ) {
        if ( !marcos[ i ].ocupado || excluidos[ i ] ) {
            continue;
        }
        if ( victima == -1 || marcos[ i ].ultimoUso < marcos[ victima ].ultimoUso ) {
            victima = i;
        }
    }
    return victima;
}


void TablaInvertida::imprimir() {
    printf( "Marcos fisicos:\n" );
    for ( int i = 0; i < NumPhysPages; i++ ) {
        if ( marcos[ i ].ocupado ) {
            printf( "  marco %3d <- pagina virtual %3d  uso %lu%s%s\n", i,
                    marcos[ i ].entrada->virtualPage, marcos[ i ].ultimoUso,
                    marcos[ i ].entrada->dirty ? " sucia" : "",
                    marcos[ i ].entrada->readOnly ? " solo-lectura" : "" );
        }
    }
}
