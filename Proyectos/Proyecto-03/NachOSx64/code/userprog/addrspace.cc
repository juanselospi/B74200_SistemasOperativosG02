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
//
// Proyecto 2: cada pagina pide un marco al bitmap de la maquina y se
// carga por pedazos desde el ejecutable.
// Proyecto 3 (VM): nada se carga al crear el espacio; las paginas entran
// con los fallos, el TLB es la unica traduccion que ve la maquina, y
// cuando no hay marcos se expulsa la pagina menos usada (LRU) al swap.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
// noff.h ya viene por addrspace.h (y no tiene guarda de inclusion)
#ifdef VM
#include "swap.h"
#include "tablainvertida.h"
#endif

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


#ifdef VM
TablaInvertida * AddrSpace::marcos = NULL;
Swap * AddrSpace::swap = NULL;
Lock * AddrSpace::candadoVM = NULL;
unsigned long AddrSpace::reloj = 0;
unsigned long AddrSpace::selloTLB[ TLBSize ];
#endif

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
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
    paginasPila = divRoundUp( UserStackSize, PageSize );
    primeraDePila = ( numPages > paginasPila ) ? numPages - paginasPila : 0;

    DEBUG('a', "Initializing address space, num pages %d, size %d\n", 
					numPages, size);

    compartido = new PaginasCompartidas;
    compartido->ejecutable = executable;
    compartido->encabezado = noffH;
    compartido->numCompartidas = primeraDePila;
    compartido->usos = 1;
    huboMarcos = true;

    // Tabla propia: sin VM es la tabla completa; con VM solo se usan las
    // entradas de la pila, las demas viven en "compartido"
    pageTable = new TranslationEntry[numPages];
    for (i = 0; i < numPages; i++) {
	pageTable[i].virtualPage = i;
	pageTable[i].physicalPage = -1;
	pageTable[i].valid = false;
	pageTable[i].use = false;
	pageTable[i].dirty = false;
	pageTable[i].readOnly = false;
    }

#ifdef VM
    // Nada se carga ahora: todas invalidas, entran con el primer acceso.
    // El codigo puro va de solo lectura, asi el hardware avisa si alguien
    // lo toca y nunca hay que mandarlo al swap.
    compartido->tabla = new TranslationEntry[ primeraDePila + 1 ];
    compartido->cupoSwap = new int[ primeraDePila + 1 ];
    for ( i = 0; i < primeraDePila; i++ ) {
        compartido->tabla[ i ] = pageTable[ i ];
        compartido->tabla[ i ].readOnly = esCodigoPuro( i );
        compartido->cupoSwap[ i ] = -1;
    }
    cupoSwapPila = new int[ numPages ];
    for ( i = 0; i < numPages; i++ ) {
        cupoSwapPila[ i ] = -1;
    }
    DEBUG('v', "Espacio de %d paginas (%d compartidas + %d de pila), sin cargar\n",
          numPages, primeraDePila, paginasPila);
#else
    // Proyecto 2: cada pagina pide un marco y se carga de una vez
    for (i = 0; i < numPages; i++) {
	int marco = machine->pedirMarco();
	if ( marco == -1 ) {
	    DEBUG('a', "Sin marcos libres al pedir la pagina %d\n", i);
	    huboMarcos = false;
	    numPages = i;
	    return;
	}
	cargarPagina( i, marco );
    }
    DEBUG('a', "Espacio cargado en %d paginas, quedan %d marcos libres\n",
          numPages, machine->marcosLibres());
#endif
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace( AddrSpace * padre )
//	Espacio para un hilo creado con el Fork de usuario.  Comparte con el
//	padre el objeto "compartido" (codigo y datos) y solo tiene pila
//	propia.  Sin VM la pila se pide y se limpia aqui; con VM queda
//	invalida y entra en cero con el primer fallo.
//----------------------------------------------------------------------

