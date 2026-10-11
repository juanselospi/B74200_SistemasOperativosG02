// tablaarchivos.h
//	Tabla de recursos abiertos de un proceso de usuario.  El programa
//	de usuario trabaja con un numero chico (OpenFileId) y aqui se guarda
//	a que descriptor de Unix corresponde y si es un archivo normal o un
//	socket, para que Read, Write y Close sepan con que estan tratando.
//
//	Los hilos que un proceso crea con Fork comparten la misma tabla; se
//	lleva la cuenta de cuantos la usan para que solo el ultimo en salir
//	cierre todo.
//
// Proyecto 2 - programas de usuario

#ifndef TABLAARCHIVOS_H
#define TABLAARCHIVOS_H

#include "bitmap.h"

#define MaxRecursosAbiertos 32

enum TipoRecurso { RECURSO_LIBRE, RECURSO_ARCHIVO, RECURSO_SOCKET };

class TablaArchivos {

public:
    TablaArchivos();
    ~TablaArchivos();

    // Registra un descriptor de Unix y devuelve el id para el usuario, -1 si no cabe
    int abrir( int descriptorUnix, TipoRecurso tipo );
    // Quita la entrada (no cierra el descriptor, eso lo hace quien llama)
    int cerrar( int idNachos );

    bool estaAbierto( int idNachos );
    bool esSocket( int idNachos );
    int descriptorUnix( int idNachos );

    // Cuantos hilos usan la tabla; quitarHilo devuelve cuantos quedan
    void agregarHilo();
    int quitarHilo();

    // Cierra en Unix todo lo que siga abierto, lo usa el ultimo hilo al salir
    void cerrarTodo();

    void imprimir();

private:
    int descriptores[ MaxRecursosAbiertos ];
    TipoRecurso tipos[ MaxRecursosAbiertos ];
    BitMap * ocupados;		// que ids estan en uso
    int hilosUsando;

};

#endif // TABLAARCHIVOS_H
