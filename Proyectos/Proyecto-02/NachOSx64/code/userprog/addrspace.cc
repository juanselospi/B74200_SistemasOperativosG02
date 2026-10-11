// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
// noff.h ya viene por addrspace.h (y no tiene guarda de inclusion)

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void 
SwapHeader (NoffHeader *noffH)
{
	noffH->noffMagic = WordToHost(noffH->noffMagic);
	noffH->code.size = WordToHost(noffH->code.size);
	noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
	noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
	noffH->initData.size = WordToHost(noffH->initData.size);
	noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
	noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
	noffH->uninitData.size = WordToHost(noffH->uninitData.size);
	noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
	noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable)
{
    NoffHeader noffH;
    unsigned int i, size;

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) && 
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
    	SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

// how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size 
			+ UserStackSize;	// we need to increase the size
						// to leave room for the stack
    numPages = divRoundUp(size, PageSize);
    size = numPages * PageSize;

    DEBUG('a', "Initializing address space, num pages %d, size %d\n", 
					numPages, size);

    // Antes el programa iba a los marcos 0..numPages-1 y solo podia
    // haber uno en memoria.  Ahora cada pagina pide un marco al mapa de
    // bits de la maquina, asi varios procesos conviven y a cada uno le
    // pueden tocar marcos salteados.
    huboMarcos = true;
    usosCompartidos = new int;
    *usosCompartidos = 1;
    paginasPila = divRoundUp( UserStackSize, PageSize );
    pageTable = new TranslationEntry[numPages];
    for (i = 0; i < numPages; i++) {
	int marco = machine->pedirMarco();
	if ( marco == -1 ) {
	    // Se acabo la memoria: se devuelve lo que ya se habia tomado
	    // y quien creo el espacio decide que hacer (Exec devuelve -1)
	    DEBUG('a', "Sin marcos libres al pedir la pagina %d\n", i);
	    huboMarcos = false;
	    numPages = i;
	    return;
	}
	pageTable[i].virtualPage = i;
	pageTable[i].physicalPage = marco;
	pageTable[i].valid = true;
	pageTable[i].use = false;
	pageTable[i].dirty = false;
	pageTable[i].readOnly = false;  // if the code segment was entirely on 
					// a separate page, we could set its 
					// pages to be read-only

	// Se limpia el marco (datos no inicializados y pila deben ir en
	// cero) y encima se copia lo que le toque de codigo y datos
	bzero( machine->mainMemory + marco * PageSize, PageSize );
	cargarPagina( i, executable, &noffH );
    }

    DEBUG('a', "Espacio cargado en %d paginas, quedan %d marcos libres\n",
          numPages, machine->marcosLibres());
}

//----------------------------------------------------------------------
// AddrSpace::cargarPagina
// 	Llena una pagina virtual leyendola del ejecutable.  La pagina
//	puede tener un pedazo de codigo, un pedazo de datos inicializados,
//	los dos (cuando el codigo termina a media pagina) o nada (datos sin
//	inicializar y pila).  Cada segmento se revisa por aparte.
//
//	Queda como metodo propio pensando en el proyecto 3: ahi la pagina
//	se va a cargar bajo demanda cuando haya un fallo de pagina.
//----------------------------------------------------------------------

void
AddrSpace::cargarPagina( unsigned int paginaVirtual, OpenFile *executable,
                         NoffHeader *encabezado )
{
    if ( encabezado->code.size > 0 ) {
        copiarPedazo( paginaVirtual, executable, &encabezado->code );
    }
    if ( encabezado->initData.size > 0 ) {
        copiarPedazo( paginaVirtual, executable, &encabezado->initData );
    }
}

//----------------------------------------------------------------------
// AddrSpace::copiarPedazo
//	Calcula la interseccion entre [inicio, fin) de la pagina virtual y
//	[virtualAddr, virtualAddr + size) del segmento.  Si hay algo en
//	comun, lo lee del archivo (desplazado igual que en memoria virtual)
//	directo al marco fisico de la pagina.
//----------------------------------------------------------------------

