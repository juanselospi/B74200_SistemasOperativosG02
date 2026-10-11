# Proyecto 2 — Memoria virtual y llamados al sistema

Resumen de lo implementado en `NachOSx64/code` para cumplir con `enunciado.html` y `Read.Me`: dar soporte de paginación a `AddrSpace` y completar los llamados al sistema de procesos, archivos y sockets.

## 1. Paso a paso de lo que se hizo

### 1.1 Preparar la memoria física paginada
- En [machine.h](../NachOSx64/code/machine/machine.h) se fijó `NumPhysPages = 32` (comentario en la línea 36 explica por qué no se subió: `ForkExec` supera la memoria disponible si se sube). `PageSize` sigue igual a `SectorSize` (128 bytes), tal como pide el enunciado.
- Se agregó un puntero `BitMap* memoryMap` a la clase `Machine` (aunque la administración real del mapa de páginas libres se centralizó en variables globales, ver siguiente punto).
- En [system.h](../NachOSx64/code/threads/system.h) y `system.cc` se declararon las estructuras globales para administrar los marcos físicos:
  - `BitMap * freePhysPages` — un bit por marco de memoria física (libre/ocupado).
  - `Lock * physPageLock` — protege el bitmap y el arreglo de referencias contra condiciones de carrera entre hilos.
  - `int * frameRefCount` — contador de referencias por marco, necesario porque con `Fork` varios hilos de un mismo proceso comparten páginas de código/datos.
  - Se inicializan en `system.cc` (`freePhysPages = new BitMap(NumPhysPages)`, etc.) al arrancar NachOS.

### 1.2 Reescribir `AddrSpace` para usar páginas (no carga secuencial)
Archivos: [addrspace.h](../NachOSx64/code/userprog/addrspace.h) y [addrspace.cc](../NachOSx64/code/userprog/addrspace.cc).

- `AllocatePhysicalPage()` / `DeallocatePhysicalPage()`: buscan (`freePhysPages->Find()`) y liberan un marco físico usando el bitmap global, bajo `physPageLock`, y llevan la cuenta de referencias en `frameRefCount`.

- `AllocatePageTable(numPages)`: crea la `TranslationEntry* pageTable` del proceso y, para cada página lógica, pide un marco físico libre (ya no asume mapeo 1:1 secuencial como el NachOS original) y lo marca `valid = true`.

      **FUNCION PROBLEMATICA!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!**
- `LoadSegment(...)`: reemplaza la carga directa del ejecutable a memoria contigua. Ahora traduce cada dirección virtual a su página física (`AddrToPhys`) y copia el segmento **página por página**, permitiendo que el código/datos de un proceso queden esparcidos en páginas físicas no contiguas.
      **!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!**

- Constructor `AddrSpace(OpenFile *executable)`: lee el header NOFF, calcula `numPages = divRoundUp(code+initData+uninitData+stack, PageSize)`, verifica que quepa en memoria (`ASSERT(nPages <= NumPhysPages)`) y carga los segmentos `code` e `initData` con `LoadSegment`.

- Constructor `AddrSpace(AddrSpace *parent)`: variante usada por `Fork` — el hilo hijo comparte las páginas de código/datos del padre (incrementando `frameRefCount`) pero obtiene páginas físicas propias para su pila, de forma que cada hilo tenga su propio stack.

- `~AddrSpace()`: libera todos los marcos físicos de la tabla de páginas del proceso (`DeallocatePhysicalPage` por cada entrada).

- Se agregó conteo de hilos por espacio de direcciones (`threadCount`, `AddThread/RemoveThread/CanDelete`) para no destruir el `AddrSpace` mientras todavía haya hilos (creados por `Fork`) usándolo.

- `ReserveTestPhysPages()` / `ReleaseTestPhysPages()`: función de prueba pedida por el enunciado ("colocar algunas páginas como ocupadas antes de la ejecución") para comprobar que el programa de usuario se carga igual en páginas no contiguas/no iniciales.

### 1.3 Tabla de archivos abiertos (recursos por proceso)
Archivos: [nachostabla.h](../NachOSx64/code/userprog/nachostabla.h) / `nachostabla.cc`.

- `NachosOpenFilesTable` mantiene un `BitMap` de slots libres y un arreglo que traduce "handle NachOS" → "handle Unix" (`open()`/`close()` reales del SO anfitrión).

- Lleva un contador de hilos (`usage`) que comparten la tabla (un proceso con varios hilos por `Fork` comparte tabla de archivos); cuando el último hilo sale, se destruye la tabla.

### 1.4 Llamados al sistema — `exception.cc`
Todo se implementó en [exception.cc](../NachOSx64/code/userprog/exception.cc), despachado desde `ExceptionHandler` según el número de syscall (registro `r2`).

**Procesos/hilos**
- `SC_Halt` → apaga la máquina (`interrupt->Halt()`).

- `SC_Exit` → marca el proceso como terminado en una tabla de procesos (`processTable`, protegida con `Semaphore joinSem` para el `Join`), libera/destruye el `AddrSpace` si ya no hay hilos usándolo, libera la tabla de archivos y termina el hilo (`currentThread->Finish()`).

- `SC_Exec` → crea una entrada en `processTable`, lanza un hilo kernel nuevo (`Thread::Fork(ExecProcess, ...)`) que abre el ejecutable, crea su `AddrSpace` y su propia tabla de archivos, y corre el programa. Devuelve un `SpaceId` (índice en la tabla de procesos) al programa que llamó a `Exec`.

- `SC_Join` → espera (`joinSem->P()`) a que el proceso hijo marque `exited = true` y devuelve su código de salida.

