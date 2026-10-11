BIN = bin

# Compila la version sin clases de enviar
$(BIN)/enviarSinClases: enviarSinClases.c
	gcc enviarSinClases.c -g -o $@

# Compila la version sin clases de recibir
$(BIN)/recibirSinClases: recibirSinClases.c
	gcc recibirSinClases.c -g -o $@

# Compila la version con clases de enviar
enviarConClases.o: enviarConClases.cc Buzon.h
	g++ -g -c enviarConClases.cc

recibirConClases.o: recibirConClases.cc Buzon.h
	g++ -g -c recibirConClases.cc

# Compila la clase Buzon
Buzon.o: Buzon.h Buzon.cc
	g++ -g -c Buzon.cc

# Enlaza los archivos t0-envia y Buzon para crear el ejecutable
$(BIN)/enviarConClases: enviarConClases.o Buzon.o
	g++ -g enviarConClases.o Buzon.o -o $@

# Enlaza los archivos t0-recibe y Buzon para crear el ejecutable
$(BIN)/recibirConClases: recibirConClases.o Buzon.o
	g++ -g recibirConClases.o Buzon.o -o $@

clean:
	@rm -f *.o* $(BIN)/*