AddrSpace::AddrSpace(AddrSpace *padre)
{
    numPages = padre->numPages;
    paginasPila = padre->paginasPila;
    primeraDePila = padre->primeraDePila;
    compartido = padre->compartido;
    compartido->usos++;
    huboMarcos = true;

    pageTable = new TranslationEntry[numPages];
    for ( unsigned int i = 0; i < numPages; i++ ) {
        pageTable[ i ] = padre->pageTable[ i ];
    }

#ifdef VM
    cupoSwapPila = new int[ numPages ];
    for ( unsigned int i = 0; i < numPages; i++ ) {
        cupoSwapPila[ i ] = -1;
        if ( i >= primeraDePila ) {
            pageTable[ i ].valid = false;
            pageTable[ i ].physicalPage = -1;
            pageTable[ i ].use = false;
            pageTable[ i ].dirty = false;
        }
    }
    DEBUG('v', "Espacio de Fork: comparte %d paginas, pila nueva sin cargar\n", primeraDePila);
#else
    for ( unsigned int i = primeraDePila; i < numPages; i++ ) {
        int marco = machine->pedirMarco();
        if ( marco == -1 ) {
            DEBUG('a', "Fork: sin marcos para la pila en la pagina %d\n", i);
            huboMarcos = false;
            for ( unsigned int j = primeraDePila; j < i; j++ ) {
                machine->soltarMarco( pageTable[ j ].physicalPage );
            }
            numPages = primeraDePila;
            paginasPila = 0;
            return;
        }
        pageTable[ i ].physicalPage = marco;
        pageTable[ i ].valid = true;
        pageTable[ i ].use = false;
        pageTable[ i ].dirty = false;
        bzero( machine->mainMemory + marco * PageSize, PageSize );
    }
    DEBUG('a', "Espacio de Fork: %d paginas compartidas, %d de pila nuevas\n",
          primeraDePila, paginasPila);
#endif
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Devuelve los marcos (y cupos de swap) al sistema.  La pila siempre
//	es propia; codigo y datos solo los suelta el ultimo hilo que los usa.
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
#ifdef VM
    candadoVM->Acquire();
    for ( unsigned int i = primeraDePila; i < numPages; i++ ) {
        soltarPagina( i );
    }
    delete [] cupoSwapPila;

    compartido->usos--;
    if ( compartido->usos == 0 ) {
        for ( unsigned int i = 0; i < primeraDePila; i++ ) {
            soltarPagina( i );
        }
        delete [] compartido->tabla;
        delete [] compartido->cupoSwap;
        delete compartido->ejecutable;
        delete compartido;
    }
    candadoVM->Release();
#else
    for ( unsigned int i = primeraDePila; i < numPages; i++ ) {
        machine->soltarMarco( pageTable[ i ].physicalPage );
    }
    compartido->usos--;
    if ( compartido->usos == 0 ) {
        for ( unsigned int i = 0; i < primeraDePila; i++ ) {
            machine->soltarMarco( pageTable[ i ].physicalPage );
        }
        delete compartido->ejecutable;
        delete compartido;
    }
#endif
    delete [] pageTable;
}

bool
AddrSpace::cargadoBien()
{
    return huboMarcos;
}

//----------------------------------------------------------------------
// AddrSpace::esCodigoPuro
//	true si toda la pagina cae dentro del segmento de codigo.
//----------------------------------------------------------------------

bool
AddrSpace::esCodigoPuro( unsigned int paginaVirtual )
{
    Segment * codigo = &compartido->encabezado.code;
    unsigned int inicio = paginaVirtual * PageSize;
    unsigned int fin = inicio + PageSize;
    return codigo->size > 0
        && inicio >= (unsigned int) codigo->virtualAddr
        && fin <= (unsigned int) ( codigo->virtualAddr + codigo->size );
}

//----------------------------------------------------------------------
// AddrSpace::entradaDe
//	Con VM las paginas compartidas tienen una sola entrada para todos
//	los hilos del proceso; la pila es de cada uno.
//----------------------------------------------------------------------

TranslationEntry *
AddrSpace::entradaDe( unsigned int paginaVirtual )
{
#ifdef VM
    if ( paginaVirtual < primeraDePila ) {
        return &compartido->tabla[ paginaVirtual ];
    }
#endif
    return &pageTable[ paginaVirtual ];
}

