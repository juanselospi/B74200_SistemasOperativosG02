// swap.h
//	Area de intercambio: un solo archivo "SWAP" para todo NachOS, dividido
//	en cupos del tamanno de una pagina.  Las paginas de datos y pila que
//	salen sucias de memoria se guardan aqui; las de codigo puro nunca,
//	porque siempre se pueden volver a leer del ejecutable.
//
//	Tamanno: regla del 2x (el doble de los marcos de memoria fisica) con
//	un minimo de 64 paginas, que es lo que salva la corrida con 4 marcos.
//
// Proyecto 3 - memoria virtual

#ifndef SWAP_H
#define SWAP_H

#include "bitmap.h"
#include "openfile.h"

#define MinimoPaginasSwap 64

class Swap {

public:
    Swap( int numMarcosFisicos );
    ~Swap();

    // Guarda una pagina en un cupo nuevo; devuelve el cupo o -1 si esta lleno
    int guardar( const char * pagina );
    // Vuelve a escribir sobre un cupo que ya era de esa pagina
    void reescribir( int cupo, const char * pagina );
    // Lee el cupo a la memoria indicada
    void leer( int cupo, char * pagina );
    void liberar( int cupo );

    int cuposLibres();
    int maximoOcupado();		// cupo mas alto que se llego a usar
    void imprimir();

private:
    OpenFile * archivo;
    BitMap * cupos;
    int numCupos;
    int maxUsado;

};

#endif // SWAP_H
