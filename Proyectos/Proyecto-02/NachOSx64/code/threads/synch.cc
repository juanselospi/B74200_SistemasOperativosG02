// synch.cc 
//	Routines for synchronizing threads.  Three kinds of
//	synchronization routines are defined here: semaphores, locks 
//   	and condition variables (the implementation of the last two
//	are left to the reader).
//
// Any implementation of a synchronization routine needs some
// primitive atomic operation.  We assume Nachos is running on
// a uniprocessor, and thus atomicity can be provided by
// turning off interrupts.  While interrupts are disabled, no
// context switch can occur, and thus the current thread is guaranteed
// to hold the CPU throughout, until interrupts are reenabled.
//
// Because some of these routines might be called with interrupts
// already disabled (Semaphore::V for one), instead of turning
// on interrupts at the end of the atomic operation, we always simply
// re-set the interrupt state back to its original value (whether
// that be disabled or enabled).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "synch.h"
#include "system.h"

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	Initialize a semaphore, so that it can be used for synchronization.
//
//	"debugName" is an arbitrary name, useful for debugging.
//	"initialValue" is the initial value of the semaphore.
//----------------------------------------------------------------------

Semaphore::Semaphore(const char* debugName, int initialValue)
{
    name = (char *)debugName;
    value = initialValue;
    queue = new List<Thread*>;
}

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	De-allocate semaphore, when no longer needed.  Assume no one
//	is still waiting on the semaphore!
//----------------------------------------------------------------------

Semaphore::~Semaphore()
{
    delete queue;
}

//----------------------------------------------------------------------
// Semaphore::P
// 	Wait until semaphore value > 0, then decrement.  Checking the
//	value and decrementing must be done atomically, so we
//	need to disable interrupts before checking the value.
//
//	Note that Thread::Sleep assumes that interrupts are disabled
//	when it is called.
//----------------------------------------------------------------------

void
Semaphore::P()
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);	// disable interrupts
    
    while (value == 0) { 			// semaphore not available
	queue->Append(currentThread);		// so go to sleep
	currentThread->Sleep();
    } 
    value--; 					// semaphore available, 
						// consume its value
    
    interrupt->SetLevel(oldLevel);		// re-enable interrupts
}

//----------------------------------------------------------------------
// Semaphore::V
// 	Increment semaphore value, waking up a waiter if necessary.
//	As with P(), this operation must be atomic, so we need to disable
//	interrupts.  Scheduler::ReadyToRun() assumes that threads
//	are disabled when it is called.
//----------------------------------------------------------------------

void
Semaphore::V()
{
    Thread *thread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    thread = queue->Remove();
    if (thread != NULL)	   // make thread ready, consuming the V immediately
	scheduler->ReadyToRun(thread);
    value++;
    interrupt->SetLevel(oldLevel);
}

#ifdef USER_PROGRAM
//----------------------------------------------------------------------
// Semaphore::Destroy
// 	Destroy the semaphore, freeing the waiting threads
//	This is used to destroy a user semaphore
//----------------------------------------------------------------------

void
Semaphore::Destroy()
{
    Thread *thread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    while ( (thread = queue->Remove() ) != NULL )	// make thread ready
	scheduler->ReadyToRun(thread);

    interrupt->SetLevel(oldLevel);
}

#endif


//----------------------------------------------------------------------
// Lock
//	Un lock es basicamente un semaforo binario, pero con un duenno:
//	solo el hilo que hizo Acquire puede hacer Release.  Lo implemento
//	con la misma idea que Semaphore::P y V: apago interrupciones para
//	que revisar el estado y dormirse sea atomico, y guardo los hilos
//	que esperan en una cola propia.
//----------------------------------------------------------------------

Lock::Lock(const char* debugName) {
    name = (char *) debugName;
    ocupado = false;
    duenno = NULL;
    colaEspera = new List<Thread*>;
}


Lock::~Lock() {
    delete colaEspera;
}


//----------------------------------------------------------------------
// Lock::Acquire
//	Espera hasta que el lock este libre y lo toma.  Uso un while y no
//	un if porque cuando me despiertan solo me pasan a la cola de
//	listos; para cuando vuelva a correr otro hilo pudo tomar el lock.
//----------------------------------------------------------------------

void Lock::Acquire() {
    IntStatus nivelAnterior = interrupt->SetLevel( IntOff );

    // Pedir dos veces el mismo lock sin soltarlo es un bug de quien lo usa
    ASSERT( !isHeldByCurrentThread() );

    while ( ocupado ) {
        colaEspera->Append( currentThread );
        currentThread->Sleep();
    }
    ocupado = true;
    duenno = currentThread;

    interrupt->SetLevel( nivelAnterior );
}


