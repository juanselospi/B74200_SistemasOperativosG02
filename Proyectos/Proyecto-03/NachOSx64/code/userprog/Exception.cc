// exception.cc 
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.  
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.
//
// Copyright (c) -2025 Universidad de Costa Rica


#include "copyright.h"
#include "system.h"
#include "syscall.h"

#include "nachostabla.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "addrspace.h"
#include "filesys.h"
#include "synch.h"
#include "bitmap.h"

#define SC_NachOS	12345

#define MAX_PROCESSES 32
#define MAX_USER_SEM 64
#define MAX_USER_LOCK 64
#define MAX_USER_COND 64
#define MAX_SOCKETS 64

NachosOpenFilesTable * nachosFileTable = NULL;

static Semaphore * consoleWriteSem = NULL;
static Semaphore * consoleReadSem = NULL;

static void InitConsoleSemaphores() {
   if(consoleWriteSem == NULL) {
      consoleWriteSem = new Semaphore("console write", 1);
      consoleReadSem = new Semaphore("console read", 1);
   }
}

class UserProcess {
  public:
   UserProcess() {
      thread = NULL;
      joinSem = NULL;
      exitStatus = 0;
      exited = false;
   }
   Thread *thread;
   Semaphore *joinSem;
   int exitStatus;
   bool exited;
};

static UserProcess *processTable[MAX_PROCESSES];
static BitMap *processMap = NULL;
static Semaphore *userSems[MAX_USER_SEM];
static Lock *userLocks[MAX_USER_LOCK];
static Condition *userConds[MAX_USER_COND];
static BitMap *semMap = NULL;
static BitMap *lockMap = NULL;
static BitMap *condMap = NULL;
static int socketHandles[MAX_SOCKETS];
static BitMap *socketMap = NULL;

static int OpenSocketHandle(int unixFd) {
   if(socketMap == NULL) {
      socketMap = new BitMap(MAX_SOCKETS);
      for(int i = 0; i < MAX_SOCKETS; i++)
         socketHandles[i] = -1;
   }
   int id = socketMap->Find();
   if(id == -1) {
      close(unixFd);
      return -1;
   }
   socketHandles[id] = unixFd;
   return id;
}

static int GetUnixSocket(int id) {
   if(socketMap == NULL || id < 0 || !socketMap->Test(id))
      return -1;
   return socketHandles[id];
}

static int CloseSocketHandle(int id) {
   if(socketMap == NULL || id < 0 || !socketMap->Test(id))
      return -1;
   int fd = socketHandles[id];
   socketMap->Clear(id);
   socketHandles[id] = -1;
   return fd;
}

static void InitProcessTable() {
   if(processMap == NULL) {
      processMap = new BitMap(MAX_PROCESSES);
      for(int i = 0; i < MAX_PROCESSES; i++)
         processTable[i] = NULL;
   }
}

static void ReadUserString(int userAddr, char *buf, int maxSize) {
   int i = 0;
   int val = 0;
   do {
      machine->ReadMem(userAddr + i, 1, &val);
      buf[i] = (char)val;
      i++;
   } while(val != 0 && i < maxSize - 1);
   buf[i] = '\0';
}

static void WriteUserBuffer(int userAddr, const char *buf, int size) {
   for(int i = 0; i < size; i++)
      machine->WriteMem(userAddr + i, 1, (int)buf[i]);
}

static void ReleaseFileTable() {
   if(nachosFileTable != NULL) {
      nachosFileTable->delThread();
      if(nachosFileTable->getUsage() <= 0) {
         delete nachosFileTable;
         nachosFileTable = NULL;
      }
   }
}

static void MarkProcessExited(int status) {
   InitProcessTable();
   for(int i = 0; i < MAX_PROCESSES; i++) {
      if(processMap->Test(i) && processTable[i] != NULL
         && processTable[i]->thread == currentThread) {
         processTable[i]->exitStatus = status;
         processTable[i]->exited = true;
         processTable[i]->joinSem->V();
         break;
      }
   }
}

struct ExecParams {
   int procId;
   char filename[256];
};

