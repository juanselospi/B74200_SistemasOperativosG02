// tablainvertida.h
//	Tabla de paginas invertida: por cada marco fisico dice que pagina
//	virtual lo ocupa (apuntando directo a su entrada en la tabla del
//	proceso y a su cupo de swap) y cuando fue usado por ultima vez, que
//	es lo que necesita el LRU para escoger una victima.
//
//	Se guardan punteros a la entrada y no un "AddrSpace *" porque los
//	hilos de un Fork comparten la tabla de codigo y datos: la entrada es
//	la misma para todos y vive mientras el ultimo de ellos no salga.
//
// Proyecto 3 - memoria virtual

#ifndef TABLAINVERTIDA_H
#define TABLAINVERTIDA_H

#include "machine.h"

class TablaInvertida {

public:
    TablaInvertida();
    ~TablaInvertida();

    void asignar( int marco, TranslationEntry * entrada, int * cupoSwap, unsigned long ahora );
    void liberar( int marco );
    void tocar( int marco, unsigned long ahora );	// la pagina fue usada

    bool ocupado( int marco );
    TranslationEntry * entrada( int marco );
    int * cupoSwap( int marco );

    // LRU: el marco ocupado con el uso mas viejo, saltando los que esten
    // en "excluidos" (los que el TLB tiene en uso ahora mismo)
    int elegirVictima( bool * excluidos );

    void imprimir();

private:
    struct Marco {
        bool ocupado;
        TranslationEntry * entrada;
        int * cupoSwap;
        unsigned long ultimoUso;
    };

    Marco marcos[ NumPhysPages ];

};

#endif // TABLAINVERTIDA_H
