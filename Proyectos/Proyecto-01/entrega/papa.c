/**
  *  Problema de la ronda o papa caliente
  *
  *  CI-0122 Sistemas Operativos
  *  Proyecto 1
  *
  *  Se arma un anillo de n procesos que se pasan una "papa" (un numero)
  *  por buzones de mensajes.  Cada participante activo le aplica Collatz;
  *  si el resultado es 1 la papa explota y ese participante sale del juego.
  *  Gana el ultimo en salir.  Aparte hay un proceso invasor que manda
  *  mensajes basura para ver si logra confundir a la ronda.
  *
  *  Uso:  ./papa [n] [v] [direccion]
  *        n = participantes, v = valor inicial de la papa,
  *        direccion = 0 horario (0 -> 1 -> 2 ...), 1 antihorario (0 -> n-1 -> ...)
  *        Lo que no se indique se genera al azar.
  *
  *  Partimos del ejemplo base papa-base.c del curso (2026/Ago/12).
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

#define MaxParticipantes 10
#define HORARIO 0
#define ANTIHORARIO 1

// Codigo con el que termina el proceso ganador, para que main sepa quien fue
#define SALIDA_GANADOR 100

// Variables globales que los hijos heredan con el fork
int participantes = MaxParticipantes;
int direccion = HORARIO;
long valorInicial = 0;
int * buzones = NULL;		// un buzon por participante, indice = id
int buzonInvasor = -1;		// por aqui main le avisa al invasor que pare

/**
  *  Estructura para el paso de mensajes entre procesos
  *
  *  Como cada participante tiene su propio buzon, mtype no se usa para
  *  escoger destinatario y siempre va en 1.  El contador de pasivos viaja
  *  dentro de la papa: asi el que explota sabe si fue el ultimo.
 **/
struct RondaPapa {
   long mtype;
   int emisor;		// id del que manda, para rechazar al invasor
   long papa;		// valor de la papa, negativo = se acabo el juego
   int pasivos;		// cuantos participantes ya salieron del juego
};

// msgsnd y msgrcv quieren el tamanno sin contar el mtype
#define TAM_MENSAJE ( sizeof( struct RondaPapa ) - sizeof( long ) )


/**
  *  Vecinos de un participante segun la direccion de giro.
  *  "anterior" es el unico que le puede mandar papas validas.
 **/
int vecinoAnterior( int id ) {
   if ( direccion == HORARIO ) {
      return ( id - 1 + participantes ) % participantes;
   }
   return ( id + 1 ) % participantes;
}

int vecinoSiguiente( int id ) {
   if ( direccion == HORARIO ) {
      return ( id + 1 ) % participantes;
   }
   return ( id - 1 + participantes ) % participantes;
}


/**
  *  Manda un mensaje al buzon de "destino".  Si falla no hay mucho que
  *  hacer mas que avisar y salir.
 **/
void enviar( int destino, struct RondaPapa * mensaje ) {
   mensaje->mtype = 1;
   if ( msgsnd( buzones[ destino ], mensaje, TAM_MENSAJE, 0 ) == -1 ) {
      perror( "msgsnd" );
      _exit( 1 );
   }
}


/**
  *  Aplica las reglas de Collatz al valor de la papa
  *
 **/
long cambiarPapa( long papa ) {

   if ( 1 == (papa & 0x1) ) {		// papa es impar
      papa = (papa << 1) + papa + 1;	// papa = papa * 2 + papa + 1
   } else {
      papa >>= 1;			// n = n / 2, utiliza corrimiento a la derecha, una posicion
   }

   return papa;

}


/**
  *  El ganador espera a que la papa negativa le de la vuelta completa.
  *  Con n = 1 se la manda a si mismo y la recibe de inmediato.
 **/
void esperarVueltaFinal( int id, int fuente ) {
   struct RondaPapa mensaje;

   while ( true ) {
      if ( msgrcv( buzones[ id ], &mensaje, TAM_MENSAJE, 0, 0 ) == -1 ) {
         _exit( 1 );
      }
      if ( mensaje.emisor == fuente && mensaje.papa < 0 ) {
         printf( "Participante %d: la papa negativa dio la vuelta, todos saben\n", id );
         return;
      }
      printf( "Participante %d: rechaza mensaje de %d con valor %ld\n",
              id, mensaje.emisor, mensaje.papa );
   }
}


/**
  *   Código para cada participante
  *   Debe cambiar el valor de la papa y determinar si explotó
  *
  *   Solo acepta papas que vengan de su vecino anterior; lo demas es del
  *   invasor y se bota.  Mientras este activo aplica Collatz; cuando le
  *   explota pasa a pasivo y solo reenvia.  Si al explotar ya salieron
  *   todos, es el ganador: manda una papa negativa para avisar que se
  *   acabo y espera a que le de la vuelta completa antes de irse, asi
  *   esta seguro de que todos la vieron.
 **/
