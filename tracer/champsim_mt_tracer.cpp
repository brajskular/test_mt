#include "pin.H"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <cstdlib>
#include <cstddef>
#include <unistd.h>
#include <map>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#if !defined(TARGET_WINDOWS)
#include <sys/syscall.h>
#endif

/* ===================================================================== */
/* Names of pthread_create */
/* ===================================================================== */
#if defined(TARGET_MAC)
#define PTHREAD_CREATE "_pthread_create"
#define START_THREAD "_start_thread"
#define MUTEX_INIT "pthread_mutex_init"
#define MUTEX_LOCK "mutex_lock"
#define MUTEX_UNLOCK "pthread_mutex_unlock"
#define SEM_INIT "sem_init"
#define SEM_WAIT "sem_wait"
#define SEM_POST "sem_post"
#define BARRIER_INIT "pthread_barrier_init"
#define BARRIER_WAIT "pthread_barrier_wait"
#else
#define PTHREAD_CREATE "pthread_create"
#define START_THREAD "start_thread"
#define MUTEX_INIT "pthread_mutex_init"
#define MUTEX_LOCK "pthread_mutex_lock"
#define MUTEX_UNLOCK "pthread_mutex_unlock"
#define SEM_INIT "sem_init"
#define SEM_WAIT "sem_wait"
#define SEM_POST "sem_post"
#define BARRIER_INIT "pthread_barrier_init"
#define BARRIER_WAIT "pthread_barrier_wait"
#endif

using std::cout;
using std::endl;
using std::cerr;
using std::string;
using std::ofstream;
using std::setw;
using std::hex;
using std::dec;
using std::map;
using std::pair;

PIN_LOCK lock;

map<UINT64, UINT64> threadMapDB;

void insertThreadMapDB(UINT64 OSID, UINT64 PINID)
{
    threadMapDB.insert(pair<UINT64, UINT64>(OSID, PINID));
}

UINT64 queryThreadMapDB(UINT64 OSID)
{
    return threadMapDB.find(OSID)->second;
}

struct threadDependencyRecord
{
    UINT64 parentThread;
    UINT64 pthreadCreateTime;
    UINT64 startTime;
    UINT64 terminateTime;
    UINT64 insCount;
};

map<UINT64, threadDependencyRecord> threadDependencyDB;

void insertThreadDependencyDB(UINT64 childID, UINT64 parentID, UINT64 pthreadTime, UINT64 actualTime)
{
    threadDependencyRecord r;
    r.parentThread = parentID;
    r.pthreadCreateTime = pthreadTime;
    r.startTime = actualTime;
    r.terminateTime = 0;
    r.insCount = 0;

    threadDependencyDB.insert(pair<UINT64, threadDependencyRecord>(childID, r));
}

void updateThreadDependencyDB(UINT64 childID, UINT64 terminateInsCount)
{
    threadDependencyDB.find(childID)->second.terminateTime = terminateInsCount;
}

void updateThreadDependencyDBTerminateInsCount(UINT64 childID, UINT64 num)
{
    threadDependencyDB.find(childID)->second.insCount = num;
}

#define NUM_INSTR_DESTINATIONS 2
#define NUM_INSTR_SOURCES 4

typedef struct trace_instr_format {
    unsigned long long int ip;  // instruction pointer (program counter) value

    unsigned char is_branch;    // is this branch
    unsigned char branch_taken; // if so, is this taken

    unsigned char destination_registers[NUM_INSTR_DESTINATIONS]; // output registers
    unsigned char source_registers[NUM_INSTR_SOURCES];           // input registers

    unsigned long long int destination_memory[NUM_INSTR_DESTINATIONS]; // output memory
    unsigned long long int source_memory[NUM_INSTR_SOURCES];           // input memory
    
} trace_instr_format_t; 

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,  "pintool", "o", "trace", 
        "specify file name for Champsim tracer output");

/*
 * Thread specific data
 */
static  TLS_KEY mlog_key = INVALID_TLS_KEY;

#define PAD_SIZE 8

ofstream threadDependency;

// ofstream syncContructRecord;

struct ThreadDependencyNode
{
    UINT64 threadPINID;
    UINT64 instructionNumber;
    ADDRINT pthread_t_holder;
    int start_order;
    struct ThreadDependencyNode* next;
};

