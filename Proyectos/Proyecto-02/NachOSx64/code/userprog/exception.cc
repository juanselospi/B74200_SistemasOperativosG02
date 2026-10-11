// exception.cc 
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.  
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.
//
// Copyright (c) -2025 Universidad de Costa Rica


#include "copyright.h"
#include "system.h"
#include "syscall.h"

#define SC_NachOS	015177

// OJO: el archivo fuente es Exception.cc (con mayuscula).  La regla
// "make depend" genera exception.cc a partir de este con un sed que
// reemplaza el 015177 de arriba.  Los cambios hechos en exception.cc se
// pierden en el siguiente "make depend".

#include "synch.h"
#include "tablaarchivos.h"
#include "tablaprocesos.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

// Mientras un hilo escribe en la consola otro no deberia meterse en
// medio, si no las salidas se mezclan letra por letra
static Lock * candadoConsola = new Lock( "consola" );

//----------------------------------------------------------------------
// returnFromSystemCall
//	Avanza los tres contadores de programa de MIPS.  Si no se hace, al
//	volver de la excepcion se vuelve a ejecutar la misma instruccion
//	syscall y el programa se queda en un ciclo infinito.
//----------------------------------------------------------------------

static void returnFromSystemCall() {
    int pc = machine->ReadRegister( PCReg );
    int siguiente = machine->ReadRegister( NextPCReg );

    machine->WriteRegister( PrevPCReg, pc );
    machine->WriteRegister( PCReg, siguiente );
    machine->WriteRegister( NextPCReg, siguiente + 4 );
}


//----------------------------------------------------------------------
// Acceso a la memoria del programa de usuario
//	Los parametros que son punteros (cadenas, buffers) traen una
//	direccion virtual del proceso; hay que pasar por ReadMem/WriteMem
//	para que la traduzca la tabla de paginas.  ReadMem solo lee
//	enteros, asi que se va byte por byte.
//----------------------------------------------------------------------

static void leerMemoriaUsuario( int direccion, int cuantos, char * destino ) {
    int valor;
    for ( int i = 0; i < cuantos; i++ ) {
        machine->ReadMem( direccion + i, 1, &valor );
        destino[ i ] = (char) valor;
    }
}

// Copia hasta encontrar el 0 final o llenar "maximo - 1"; siempre deja
// la cadena terminada en 0
static void leerCadenaUsuario( int direccion, char * destino, int maximo ) {
    int valor;
    int i = 0;
    do {
        machine->ReadMem( direccion + i, 1, &valor );
        destino[ i ] = (char) valor;
        i++;
    } while ( valor != 0 && i < maximo - 1 );
    destino[ i ] = 0;
}

static void escribirMemoriaUsuario( int direccion, int cuantos, const char * origen ) {
    for ( int i = 0; i < cuantos; i++ ) {
        machine->WriteMem( direccion + i, 1, origen[ i ] );
    }
}


/*
 *  System call interface: Halt()
 */
void NachOS_Halt() {		// System call 0

	DEBUG('a', "Shutdown, initiated by user program.\n");
   	interrupt->Halt();

}


/*
 *  System call interface: void Exit( int )
 *
 *  Libera lo que el hilo tenia: su espacio de direcciones (los marcos
 *  compartidos con otros hilos del Fork solo se van con el ultimo), la
 *  tabla de archivos si es el ultimo que la usa, y si era el hilo
 *  principal del proceso avisa a quien este en Join con el estado.
 */
static void terminarHiloActual( int estado ) {
    DEBUG( 'u', "Exit con estado %d del hilo %s\n", estado, currentThread->getName() );

    if ( currentThread->esHiloPrincipal ) {
        tablaProcesos->termino( currentThread->idProceso, estado );
    }

    // El destructor de Thread tambien borra space; se deja en NULL para
    // que no lo borre dos veces
    delete currentThread->space;
    currentThread->space = NULL;

    // La tabla de archivos es compartida: solo el ultimo hilo que la usa
    // cierra lo que quedo abierto y la borra
    if ( currentThread->archivos != NULL ) {
        if ( currentThread->archivos->quitarHilo() == 0 ) {
            currentThread->archivos->cerrarTodo();
            delete currentThread->archivos;
        }
        currentThread->archivos = NULL;
    }

    currentThread->Finish();
}

