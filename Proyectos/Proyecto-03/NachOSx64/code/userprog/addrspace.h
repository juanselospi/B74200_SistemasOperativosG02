// addrspace.h 
//	Data structures to keep track of executing user programs 
//	(address spaces).
//
//	For now, we don't keep any information about address spaces.
//	The user level CPU state is saved and restored in the thread
//	executing the user program (see thread.h).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.
//
// Proyecto 2: paginacion con bitmap de marcos y carga por paginas.
// Proyecto 3: carga bajo demanda, TLB, swap y reemplazo LRU (bajo VM).

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"
#include "noff.h"
#ifdef VM
// Solo declaraciones adelantadas: incluir synch.h aqui arma un ciclo con
// thread.h (thread.h -> addrspace.h -> synch.h -> thread.h)
class Lock;
class Swap;
class TablaInvertida;
#endif

// copy.c declara un buffer de 1024 bytes en la pila, con 1024 de pila se
// desbordaba sobre el codigo; con 2048 (16 paginas) alcanza
#define UserStackSize		2048

// Lo que comparten los hilos de un mismo proceso (los que nacen con el
// Fork de usuario): el ejecutable abierto, su encabezado, y bajo VM la
// tabla de las paginas de codigo y datos con sus cupos de swap.  Es un
// solo objeto para que una expulsion la vean todos; se borra cuando el
// ultimo hilo lo suelta.
struct PaginasCompartidas {
    OpenFile * ejecutable;
    NoffHeader encabezado;
    unsigned int numCompartidas;	// paginas de codigo y datos, sin la pila
    int usos;				// cuantos AddrSpace lo usan
#ifdef VM
    TranslationEntry * tabla;		// numCompartidas entradas
    int * cupoSwap;			// cupo en el swap por pagina, -1 si nunca salio
#endif
};

class AddrSpace {
  public:
    AddrSpace(OpenFile *executable);	// Create an address space,
					// initializing it with the program
					// stored in the file "executable".
					// Se queda con el archivo y lo
					// cierra en el destructor.
    // Para Fork: comparte codigo y datos con "padre" y solo le da
    // paginas nuevas a la pila
    AddrSpace(AddrSpace *padre);
    ~AddrSpace();			// De-allocate an address space

    void InitRegisters();		// Initialize user-level CPU registers,
					// before jumping to user code

    void SaveState();			// Save/restore address space-specific
    void RestoreState();		// info on a context switch 

    bool cargadoBien();			// false si no hubo marcos suficientes

#ifdef VM
    // Atiende la falta de pagina de esa direccion virtual.  Devuelve true
    // si la pagina quedo en memoria y en el TLB, false si el proceso tiene
    // que morir (direccion fuera de su espacio o swap lleno).
    bool atenderFallo( int direccionVirtual );

    static void iniciarMemoriaVirtual();	// crea swap, tabla invertida, lock
    static void terminarMemoriaVirtual();
    static void imprimirMemoria();		// marcos, TLB y swap, para depurar
#endif

  private:
    // Lee del ejecutable (o del swap, bajo VM) lo que le toca a una
    // pagina virtual y lo deja en el marco; marca la entrada como valida
    void cargarPagina( unsigned int paginaVirtual, int marco );
    // Copia al marco la parte de un segmento que cae dentro de la pagina
    void copiarPedazo( unsigned int paginaVirtual, int marco, Segment *segmento );
    // true si la pagina solo tiene codigo: nunca se ensucia ni va al swap
    bool esCodigoPuro( unsigned int paginaVirtual );
    // La entrada que manda para esa pagina: la compartida o la propia
    TranslationEntry * entradaDe( unsigned int paginaVirtual );

    PaginasCompartidas * compartido;
    TranslationEntry *pageTable;	// sin VM: tabla completa del hilo;
					// con VM: solo se usan las de la pila
    unsigned int numPages;		// Number of pages in the virtual 
					// address space
    unsigned int paginasPila;		// las ultimas paginas, privadas de cada hilo
    unsigned int primeraDePila;		// numPages - paginasPila
    bool huboMarcos;			// quedo false si el bitmap se quedo sin marcos

#ifdef VM
    int * cupoSwapPila;			// cupo de swap de cada pagina de pila
    int * cupoDe( unsigned int paginaVirtual );

    void sincronizarTLB();		// use/dirty del TLB a las tablas, y sellos LRU
    void meterEnTLB( TranslationEntry * entrada );
    void sacarDelTLB( int marco );	// invalida la entrada que apunte al marco
    int conseguirMarco();		// libre, o expulsando una victima; -1 si no se pudo
    void soltarPagina( unsigned int paginaVirtual );	// marco y cupo de una pagina

    static TablaInvertida * marcos;
    static Swap * swap;
    static Lock * candadoVM;
    static unsigned long reloj;		// contador para los sellos LRU
    static unsigned long selloTLB[ TLBSize ];
#endif
};

#endif // ADDRSPACE_H