struct child_thread_record_node
{
    UINT64 parent_thread_pthread_create_ins;
    UINT64 parent_thread_thread_start_ins;
    UINT32 parent_thread_pthread_create_order;
    UINT32 parent_thread_thread_start_order;
    ADDRINT pthread_t_value;
    UINT32 pin_assigned_thread_ID;
    UINT32 pin_assigned_parent_thread_ID;
    struct child_thread_record_node* next;
};

/*
 * MLOG - thread specific data that is not handled by the buffering API.
 */
class MLOG
{
  public:
    ofstream symbolFile;

    ofstream sync_construct_access_file;

    FILE *traceFile;

    trace_instr_format_t trace;

    UINT64 libCounter;

    UINT64 ip;

    UINT64 insNum;

    UINT64 parentThreadID;

    ThreadDependencyNode* threadDependencyNode;

    int threadCounter;

    PIN_MUTEX threadLockMutex;

    ADDRINT *tmpp_pthread_t;

    UINT32 pthread_create_counter;

    UINT32 running_child_thread_counter;

    child_thread_record_node *child_thread_record_root_node;

    UINT8 _pad[56];

    void updatePthread_tHolder(
        ADDRINT *tmpp
    );

    void insertChildThreadRecordNode(
        UINT64 parent_thread_pthread_create_ins_num,
        ADDRINT *tmpp_pthread_t_pointer, 
        UINT32 parent_thread_ID
    );

    void updateChildTHreadRecordNode(
        UINT64 parent_thread_thread_start_ins_num,
        UINT32 pin_assigned_thread_ID,
        ADDRINT pthread_t_value
    );

    void insertSpaceInThreadCreation();

    void popSpaceInThreadCreation(UINT64 parent, UINT64 child);
};

void MLOG::updatePthread_tHolder(
        ADDRINT *tmpp
    )
{
    PIN_MutexLock(&threadLockMutex);

    tmpp_pthread_t = tmpp;

    PIN_MutexUnlock(&threadLockMutex);
}

void MLOG::insertChildThreadRecordNode(
    UINT64 parent_thread_pthread_create_ins_num,
    ADDRINT *tmpp_pthread_t_pointer, 
    UINT32 parent_thread_ID
    )
{   
    PIN_MutexLock(&threadLockMutex);

    if (child_thread_record_root_node == NULL)
    {
        child_thread_record_root_node = new child_thread_record_node;
        child_thread_record_root_node->parent_thread_pthread_create_ins = parent_thread_pthread_create_ins_num;
        child_thread_record_root_node->parent_thread_pthread_create_order = pthread_create_counter;
        child_thread_record_root_node->pthread_t_value = *tmpp_pthread_t_pointer;
        child_thread_record_root_node->pin_assigned_parent_thread_ID = parent_thread_ID;
        child_thread_record_root_node->next = NULL;
    }
    else
    {
        child_thread_record_node* tmpp = child_thread_record_root_node;

        while (tmpp->next != NULL)
        {
            tmpp = tmpp->next;            
        }

        tmpp->next = new child_thread_record_node;
        tmpp = tmpp->next;
        tmpp->parent_thread_pthread_create_ins = parent_thread_pthread_create_ins_num;
        tmpp->parent_thread_pthread_create_order = pthread_create_counter;
        tmpp->pthread_t_value = *tmpp_pthread_t_pointer;
        tmpp->pin_assigned_parent_thread_ID = parent_thread_ID;
        tmpp->next = NULL;
    }

    pthread_create_counter++;

    PIN_MutexUnlock(&threadLockMutex);
}

void MLOG::updateChildTHreadRecordNode(
        UINT64 parent_thread_thread_start_ins_num,
        UINT32 assigned_thread_ID,
        ADDRINT pthread_t_value
    )
{
    PIN_MutexLock(&threadLockMutex);

    child_thread_record_node* tmpp = child_thread_record_root_node;

    int i = 0;

    while ((tmpp != NULL) && (tmpp->next != NULL) && (tmpp->pthread_t_value != 0))
    {
        tmpp = tmpp->next;     
        i++;           

        if (((tmpp->pthread_t_value)) == pthread_t_value)
        {
            break;
        }
    }

    tmpp->parent_thread_thread_start_ins = parent_thread_thread_start_ins_num;
    tmpp->pin_assigned_thread_ID = assigned_thread_ID;
    tmpp->parent_thread_thread_start_order = running_child_thread_counter;

    running_child_thread_counter++; 

    PIN_MutexUnlock(&threadLockMutex);
}