void NachOS_Exit() {		// System call 1
    terminarHiloActual( machine->ReadRegister( 4 ) );
}


//----------------------------------------------------------------------
// arrancarProceso
//	Lo primero que corre el hilo de kernel de un Exec: igual que
//	StartProcess pero el espacio ya viene cargado.
//----------------------------------------------------------------------

static void arrancarProceso( void * nada ) {
    currentThread->space->InitRegisters();
    currentThread->space->RestoreState();
    machine->Run();
    ASSERT( false );		// Run no regresa, el programa sale con Exit
}


/*
 *  System call interface: SpaceId Exec( char * )
 *
 *  Carga el ejecutable en un espacio nuevo y lo pone a correr en un hilo
 *  de kernel nuevo.  Devuelve el SpaceId para Join, o -1 si el archivo
 *  no existe, no cabe en memoria o la tabla de procesos esta llena.
 */
void NachOS_Exec() {		// System call 2
    char nombre[ 256 ];
    leerCadenaUsuario( machine->ReadRegister( 4 ), nombre, sizeof( nombre ) );
    DEBUG( 'u', "Exec %s\n", nombre );

    OpenFile * ejecutable = fileSystem->Open( nombre );
    if ( ejecutable == NULL ) {
        DEBUG( 'u', "Exec: no existe %s\n", nombre );
        machine->WriteRegister( 2, -1 );
        returnFromSystemCall();
        return;
    }

    AddrSpace * espacio = new AddrSpace( ejecutable );
    delete ejecutable;
    if ( !espacio->cargadoBien() ) {
        DEBUG( 'u', "Exec: no hay memoria para %s\n", nombre );
        delete espacio;
        machine->WriteRegister( 2, -1 );
        returnFromSystemCall();
        return;
    }

    int id = tablaProcesos->registrar( nombre );
    if ( id == -1 ) {
        delete espacio;
        machine->WriteRegister( 2, -1 );
        returnFromSystemCall();
        return;
    }

    // El nombre del hilo apunta a la copia que guarda la tabla de procesos
    Thread * hilo = new Thread( tablaProcesos->nombre( id ) );
    hilo->space = espacio;
    hilo->archivos = new TablaArchivos();
    hilo->archivos->agregarHilo();
    hilo->idProceso = id;
    hilo->esHiloPrincipal = true;

    hilo->Fork( arrancarProceso, NULL );

    machine->WriteRegister( 2, id );
    returnFromSystemCall();
}


/*
 *  System call interface: int Join( SpaceId )
 *
 *  Se duerme hasta que el proceso termine y devuelve su estado de Exit;
 *  -1 si el id no existe o es el propio.
 */
void NachOS_Join() {		// System call 3
    int id = machine->ReadRegister( 4 );
    DEBUG( 'u', "Join %d\n", id );

    if ( id == currentThread->idProceso || !tablaProcesos->existe( id ) ) {
        machine->WriteRegister( 2, -1 );
    } else {
        int estado = tablaProcesos->esperar( id );
        machine->WriteRegister( 2, estado );
    }
    returnFromSystemCall();
}


/*
 *  System call interface: void Create( char * )
 *
 *  Crea el archivo vacio (en Unix, porque estamos con FILESYS_STUB).
 *  No lo deja abierto, el usuario hace Open despues.
 */
void NachOS_Create() {		// System call 4
    char nombre[ 256 ];
    leerCadenaUsuario( machine->ReadRegister( 4 ), nombre, sizeof( nombre ) );
    DEBUG( 'u', "Create %s\n", nombre );

    if ( fileSystem->Create( nombre, 0 ) ) {
        machine->WriteRegister( 2, 0 );
    } else {
        machine->WriteRegister( 2, -1 );
    }
    returnFromSystemCall();
}


/*
 *  System call interface: OpenFileId Open( char * )
 *
 *  Abre en Unix para lectura y escritura y registra el descriptor en la
 *  tabla del proceso.  Devuelve el id de NachOS o -1.
 */
