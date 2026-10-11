// tablaprocesos.h
//	Tabla de los procesos de usuario vivos.  Exec registra uno nuevo y
//	devuelve su SpaceId; Join se duerme en el semaforo de esa entrada
//	hasta que el hilo principal del proceso haga Exit, y se lleva el
//	estado de salida.  La entrada se libera cuando alguien hace Join.
//
// Proyecto 2 - programas de usuario

#ifndef TABLAPROCESOS_H
#define TABLAPROCESOS_H

#include "synch.h"
#include "bitmap.h"

#define MaxProcesos 64

class TablaProcesos {

public:
    TablaProcesos();
    ~TablaProcesos();

    // Aparta una entrada para un proceso nuevo; devuelve su id o -1
    int registrar( const char * nombre );
    bool existe( int id );
    const char * nombre( int id );

    // El hilo principal termino: guarda el estado y despierta a quien espere
    void termino( int id, int estado );
    // Espera a que el proceso termine y devuelve su estado; libera la entrada
    int esperar( int id );

    void imprimir();

private:
    struct Entrada {
        char nombre[ 64 ];
        int estado;
        bool termino;
        Semaphore * esperaFin;
    };

    Entrada entradas[ MaxProcesos ];
    BitMap * ocupados;

};

#endif // TABLAPROCESOS_H