void MLOG::insertSpaceInThreadCreation()
{
    PIN_MutexLock(&threadLockMutex);
    if (threadDependencyNode == NULL)
    {
        threadDependencyNode = new ThreadDependencyNode;
        threadDependencyNode->threadPINID = 0;
        threadDependencyNode->instructionNumber = insNum;
        threadDependencyNode->next = NULL;
    }
    else
    {
        ThreadDependencyNode* tmpp = threadDependencyNode;

        while (tmpp->next != NULL)
        {
            tmpp = tmpp->next;
        }
        
        tmpp->next = new ThreadDependencyNode;
        tmpp = tmpp->next;
        tmpp->threadPINID = 0;
        tmpp->instructionNumber = insNum;
        tmpp->next = NULL;
    }
    PIN_MutexUnlock(&threadLockMutex);
}

void MLOG::popSpaceInThreadCreation(UINT64 parent, UINT64 child)
{
    PIN_MutexLock(&threadLockMutex);
    ThreadDependencyNode* tmpp = threadDependencyNode;
    threadDependencyNode = threadDependencyNode->next;

    insertThreadDependencyDB(child, parent, tmpp->instructionNumber, insNum);
    // threadDependency << "Thread " 
    // << setw(5) << child 
    // << " started by thread " << setw(5) << parent << " with pthread_create"
    // << " at instruction " << setw(12) << dec << tmpp->instructionNumber 
    // << ", actually started at instruction " << setw(12) << insNum
    // << endl;

    delete tmpp;
    PIN_MutexUnlock(&threadLockMutex);
}

void writeToFile(string *rtnName, string *imgName, ADDRINT rtnAddress, THREADID threadid)
{
    MLOG* mlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, threadid)); //volatile 

    if (*rtnName == "pthread_create") // else
    {
        mlog->insertSpaceInThreadCreation();

        // mlog->symbolFile 
        // << setw(70) << *rtnName << " " 
        // << setw(70) << *imgName << "       " 
        // << hex << rtnAddress 
        // << " creating thread, instruction count = " << dec << mlog->insNum << endl;
    }
    // else if (*rtnName == "pthread_join")
    // {
    //     // mlog->symbolFile 
    //     // << setw(70) << *rtnName << " " 
    //     // << setw(70) << *imgName << "       " 
    //     // << hex << rtnAddress 
    //     // << " waiting completion of thread " << endl; // 
    // }
    // else
    // {
    //     mlog->symbolFile 
    //     << setw(70) << *rtnName << " " 
    //     << setw(70) << *imgName << "       " 
    //     << hex << rtnAddress << endl; // 
    // }

    mlog->symbolFile 
        << setw(70) << *rtnName << " " 
        << setw(70) << *imgName << "       " 
        << hex << rtnAddress << "    " << dec << mlog->insNum << endl; // 

    mlog->libCounter++;
}

INT32 Usage()
{
    cerr << "This Pintool prints the routine names and relavent information of each thread" << endl;
    return -1;
}

void BeginInstruction(VOID *ip, UINT32 op_code, THREADID threadid)
{                  
    MLOG* mlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, threadid));

    mlog->insNum += 1;
    mlog->ip = (unsigned long long int)ip;

    // reset the current instruction
    mlog->trace.ip = (unsigned long long int)ip;

    mlog->trace.is_branch = 0;
    mlog->trace.branch_taken = 0;

    for(int i=0; i<NUM_INSTR_DESTINATIONS; i++) 
    {
        mlog->trace.destination_registers[i] = 0;
        mlog->trace.destination_memory[i] = 0;
    }

    for(int i=0; i<NUM_INSTR_SOURCES; i++) 
    {
        mlog->trace.source_registers[i] = 0;
        mlog->trace.source_memory[i] = 0;
    }               
}

void EndInstruction(THREADID threadid)
{             
    MLOG* mlog = static_cast<MLOG*>(PIN_GetThreadData( mlog_key, threadid));
    
    fwrite(&(mlog->trace), sizeof(trace_instr_format_t), 1, mlog->traceFile);
}

void BranchOrNot(UINT32 taken, THREADID threadid)
{       
    MLOG* mlog = static_cast<MLOG*>(PIN_GetThreadData( mlog_key, threadid));

    mlog->trace.is_branch = 1;

    if(taken != 0)
    {
        mlog->trace.branch_taken = 1;        
    }
}