void NachOS_Open() {		// System call 5
    char nombre[ 256 ];
    leerCadenaUsuario( machine->ReadRegister( 4 ), nombre, sizeof( nombre ) );
    DEBUG( 'u', "Open %s\n", nombre );

    int descriptor = OpenForReadWrite( nombre, false );
    if ( descriptor == -1 ) {
        machine->WriteRegister( 2, -1 );
    } else {
        int id = currentThread->archivos->abrir( descriptor, RECURSO_ARCHIVO );
        if ( id == -1 ) {		// tabla llena
            close( descriptor );
        }
        machine->WriteRegister( 2, id );
    }
    returnFromSystemCall();
}


/*
 *  System call interface: void Write( char *, int, OpenFileId )
 *
 *  r4 = direccion del buffer en el usuario, r5 = cuantos bytes, r6 = archivo
 */
void NachOS_Write() {		// System call 7
    int direccion = machine->ReadRegister( 4 );
    int tamanno = machine->ReadRegister( 5 );
    OpenFileId archivo = machine->ReadRegister( 6 );
    DEBUG( 'u', "Write %d bytes al id %d\n", tamanno, archivo );

    if ( tamanno < 0 ) {
        tamanno = 0;
    }
    char * buffer = new char[ tamanno + 1 ];
    leerMemoriaUsuario( direccion, tamanno, buffer );
    // El 0 que cierra la cadena va antes del switch para que sirva en
    // todos los casos, no solo en el de la consola
    buffer[ tamanno ] = 0;

    candadoConsola->Acquire();
    switch ( archivo ) {
        case ConsoleInput:	// nadie escribe en la entrada estandar
            machine->WriteRegister( 2, -1 );
            break;
        case ConsoleOutput:
            printf( "%s", buffer );
            fflush( stdout );
            machine->WriteRegister( 2, tamanno );
            break;
        case ConsoleError:	// truco de la asistencia para sacar enteros
            printf( "%d\n", machine->ReadRegister( 4 ) );
            machine->WriteRegister( 2, tamanno );
            break;
        default: {		// archivos y sockets abiertos, segun la tabla
            if ( currentThread->archivos->estaAbierto( archivo ) ) {
                // write() sirve igual para archivo o socket; el tipo queda en
                // la tabla para quien lo necesite (Close, imprimir)
                int escritos = write( currentThread->archivos->descriptorUnix( archivo ),
                                      buffer, tamanno );
                machine->WriteRegister( 2, escritos );
            } else {
                machine->WriteRegister( 2, -1 );
            }
            break;
        }
    }
    candadoConsola->Release();

    delete [] buffer;
    returnFromSystemCall();
}


/*
 *  System call interface: int Read( char *, int, OpenFileId )
 *
 *  r4 = buffer del usuario, r5 = cuantos bytes, r6 = archivo.
 *  Devuelve los bytes leidos (0 en fin de archivo) o -1.
 */
void NachOS_Read() {		// System call 6
    int direccion = machine->ReadRegister( 4 );
    int tamanno = machine->ReadRegister( 5 );
    OpenFileId archivo = machine->ReadRegister( 6 );
    int leidos = -1;
    DEBUG( 'u', "Read %d bytes del id %d\n", tamanno, archivo );

    if ( tamanno < 0 ) {
        tamanno = 0;
    }
    char * buffer = new char[ tamanno + 1 ];

    switch ( archivo ) {
        case ConsoleInput:	// entrada estandar de Unix
            leidos = read( 0, buffer, tamanno );
            break;
        case ConsoleOutput:	// de la salida no se lee
        case ConsoleError:
            leidos = -1;
            break;
        default:
            if ( currentThread->archivos->estaAbierto( archivo ) ) {
                leidos = read( currentThread->archivos->descriptorUnix( archivo ),
                               buffer, tamanno );
            }
            break;
    }

    if ( leidos > 0 ) {
        escribirMemoriaUsuario( direccion, leidos, buffer );
    }
    machine->WriteRegister( 2, leidos );

    delete [] buffer;
    returnFromSystemCall();
}


/*
 *  System call interface: void Close( OpenFileId )
 */