static void ExecProcess(void *arg) {
   ExecParams *params = (ExecParams *)arg;
   int procId = params->procId;
   char filename[256];
   strncpy(filename, params->filename, sizeof(filename) - 1);
   filename[sizeof(filename) - 1] = '\0';
   delete params;

   OpenFile *executable = fileSystem->Open(filename);
   if(executable == NULL) {
      processTable[procId]->exitStatus = -1;
      processTable[procId]->exited = true;
      processTable[procId]->joinSem->V();
      currentThread->Finish();
      return;
   }

   AddrSpace *space = new AddrSpace(executable);
   delete executable;
   currentThread->space = space;
   nachosFileTable = new NachosOpenFilesTable();
   nachosFileTable->addThread();

   space->InitRegisters();
   space->RestoreState();
   machine->Run();
   ASSERT(false);
}

static void NachosForkThread(void *arg) {
   int func = (int)(long)arg;
   AddrSpace *space = currentThread->space;
   space->InitRegistersForFork(func);
   space->RestoreState();
   machine->Run();
   ASSERT(false);
}

void returnFromSystemCall() {
   machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
   machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
   machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg) + 4);
}

void NachOS_Halt() {
   DEBUG('a', "Shutdown, initiated by user program.\n");
   interrupt->Halt();
}

void NachOS_Exit() {
   int status = machine->ReadRegister(4);
   DEBUG('a', "Exit, called with status %d\n", status);
   MarkProcessExited(status);
   if(currentThread->space != NULL) {
      currentThread->space->RemoveThread();
      if(currentThread->space->CanDelete())
         delete currentThread->space;
      currentThread->space = NULL;
   }
   ReleaseFileTable();
   currentThread->Finish();
}


void NachOS_Exec() {
   char name[256];
   ReadUserString(machine->ReadRegister(4), name, sizeof(name));
   InitProcessTable();
   int procId = processMap->Find();
   if(procId == -1) {
      machine->WriteRegister(2, -1);
      returnFromSystemCall();
      return;
   }
   processTable[procId] = new UserProcess();
   processTable[procId]->joinSem = new Semaphore("join", 0);
   processTable[procId]->exited = false;
   ExecParams *params = new ExecParams();
   params->procId = procId;
   strncpy(params->filename, name, sizeof(params->filename) - 1);
   params->filename[sizeof(params->filename) - 1] = '\0';
   Thread *t = new Thread("Exec");
   processTable[procId]->thread = t;
   t->Fork(ExecProcess, params);
   machine->WriteRegister(2, procId);
   returnFromSystemCall();
}

void NachOS_Join() {
   int procId = machine->ReadRegister(4);
   InitProcessTable();
   if(procId < 0 || !processMap->Test(procId) || processTable[procId] == NULL) {
      machine->WriteRegister(2, -1);
      returnFromSystemCall();
      return;
   }
   if(!processTable[procId]->exited)
      processTable[procId]->joinSem->P();
   int status = processTable[procId]->exitStatus;
   delete processTable[procId]->joinSem;
   delete processTable[procId];
   processTable[procId] = NULL;
   processMap->Clear(procId);
   machine->WriteRegister(2, status);
   returnFromSystemCall();
}

void NachOS_Create() {
   char name[256];
   ReadUserString(machine->ReadRegister(4), name, sizeof(name));
   bool good = fileSystem->Create(name, 0);
   machine->WriteRegister(2, good ? 0 : -1);
   returnFromSystemCall();
}

void NachOS_Open() {
   char name[256];
   ReadUserString(machine->ReadRegister(4), name, sizeof(name));
   if(nachosFileTable == NULL) {
      nachosFileTable = new NachosOpenFilesTable();
      nachosFileTable->addThread();
   }
   int UnixHandle = open(name, O_RDWR);
   if(UnixHandle == -1)
      UnixHandle = open(name, O_RDONLY);
   if(UnixHandle == -1)
      machine->WriteRegister(2, -1);
   else
      machine->WriteRegister(2, nachosFileTable->Open(UnixHandle));
   returnFromSystemCall();
}