void RegRead(UINT32 i, UINT32 index, THREADID threadid)
{          
    MLOG* mlog = static_cast<MLOG*>(PIN_GetThreadData( mlog_key, threadid));

    REG r = (REG)i;   

    // check to see if this register is already in the list
    int already_found = 0;
    for(int i=0; i<NUM_INSTR_SOURCES; i++)
    {
        if(mlog->trace.source_registers[i] == ((unsigned char)r))
        {
            already_found = 1;
            break;
        }
    }
    if(already_found == 0)
    {
        for(int i=0; i<NUM_INSTR_SOURCES; i++)
        {
            if(mlog->trace.source_registers[i] == 0)
            {
                mlog->trace.source_registers[i] = (unsigned char)r;
                break;
            }
        }
    }    
}

void RegWrite(REG i, UINT32 index, THREADID threadid)
{          
    MLOG* mlog = static_cast<MLOG*>(PIN_GetThreadData( mlog_key, threadid));

    REG r = (REG)i;

    /*
    if(r == 26)
    {
    // 26 is the IP, which is read and written by branches
    return;
    }
    */

    //cout << "<" << r << " " << REG_StringShort((REG)r) << "> ";
    //cout << "<" << REG_StringShort((REG)r) << "> ";

    //printf("<%d> ", (int)r);

    int already_found = 0;
    for(int i=0; i<NUM_INSTR_DESTINATIONS; i++)
    {
        if(mlog->trace.destination_registers[i] == ((unsigned char)r))
        {
            already_found = 1;
            break;
        }
    }
    if(already_found == 0)
    {
        for(int i=0; i<NUM_INSTR_DESTINATIONS; i++)
        {
            if(mlog->trace.destination_registers[i] == 0)
            {
                mlog->trace.destination_registers[i] = (unsigned char)r;
                break;
            }
        }
    }       
}

void MemoryRead(VOID* addr, UINT32 index, UINT32 read_size, THREADID threadid)
{         
    MLOG* mlog = static_cast<MLOG*>(PIN_GetThreadData( mlog_key, threadid));

    // check to see if this memory read location is already in the list
    int already_found = 0;
    for(int i=0; i<NUM_INSTR_SOURCES; i++)
    {
        if(mlog->trace.source_memory[i] == ((unsigned long long int)addr))
        {
            already_found = 1;
            break;
        }
    }
    if(already_found == 0)
    {
        for(int i=0; i<NUM_INSTR_SOURCES; i++)
        {
            if(mlog->trace.source_memory[i] == 0)
            {
                mlog->trace.source_memory[i] = (unsigned long long int)addr;
                break;
            }
        }
    }
}

void MemoryWrite(VOID* addr, UINT32 index, THREADID threadid)
{               
    MLOG* mlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, threadid));

    // check to see if this memory write location is already in the list
    int already_found = 0;
    for(int i=0; i<NUM_INSTR_DESTINATIONS; i++)
    {
        if(mlog->trace.destination_memory[i] == ((unsigned long long int)addr))
        {
            already_found = 1;
            break;
        }
    }
    if(already_found == 0)
    {
        for(int i=0; i<NUM_INSTR_DESTINATIONS; i++)
        {
            if(mlog->trace.destination_memory[i] == 0)
            {
                mlog->trace.destination_memory[i] = (unsigned long long int)addr;
                break;
            }
        }
    }      
}