//----------------------------------------------------------------------
// AddrSpace::cargarPagina
// 	Llena el marco con lo que le toca a la pagina virtual y deja la
//	entrada valida.  Si la pagina ya estuvo en el swap, viene de ahi
//	(trae los datos tal como quedaron); si no, codigo y datos
//	inicializados se leen del ejecutable, y datos sin inicializar y pila
//	quedan en cero.  Una pagina puede tener pedazos de codigo y de
//	datos a la vez, por eso se revisan los dos segmentos.
//----------------------------------------------------------------------

void
AddrSpace::cargarPagina( unsigned int paginaVirtual, int marco )
{
    TranslationEntry * entrada = entradaDe( paginaVirtual );
    char * destino = machine->mainMemory + marco * PageSize;

    bzero( destino, PageSize );

#ifdef VM
    int * cupo = cupoDe( paginaVirtual );
    if ( *cupo != -1 ) {
        DEBUG('v', "  pagina %d: viene del swap (cupo %d) al marco %d\n",
              paginaVirtual, *cupo, marco);
        swap->leer( *cupo, destino );
    } else
#endif
    {
        if ( compartido->encabezado.code.size > 0 ) {
            copiarPedazo( paginaVirtual, marco, &compartido->encabezado.code );
        }
        if ( compartido->encabezado.initData.size > 0 ) {
            copiarPedazo( paginaVirtual, marco, &compartido->encabezado.initData );
        }
    }

    entrada->physicalPage = marco;
    entrada->valid = true;
    entrada->use = false;
    entrada->dirty = false;
}

//----------------------------------------------------------------------
// AddrSpace::copiarPedazo
//	Calcula la interseccion entre [inicio, fin) de la pagina virtual y
//	[virtualAddr, virtualAddr + size) del segmento.  Si hay algo en
//	comun, lo lee del archivo (desplazado igual que en memoria virtual)
//	directo al marco fisico.
//----------------------------------------------------------------------

void
AddrSpace::copiarPedazo( unsigned int paginaVirtual, int marco, Segment *segmento )
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

    char * destino = machine->mainMemory + marco * PageSize + ( desde - inicioPagina );
    int enArchivo = segmento->inFileAddr + ( desde - inicioSeg );

    DEBUG('a', "  pagina %d: %d bytes del archivo (pos %d) al marco %d\n",
          paginaVirtual, hasta - desde, enArchivo, marco);
    compartido->ejecutable->ReadAt( destino, hasta - desde, enArchivo );
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
//	Con VM: el hardware pone use y dirty en la entrada del TLB, no en
//	la tabla; antes de perder el TLB hay que pasarlos a la tabla.
//----------------------------------------------------------------------

void AddrSpace::SaveState() 
{
#ifdef VM
    sincronizarTLB();
#endif
}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//	Sin VM: se instala la tabla de paginas.  Con VM la maquina solo
//	mira el TLB (no puede haber tabla y TLB a la vez), asi que se vacia
//	el TLB: las entradas eran del hilo anterior.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() 
{
#ifdef VM
    for ( int i = 0; i < TLBSize; i++ ) {
        machine->tlb[ i ].valid = false;
        selloTLB[ i ] = 0;
    }
#else
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
#endif
}


#ifdef VM
//======================================================================
//  Memoria virtual
//======================================================================

void
AddrSpace::iniciarMemoriaVirtual()
{
    marcos = new TablaInvertida();
    swap = new Swap( NumPhysPages );
    candadoVM = new Lock( "memoria virtual" );
    reloj = 0;
    for ( int i = 0; i < TLBSize; i++ ) {
        selloTLB[ i ] = 0;
    }
}

void
AddrSpace::terminarMemoriaVirtual()
{
    DEBUG('v', "Al salir: swap maximo ocupado %d, %d cupos libres\n",
          swap->maximoOcupado(), swap->cuposLibres());
    delete candadoVM;
    delete swap;
    delete marcos;
}

void
AddrSpace::imprimirMemoria()
{
    marcos->imprimir();
    swap->imprimir();
    printf( "TLB:\n" );
    for ( int i = 0; i < TLBSize; i++ ) {
        if ( machine->tlb[ i ].valid ) {
            printf( "  %d: pagina %d -> marco %d  sello %lu\n", i,
                    machine->tlb[ i ].virtualPage, machine->tlb[ i ].physicalPage, selloTLB[ i ] );
        }
    }
}