int participante( int id ) {
   struct RondaPapa mensaje;
   int fuente = vecinoAnterior( id );
   int siguiente = vecinoSiguiente( id );
   bool activo = true;
   long nueva;

   while ( true ) {

      if ( msgrcv( buzones[ id ], &mensaje, TAM_MENSAJE, 0, 0 ) == -1 ) {
         // normalmente es porque main borro el buzon (Ctrl-C)
         _exit( 1 );
      }

      if ( mensaje.emisor != fuente ) {
         printf( "Participante %d: rechaza mensaje de %d con valor %ld\n",
                 id, mensaje.emisor, mensaje.papa );
         continue;
      }

      if ( mensaje.papa < 0 ) {
         // Se acabo el juego.  La papa negativa trae -(ganador + 1)
         printf( "Participante %d: se entera de que el juego termino, gano %ld\n",
                 id, -mensaje.papa - 1 );
         mensaje.emisor = id;
         enviar( siguiente, &mensaje );
         _exit( 0 );
      }

      if ( activo ) {
         nueva = cambiarPapa( mensaje.papa );
         printf( "Participante %d: recibe %ld y la cambia a %ld\n",
                 id, mensaje.papa, nueva );

         if ( nueva == 1 ) {
            activo = false;
            mensaje.pasivos++;
            printf( "Participante %d: le exploto la papa, sale del juego (%d de %d fuera)\n",
                    id, mensaje.pasivos, participantes );

            if ( mensaje.pasivos == participantes ) {
               printf( "Participante %d: soy el ultimo, GANE\n", id );
               mensaje.papa = -( id + 1 );
               mensaje.emisor = id;
               enviar( siguiente, &mensaje );
               esperarVueltaFinal( id, fuente );
               _exit( SALIDA_GANADOR );
            }

            nueva = 2 + random() % 1000;
            printf( "Participante %d: nueva papa %ld\n", id, nueva );
         }
         mensaje.papa = nueva;
      } else {
         printf( "Participante %d: pasivo, pasa %ld sin tocarla\n", id, mensaje.papa );
      }

      // Una pausa corta para que la ronda se vea y el invasor alcance a
      // meterse; si no, el juego entero termina en milisegundos
      usleep( 5000 + random() % 25000 );

      mensaje.emisor = id;
      enviar( siguiente, &mensaje );
   }

}


/**
  *   Código para el invasor
  *   Manda mensajes al azar a los participantes de la ronda
  *
  *   Arranca lento (asi la primera ronda es segura, como dice el
  *   enunciado) y luego manda basura a participantes al azar: valores
  *   cualquiera, incluso negativos y con el contador de pasivos alterado,
  *   para ver si logra terminar el juego antes de tiempo.  Siempre firma
  *   con su propio id, que es lo que lo delata.  Para cuando main le deja
  *   un mensaje en su buzon.
 **/
int invasor( int id ) {
   struct RondaPapa basura, orden;
   int destino, enviados = 0;

   // El struct tiene bytes de relleno entre los campos y msgsnd lo copia
   // completo; si no se limpian, valgrind acusa bytes sin inicializar
   memset( &basura, 0, sizeof( basura ) );

   usleep( 500000 + random() % 1000000 );	// entre medio segundo y segundo y medio

   while ( msgrcv( buzonInvasor, &orden, TAM_MENSAJE, 0, IPC_NOWAIT ) == -1 ) {
      destino = random() % participantes;
      basura.emisor = id;
      basura.papa = random() % 200 - 50;
      basura.pasivos = random() % ( participantes + 1 );
      enviar( destino, &basura );
      enviados++;

      usleep( 50000 + random() % 200000 );
   }

   printf( "Invasor: me dijeron que pare, mande %d mensajes basura\n", enviados );
   _exit( 0 );

}


/**
  *  Crea los buzones (uno por participante y uno para el invasor).
  *  Se hace antes de los fork para que todos hereden los identificadores.
 **/
void crearBuzones() {
   int i;

   buzones = (int *) malloc( participantes * sizeof( int ) );
   for ( i = 0; i < participantes; i++ ) {
      buzones[ i ] = msgget( IPC_PRIVATE, 0600 | IPC_CREAT );
      if ( buzones[ i ] == -1 ) {
         perror( "msgget" );
         exit( 1 );
      }
   }
   buzonInvasor = msgget( IPC_PRIVATE, 0600 | IPC_CREAT );
   if ( buzonInvasor == -1 ) {
      perror( "msgget invasor" );
      exit( 1 );
   }
}


