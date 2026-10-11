#include "diningph.h"

DiningPh::DiningPh() {

    dp = new Lock( "dp" );

    for ( int i = 0; i < 5; i++ ) {
        self[ i ] = new Condition( "philo" );
        state[ i ] = Thinking;
    }

}


DiningPh::~DiningPh() {
    for ( int i = 0; i < 5; i++ ) {
        delete self[ i ];
    }
    delete dp;
}


void DiningPh::pickup( long who ) {

    dp->Acquire();

    state[ who ] = Hungry;
    test( who );
    // while y no if: Signal es Mesa, cuando despierto otro hilo pudo
    // correr antes que yo y no hay garantia de que siga pudiendo comer
    while ( state[ who ] == Hungry )
        self[ who ]->Wait( dp );

    dp->Release();

}


void DiningPh::putdown( long who ) {

    dp->Acquire();

    state[ who ] = Thinking;
    test( (who + 4) % 5 );
    test( (who + 1) % 5 );

    dp->Release();

}


void DiningPh::test( long i ) {

    if ( ( state[ (i + 4) % 5 ] != Eating ) && 
         ( state[ i ] == Hungry ) && 
         ( state[ (i + 1) % 5] != Eating ) ) {
        state[ i ] = Eating;
        self[ i ]->Signal( dp );

    }

}

void DiningPh::print() {

    // Leer state sin el lock daba una foto inconsistente de la mesa
    dp->Acquire();

    for ( int i = 0; i < 5; i++ ) {
        printf( "Philosopher %d is %s \n", i + 1, (state[i]==Hungry)?"Hungry":(state[i]==Thinking)?"Thinking":"Eating");
    }

    dp->Release();

}