int *
AddrSpace::cupoDe( unsigned int paginaVirtual )
{
    if ( paginaVirtual < primeraDePila ) {
        return &compartido->cupoSwap[ paginaVirtual ];
    }
    return &cupoSwapPila[ paginaVirtual ];
}

//----------------------------------------------------------------------
// AddrSpace::sincronizarTLB
//	Pasa use y dirty de cada entrada valida del TLB a la entrada de la
//	tabla que le corresponde.  Si la pagina fue usada desde la ultima
//	vez, se le renueva el sello LRU (al marco y a la entrada del TLB)
//	y se apaga el bit para detectar el proximo uso.  Es la unica forma
//	de saber que paginas se estan usando sin que el TLB avise en cada
//	acierto.  Solo tiene sentido para el espacio que esta corriendo.
//----------------------------------------------------------------------

void
AddrSpace::sincronizarTLB()
{
    for ( int i = 0; i < TLBSize; i++ ) {
        TranslationEntry * enTLB = &machine->tlb[ i ];
        if ( !enTLB->valid ) {
            continue;
        }
        TranslationEntry * entrada = entradaDe( enTLB->virtualPage );
        if ( enTLB->dirty ) {
            entrada->dirty = true;
        }
        if ( enTLB->use ) {
            entrada->use = true;
            reloj++;
            marcos->tocar( enTLB->physicalPage, reloj );
            selloTLB[ i ] = reloj;
            enTLB->use = false;
        }
    }
}

//----------------------------------------------------------------------
// AddrSpace::meterEnTLB
//	Pone la entrada en el TLB: en una libre si hay, si no en la de uso
//	mas viejo (LRU por sello).  Los bits de la entrada que sale ya se
//	pasaron a su tabla en sincronizarTLB.
//----------------------------------------------------------------------

void
AddrSpace::meterEnTLB( TranslationEntry * entrada )
{
    int lugar = -1;
    for ( int i = 0; i < TLBSize; i++ ) {
        if ( !machine->tlb[ i ].valid ) {
            lugar = i;
            break;
        }
        if ( lugar == -1 || selloTLB[ i ] < selloTLB[ lugar ] ) {
            lugar = i;
        }
    }
    reloj++;
    machine->tlb[ lugar ] = *entrada;
    machine->tlb[ lugar ].use = false;	// el bit de uso se mide desde ahora
    selloTLB[ lugar ] = reloj;
    marcos->tocar( entrada->physicalPage, reloj );
}

void
AddrSpace::sacarDelTLB( int marco )
{
    for ( int i = 0; i < TLBSize; i++ ) {
        if ( machine->tlb[ i ].valid && machine->tlb[ i ].physicalPage == marco ) {
            // Los bits que haya acumulado se pasan antes de perderlos
            TranslationEntry * entrada = marcos->entrada( marco );
            if ( entrada != NULL ) {
                if ( machine->tlb[ i ].dirty ) entrada->dirty = true;
                if ( machine->tlb[ i ].use ) entrada->use = true;
            }
            machine->tlb[ i ].valid = false;
            selloTLB[ i ] = 0;
        }
    }
}

//----------------------------------------------------------------------
// AddrSpace::conseguirMarco
//	Un marco libre del bitmap, o si no hay, la victima LRU de la tabla
//	invertida.  Se excluyen los marcos que el TLB tiene validos ahora:
//	son los de la instruccion en curso (codigo y, si acaso, su dato) y
//	expulsarlos solo provocaria otro fallo inmediato.  Si la victima
//	esta sucia y no es codigo puro, va al swap: a su cupo de siempre si
//	ya tenia, o a uno nuevo.  Devuelve -1 si el swap esta lleno.
//----------------------------------------------------------------------