void NachOS_Close() {		// System call 8
    OpenFileId archivo = machine->ReadRegister( 4 );
    DEBUG( 'u', "Close id %d\n", archivo );

    if ( currentThread->archivos->estaAbierto( archivo ) ) {
        close( currentThread->archivos->descriptorUnix( archivo ) );
        currentThread->archivos->cerrar( archivo );
        machine->WriteRegister( 2, 0 );
    } else {
        machine->WriteRegister( 2, -1 );
    }
    returnFromSystemCall();
}


//----------------------------------------------------------------------
// arrancarHilo
//	Primer codigo del hilo de kernel que atiende un Fork de usuario.
//	"p" trae la direccion de la rutina de usuario.  La direccion de
//	retorno se pone en 4: ahi esta el "jal Exit" de start.s, asi cuando
//	la rutina termina el hilo hace Exit(0) solo.
//----------------------------------------------------------------------

static void arrancarHilo( void * p ) {
    currentThread->space->InitRegisters();	// pila nueva, registros en cero
    currentThread->space->RestoreState();

    machine->WriteRegister( RetAddrReg, 4 );
    machine->WriteRegister( PCReg, (long) p );
    machine->WriteRegister( NextPCReg, (long) p + 4 );

    machine->Run();
    ASSERT( false );
}


/*
 *  System call interface: void Fork( void (*func)() )
 *
 *  Hilo nuevo dentro del mismo proceso: comparte codigo, datos y tabla
 *  de archivos con el que llama, pero tiene su propia pila.
 */
void NachOS_Fork() {		// System call 9
    long rutina = machine->ReadRegister( 4 );
    DEBUG( 'u', "Fork a la direccion %ld\n", rutina );

    AddrSpace * espacio = new AddrSpace( currentThread->space );
    if ( !espacio->cargadoBien() ) {
        DEBUG( 'u', "Fork: sin memoria para la pila del hilo nuevo\n" );
        delete espacio;
        returnFromSystemCall();
        return;
    }

    Thread * hilo = new Thread( "hilo de Fork" );
    hilo->space = espacio;
    hilo->archivos = currentThread->archivos;
    hilo->archivos->agregarHilo();
    hilo->idProceso = currentThread->idProceso;
    hilo->esHiloPrincipal = false;

    hilo->Fork( arrancarHilo, (void *) rutina );

    returnFromSystemCall();
}


/*
 *  System call interface: void Yield()
 */
void NachOS_Yield() {		// System call 10
    DEBUG( 'u', "Yield\n" );
    returnFromSystemCall();
    currentThread->Yield();
}


//----------------------------------------------------------------------
// Sincronizacion para programas de usuario
//	Los semaforos, locks y condiciones de usuario son objetos del
//	kernel guardados en tres arreglos chicos; el usuario solo ve el
//	indice.  Son globales a todo NachOS, no por proceso, igual que
//	los ids que define syscall.h (enteros sin dueno).
//----------------------------------------------------------------------

#define MaxObjetosSincro 32

static Semaphore * semaforosUsuario[ MaxObjetosSincro ];
static Lock * locksUsuario[ MaxObjetosSincro ];
static Condition * condicionesUsuario[ MaxObjetosSincro ];

static bool idSincroValido( int id ) {
    return id >= 0 && id < MaxObjetosSincro;
}

// Busca un hueco en el arreglo que se le pase (todos son de punteros)
template <class T>
static int buscarHueco( T * arreglo[] ) {
    for ( int i = 0; i < MaxObjetosSincro; i++ ) {
        if ( arreglo[ i ] == NULL ) {
            return i;
        }
    }
    return -1;
}


/*
 *  System call interface: Sem_t SemCreate( int )
 */
void NachOS_SemCreate() {		// System call 11
    int valorInicial = machine->ReadRegister( 4 );
    int id = buscarHueco( semaforosUsuario );
    DEBUG( 'u', "SemCreate(%d) -> %d\n", valorInicial, id );

    if ( id != -1 ) {
        semaforosUsuario[ id ] = new Semaphore( "semaforo de usuario", valorInicial );
    }
    machine->WriteRegister( 2, id );
    returnFromSystemCall();
}


/*
 *  System call interface: int SemDestroy( Sem_t )
 */