void NachOS_Write() {
   int bufAddr = machine->ReadRegister(4);
   int size = machine->ReadRegister(5);
   OpenFileId descriptor = machine->ReadRegister(6);
   char *buffer = new char[size + 1];
   int val = 0;
   for(int i = 0; i < size; i++) {
      machine->ReadMem(bufAddr + i, 1, &val);
      buffer[i] = (char)val;
   }
   buffer[size] = '\0';
   switch(descriptor) {
      case ConsoleInput:
         machine->WriteRegister(2, -1);
         break;
      case ConsoleOutput:
         InitConsoleSemaphores();
         consoleWriteSem->P();
         printf("%s", buffer);
         stats->numConsoleCharsWritten += size;
         consoleWriteSem->V();
         machine->WriteRegister(2, size);
         break;
      case ConsoleError:
         InitConsoleSemaphores();
         consoleWriteSem->P();
         printf("%d\n", machine->ReadRegister(4));
         stats->numConsoleCharsWritten += size;
         consoleWriteSem->V();
         machine->WriteRegister(2, size);
         break;
      default:
         if(nachosFileTable == NULL || !nachosFileTable->isOpened(descriptor))
            machine->WriteRegister(2, -1);
         else {
            int UnixHandle = nachosFileTable->getUnixHandle(descriptor);
            machine->WriteRegister(2, write(UnixHandle, buffer, size));
         }
         break;
   }
   delete[] buffer;
   returnFromSystemCall();
}

void NachOS_Read() {
   int bufAddr = machine->ReadRegister(4);
   int size = machine->ReadRegister(5);
   OpenFileId descriptor = machine->ReadRegister(6);
   switch(descriptor) {
      case ConsoleInput: {
         InitConsoleSemaphores();
         int bytesRead = 0;
         while(bytesRead < size) {
            char c;
            int n;
            consoleReadSem->P();
            do { n = read(0, &c, 1); } while(n <= 0);
            stats->numConsoleCharsRead++;
            consoleReadSem->V();
            machine->WriteMem(bufAddr + bytesRead, 1, (int)c);
            bytesRead++;
         }
         machine->WriteRegister(2, bytesRead);
         break;
      }
      case ConsoleOutput:
      case ConsoleError:
         machine->WriteRegister(2, -1);
         break;
      default:
         if(nachosFileTable == NULL || !nachosFileTable->isOpened(descriptor)) {
            machine->WriteRegister(2, -1);
         } else {
            char *buffer = new char[size];
            int UnixHandle = nachosFileTable->getUnixHandle(descriptor);
            int bytesRead = read(UnixHandle, buffer, size);
            if(bytesRead > 0)
               WriteUserBuffer(bufAddr, buffer, bytesRead);
            delete[] buffer;
            machine->WriteRegister(2, bytesRead);
         }
         break;
   }
   returnFromSystemCall();
}

void NachOS_Close() {
   OpenFileId descriptor = machine->ReadRegister(4);
   if(descriptor >= ConsoleInput && descriptor <= ConsoleError) {
      machine->WriteRegister(2, 0);
      returnFromSystemCall();
      return;
   }
   if(nachosFileTable == NULL || !nachosFileTable->isOpened(descriptor))
      machine->WriteRegister(2, -1);
   else {
      int UnixHandle = nachosFileTable->Close(descriptor);
      close(UnixHandle);
      machine->WriteRegister(2, 0);
   }
   returnFromSystemCall();
}

void NachOS_Fork() {
   int func = machine->ReadRegister(4);
   Thread *newT = new Thread("child to execute fork code");
   if(nachosFileTable != NULL)
      nachosFileTable->addThread();
   newT->space = new AddrSpace(currentThread->space);
   newT->Fork(NachosForkThread, (void *)(long)func);
   returnFromSystemCall();
}

void NachOS_Yield() {
   currentThread->Yield();
   returnFromSystemCall();
}


void NachOS_SemCreate() {
   int initVal = machine->ReadRegister(4);
   if(semMap == NULL) {
      semMap = new BitMap(MAX_USER_SEM);
      for(int i = 0; i < MAX_USER_SEM; i++) userSems[i] = NULL;
   }
   int id = semMap->Find();
   if(id == -1) machine->WriteRegister(2, -1);
   else {
      userSems[id] = new Semaphore("semUser", initVal);
      machine->WriteRegister(2, id);
   }
   returnFromSystemCall();
}

