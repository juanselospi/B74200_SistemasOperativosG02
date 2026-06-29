#ifndef NACHOSTABLA_H
#define NACHOSTABLA_H

#include "bitmap.h"

#define MAX_OPEN_FILES 128 // el maximo de archivos abiertos simultaneamente

class NachosOpenFilesTable {

  public:

    NachosOpenFilesTable(); // inicializa la tabla
    ~NachosOpenFilesTable(); // liberar la memoria de tabla

    int Open(int UnixHandle); // retorna el NachOS handle
    int Close(int NachosHandle); // retorna el unix handle
    bool isOpened(int NachosHandle);
    int getUnixHandle(int NachosHandle);

    void addThread(); // incrementar contador de hilos que se usan en esta tabla
    void delThread(); // decrementar contador y si llega a 0 cierra todo

    void Print(); // imprimir contenido

    int getUsage(){return usage;}

  private:

    int * openFiles; // vector de descriptores unix
    BitMap * openFilesMap; // bitmap para controlar slots libres
    int usage; // numero de hilos que comparten esta tabla

};

#endif
