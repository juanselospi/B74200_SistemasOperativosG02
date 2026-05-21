// problema del agua H-2-O usando sincronizacion en el sistema opeartivo NACHOS

#ifndef AGUA_H
#define AGUA_H

#include "copyright.h"
#include "synch.h"


class Agua {

  public:
    Agua();

    void H(long id);
    

    void O(long id);

  private:
    int h_counter;
    int o_counter;

    Semaphore *mutex;
    Semaphore *bubble;

};

#endif