all: Rotonda.out

Rotonda.out: Rotonda.cc
	g++ -g -pthread Rotonda.cc -o Rotonda.out

run: Rotonda.out
	./Rotonda.out

clean:
	rm -f *.o *.out
