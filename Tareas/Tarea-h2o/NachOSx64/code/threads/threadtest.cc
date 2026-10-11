// threadtest.cc 
//	Simple test case for the threads assignment.
//
//	Create several threads, and have them context switch
//	back and forth between themselves by calling Thread::Yield, 
//	to illustrate the inner workings of the thread system.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.
//

#include <unistd.h>
#include "copyright.h"
#include "system.h"
#include "diningph.h"
#include "agua.h"

DiningPh * dp;

//----------------------------------------------------------------------
// Pausa
//	Reemplaza los sleep() que traia el ejemplo.  sleep() duerme el
//	proceso entero de NachOS, no solo al hilo, asi que no servia para
//	probar concurrencia y ademas hacia la corrida eterna.  Ceder el CPU
//	varias veces si deja que los otros hilos se metan en medio.
//----------------------------------------------------------------------

static void Pausa( int veces ) {
    for ( int i = 0; i < veces; i++ ) {
        currentThread->Yield();
    }
}


void Philo( void * p ) {

    int eats, thinks;
    long who = (long) p;

    currentThread->Yield();

    for ( int i = 0; i < 10; i++ ) {

        printf(" Philosopher %ld will try to pickup sticks\n", who + 1);

        dp->pickup( who );
        dp->print();
        eats = Random() % 6;
        Pausa( eats );

        dp->putdown( who );

        thinks = Random() % 6;
        Pausa( thinks );
    }

    printf(" Philosopher %ld is done\n", who + 1);

}


//----------------------------------------------------------------------
// PruebaFilosofos
//	Lanza los 5 filosofos sobre la misma mesa.  El hilo main no come,
//	solo los crea; cuando termina ThreadTest, main hace Finish y los
//	demas siguen corriendo hasta acabar.
//----------------------------------------------------------------------

void PruebaFilosofos() {

    dp = new DiningPh();

    for ( long k = 0; k < 5; k++ ) {
        Thread * Ph = new Thread( "filosofo" );
        Ph->Fork( Philo, (void *) k );
    }

}


//----------------------------------------------------------------------
// Problema del agua
//	10 hidrogenos y 5 oxigenos llegan en orden revuelto y con una
//	pausa aleatoria antes de llegar, para que no entren siempre en el
//	mismo orden.  Deben salir exactamente 5 moleculas de 2 H y 1 O.
//----------------------------------------------------------------------

FormadorAgua * agua;

void Hidrogeno( void * p ) {
    long id = (long) p;
    Pausa( Random() % 8 );
    printf( " H%ld llega\n", id );
    agua->llegaHidrogeno( id );
    printf( " H%ld se fue enlazado\n", id );
}


void Oxigeno( void * p ) {
    long id = (long) p;
    Pausa( Random() % 8 );
    printf( " O%ld llega\n", id );
    agua->llegaOxigeno( id );
    printf( " O%ld se fue enlazado\n", id );
}


void PruebaAgua() {

    agua = new FormadorAgua();

    // 'H' u 'O' segun lo que se crea en cada posicion: 10 H y 5 O revueltos
    const char * orden = "HHOHOHHHOOHHOHH";
    long numH = 0, numO = 0;

    for ( int i = 0; orden[ i ] != 0; i++ ) {
        Thread * t;
        if ( orden[ i ] == 'H' ) {
            t = new Thread( "hidrogeno" );
            t->Fork( Hidrogeno, (void *) numH++ );
        } else {
            t = new Thread( "oxigeno" );
            t->Fork( Oxigeno, (void *) numO++ );
        }
    }

}


//----------------------------------------------------------------------
// SimpleThread
// 	Loop 10 times, yielding the CPU to another ready thread 
//	each iteration.
//
//	"name" points to a string with a thread name, just for
//      debugging purposes.
//----------------------------------------------------------------------

void
SimpleThread(void* name)
{
    // Reinterpret arg "name" as a string
    char* threadName = (char*)name;
    
    // If the lines dealing with interrupts are commented,
    // the code will behave incorrectly, because
    // printf execution may cause race conditions.
    for (int num = 0; num < 10; num++) {
        //IntStatus oldLevel = interrupt->SetLevel(IntOff);
	printf("*** thread %s looped %d times\n", threadName, num);
	//interrupt->SetLevel(oldLevel);
        //currentThread->Yield();
    }
    //IntStatus oldLevel = interrupt->SetLevel(IntOff);
    printf(">>> Thread %s has finished\n", threadName);
    //interrupt->SetLevel(oldLevel);
}



//----------------------------------------------------------------------
// ThreadTest
//	Elige que prueba correr.  Las dos pueden correr a la vez porque
//	cada una usa su propio monitor; solo se mezclan las impresiones.
//----------------------------------------------------------------------

enum { SOLO_FILOSOFOS = 1, SOLO_AGUA = 2, AMBAS = 3 };
static const int PRUEBA_A_CORRER = AMBAS;

void
ThreadTest()
{
    DEBUG('t', "Entering ThreadTest");

    if ( PRUEBA_A_CORRER & SOLO_FILOSOFOS ) {
        PruebaFilosofos();
    }
    if ( PRUEBA_A_CORRER & SOLO_AGUA ) {
        PruebaAgua();
    }
}
