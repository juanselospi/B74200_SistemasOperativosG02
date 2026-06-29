#include <iostream>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <sys/wait.h>

using namespace std;

#define TIPO_CARRO 1
#define TIPO_FIN   2

struct mensaje {

    long tipo;
    int pid;
};

int M = 5; // cantidad de calles disponibles
int N = 7; // numero maximo de carros permitidos
int C = 15; // numero de carros para el ejercicio

vector <int>colas(M);


// cuanta los mensajes en una cola
int contarMensajes(int colaId) {

    struct msqid_ds colaElementos;

    msgctl(colaId, IPC_STAT, &colaElementos);

    return colaElementos.msg_qnum;
}


// busca la cola con mas carros dentro
int encontrarMaxQ() {

    int carrosMax = -1; // negativo por si etengo colas vacias
    int colaId = 0;

    for(int i = 0; i < M; i++) {

        int carrosS = contarMensajes(colas[i]);

        // si la cola actual es mayor la usa
        if(carrosS > carrosMax) {

            carrosMax = carrosS;
            colaId = i;
        }
    }

    return colaId;
}


// calcula el total de carros entre todas las colas
int calcularTotal() {

    int total = 0;

    for(int i = 0; i < M; i++) {

        total += contarMensajes(colas[i]);
    }

    return total;
}


void carro(int carroPID) {
    
    srand(getpid());

    mensaje msg;
    msg.tipo = TIPO_CARRO;
    msg.pid = getpid();


    int calle = rand() % M; // calle aleatoria entre el numero total

    msgsnd(colas[calle], &msg, sizeof(int), 0);


    cout << "Carro " << getpid() << " entra en calle " << calle << endl;

    while(true) {

        mensaje checkFIN; // checkeo por señal de final de ejecucion

        if(msgrcv(colas[calle], &checkFIN, sizeof(int), TIPO_FIN, IPC_NOWAIT) != -1) {

            cout << "Carro " << getpid() << " termina por señal de abortar" << endl;

            msgsnd(colas[calle], &checkFIN, sizeof(int), 0); // reenvio la señal de aborto para que otros procesos la vean

            break;
        }

        int totalCarros = calcularTotal();

        if(totalCarros >= N) {

            int maxQ = encontrarMaxQ(); // cola con mas carros

            if(maxQ == calle) {

                int cantidad = contarMensajes(colas[maxQ]);

                cout << endl << ">>> EVACUANDO LA CALLE " << maxQ << " <<<" << endl;

                // evacuacion de cola
                for(int i = 0; i < cantidad; i++) {

                    mensaje salida;

                    msgrcv(colas[maxQ], &salida, sizeof(int), TIPO_CARRO, 0);

                    cout << "Carro " << salida.pid << " pasa desde la calle " << maxQ << endl;
                }

                cout << ">>> CALLE " << maxQ << " EVACUADA <<<\n" << endl;

                // envio señal de aboraje a las demas calles
                mensaje fin;
                fin.tipo = TIPO_FIN;
                fin.pid = 0;

                for(int i = 0; i < M; i++) {

                    msgsnd(colas[i], &fin, sizeof(int), 0);
                }

                break;
            }
        }

        usleep(200000);
    }

    exit(0);
}



int main() {
    srand(time(NULL));

    // creo las colas
    for(int i = 0; i < M; i++){

        colas[i] = msgget(IPC_PRIVATE, 0666 | IPC_CREAT); // msg de creacion por cola
    }

    // inicio los procesos carros
    for(int i = 0; i < C; i++) {

        pid_t pid = fork();

        if(pid == 0) {

            carro(i);
        }

        usleep(100000);
    }

    // espero que terminen
    for(int i = 0; i < C; i++) {

        wait(NULL);
    }

    for(int i = 0; i < M; i++) {

        msgctl(colas[i], IPC_RMID, NULL); // se limpian las colas al terminar
    }

    return 0;
}

/*

PARA COMPILAR: make

PARA CORRER: make run

SALIDA ESPERADA:


 I  ~/Desktop/VSCode/Sistemas Operativos/Proyectos/Rotonda  main ?1  make                                              ✔  19:10:06 
g++ -g -pthread Rotonda.cc -o Rotonda.out
 I  ~/Desktop/VSCode/Sistemas Operativos/Proyectos/Rotonda  main ?1  make run                                          ✔  19:10:08 
./Rotonda.out
Carro 8738 entra en calle 1
Carro 8739 entra en calle 0
Carro 8740 entra en calle 2
Carro 8741 entra en calle 4
Carro 8742 entra en calle 4
Carro 8743 entra en calle 0
Carro 8744 entra en calle 4

>>> EVACUANDO LA CALLE 4 <<<
Carro 8741 pasa desde la calle 4
Carro 8742 pasa desde la calle 4
Carro 8744 pasa desde la calle 4
>>> CALLE 4 EVACUADA <<<

Carro 8739 termina por señal de abortar
Carro 8741 termina por señal de abortar
Carro 8743 termina por señal de abortar
Carro 8745 entra en calle 3
Carro 8745 termina por señal de abortar
Carro 8738 termina por señal de abortar
Carro 8740 termina por señal de abortar
Carro 8742 termina por señal de abortar
Carro 8746 entra en calle 4
Carro 8746 termina por señal de abortar
Carro 8747 entra en calle 2
Carro 8747 termina por señal de abortar
Carro 8748 entra en calle 0
Carro 8748 termina por señal de abortar
Carro 8749 entra en calle 0
Carro 8749 termina por señal de abortar
Carro 8750 entra en calle 4
Carro 8750 termina por señal de abortar
Carro 8751 entra en calle 0
Carro 8751 termina por señal de abortar
Carro 8752 entra en calle 3
Carro 8752 termina por señal de abortar

*/