/**
  *  Borra los buzones del sistema.  Si no se hace quedan vivos aunque el
  *  programa termine (se ven con ipcs -q).
 **/
void borrarBuzones() {
   int i;

   if ( buzones != NULL ) {
      for ( i = 0; i < participantes; i++ ) {
         msgctl( buzones[ i ], IPC_RMID, NULL );
      }
   }
   if ( buzonInvasor != -1 ) {
      msgctl( buzonInvasor, IPC_RMID, NULL );
   }
}


/**
  *  Si matan a main con Ctrl-C hay que limpiar igual.  Los hijos que esten
  *  bloqueados en msgrcv fallan solos cuando desaparece su buzon.
 **/
void atenderInterrupcion( int sennal ) {
   (void) sennal;
   printf( "\nInterrumpido, borrando buzones\n" );
   borrarBuzones();
   _exit( 1 );
}


/**
  *  Lee n, v y direccion de la linea de comandos; lo que falte o venga
  *  mal se genera al azar (o se usa el valor por defecto en el caso de n).
 **/
void leerParametros( int argc, char ** argv ) {

   if ( argc > 1 ) {
      participantes = atoi( argv[ 1 ] );
   }
   if ( participantes <= 0 ) {
      participantes = MaxParticipantes;
   }

   if ( argc > 2 ) {
      valorInicial = atol( argv[ 2 ] );
   }
   if ( valorInicial <= 0 ) {
      valorInicial = 2 + random() % 1000;
   }

   if ( argc > 3 ) {
      direccion = atoi( argv[ 3 ] );
   } else {
      direccion = random() % 2;
   }
   if ( direccion != HORARIO && direccion != ANTIHORARIO ) {
      direccion = random() % 2;
   }
}


int main( int argc, char ** argv ) {
   int id, resultado;
   pid_t pid, pidInvasor;
   pid_t * pids;
   int ganador = -1;
   struct RondaPapa inicial;

   // Que cada printf salga completo aunque la salida vaya a un archivo,
   // si no las lineas de los distintos procesos se mezclan
   setvbuf( stdout, NULL, _IOLBF, 0 );
   memset( &inicial, 0, sizeof( inicial ) );	// mismo motivo que en invasor()

   srandom( getpid() );
   leerParametros( argc, argv );

   printf( "Creando una ronda de %d participantes, papa inicial %ld, sentido %s\n",
           participantes, valorInicial,
           ( direccion == HORARIO ) ? "horario" : "antihorario" );

   crearBuzones();
   pids = (pid_t *) malloc( participantes * sizeof( pid_t ) );

   for ( id = 0; id < participantes; id++ ) {
      pid = fork();
      if ( pid == 0 ) {
         // cada hijo arranca su generador distinto, si no todos sacan lo mismo
         srandom( getpid() );
         participante( id );
      }
      pids[ id ] = pid;
   }

   // Creación del proceso invasor, su id es el siguiente al ultimo participante
   pidInvasor = fork();
   if ( pidInvasor == 0 ) {
      srandom( getpid() );
      invasor( participantes );
   }

   // Solo main limpia los buzones si lo interrumpen
   signal( SIGINT, atenderInterrupcion );

   // Main escoge al azar quien arranca y le manda la papa haciendose pasar
   // por su vecino anterior, para que la validacion del emisor sea la
   // misma desde el primer mensaje
   id = random() % participantes;
   printf( "Arranca el participante %d con la papa en %ld\n", id, valorInicial );
   inicial.emisor = vecinoAnterior( id );
   inicial.papa = valorInicial;
   inicial.pasivos = 0;
   enviar( id, &inicial );

   // Espera a cada participante por su pid; con wait() a secas el invasor
   // se puede colar en medio y el ganador quedar sin revisar
   for ( id = 0; id < participantes; id++ ) {
      waitpid( pids[ id ], &resultado, 0 );
      if ( WIFEXITED( resultado ) && WEXITSTATUS( resultado ) == SALIDA_GANADOR ) {
         ganador = id;
      }
   }

   // Ya nadie juega: le avisa al invasor que pare y lo espera
   inicial.mtype = 1;
   inicial.emisor = -1;
   inicial.papa = -1;
   if ( msgsnd( buzonInvasor, &inicial, TAM_MENSAJE, 0 ) == -1 ) {
      perror( "msgsnd invasor" );
   }
   waitpid( pidInvasor, &resultado, 0 );

   if ( ganador != -1 ) {
      printf( "La ronda termino, el ganador fue el participante %d\n", ganador );
   }

   borrarBuzones();
   free( buzones );
   free( pids );

   return 0;
}
