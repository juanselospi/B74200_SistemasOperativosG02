/* Programa que se porta mal a proposito: escribe fuera de su espacio
 * de direcciones.  NachOS debe matar solo a este proceso, no caerse. */
#include "syscall.h"

int main() {
    int * fuera = (int *) 1000000;

    Write( "voy a tocar memoria ajena\n", 26, 1 );
    *fuera = 7;
    Write( "esto no deberia imprimirse\n", 27, 1 );
    Exit( 0 );
}
