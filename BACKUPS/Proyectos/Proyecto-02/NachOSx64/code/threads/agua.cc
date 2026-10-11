#include <stdio.h>
#include "system.h"
#include "agua.h"


Agua::Agua() {

    h_counter = 0;
    o_counter = 0;

    mutex = new Semaphore("mutex", 1);

    bubble = new Semaphore("bubble", 0); // barrera

}


// hidrogeno
void Agua::H(long id) {

    mutex->P();

    h_counter++;

    // verifico si se puede formar la molecula
    if(h_counter >= 2 && o_counter >= 1) {

        bubble->V(); // H

        printf( "H %ld presente\n", id );

        bubble->V(); // H

        printf( "H %ld presente\n", id );

        bubble->V(); // O

        printf( "O %ld presente\n", id );

        // y esos atomos se consumen
        h_counter -= 2;
        o_counter -= 1;

        printf( " ...H2O formado!!!\n" );

    } 

    mutex->V();

    bubble->P(); // espero a formar molecula

}


// oxigeno
void Agua::O(long id) {

    mutex->P();

    o_counter++;

    // mismo que arriba busco suficientes para formar la H 2 0
    if(h_counter >= 2 && o_counter >= 1) {
        
        bubble->V();

        printf( "H %ld presente\n", id );

        bubble->V();

        printf( "H %ld presente\n", id );

        bubble->V();

        printf( "O %ld presente\n", id );

        h_counter -= 2;
        o_counter -= 1;

        printf(" ...H2O formado!!!\n");

    }

    mutex->V();

    bubble->P();
}