void Instruction(INS ins, VOID *v)
{
    // numberOfInstructionsFound++;
    // begin each instruction with this function
    UINT32 opcode = INS_Opcode(ins);
    
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)BeginInstruction, IARG_INST_PTR, IARG_UINT32, opcode, IARG_THREAD_ID, IARG_END);

    // instrument branch instructions
    if(INS_IsBranch(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)BranchOrNot, IARG_BRANCH_TAKEN, IARG_THREAD_ID, IARG_END);

    // // instrument register reads
    UINT32 readRegCount = INS_MaxNumRRegs(ins);
    for(UINT32 i=0; i<readRegCount; i++) 
    {
        UINT32 regNum = INS_RegR(ins, i);

        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RegRead,
                IARG_UINT32, regNum, IARG_UINT32, i, IARG_THREAD_ID, 
                IARG_END);
    }

    // // instrument register writes
    UINT32 writeRegCount = INS_MaxNumWRegs(ins);
    for(UINT32 i=0; i<writeRegCount; i++) 
    {
        UINT32 regNum = INS_RegW(ins, i);

        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RegWrite,
                IARG_UINT32, regNum, IARG_UINT32, i, IARG_THREAD_ID,
                IARG_END);
    }

    // // instrument memory reads and writes
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    // // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++) 
    {
        if (INS_MemoryOperandIsRead(ins, memOp)) 
        {
            UINT32 read_size = INS_MemoryOperandSize(ins, memOp);

            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MemoryRead,
                    IARG_MEMORYOP_EA, memOp, IARG_UINT32, memOp, IARG_UINT32, read_size, IARG_THREAD_ID,
                    IARG_END);
        }
        if (INS_MemoryOperandIsWritten(ins, memOp)) 
        {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MemoryWrite,
                    IARG_MEMORYOP_EA, memOp, IARG_UINT32, memOp, IARG_THREAD_ID,
                    IARG_END);
        }
    }

    // finalize each instruction with this function
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)EndInstruction, IARG_THREAD_ID, IARG_END);
}

void ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v)
{   
    // Insert <OS thread ID, PIN thread ID> into thread ID mapping DB
    // insertThreadMapDB(PIN_GetTid(), tid);

    // MLOG * mlog = new MLOG;

    // // Set the parent thread ID
    // if (tid != 0)
    // {
    //     mlog->parentThreadID = queryThreadMapDB(PIN_GetParentTid());
    // }
    // else
    // {
    //     mlog->parentThreadID = 0;
    // }
    
    // const string traceFileName = "named_pipe_" + decstr(tid); // + decstr(getpid()) + "."
    // mlog->traceFile = fopen(traceFileName.c_str(), "ab");

    // if ( ! mlog->traceFile )
    // {
    //     cerr << "Error: could not open output trace file." << endl;
    //     exit(1);
    // }

    // mlog->libCounter = 0;

    // mlog->insNum = 0;    

    // mlog->pthread_create_counter = 0;

    // mlog->running_child_thread_counter = 0;

    // PIN_MutexInit(&mlog->threadLockMutex);

    // if (tid != 0)
    // {
    //     MLOG* parentMlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, mlog->parentThreadID));

    //     // cout << "Thread " << tid << " started by thread " << mlog->parentThreadID << " at instruction " << parentMlog->insNum << endl;  

    //     parentMlog->popSpaceInThreadCreation(mlog->parentThreadID, tid);
    // }

    // mlog->threadDependencyNode = NULL;

    // mlog->child_thread_record_root_node = NULL;

    // // PIN_MutexInit(&mlog->threadLockMutex);

    // // A thread will need to look up its MLOG, so save pointer in TLS    
    // if (PIN_SetThreadData(mlog_key, mlog, tid) == FALSE)
    // {
    //     cerr << "PIN_SetThreadData failed" << endl;
    //     PIN_ExitProcess(1);
    // }      

    cout << "Thread " << tid << " created"  << endl;
}

void ThreadFini(THREADID tid, const CONTEXT *ctxt, INT32 code, VOID *v)
{       
    cout << "Thread " << tid << " finished"  << endl;
    // MLOG * mlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, tid));

    // volatile MLOG * parentMlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, mlog->parentThreadID));

    // // cout
    // // << "Thread " << setw(5) << tid 
    // // << " finished when thread " << setw(5) << mlog->parentThreadID 
    // // << " reaches instruction " << setw(12) << dec << parentMlog->insNum << endl;

    // if (tid != 0)
    // {
    //     updateThreadDependencyDB(tid, parentMlog->insNum);
    //     updateThreadDependencyDBTerminateInsCount(tid, mlog->insNum);
    //     // threadDependency 
    //     // << "Thread " << setw(5) << tid 
    //     // << " finished when thread " << setw(5) << mlog->parentThreadID 
    //     // << " reaches instruction " << setw(12) << dec << parentMlog->insNum << endl;
    // }   
    
    // fclose(mlog->traceFile);    

    // cout << "Thread " << setw(5) << tid << " finishes after " << setw(10) << mlog->insNum << " instructions" << endl;

    // delete mlog;
}