int
AddrSpace::conseguirMarco()
{
    int marco = machine->pedirMarco();
    if ( marco != -1 ) {
        return marco;
    }

    bool excluidos[ NumPhysPages ];
    for ( int i = 0; i < NumPhysPages; i++ ) {
        excluidos[ i ] = false;
    }
    for ( int i = 0; i < TLBSize; i++ ) {
        if ( machine->tlb[ i ].valid ) {
            excluidos[ machine->tlb[ i ].physicalPage ] = true;
        }
    }
    marco = marcos->elegirVictima( excluidos );
    if ( marco == -1 ) {
        // Todo lo que hay en memoria esta en el TLB (pasa con 4 marcos):
        // se sacrifica cualquiera menos la pagina del PC actual
        unsigned int paginaPC = machine->ReadRegister( PCReg ) / PageSize;
        TranslationEntry * entradaPC = ( paginaPC < numPages ) ? entradaDe( paginaPC ) : NULL;
        for ( int i = 0; i < NumPhysPages; i++ ) {
            excluidos[ i ] = ( entradaPC != NULL && entradaPC->valid
                               && entradaPC->physicalPage == i );
        }
        marco = marcos->elegirVictima( excluidos );
    }
    ASSERT( marco != -1 );

    TranslationEntry * victima = marcos->entrada( marco );
    int * cupo = marcos->cupoSwap( marco );
    sacarDelTLB( marco );

    if ( victima->dirty && !victima->readOnly ) {
        char * pagina = machine->mainMemory + marco * PageSize;
        if ( *cupo == -1 ) {
            *cupo = swap->guardar( pagina );
            if ( *cupo == -1 ) {
                printf( "SWAP lleno: no hay donde poner la pagina %d\n", victima->virtualPage );
                return -1;
            }
        } else {
            swap->reescribir( *cupo, pagina );
        }
        DEBUG('v', "  victima: pagina %d del marco %d, sucia, al swap cupo %d\n",
              victima->virtualPage, marco, *cupo);
    } else {
        DEBUG('v', "  victima: pagina %d del marco %d, limpia%s\n",
              victima->virtualPage, marco, victima->readOnly ? " (codigo)" : "");
    }

    victima->valid = false;
    victima->physicalPage = -1;
    marcos->liberar( marco );
    return marco;
}

//----------------------------------------------------------------------
// AddrSpace::atenderFallo
//	Aqui llega cada PageFaultException.  Dos casos: la pagina esta en
//	memoria y solo falta en el TLB, o hay que traerla (fallo de pagina
//	de verdad, que es el que cuenta la estadistica).
//----------------------------------------------------------------------

bool
AddrSpace::atenderFallo( int direccionVirtual )
{
    unsigned int paginaVirtual = (unsigned int) direccionVirtual / PageSize;

    if ( paginaVirtual >= numPages ) {
        DEBUG('v', "Fallo en %d: fuera del espacio (%d paginas)\n", direccionVirtual, numPages);
        return false;
    }

    candadoVM->Acquire();
    sincronizarTLB();

    TranslationEntry * entrada = entradaDe( paginaVirtual );
    if ( !entrada->valid ) {
        stats->numPageFaults++;
        int marco = conseguirMarco();
        if ( marco == -1 ) {
            candadoVM->Release();
            return false;
        }
        cargarPagina( paginaVirtual, marco );
        reloj++;
        marcos->asignar( marco, entrada, cupoDe( paginaVirtual ), reloj );
        DEBUG('v', "Fallo real: pagina %d al marco %d (%d libres)\n",
              paginaVirtual, marco, machine->marcosLibres());
    } else {
        DEBUG('v', "Solo TLB: pagina %d ya estaba en el marco %d\n",
              paginaVirtual, entrada->physicalPage);
    }

    meterEnTLB( entrada );
    candadoVM->Release();
    return true;
}

//----------------------------------------------------------------------
// AddrSpace::soltarPagina
//	Al morir el proceso: devuelve el marco si la pagina esta en memoria
//	y el cupo de swap si lo tenia.
//----------------------------------------------------------------------

void
AddrSpace::soltarPagina( unsigned int paginaVirtual )
{
    TranslationEntry * entrada = entradaDe( paginaVirtual );
    if ( entrada->valid ) {
        sacarDelTLB( entrada->physicalPage );
        marcos->liberar( entrada->physicalPage );
        machine->soltarMarco( entrada->physicalPage );
        entrada->valid = false;
        entrada->physicalPage = -1;
    }
    int * cupo = cupoDe( paginaVirtual );
    if ( *cupo != -1 ) {
        swap->liberar( *cupo );
        *cupo = -1;
    }
}
#endif // VM