void NachOS_SemDestroy() {		// System call 12
    int id = machine->ReadRegister( 4 );
    DEBUG( 'u', "SemDestroy(%d)\n", id );

    if ( idSincroValido( id ) && semaforosUsuario[ id ] != NULL ) {
        delete semaforosUsuario[ id ];
        semaforosUsuario[ id ] = NULL;
        machine->WriteRegister( 2, 0 );
    } else {
        machine->WriteRegister( 2, -1 );
    }
    returnFromSystemCall();
}


/*
 *  System call interface: int SemSignal( Sem_t )
 */
void NachOS_SemSignal() {		// System call 13
    int id = machine->ReadRegister( 4 );
    DEBUG( 'u', "SemSignal(%d)\n", id );

    if ( idSincroValido( id ) && semaforosUsuario[ id ] != NULL ) {
        semaforosUsuario[ id ]->V();
        machine->WriteRegister( 2, 0 );
    } else {
        machine->WriteRegister( 2, -1 );
    }
    returnFromSystemCall();
}


/*
 *  System call interface: int SemWait( Sem_t )
 *
 *  Los PC se avanzan antes del P porque el hilo se puede dormir ahi y
 *  los registros se guardan en el cambio de contexto.
 */
void NachOS_SemWait() {		// System call 14
    int id = machine->ReadRegister( 4 );
    DEBUG( 'u', "SemWait(%d)\n", id );

    if ( idSincroValido( id ) && semaforosUsuario[ id ] != NULL ) {
        machine->WriteRegister( 2, 0 );
        returnFromSystemCall();
        semaforosUsuario[ id ]->P();
    } else {
        machine->WriteRegister( 2, -1 );
        returnFromSystemCall();
    }
}


/*
 *  System call interface: Lock_t LockCreate( int )
 */
void NachOS_LockCreate() {		// System call 15
    int id = buscarHueco( locksUsuario );
    DEBUG( 'u', "LockCreate -> %d\n", id );

    if ( id != -1 ) {
        locksUsuario[ id ] = new Lock( "lock de usuario" );
    }
    machine->WriteRegister( 2, id );
    returnFromSystemCall();
}


/*
 *  System call interface: int LockDestroy( Lock_t )
 */
void NachOS_LockDestroy() {		// System call 16
    int id = machine->ReadRegister( 4 );

    if ( idSincroValido( id ) && locksUsuario[ id ] != NULL ) {
        delete locksUsuario[ id ];
        locksUsuario[ id ] = NULL;
        machine->WriteRegister( 2, 0 );
    } else {
        machine->WriteRegister( 2, -1 );
    }
    returnFromSystemCall();
}


/*
 *  System call interface: int LockAcquire( Lock_t )
 */
void NachOS_LockAcquire() {		// System call 17
    int id = machine->ReadRegister( 4 );

    if ( idSincroValido( id ) && locksUsuario[ id ] != NULL ) {
        machine->WriteRegister( 2, 0 );
        returnFromSystemCall();
        locksUsuario[ id ]->Acquire();		// se puede dormir
    } else {
        machine->WriteRegister( 2, -1 );
        returnFromSystemCall();
    }
}


/*
 *  System call interface: int LockRelease( Lock_t )
 */
void NachOS_LockRelease() {		// System call 18
    int id = machine->ReadRegister( 4 );

    if ( idSincroValido( id ) && locksUsuario[ id ] != NULL
         && locksUsuario[ id ]->isHeldByCurrentThread() ) {
        locksUsuario[ id ]->Release();
        machine->WriteRegister( 2, 0 );
    } else {
        machine->WriteRegister( 2, -1 );
    }
    returnFromSystemCall();
}


/*
 *  System call interface: Cond_t CondCreate( int )
 */
void NachOS_CondCreate() {		// System call 19
    int id = buscarHueco( condicionesUsuario );
    DEBUG( 'u', "CondCreate -> %d\n", id );

    if ( id != -1 ) {
        condicionesUsuario[ id ] = new Condition( "condicion de usuario" );
    }
    machine->WriteRegister( 2, id );
    returnFromSystemCall();
}


/*
 *  System call interface: int CondDestroy( Cond_t )
 */