//----------------------------------------------------------------------
// Lock::Release
//	Suelta el lock y despierta al primero que estaba esperando.
//	No fuerzo IntOn al final sino que restauro el nivel que habia,
//	porque Condition::Wait me llama con las interrupciones apagadas.
//----------------------------------------------------------------------

void Lock::Release() {
    IntStatus nivelAnterior = interrupt->SetLevel( IntOff );

    ASSERT( isHeldByCurrentThread() );

    Thread* siguiente = colaEspera->Remove();
    if ( siguiente != NULL ) {
        scheduler->ReadyToRun( siguiente );
    }
    ocupado = false;
    duenno = NULL;

    interrupt->SetLevel( nivelAnterior );
}


bool Lock::isHeldByCurrentThread() {
    return ( ocupado && duenno == currentThread );
}


//----------------------------------------------------------------------
// Condition
//	Variable de condicion con semantica Mesa: Signal solo pone al hilo
//	en la cola de listos, no le cede el lock.  Por eso quien usa Wait
//	tiene que volver a revisar su condicion en un while.
//----------------------------------------------------------------------

Condition::Condition(const char* debugName) {
    name = (char *) debugName;
    colaEspera = new List<Thread*>;
}


Condition::~Condition() {
    delete colaEspera;
}


//----------------------------------------------------------------------
// Condition::Wait
//	Suelta el lock y se duerme, de forma atomica.  Si soltara el lock
//	con interrupciones encendidas, otro hilo podria hacer Signal entre
//	el Release y el Sleep y ese Signal se perderia.
//----------------------------------------------------------------------

void Condition::Wait( Lock * conditionLock ) {
    ASSERT( conditionLock->isHeldByCurrentThread() );

    IntStatus nivelAnterior = interrupt->SetLevel( IntOff );

    colaEspera->Append( currentThread );
    conditionLock->Release();
    currentThread->Sleep();

    // Al despertar tengo que recuperar el lock antes de volver
    conditionLock->Acquire();

    interrupt->SetLevel( nivelAnterior );
}


void Condition::Signal( Lock * conditionLock ) {
    ASSERT( conditionLock->isHeldByCurrentThread() );

    IntStatus nivelAnterior = interrupt->SetLevel( IntOff );

    Thread* despertado = colaEspera->Remove();
    if ( despertado != NULL ) {
        scheduler->ReadyToRun( despertado );
    }

    interrupt->SetLevel( nivelAnterior );
}


void Condition::Broadcast( Lock * conditionLock ) {
    ASSERT( conditionLock->isHeldByCurrentThread() );

    IntStatus nivelAnterior = interrupt->SetLevel( IntOff );

    Thread* despertado;
    while ( ( despertado = colaEspera->Remove() ) != NULL ) {
        scheduler->ReadyToRun( despertado );
    }

    interrupt->SetLevel( nivelAnterior );
}


//----------------------------------------------------------------------
// Mutex
//	Lock sin duenno: cualquier hilo puede hacer Unlock.  Como no hay
//	que verificar quien lo tiene, un semaforo en 1 resuelve todo.
//----------------------------------------------------------------------

Mutex::Mutex( const char * debugName ) {
    name = (char *) debugName;
    turno = new Semaphore( debugName, 1 );
}

Mutex::~Mutex() {
    delete turno;
}

void Mutex::Lock() {
    turno->P();
}

void Mutex::Unlock() {
    turno->V();
}


//----------------------------------------------------------------------
// Barrier
//	Los primeros "cupo - 1" hilos que llegan se duermen; el ultimo
//	abre la barrera con un Broadcast.  Guardo el numero de ronda para
//	que la barrera sea reutilizable: un hilo que despierta compara la
//	ronda en la que entro con la actual, asi un hilo rapido que ya va
//	en la siguiente ronda no se cuela por una barrera recien abierta.
//----------------------------------------------------------------------

Barrier::Barrier( const char * debugName, int count ) {
    name = (char *) debugName;
    cupo = count;
    llegados = 0;
    ronda = 0;
    candado = new ::Lock( debugName );
    todosLlegaron = new Condition( debugName );
}

Barrier::~Barrier() {
    delete todosLlegaron;
    delete candado;
}

void Barrier::Wait() {
    candado->Acquire();

    int miRonda = ronda;
    llegados++;

    if ( llegados == cupo ) {
        // Soy el ultimo: reinicio para la proxima ronda y suelto a todos
        llegados = 0;
        ronda++;
        todosLlegaron->Broadcast( candado );
    } else {
        while ( miRonda == ronda ) {
            todosLlegaron->Wait( candado );
        }
    }

    candado->Release();
}
