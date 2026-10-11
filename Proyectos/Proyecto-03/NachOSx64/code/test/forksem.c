/* Ejemplo de la asistencia de Fork: el hijo imprime y hace Signal,
 * el padre espera en el semaforo.  Debe salir "hijo padre". */
#include "syscall.h"

void hijo( int );
int id;

int main() {
    id = SemCreate( 0 );
    Fork( hijo );

    SemWait( id );
    Write( "padre\n", 6, 1 );
    SemDestroy( id );
    Exit( 0 );
}

void hijo( int dummy ) {
    Write( "hijo ", 5, 1 );
    SemSignal( id );
}
