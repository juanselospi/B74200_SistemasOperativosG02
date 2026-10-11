// agua.cc
//	Implementacion del monitor del problema del agua.  Ver agua.h.
//
//	La parte delicada es la semantica Mesa de Condition: cuando se le
//	hace Signal a un atomo dormido, otro atomo del mismo tipo que apenas
//	va llegando puede tomar el lock antes y "robarle" el cupo.  Por eso
//	los cupos no son de un hilo en particular sino de un tipo (hCupos,
//	oCupos): el que llega y encuentra cupo se lo lleva, y el que
//	despierta sin cupo simplemente se vuelve a anotar como esperando.
//	Como cada Signal corresponde a una reserva que ya lo desconto de
//	los esperando, los contadores nunca se desajustan.
//
// Tarea corta: Lock, Condition y problema del agua.

#include "agua.h"
#include "system.h"

FormadorAgua::FormadorAgua() {
    hEsperando = 0;
    oEsperando = 0;
    hCupos = 0;
    oCupos = 0;
    formando = false;
    enlazados = 0;
    moleculas = 0;

    candado = new Lock( "agua" );
    esperaH = new Condition( "esperaH" );
    esperaO = new Condition( "esperaO" );
    enlace = new Barrier( "enlace", 3 );
}


FormadorAgua::~FormadorAgua() {
    delete enlace;
    delete esperaO;
    delete esperaH;
    delete candado;
}


//----------------------------------------------------------------------
// FormadorAgua::llegaHidrogeno / llegaOxigeno
//	Las dos tienen la misma forma: mientras no haya un cupo de mi tipo
//	me anoto como esperando, reviso si con mi llegada se completa una
//	molecula, y si no me duermo.  Al despertar vuelvo a revisar porque
//	el cupo pudo haberselo llevado otro (ver comentario de arriba).
//----------------------------------------------------------------------

void FormadorAgua::llegaHidrogeno( long id ) {
    candado->Acquire();

    while ( hCupos == 0 ) {
        hEsperando++;
        intentarFormar();
        if ( hCupos == 0 ) {
            esperaH->Wait( candado );
        }
    }
    hCupos--;

    candado->Release();

    enlazar( 'H', id );
}


void FormadorAgua::llegaOxigeno( long id ) {
    candado->Acquire();

    while ( oCupos == 0 ) {
        oEsperando++;
        intentarFormar();
        if ( oCupos == 0 ) {
            esperaO->Wait( candado );
        }
    }
    oCupos--;

    candado->Release();

    enlazar( 'O', id );
}


//----------------------------------------------------------------------
// FormadorAgua::intentarFormar
//	Reserva 2 cupos de H y 1 de O si hay con quien.  Si el que llama
//	acaba de anotarse como esperando, uno de esos cupos es para el; el
//	Signal de mas que se hace no molesta porque Signal sobre una cola
//	vacia no hace nada.  Mientras "formando" sea true no se arma otra
//	molecula, asi la barrera de 3 solo junta atomos de la misma.
//----------------------------------------------------------------------

void FormadorAgua::intentarFormar() {
    if ( formando ) {
        return;
    }
    if ( hEsperando >= 2 && oEsperando >= 1 ) {
        hEsperando -= 2;
        oEsperando -= 1;
        hCupos += 2;
        oCupos += 1;
        formando = true;

        esperaH->Signal( candado );
        esperaH->Signal( candado );
        esperaO->Signal( candado );
    }
}


//----------------------------------------------------------------------
// FormadorAgua::enlazar
//	Los 3 atomos con cupo se esperan en la barrera para enlazarse
//	juntos.  El ultimo en registrarse cierra la molecula, imprime
//	quienes la formaron y revisa si ya se puede armar la siguiente
//	con los que quedaron dormidos.
//----------------------------------------------------------------------

void FormadorAgua::enlazar( char tipo, long id ) {
    printf( "   %c%ld tiene cupo y espera a sus companeros\n", tipo, id );

    enlace->Wait();

    candado->Acquire();

    miembros[ enlazados ] = tipo;
    idsMiembros[ enlazados ] = id;
    enlazados++;

    if ( enlazados == 3 ) {
        moleculas++;
        printf( "=> Molecula %d formada: %c%ld %c%ld %c%ld\n", moleculas,
                miembros[ 0 ], idsMiembros[ 0 ],
                miembros[ 1 ], idsMiembros[ 1 ],
                miembros[ 2 ], idsMiembros[ 2 ] );
        enlazados = 0;
        formando = false;
        intentarFormar();
    }

    candado->Release();
}