void NachOS_CondDestroy() {		// System call 20
    int id = machine->ReadRegister( 4 );

    if ( idSincroValido( id ) && condicionesUsuario[ id ] != NULL ) {
        delete condicionesUsuario[ id ];
        condicionesUsuario[ id ] = NULL;
        machine->WriteRegister( 2, 0 );
    } else {
        machine->WriteRegister( 2, -1 );
    }
    returnFromSystemCall();
}


// Las tres operaciones de condicion reciben (condicion, lock) en r4 y r5
// y necesitan que el lock exista y lo tenga el hilo que llama
static bool paresCondicionLock( int cond, int lock ) {
    return idSincroValido( cond ) && condicionesUsuario[ cond ] != NULL
        && idSincroValido( lock ) && locksUsuario[ lock ] != NULL
        && locksUsuario[ lock ]->isHeldByCurrentThread();
}


/*
 *  System call interface: int CondSignal( Cond_t, Lock_t )
 */
void NachOS_CondSignal() {		// System call 21
    int cond = machine->ReadRegister( 4 );
    int lock = machine->ReadRegister( 5 );

    if ( paresCondicionLock( cond, lock ) ) {
        condicionesUsuario[ cond ]->Signal( locksUsuario[ lock ] );
        machine->WriteRegister( 2, 0 );
    } else {
        machine->WriteRegister( 2, -1 );
    }
    returnFromSystemCall();
}


/*
 *  System call interface: int CondWait( Cond_t, Lock_t )
 */
void NachOS_CondWait() {		// System call 22
    int cond = machine->ReadRegister( 4 );
    int lock = machine->ReadRegister( 5 );

    if ( paresCondicionLock( cond, lock ) ) {
        machine->WriteRegister( 2, 0 );
        returnFromSystemCall();
        condicionesUsuario[ cond ]->Wait( locksUsuario[ lock ] );	// se duerme
    } else {
        machine->WriteRegister( 2, -1 );
        returnFromSystemCall();
    }
}


/*
 *  System call interface: int CondBroadcast( Cond_t, Lock_t )
 */
void NachOS_CondBroadcast() {		// System call 23
    int cond = machine->ReadRegister( 4 );
    int lock = machine->ReadRegister( 5 );

    if ( paresCondicionLock( cond, lock ) ) {
        condicionesUsuario[ cond ]->Broadcast( locksUsuario[ lock ] );
        machine->WriteRegister( 2, 0 );
    } else {
        machine->WriteRegister( 2, -1 );
    }
    returnFromSystemCall();
}


//----------------------------------------------------------------------
// Sockets
//	Son descriptores de Unix como cualquier archivo, por eso van en la
//	misma tabla de recursos abiertos pero marcados como RECURSO_SOCKET.
//	Con eso Read, Write y Close les sirven sin cambios.
//----------------------------------------------------------------------

/*
 *  System call interface: Socket_t Socket( int, int )
 *
 *  r4 = familia (0 IPv4, 1 IPv6), r5 = tipo (0 stream, 1 datagrama)
 */
void NachOS_Socket() {			// System call 30
    int familia = ( machine->ReadRegister( 4 ) == AF_INET6_NachOS ) ? AF_INET6 : AF_INET;
    int tipo = ( machine->ReadRegister( 5 ) == SOCK_DGRAM_NachOS ) ? SOCK_DGRAM : SOCK_STREAM;
    DEBUG( 'u', "Socket familia %d tipo %d\n", familia, tipo );

    int descriptor = socket( familia, tipo, 0 );
    if ( descriptor == -1 ) {
        machine->WriteRegister( 2, -1 );
    } else {
        int id = currentThread->archivos->abrir( descriptor, RECURSO_SOCKET );
        if ( id == -1 ) {
            close( descriptor );
        }
        machine->WriteRegister( 2, id );
    }
    returnFromSystemCall();
}


/*
 *  System call interface: int Connect( Socket_t, char *, int )
 *
 *  r4 = id del socket, r5 = cadena con la IP (numerica), r6 = puerto
 */
