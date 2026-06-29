#include "syscall.h"

void hijo(int dummy);
int id;

int main() {
    id = SemCreate(0);
    Fork(hijo);
    SemWait(id);
    Write("padre", 5, ConsoleOutput);
    SemDestroy(id);
    Exit(0);
}

void hijo(int dummy) {
    (void)dummy;
    Write("hijo", 4, ConsoleOutput);
    SemSignal(id);
}