VOID processArgumentPThreadCreate(CHAR * name, ADDRINT *ptr) // unsigned long*
{
    // Extract the pointer to the variable of type pthread_t passed into pthread_create
    // Place the extracted pointer at the temporary holder in the parent thread's record
    // cout << name << " Before " << dec << setw(16) << *ptr  << dec << " Pin thread ID " << PIN_ThreadId() << endl;

    MLOG * mlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, PIN_ThreadId()));

    mlog->updatePthread_tHolder(ptr);
}

VOID processArgumentPThreadCreateAfter(CHAR * name, ADDRINT *ptr) // unsigned long*
{      
    // The value pointed by the pointer to the variable of type pthread_t passed into pthread_create
    // has changed after pthread_create returns
    // Create a new child_thread_record_node
    // Insert the newly created node into the linked list rooted in the parent's record
    MLOG * mlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, PIN_ThreadId()));

    // cout << name << " After  " << dec << setw(16) << (mlog->tmpp_pthread_t) << dec << " Pin thread ID " << PIN_ThreadId() << endl;
    
    mlog->insertChildThreadRecordNode(mlog->insNum, mlog->tmpp_pthread_t, PIN_ThreadId());
}

VOID processArgumentStartThread(CHAR * name, ADDRINT ptr)
{   
    // Access and change the records for the parent thread
    MLOG * mlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, queryThreadMapDB(PIN_GetParentTid())));

    // cout << name << setw(26) << dec << ptr << dec << " Pin thread ID " << PIN_ThreadId() << " Parent thread ID " << queryThreadMapDB(PIN_GetParentTid()) << endl;

    if (PIN_ThreadId() != 0)
    {
        mlog->updateChildTHreadRecordNode(mlog->insNum, PIN_ThreadId(), ptr);
    }    
}