void NachOS_Connect() {		// System call 31
    int id = machine->ReadRegister( 4 );
    char ip[ 64 ];
    leerCadenaUsuario( machine->ReadRegister( 5 ), ip, sizeof( ip ) );
    int puerto = machine->ReadRegister( 6 );
    DEBUG( 'u', "Connect id %d a %s:%d\n", id, ip, puerto );

    int resultado = -1;
    if ( currentThread->archivos->esSocket( id ) ) {
        int descriptor = currentThread->archivos->descriptorUnix( id );

        // Se intenta IPv4 primero y si la direccion no parsea, IPv6
        struct sockaddr_in direccion4;
        struct sockaddr_in6 direccion6;
        memset( &direccion4, 0, sizeof( direccion4 ) );
        memset( &direccion6, 0, sizeof( direccion6 ) );

        if ( inet_pton( AF_INET, ip, &direccion4.sin_addr ) == 1 ) {
            direccion4.sin_family = AF_INET;
            direccion4.sin_port = htons( puerto );
            resultado = connect( descriptor, (struct sockaddr *) &direccion4, sizeof( direccion4 ) );
        } else if ( inet_pton( AF_INET6, ip, &direccion6.sin6_addr ) == 1 ) {
            direccion6.sin6_family = AF_INET6;
            direccion6.sin6_port = htons( puerto );
            resultado = connect( descriptor, (struct sockaddr *) &direccion6, sizeof( direccion6 ) );
        }
    }
    machine->WriteRegister( 2, resultado );
    returnFromSystemCall();
}


/*
 *  System call interface: int Bind( Socket_t, int )
 */
void NachOS_Bind() {		// System call 32
    int id = machine->ReadRegister( 4 );
    int puerto = machine->ReadRegister( 5 );
    int resultado = -1;

    if ( currentThread->archivos->esSocket( id ) ) {
        struct sockaddr_in direccion;
        memset( &direccion, 0, sizeof( direccion ) );
        direccion.sin_family = AF_INET;
        direccion.sin_addr.s_addr = htonl( INADDR_ANY );
        direccion.sin_port = htons( puerto );
        resultado = bind( currentThread->archivos->descriptorUnix( id ),
                          (struct sockaddr *) &direccion, sizeof( direccion ) );
    }
    machine->WriteRegister( 2, resultado );
    returnFromSystemCall();
}


/*
 *  System call interface: int Listen( Socket_t, int )
 */
void NachOS_Listen() {		// System call 33
    int id = machine->ReadRegister( 4 );
    int cola = machine->ReadRegister( 5 );
    int resultado = -1;

    if ( currentThread->archivos->esSocket( id ) ) {
        resultado = listen( currentThread->archivos->descriptorUnix( id ), cola );
    }
    machine->WriteRegister( 2, resultado );
    returnFromSystemCall();
}


/*
 *  System call interface: int Accept( Socket_t )
 *
 *  Devuelve un id nuevo en la tabla para la conexion aceptada
 */
void NachOS_Accept() {		// System call 34
    int id = machine->ReadRegister( 4 );
    int resultado = -1;

    if ( currentThread->archivos->esSocket( id ) ) {
        int nuevo = accept( currentThread->archivos->descriptorUnix( id ), NULL, NULL );
        if ( nuevo != -1 ) {
            resultado = currentThread->archivos->abrir( nuevo, RECURSO_SOCKET );
            if ( resultado == -1 ) {
                close( nuevo );
            }
        }
    }
    machine->WriteRegister( 2, resultado );
    returnFromSystemCall();
}


/*
 *  System call interface: int Shutdown( Socket_t, int )
 *
 *  modo: 0 cierra lectura, 1 escritura, 2 ambas (igual que Unix)
 */
void NachOS_Shutdown() {	// System call 35
    int id = machine->ReadRegister( 4 );
    int modo = machine->ReadRegister( 5 );
    int resultado = -1;

    if ( currentThread->archivos->esSocket( id ) ) {
        resultado = shutdown( currentThread->archivos->descriptorUnix( id ), modo );
    }
    machine->WriteRegister( 2, resultado );
    returnFromSystemCall();
}


//----------------------------------------------------------------------
// matarProceso
//	Una excepcion del programa de usuario (acceso invalido, division
//	entre cero, etc.) antes tumbaba todo NachOS con un ASSERT.  Ahora
//	se avisa y se termina solo ese hilo como si hubiera hecho Exit(-1),
//	devolviendo su memoria; los demas procesos siguen.
//----------------------------------------------------------------------