- `SC_Fork` → crea un hilo nuevo dentro del mismo proceso: comparte la tabla de archivos y crea un `AddrSpace` "hijo" (comparte páginas de código/datos, pila propia); el hilo arranca en la función pasada por el usuario vía `InitRegistersForFork`.

- `SC_Yield` → cede la CPU (`currentThread->Yield()`).

**Archivos**
- `SC_Create` → `fileSystem->Create`.

- `SC_Open` → abre el archivo Unix real y lo registra en `NachosOpenFilesTable`, devolviendo un `OpenFileId` de NachOS.

- `SC_Read` / `SC_Write` → distinguen `ConsoleInput/Output/Error` (usan semáforos de consola para serializar acceso) de archivos regulares (usan el handle Unix real vía `read`/`write`). Se copian los datos entre el buffer de usuario (memoria virtual del proceso) y un buffer de kernel con `machine->ReadMem`/`WriteMem`.

- `SC_Close` → cierra el descriptor tanto en la tabla NachOS como en Unix.

**Sincronización de usuario** (no pedida explícitamente en la rúbrica de puntos pero completada): `SC_SemCreate/Destroy/Signal/Wait`, `SC_LckCreate/Destroy/Acquire/Release`, `SC_CondCreate/Destroy/Signal/Wait/Broadcast` — cada uno con su propio `BitMap` de ids y arreglo de objetos (`Semaphore*`, `Lock*`, `Condition*`).

**Sockets**
- `SC_Socket` → crea un socket Unix real (`socket()`), traduciendo las constantes de NachOS (`AF_INET6_NachOS`, `SOCK_DGRAM_NachOS`) a las de Unix, y lo registra en una tabla de handles (`socketMap`/`socketHandles`).

- `SC_Connect` → resuelve la IP con `inet_pton`/`inet_addr` y llama a `connect()` sobre el socket Unix real.

- También se completaron `SC_Bind`, `SC_Listen`, `SC_Accept`, `SC_Shutdown` (no pedidos por la rúbrica, pero necesarios para que un socket sea usable de punta a punta).

### 1.5 Arranque de un proceso (`progtest.cc`)
- `StartProcess` (llamado por `-x archivo`) abre el ejecutable, crea la tabla de archivos si no existe, construye el `AddrSpace` (que ya internamente pagina y carga el programa), inicializa registros y salta a `machine->Run()`.

### 1.6 Programas de usuario de prueba (`test/`)
- Se usaron/ajustaron los programas ya provistos por NachOS (`halt`, `shell`, `sort`, `matmult`) y los pedidos por el enunciado:
  - `shell.c` — bucle que lee un comando de la consola, hace `Exec` + `Join` sobre lo que el usuario escriba.
  
  - `copy.c` — crea `nachos.2`, abre `nachos.1`, copia su contenido con `Read`/`Write`.
  
  - `todos.c` — ejecuta `copy` con `Exec`+`Join`, luego con `Fork` corre `Verifica()` que abre el archivo `nachos.1` resultante para comprobar que la copia existe.
  
  - Se agregaron programas propios adicionales de comprobación: `addrspacetest.c`, `ForkExec.c`, `SimpleFork.c`, `forksem.c`, `create.c`, `fork.c`, `socket.c`, `sort-ok.c` — usados durante el desarrollo para probar paginación, `Fork`/`Exec`/`Join` y sockets de forma aislada.

## 2. Guía corta para compilar y correr

Desde la raíz del proyecto:

```bash
cd "NachOSx64/code"
```

### 2.1 Compilar todo (threads, userprog, vm, filesys, network, herramientas y programas de test)

```bash
make all
```

Esto entra a cada subcarpeta y hace `make depend && make nachos`, construye `bin/coff2noff` y compila los programas base de `test/` (`halt`, `shell`, `matmult`, `sort`).

Si solo se quiere reconstruir el kernel de `userprog` (donde vive la paginación y los syscalls) después de editar `addrspace.cc`/`exception.cc`:

```bash
cd userprog
make depend
make nachos
```

### 2.2 Compilar un programa de usuario individual

Desde `code/test`, cualquier `programa.c` se compila con la regla genérica:

```bash
cd ../test
make halt        # o: make shell / make sort / make matmult
make todos        # ejemplo de programa nuevo (usa la regla implícita %: %.c)
make copy
```

Esto genera `programa.o`, `programa.coff` y finalmente `programa` (formato NOFF que NachOS puede cargar), vía `coff2noff`.

### 2.3 Ejecutar NachOS con un programa de usuario

El binario `nachos` queda en `code/userprog/nachos`. Se ejecuta indicando la ruta del programa NOFF con `-x`:

```bash
cd ../userprog
./nachos -x ../test/halt
```

### 2.4 Corridas específicas pedidas por el enunciado

```bash
# 1) que "halt" corra
./nachos -x ../test/halt

# 2) que "shell" corra
./nachos -x ../test/shell

# 3) preparar nachos.1 (copia del propio binario) para que "todos" -> "copy" tenga qué copiar
cp nachos nachos.1

# 4) correr "todos" (ejecuta copy internamente vía Exec/Join)
./nachos -x ../test/todos

# 5) comprobar que "copy" funciona por separado
./nachos -x ../test/copy

# 6) comprobar que nachos.2 (generado por copy) es un binario válido/idéntico
chmod u+x nachos.2
./nachos.2 -x ../test/shell

# 7) correr "todos" dentro de "shell" (shell hace Exec+Join de lo que se escriba)
./nachos -x ../test/shell
# dentro del prompt "--" que imprime shell, escribir:
../test/todos
```

### 2.5 Limpiar binarios compilados

```bash
cd "NachOSx64/code"
make clean
```