void NachOS_SemDestroy() {
   int id = machine->ReadRegister(4);
   if(semMap == NULL || id < 0 || !semMap->Test(id) || userSems[id] == NULL)
      machine->WriteRegister(2, -1);
   else {
      userSems[id]->Destroy();
      delete userSems[id];
      userSems[id] = NULL;
      semMap->Clear(id);
      machine->WriteRegister(2, 0);
   }
   returnFromSystemCall();
}

void NachOS_SemSignal() {
   int id = machine->ReadRegister(4);
   if(semMap == NULL || id < 0 || !semMap->Test(id) || userSems[id] == NULL)
      machine->WriteRegister(2, -1);
   else {
      userSems[id]->V();
      machine->WriteRegister(2, 0);
   }
   returnFromSystemCall();
}

void NachOS_SemWait() {
   int id = machine->ReadRegister(4);
   if(semMap == NULL || id < 0 || !semMap->Test(id) || userSems[id] == NULL)
      machine->WriteRegister(2, -1);
   else {
      userSems[id]->P();
      machine->WriteRegister(2, 0);
   }
   returnFromSystemCall();
}

void NachOS_LockCreate() {
   if(lockMap == NULL) {
      lockMap = new BitMap(MAX_USER_LOCK);
      for(int i = 0; i < MAX_USER_LOCK; i++) userLocks[i] = NULL;
   }
   int id = lockMap->Find();
   if(id == -1) machine->WriteRegister(2, -1);
   else {
      userLocks[id] = new Lock("lockUser");
      machine->WriteRegister(2, id);
   }
   returnFromSystemCall();
}

void NachOS_LockDestroy() {
   int id = machine->ReadRegister(4);
   if(lockMap == NULL || id < 0 || !lockMap->Test(id) || userLocks[id] == NULL)
      machine->WriteRegister(2, -1);
   else {
      delete userLocks[id];
      userLocks[id] = NULL;
      lockMap->Clear(id);
      machine->WriteRegister(2, 0);
   }
   returnFromSystemCall();
}

void NachOS_LockAcquire() {
   int id = machine->ReadRegister(4);
   if(lockMap == NULL || id < 0 || !lockMap->Test(id) || userLocks[id] == NULL)
      machine->WriteRegister(2, -1);
   else {
      userLocks[id]->Acquire();
      machine->WriteRegister(2, 0);
   }
   returnFromSystemCall();
}

void NachOS_LockRelease() {
   int id = machine->ReadRegister(4);
   if(lockMap == NULL || id < 0 || !lockMap->Test(id) || userLocks[id] == NULL)
      machine->WriteRegister(2, -1);
   else {
      userLocks[id]->Release();
      machine->WriteRegister(2, 0);
   }
   returnFromSystemCall();
}

void NachOS_CondCreate() {
   if(condMap == NULL) {
      condMap = new BitMap(MAX_USER_COND);
      for(int i = 0; i < MAX_USER_COND; i++) userConds[i] = NULL;
   }
   int id = condMap->Find();
   if(id == -1) machine->WriteRegister(2, -1);
   else {
      userConds[id] = new Condition("userCond");
      machine->WriteRegister(2, id);
   }
   returnFromSystemCall();
}

void NachOS_CondDestroy() {
   int id = machine->ReadRegister(4);
   if(condMap == NULL || id < 0 || !condMap->Test(id) || userConds[id] == NULL)
      machine->WriteRegister(2, -1);
   else {
      delete userConds[id];
      userConds[id] = NULL;
      condMap->Clear(id);
      machine->WriteRegister(2, 0);
   }
   returnFromSystemCall();
}

void NachOS_CondSignal() {
   int condId = machine->ReadRegister(4);
   int lockId = machine->ReadRegister(5);
   if(condMap == NULL || lockMap == NULL || condId < 0 || !condMap->Test(condId)
      || userConds[condId] == NULL || lockId < 0 || !lockMap->Test(lockId)
      || userLocks[lockId] == NULL)
      machine->WriteRegister(2, -1);
   else {
      userConds[condId]->Signal(userLocks[lockId]);
      machine->WriteRegister(2, 0);
   }
   returnFromSystemCall();
}