static void matarProceso( const char * motivo, ExceptionType which ) {
    printf( "Proceso \"%s\": %s (excepcion %d, PC %d). Se termina.\n",
            currentThread->getName(), motivo, which,
            machine->ReadRegister( PCReg ) );
    fflush( stdout );
    terminarHiloActual( -1 );
}


void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2) - SC_Base;

    switch ( which ) {

       case SyscallException:
          switch ( type ) {
             case SC_Halt:		// System call # 0
                NachOS_Halt();
                break;
             case SC_Exit:		// System call # 1
                NachOS_Exit();
                break;
             case SC_Exec:		// System call # 2
                NachOS_Exec();
                break;
             case SC_Join:		// System call # 3
                NachOS_Join();
                break;

             case SC_Create:		// System call # 4
                NachOS_Create();
                break;
             case SC_Open:		// System call # 5
                NachOS_Open();
                break;
             case SC_Read:		// System call # 6
                NachOS_Read();
                break;
             case SC_Write:		// System call # 7
                NachOS_Write();
                break;
             case SC_Close:		// System call # 8
                NachOS_Close();
                break;

             case SC_Fork:		// System call # 9
                NachOS_Fork();
                break;
             case SC_Yield:		// System call # 10
                NachOS_Yield();
                break;

             case SC_SemCreate:         // System call # 11
                NachOS_SemCreate();
                break;
             case SC_SemDestroy:        // System call # 12
                NachOS_SemDestroy();
                break;
             case SC_SemSignal:         // System call # 13
                NachOS_SemSignal();
                break;
             case SC_SemWait:           // System call # 14
                NachOS_SemWait();
                break;

             case SC_LckCreate:         // System call # 15
                NachOS_LockCreate();
                break;
             case SC_LckDestroy:        // System call # 16
                NachOS_LockDestroy();
                break;
             case SC_LckAcquire:         // System call # 17
                NachOS_LockAcquire();
                break;
             case SC_LckRelease:           // System call # 18
                NachOS_LockRelease();
                break;

             case SC_CondCreate:         // System call # 19
                NachOS_CondCreate();
                break;
             case SC_CondDestroy:        // System call # 20
                NachOS_CondDestroy();
                break;
             case SC_CondSignal:         // System call # 21
                NachOS_CondSignal();
                break;
             case SC_CondWait:           // System call # 22
                NachOS_CondWait();
                break;
             case SC_CondBroadcast:           // System call # 23
                NachOS_CondBroadcast();
                break;

             case SC_Socket:	// System call # 30
		NachOS_Socket();
               break;
             case SC_Connect:	// System call # 31
		NachOS_Connect();
               break;
             case SC_Bind:	// System call # 32
		NachOS_Bind();
               break;
             case SC_Listen:	// System call # 33
		NachOS_Listen();
               break;
             case SC_Accept:	// System call # 32
		NachOS_Accept();
               break;
             case SC_Shutdown:	// System call # 33
		NachOS_Shutdown();
               break;

             default:
                printf("NachOS version: %d-%d\n", (SC_Base + SC_NachOS)/10, (SC_Base + SC_NachOS)%10 );
                printf("Unexpected syscall exception %d\n", type );
                ASSERT( false );
                break;
          }
          break;

       case PageFaultException:
          // En este proyecto todas las paginas estan cargadas, esto no
          // deberia pasar; se trata igual que un acceso invalido
          matarProceso( "fallo de pagina", which );
          break;
       case ReadOnlyException:
          matarProceso( "escritura en pagina de solo lectura", which );
          break;
       case BusErrorException:
          matarProceso( "error de bus (direccion mal alineada)", which );
          break;
       case AddressErrorException:
          matarProceso( "direccion fuera del espacio del proceso", which );
          break;
       case OverflowException:
          matarProceso( "desbordamiento aritmetico", which );
          break;
       case IllegalInstrException:
          matarProceso( "instruccion ilegal", which );
          break;
       default:
          printf( "Unexpected exception %d\n", which );
          ASSERT( false );
          break;
    }

}