void
AddrSpace::copiarPedazo( unsigned int paginaVirtual, OpenFile *executable,
                         Segment *segmento )
{
    unsigned int inicioPagina = paginaVirtual * PageSize;
    unsigned int finPagina = inicioPagina + PageSize;
    unsigned int inicioSeg = segmento->virtualAddr;
    unsigned int finSeg = inicioSeg + segmento->size;

    unsigned int desde = ( inicioPagina > inicioSeg ) ? inicioPagina : inicioSeg;
    unsigned int hasta = ( finPagina < finSeg ) ? finPagina : finSeg;

    if ( desde >= hasta ) {
        return;		// esta pagina no tiene nada de este segmento
    }

    int marco = pageTable[ paginaVirtual ].physicalPage;
    char * destino = machine->mainMemory + marco * PageSize + ( desde - inicioPagina );
    int enArchivo = segmento->inFileAddr + ( desde - inicioSeg );

    DEBUG('a', "  pagina %d: %d bytes del archivo (pos %d) al marco %d\n",
          paginaVirtual, hasta - desde, enArchivo, marco);
    executable->ReadAt( destino, hasta - desde, enArchivo );
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Devuelve los marcos al mapa de bits y borra la tabla de paginas.
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
    unsigned int primeraDePila = ( numPages > paginasPila ) ? numPages - paginasPila : 0;

    // La pila siempre es mia
    for ( unsigned int i = primeraDePila; i < numPages; i++ ) {
        machine->soltarMarco( pageTable[ i ].physicalPage );
    }
    // Codigo y datos solo los devuelve el ultimo que los usa
    (*usosCompartidos)--;
    if ( *usosCompartidos == 0 ) {
        for ( unsigned int i = 0; i < primeraDePila; i++ ) {
            machine->soltarMarco( pageTable[ i ].physicalPage );
        }
        delete usosCompartidos;
    }
    delete [] this->pageTable;
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace( AddrSpace * padre )
//	Espacio para un hilo creado con el Fork de usuario.  Comparte con
//	el padre los marcos de codigo y datos (misma pagina fisica en la
//	tabla) y pide marcos nuevos, en cero, para la pila, que es lo unico
//	que cada hilo tiene aparte.
//----------------------------------------------------------------------

AddrSpace::AddrSpace(AddrSpace *padre)
{
    numPages = padre->numPages;
    paginasPila = padre->paginasPila;
    usosCompartidos = padre->usosCompartidos;
    (*usosCompartidos)++;
    huboMarcos = true;

    unsigned int primeraDePila = ( numPages > paginasPila ) ? numPages - paginasPila : 0;
    pageTable = new TranslationEntry[numPages];

    for ( unsigned int i = 0; i < numPages; i++ ) {
        pageTable[ i ] = padre->pageTable[ i ];
        if ( i >= primeraDePila ) {
            int marco = machine->pedirMarco();
            if ( marco == -1 ) {
                DEBUG('a', "Fork: sin marcos para la pila en la pagina %d\n", i);
                huboMarcos = false;
                // Lo que ya se habia pedido de pila se devuelve aqui mismo;
                // el destructor no debe tocar estas paginas
                for ( unsigned int j = primeraDePila; j < i; j++ ) {
                    machine->soltarMarco( pageTable[ j ].physicalPage );
                }
                numPages = primeraDePila;
                paginasPila = 0;
                return;
            }
            pageTable[ i ].physicalPage = marco;
            pageTable[ i ].use = false;
            pageTable[ i ].dirty = false;
            bzero( machine->mainMemory + marco * PageSize, PageSize );
        }
    }

    DEBUG('a', "Espacio de Fork: %d paginas compartidas, %d de pila nuevas\n",
          primeraDePila, paginasPila);
}

bool
AddrSpace::cargadoBien()
{
    return huboMarcos;
}

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);	

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

   // Set the stack register to the end of the address space, where we
   // allocated the stack; but subtract off a bit, to make sure we don't
   // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG('a', "Initializing stack register to %d\n", numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState() 
{}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() 
{
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
}
