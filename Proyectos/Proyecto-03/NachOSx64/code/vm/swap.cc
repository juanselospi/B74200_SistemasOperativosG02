// swap.cc
//	Ver swap.h.  Cada lectura y escritura se cuenta en las estadisticas
//	de disco, que es donde el enunciado las muestra.
//
// Proyecto 3 - memoria virtual

#include "swap.h"
#include "system.h"

Swap::Swap( int numMarcosFisicos ) {
    numCupos = 2 * numMarcosFisicos;
    if ( numCupos < MinimoPaginasSwap ) {
        numCupos = MinimoPaginasSwap;
    }
    cupos = new BitMap( numCupos );
    maxUsado = -1;

    // Con FILESYS_STUB esto es un archivo normal de Unix en el directorio
    // de trabajo; se borra en el destructor
    fileSystem->Remove( "SWAP" );
    if ( !fileSystem->Create( "SWAP", numCupos * PageSize ) ) {
        printf( "No se pudo crear el archivo de SWAP\n" );
        ASSERT( false );
    }
    archivo = fileSystem->Open( "SWAP" );
    ASSERT( archivo != NULL );

    DEBUG( 'v', "SWAP de %d paginas (%d marcos fisicos)\n", numCupos, numMarcosFisicos );
}


Swap::~Swap() {
    delete archivo;
    delete cupos;
    fileSystem->Remove( "SWAP" );
}


int Swap::guardar( const char * pagina ) {
    int cupo = cupos->Find();
    if ( cupo == -1 ) {
        return -1;
    }
    if ( cupo > maxUsado ) {
        maxUsado = cupo;
    }
    reescribir( cupo, pagina );
    return cupo;
}


void Swap::reescribir( int cupo, const char * pagina ) {
    ASSERT( cupo >= 0 && cupo < numCupos && cupos->Test( cupo ) );
    archivo->WriteAt( pagina, PageSize, cupo * PageSize );
    stats->numDiskWrites++;
}


void Swap::leer( int cupo, char * pagina ) {
    ASSERT( cupo >= 0 && cupo < numCupos && cupos->Test( cupo ) );
    archivo->ReadAt( pagina, PageSize, cupo * PageSize );
    stats->numDiskReads++;
}


void Swap::liberar( int cupo ) {
    ASSERT( cupo >= 0 && cupo < numCupos );
    cupos->Clear( cupo );
}


int Swap::cuposLibres() {
    return cupos->NumClear();
}


int Swap::maximoOcupado() {
    return maxUsado;
}


void Swap::imprimir() {
    printf( "SWAP: %d cupos, %d libres, maximo ocupado %d\n",
            numCupos, cupos->NumClear(), maxUsado );
}
