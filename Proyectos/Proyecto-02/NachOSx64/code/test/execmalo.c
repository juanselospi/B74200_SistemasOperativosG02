/* Ejecuta a "malo" y a "halt"-like hijos para ver que el padre sigue
 * vivo aunque el hijo muera por una excepcion. */
#include "syscall.h"

int main() {
    SpaceId hijo;
    int estado;

    hijo = Exec( "../test/malo" );
    estado = Join( hijo );
    Write( "el hijo malo termino con estado ", 32, 1 );
    Write( estado, 0, 2 );		/* truco de ConsoleError: imprime r4 como entero */
    Write( "el padre sigue vivo\n", 20, 1 );
    Exit( 0 );
}
