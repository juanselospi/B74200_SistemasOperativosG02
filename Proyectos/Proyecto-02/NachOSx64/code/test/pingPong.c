#include "syscall.h"

void SimpleThread(int num);


// Fork solo esta pasando la direccion de la funcion por eso se usan wrappers para tener el argumento
void ThreadOne(void) {

    SimpleThread(1);
}


void ThreadTwo(void) {

    SimpleThread(2);
}


int main() {

    Fork(ThreadTwo);
    SimpleThread(1);

    Write("Main\n", 5, ConsoleOutput);

    Exit(0);
}


void SimpleThread(int num) {

    int i;

    if(num == 1) {

        for(i = 0; i < 5; i++) {

            Write("Hola 1\n", 7, ConsoleOutput);
            Yield();
        }

    } else {

        for(i = 0; i < 5; i++) {

            Write("Hola 2\n", 7, ConsoleOutput);
            Yield();
        }
    }

    Write("Fin de\n", 7, ConsoleOutput);
}
