/* Cliente de prueba para los sockets: se conecta a un servidor local
 * en el puerto 9090, manda un saludo y muestra lo que el servidor
 * contesta.  El servidor puede ser cualquier cosa que haga eco. */
#include "syscall.h"

int main() {
    char respuesta[ 64 ];
    int id, leidos, i;

    for ( i = 0; i < 64; i++ ) {
        respuesta[ i ] = 0;
    }

    id = Socket( AF_INET_NachOS, SOCK_STREAM_NachOS );
    if ( id < 0 ) {
        Write( "no pude crear el socket\n", 24, 1 );
        Exit( 1 );
    }
    if ( Connect( id, "127.0.0.1", 9090 ) < 0 ) {
        Write( "no pude conectar\n", 17, 1 );
        Exit( 2 );
    }
    Write( "hola desde nachos\n", 18, id );
    leidos = Read( respuesta, 63, id );
    Write( "el servidor dijo: ", 18, 1 );
    Write( respuesta, leidos, 1 );
    Close( id );
    Exit( 0 );
}