VOID Image(IMG img, VOID *v)
{
    // Use the following block is no name mangling takes place during compilation
    // RTN freeRtn = RTN_FindByName(img, PTHREAD_CREATE);
    // if (RTN_Valid(freeRtn))
    // {
    //     RTN_Open(freeRtn);
    //     RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR)processArgument, IARG_ADDRINT, PTHREAD_CREATE, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
    //                    IARG_END);
    //     RTN_Close(freeRtn);
    // }

    // Otherwise use the following code block if name mangling takes place during compilation
    for(SYM sym= IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym))
    {
        string undecoratedSymbolName = PIN_UndecorateSymbolName(SYM_Name(sym), UNDECORATION_NAME_ONLY);
        
        if (undecoratedSymbolName == PTHREAD_CREATE)
        {
            RTN freeRtn = RTN_FindByName(img, SYM_Name(sym).c_str());
            
            if(RTN_Valid(freeRtn))
            {
                RTN_Open(freeRtn);
                RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR)processArgumentPThreadCreate, IARG_ADDRINT, PTHREAD_CREATE, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                            IARG_END);
                RTN_InsertCall(freeRtn, IPOINT_AFTER, (AFUNPTR)processArgumentPThreadCreateAfter, IARG_ADDRINT, PTHREAD_CREATE, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                            IARG_END);
                RTN_Close(freeRtn);
            }    
            break;        
        }       
    }

    RTN freeRtnPthreadCreate = RTN_FindByName(img, PTHREAD_CREATE);
            
    if(RTN_Valid(freeRtnPthreadCreate))
    {
        RTN_Open(freeRtnPthreadCreate);
        RTN_InsertCall(freeRtnPthreadCreate, IPOINT_BEFORE, (AFUNPTR)processArgumentPThreadCreate, IARG_ADDRINT, PTHREAD_CREATE, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                    IARG_END);
        RTN_InsertCall(freeRtnPthreadCreate, IPOINT_AFTER, (AFUNPTR)processArgumentPThreadCreateAfter, IARG_ADDRINT, PTHREAD_CREATE, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                    IARG_END);
        RTN_Close(freeRtnPthreadCreate);
    }    

    RTN freeRtnStartThread = RTN_FindByName(img, START_THREAD);

    if(RTN_Valid(freeRtnStartThread))
    {
        RTN_Open(freeRtnStartThread);
        RTN_InsertCall(freeRtnStartThread, IPOINT_BEFORE, (AFUNPTR)processArgumentStartThread, IARG_ADDRINT, START_THREAD, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                    IARG_END);
        RTN_Close(freeRtnStartThread);
    }         
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */
void Fini(INT32 code, VOID *v)
{   
    cout << "Program finished" << endl;
    // Dump the thread dependency information
    // cout << "============================================================================================================" << endl;

    // cout << "Child Thread    Parent Thread    pthread_create at    Thread Start    Thread Terminate    #Instructions Run" << endl;

    // cout << "------------------------------------------------------------------------------------------------------------" << endl;

    // threadDependency << "Child Thread    Parent Thread    pthread_create at    Thread Start    Thread Terminate    #Instructions Run" << endl;

    // threadDependency << "------------------------------------------------------------------------------------------------------------" << endl;
    
    // map<UINT64, threadDependencyRecord>::iterator itR;
    // for (itR = threadDependencyDB.begin(); itR != threadDependencyDB.end(); ++itR) {
    //     cout << setw(12) << itR->first << "    " 
    //     << setw(13) << itR->second.parentThread << "    "
    //     << setw(17) << itR->second.pthreadCreateTime << "    "
    //     << setw(12) << itR->second.startTime << "    "
    //     << setw(16) << itR->second.terminateTime  << "    "
    //     << setw(17) << itR->second.insCount << endl;

    //     threadDependency << setw(12) << itR->first << "    " 
    //     << setw(13) << itR->second.parentThread << "    "
    //     << setw(17) << itR->second.pthreadCreateTime << "    "
    //     << setw(12) << itR->second.startTime << "    "
    //     << setw(16) << itR->second.terminateTime  << "    "
    //     << setw(17) << itR->second.insCount << endl;
    // }

    // threadDependency.close();

    // // syncContructRecord.close();

    // map<UINT64, UINT64>::iterator itr;

    // cout << "============================================================================================================" << endl;

    // cout << "OS thread ID    PIN thread ID " << endl;

    // cout << "------------------------------------------------------------------------------------------------------------" << endl;

    // for (itr = threadMapDB.begin(); itr != threadMapDB.end(); ++itr) {
    //     cout << setw(12) << itr->first << "    " << setw(13) << itr->second << '\n';
    // }

    // cout << "============================================================================================================" << endl;

    // cout << "Parent thread ID  pthread_create order  start_thread order  Thread ID" << endl;

    // cout << "------------------------------------------------------------------------------------------------------------" << endl;

    // for (itr = threadMapDB.begin(); itr != threadMapDB.end(); ++itr) {
    //     MLOG *mlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, itr->second));

    //     if (mlog->child_thread_record_root_node != NULL)
    //     {
    //         child_thread_record_node *tmpp = mlog->child_thread_record_root_node;

    //         while (tmpp != NULL)
    //         {
    //             cout << setw(16) << tmpp->pin_assigned_parent_thread_ID
    //             << setw(22) << tmpp->parent_thread_pthread_create_order
    //             << setw(20) << tmpp->parent_thread_thread_start_order
    //             << setw(11) << tmpp->pin_assigned_thread_ID
    //             << endl;
    //             tmpp = tmpp->next;
    //         }
    //     }
        
    // }

    // cout << "============================================================================================================" << endl;

    // cout << "The end!" << endl;  
}

int main(int argc, char *argv[])
{
    PIN_InitSymbols();

    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

    // Initialize thread-specific data not handled by buffering api.
    // mlog_key = PIN_CreateThreadDataKey(NULL);    
    // if (mlog_key == INVALID_TLS_KEY)
    // {
    //     cerr << "number of already allocated keys reached the MAX_CLIENT_TLS_KEYS limit" << endl;
    //     PIN_ExitProcess(1);
    // }

    // PIN_InitLock(&lock);

    // cout << "Size of MLOG = " << sizeof(MLOG) << endl;

    // Initialize the thread dependency file
    // const string threadDependencyFileName = KnobOutputFile.Value() + ".Dependency";
    // threadDependency.open(threadDependencyFileName.c_str());

    // Initialize the file that records the synchronization constructs
    // const string syncContructRecordFileName = KnobOutputFile.Value() + ".sync";
    // syncContructRecord.open(syncContructRecordFileName.c_str());

    // Add callbacks
    PIN_AddThreadStartFunction(ThreadStart, NULL);
    PIN_AddThreadFiniFunction(ThreadFini, NULL);    

    // INS_AddInstrumentFunction(Instruction, NULL);

    PIN_AddFiniFunction(Fini, NULL); 

    // IMG_AddInstrumentFunction(Image, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}