void NachOS_CondWait() {
   int condId = machine->ReadRegister(4);
   int lockId = machine->ReadRegister(5);
   if(condMap == NULL || lockMap == NULL || condId < 0 || !condMap->Test(condId)
      || userConds[condId] == NULL || lockId < 0 || !lockMap->Test(lockId)
      || userLocks[lockId] == NULL)
      machine->WriteRegister(2, -1);
   else {
      userConds[condId]->Wait(userLocks[lockId]);
      machine->WriteRegister(2, 0);
   }
   returnFromSystemCall();
}

void NachOS_CondBroadcast() {
   int condId = machine->ReadRegister(4);
   int lockId = machine->ReadRegister(5);
   if(condMap == NULL || lockMap == NULL || condId < 0 || !condMap->Test(condId)
      || userConds[condId] == NULL || lockId < 0 || !lockMap->Test(lockId)
      || userLocks[lockId] == NULL)
      machine->WriteRegister(2, -1);
   else {
      userConds[condId]->Broadcast(userLocks[lockId]);
      machine->WriteRegister(2, 0);
   }
   returnFromSystemCall();
}

void NachOS_Socket() {
   int family = machine->ReadRegister(4);
   int type = machine->ReadRegister(5);
   int af = (family == AF_INET6_NachOS) ? AF_INET6 : AF_INET;
   int st = (type == SOCK_DGRAM_NachOS) ? SOCK_DGRAM : SOCK_STREAM;
   int unixFd = socket(af, st, 0);
   if(unixFd < 0) machine->WriteRegister(2, -1);
   else machine->WriteRegister(2, OpenSocketHandle(unixFd));
   returnFromSystemCall();
}

void NachOS_Connect() {
   int sockId = machine->ReadRegister(4);
   char ip[256];
   ReadUserString(machine->ReadRegister(5), ip, sizeof(ip));
   int port = machine->ReadRegister(6);
   int unixFd = GetUnixSocket(sockId);
   if(unixFd < 0) {
      machine->WriteRegister(2, -1);
      returnFromSystemCall();
      return;
   }
   struct sockaddr_in addr;
   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_port = htons((unsigned short)port);
   if(inet_pton(AF_INET, ip, &addr.sin_addr) <= 0)
      addr.sin_addr.s_addr = inet_addr(ip);
   machine->WriteRegister(2, connect(unixFd, (struct sockaddr *)&addr, sizeof(addr)));
   returnFromSystemCall();
}

void NachOS_Bind() {
   int sockId = machine->ReadRegister(4);
   int port = machine->ReadRegister(5);
   int unixFd = GetUnixSocket(sockId);
   if(unixFd < 0) {
      machine->WriteRegister(2, -1);
      returnFromSystemCall();
      return;
   }
   struct sockaddr_in addr;
   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons((unsigned short)port);
   machine->WriteRegister(2, bind(unixFd, (struct sockaddr *)&addr, sizeof(addr)));
   returnFromSystemCall();
}

void NachOS_Listen() {
   int sockId = machine->ReadRegister(4);
   int backlog = machine->ReadRegister(5);
   int unixFd = GetUnixSocket(sockId);
   if(unixFd < 0) machine->WriteRegister(2, -1);
   else machine->WriteRegister(2, listen(unixFd, backlog));
   returnFromSystemCall();
}

void NachOS_Accept() {
   int sockId = machine->ReadRegister(4);
   int unixFd = GetUnixSocket(sockId);
   if(unixFd < 0) {
      machine->WriteRegister(2, -1);
      returnFromSystemCall();
      return;
   }
   struct sockaddr_in addr;
   socklen_t len = sizeof(addr);
   int newFd = accept(unixFd, (struct sockaddr *)&addr, &len);
   if(newFd < 0) machine->WriteRegister(2, -1);
   else machine->WriteRegister(2, OpenSocketHandle(newFd));
   returnFromSystemCall();
}

