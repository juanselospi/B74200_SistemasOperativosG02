// agua.h
//	Problema del agua: hay hilos "hidrogeno" e hilos "oxigeno" que van
//	llegando en cualquier orden.  Una molecula necesita 2 H y 1 O, y
//	ningun hilo puede enlazarse hasta que esten los tres.  Esta clase
//	es un monitor: un solo lock protege los contadores y dos variables
//	de condicion, una por tipo de atomo.
//
// Tarea corta: Lock, Condition y problema del agua.

#ifndef AGUA_H
#define AGUA_H

#include "synch.h"

class FormadorAgua {

public:
    FormadorAgua();
    ~FormadorAgua();

    // Cada hilo llama a la que le toca; vuelve cuando ya se enlazo
    void llegaHidrogeno( long id );
    void llegaOxigeno( long id );

private:
    // Si hay 2 H y 1 O esperando y no hay otra molecula a medio hacer,
    // les reserva los cupos y los despierta.  Se llama con el lock tomado.
    void intentarFormar();

    // Lo que hace cada atomo despues de que le dieron cupo en una molecula
    void enlazar( char tipo, long id );

    int hEsperando;		// hidrogenos dormidos sin molecula asignada
    int oEsperando;		// oxigenos dormidos sin molecula asignada
    int hCupos;			// cupos reservados para hidrogenos de la molecula actual
    int oCupos;			// cupos reservados para oxigenos de la molecula actual

    bool formando;		// true desde que se reservan cupos hasta que los 3 se enlazan
    int enlazados;		// cuantos de los 3 ya pasaron por la barrera
    char miembros[ 3 ];		// 'H' u 'O' de los que van enlazados, solo para imprimir
    long idsMiembros[ 3 ];
    int moleculas;		// total de moleculas completas

    Lock * candado;
    Condition * esperaH;
    Condition * esperaO;
    Barrier * enlace;		// los 3 atomos de una molecula se juntan aqui

};

#endif // AGUA_H
