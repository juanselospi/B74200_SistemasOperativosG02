/**
  *  Ejemplo base para el problema de la ronda o papa caliente
  *
  *  CI-0122 Sistemas Operativos
  *  Fecha: 2026/Ago/12
  *
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

#define MaxParticipantes 10

// Variable global para el número de participantes
int participantes = MaxParticipantes;

/**
  *  Estructura para el paso de mensajes entre procesos
 **/
struct RondaPapa {
   long mtype;
   // otros elementos a definir por el estudiante
};


/**
  *  Aplica las reglas de Collatz al valor de la papa
  *
 **/
int cambiarPapa( int papa ) {

   if ( 1 == (papa & 0x1) ) {		// papa es impar
            papa = (papa << 1) + papa + 1;	// papa = papa * 2 + papa + 1
         } else {
            papa >>= 1;			// n = n / 2, utiliza corrimiento a la d
erecha, una posicion
         }

   return papa;

}


/**
  *   Código para cada participante
  *   Debe cambiar el valor de la papa y determinar si explotó
 **/
int participante( int id ) {

   _exit( 0 );	// Everything OK

}


/**
  *   Código para el invasor
  *   Manda mensajes al azar a los participantes de la ronda
  *
 **/
int invasor( int id ) {

   _exit( 0 );	// Everything OK

}


int main( int argc, char ** argv ) {
   int buzon, id, i, j, resultado;

   if ( argc > 1 ) {
      participantes = atoi( argv[ 1 ] );
   }
   if ( participantes <= 0 ) {
      participantes = MaxParticipantes;
   }

   srandom( getpid() );

   printf( "Creando una ronda de %d participantes\n", participantes );
   for ( i = 1; i <= participantes; i++ ) {
      if ( ! fork() ) {
         participante( i );
      }
   }

// El programa principal decidirá cual es el primer participante en arrancar y e
l valor inicial de la papa

// Creación del proceso invasor
   if ( ! fork() ) {
      invasor( i );
   }

// Espera que los participantes finalicen
   for ( i = 1; i <= participantes; i++ ) {
      j = wait( &resultado );
   }
   
   j = wait( &resultado );  // Espera por el invador

}