void NachOS_Shutdown() {
   int sockId = machine->ReadRegister(4);
   int mode = machine->ReadRegister(5);
   int how = SHUT_RDWR;
   if(mode == 0) how = SHUT_RD;
   else if(mode == 1) how = SHUT_WR;
   int unixFd = CloseSocketHandle(sockId);
   if(unixFd < 0) machine->WriteRegister(2, -1);
   else {
      machine->WriteRegister(2, shutdown(unixFd, how));
      close(unixFd);
   }
   returnFromSystemCall();
}


//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2. 
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions 
//	are in machine.h.
//----------------------------------------------------------------------

void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2) - SC_Base;

    switch ( which ) {

       case SyscallException:
          switch ( type ) {
             case SC_Halt:		// System call # 0
                NachOS_Halt();
                break;
             case SC_Exit:		// System call # 1
                NachOS_Exit();
                break;
             case SC_Exec:		// System call # 2
                NachOS_Exec();
                break;
             case SC_Join:		// System call # 3
                NachOS_Join();
                break;

             case SC_Create:		// System call # 4
                NachOS_Create();
                break;
             case SC_Open:		// System call # 5
                NachOS_Open();
                break;
             case SC_Read:		// System call # 6
                NachOS_Read();
                break;
             case SC_Write:		// System call # 7
                NachOS_Write();
                break;
             case SC_Close:		// System call # 8
                NachOS_Close();
                break;

             case SC_Fork:		// System call # 9
                NachOS_Fork();
                break;
             case SC_Yield:		// System call # 10
                NachOS_Yield();
                break;

             case SC_SemCreate:         // System call # 11
                NachOS_SemCreate();
                break;
             case SC_SemDestroy:        // System call # 12
                NachOS_SemDestroy();
                break;
             case SC_SemSignal:         // System call # 13
                NachOS_SemSignal();
                break;
             case SC_SemWait:           // System call # 14
                NachOS_SemWait();
                break;

             case SC_LckCreate:         // System call # 15
                NachOS_LockCreate();
                break;
             case SC_LckDestroy:        // System call # 16
                NachOS_LockDestroy();
                break;
             case SC_LckAcquire:         // System call # 17
                NachOS_LockAcquire();
                break;
             case SC_LckRelease:           // System call # 18
                NachOS_LockRelease();
                break;

             case SC_CondCreate:         // System call # 19
                NachOS_CondCreate();
                break;
             case SC_CondDestroy:        // System call # 20
                NachOS_CondDestroy();
                break;
             case SC_CondSignal:         // System call # 21
                NachOS_CondSignal();
                break;
             case SC_CondWait:           // System call # 22
                NachOS_CondWait();
                break;
             case SC_CondBroadcast:           // System call # 23
                NachOS_CondBroadcast();
                break;

             case SC_Socket:	// System call # 30
		NachOS_Socket();
               break;
             case SC_Connect:	// System call # 31
		NachOS_Connect();
               break;
             case SC_Bind:	// System call # 32
		NachOS_Bind();
               break;
             case SC_Listen:	// System call # 33
		NachOS_Listen();
               break;
             case SC_Accept:	// System call # 32
		NachOS_Accept();
               break;
             case SC_Shutdown:	// System call # 33
		NachOS_Shutdown();
               break;

             default:
                printf("NachOS version: %d-%d\n", (SC_Base + SC_NachOS)/10, (SC_Base + SC_NachOS)%10 );
                printf("Unexpected syscall exception %d\n", type );
                ASSERT( false );
                break;
          }
          break;

       case PageFaultException:
          stats->numPageFaults++;
          printf("Page fault at address %d\n",
                 machine->ReadRegister(BadVAddrReg));
          interrupt->Halt();
          break;

       case ReadOnlyException:
          printf( "Read Only exception (%d)\n", which );
          ASSERT( false );
          break;

       case BusErrorException:
          printf( "Bus error exception (%d)\n", which );
          ASSERT( false );
          break;

       case AddressErrorException:
          printf( "Address error exception (%d)\n", which );
          ASSERT( false );
          break;

       case OverflowException:
          printf( "Overflow exception (%d)\n", which );
          ASSERT( false );
          break;

       case IllegalInstrException:
          printf( "Ilegal instruction exception (%d)\n", which );
          ASSERT( false );
          break;

       default:
          printf( "Unexpected exception %d\n", which );
          ASSERT( false );
          break;
    }

}
