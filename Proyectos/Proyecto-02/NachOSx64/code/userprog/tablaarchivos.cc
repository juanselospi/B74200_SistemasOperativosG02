// tablaarchivos.cc
//	Implementacion de la tabla de recursos abiertos.  Ver tablaarchivos.h
//
// Proyecto 2 - programas de usuario

#include "tablaarchivos.h"
#include "syscall.h"
#include "system.h"
#include <unistd.h>

TablaArchivos::TablaArchivos() {
    ocupados = new BitMap( MaxRecursosAbiertos );
    hilosUsando = 0;

    for ( int i = 0; i < MaxRecursosAbiertos; i++ ) {
        descriptores[ i ] = -1;
        tipos[ i ] = RECURSO_LIBRE;
    }

    // 0, 1 y 2 son la consola (ConsoleInput, ConsoleOutput, ConsoleError);
    // se marcan para que Find nunca los entregue
    ocupados->Mark( ConsoleInput );
    ocupados->Mark( ConsoleOutput );
    ocupados->Mark( ConsoleError );
}


TablaArchivos::~TablaArchivos() {
    delete ocupados;
}


int TablaArchivos::abrir( int descriptorUnix, TipoRecurso tipo ) {
    int id = ocupados->Find();
    if ( id == -1 ) {
        return -1;
    }
    descriptores[ id ] = descriptorUnix;
    tipos[ id ] = tipo;
    return id;
}


int TablaArchivos::cerrar( int idNachos ) {
    if ( !estaAbierto( idNachos ) ) {
        return -1;
    }
    ocupados->Clear( idNachos );
    descriptores[ idNachos ] = -1;
    tipos[ idNachos ] = RECURSO_LIBRE;
    return 0;
}


// La consola no cuenta como "abierta" aqui: la atienden aparte los syscalls
bool TablaArchivos::estaAbierto( int idNachos ) {
    if ( idNachos <= ConsoleError || idNachos >= MaxRecursosAbiertos ) {
        return false;
    }
    return ocupados->Test( idNachos );
}


bool TablaArchivos::esSocket( int idNachos ) {
    return estaAbierto( idNachos ) && tipos[ idNachos ] == RECURSO_SOCKET;
}


int TablaArchivos::descriptorUnix( int idNachos ) {
    if ( !estaAbierto( idNachos ) ) {
        return -1;
    }
    return descriptores[ idNachos ];
}


void TablaArchivos::agregarHilo() {
    hilosUsando++;
}


int TablaArchivos::quitarHilo() {
    hilosUsando--;
    return hilosUsando;
}


void TablaArchivos::cerrarTodo() {
    for ( int i = ConsoleError + 1; i < MaxRecursosAbiertos; i++ ) {
        if ( ocupados->Test( i ) ) {
            close( descriptores[ i ] );
            cerrar( i );
        }
    }
}


void TablaArchivos::imprimir() {
    printf( "Tabla de recursos abiertos (%d hilos la usan):\n", hilosUsando );
    for ( int i = ConsoleError + 1; i < MaxRecursosAbiertos; i++ ) {
        if ( ocupados->Test( i ) ) {
            printf( "  id %2d -> unix %2d  %s\n", i, descriptores[ i ],
                    ( tipos[ i ] == RECURSO_SOCKET ) ? "socket" : "archivo" );
        }
    }
}
