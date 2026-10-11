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

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"
#include "noff.h"

// copy.c declara un buffer de 1024 bytes en la pila, con 1024 de pila se
// desbordaba sobre el codigo; con 2048 (16 paginas) alcanza
#define UserStackSize		2048

class AddrSpace {
  public:
    AddrSpace(OpenFile *executable);	// Create an address space,
					// initializing it with the program
					// stored in the file "executable"
    // Para Fork: comparte codigo y datos con "padre" y solo le da marcos
    // nuevos a la pila
    AddrSpace(AddrSpace *padre);
    ~AddrSpace();			// De-allocate an address space

    bool cargadoBien();			// false si no hubo marcos suficientes

    void InitRegisters();		// Initialize user-level CPU registers,
					// before jumping to user code

    void SaveState();			// Save/restore address space-specific
    void RestoreState();		// info on a context switch 

  private:
    // Lee del ejecutable lo que le toca a una pagina virtual (puede ser
    // un pedazo de codigo, de datos, o de los dos) y lo deja en su marco
    void cargarPagina( unsigned int paginaVirtual, OpenFile *executable,
                       NoffHeader *encabezado );
    // Copia al marco la parte de un segmento que cae dentro de la pagina
    void copiarPedazo( unsigned int paginaVirtual, OpenFile *executable,
                       Segment *segmento );

    TranslationEntry *pageTable;	// Assume linear page table translation
					// for now!
    unsigned int numPages;		// Number of pages in the virtual 
					// address space
    bool huboMarcos;			// quedo false si el bitmap se quedo sin marcos

    // Las paginas de codigo y datos se comparten entre los hilos de un
    // Fork; este contador dice cuantos espacios las usan para que solo el
    // ultimo las devuelva.  Las ultimas "paginasPila" son privadas.
    int * usosCompartidos;
    unsigned int paginasPila;
};

#endif // ADDRSPACE_H
