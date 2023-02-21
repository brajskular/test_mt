#include <iostream>
#include <fstream>
#include <map>

#include "pin.H"

using std::ostream;
using std::cout;
using std::cerr;
using std::string;
using std::endl;
using std::map;
using std::pair;
using std::setw;

// key for accessing TLS storage in the threads. initialized once in main()
static  TLS_KEY mlog_key = INVALID_TLS_KEY;

PIN_LOCK global_lock;

map<UINT64, UINT64> threadMapDB;

void insertThreadMapDB(UINT64 OSID, UINT64 PINID)
{
    threadMapDB.insert(pair<UINT64, UINT64>(OSID, PINID));
}

UINT64 queryThreadMapDB(UINT64 OSID)
{
    return threadMapDB.find(OSID)->second;
}

struct ThreadDependencyNode
{
    UINT64 threadPINID;
    UINT64 instructionNumber;
    ADDRINT pthread_t_holder;
    int start_order;
    struct ThreadDependencyNode* next;
};

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

#define PAD_SIZE 8

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

/*
 * MLOG - thread specific data that is not handled by the buffering API.
 */
class MLOG
{
  public:

    FILE *traceFile;

    trace_instr_format_t trace;

    UINT64 ip;

    UINT64 insNum;

    UINT64 parentThreadID;

    ThreadDependencyNode* threadDependencyNode;

    UINT32 threadCounter;

    PIN_MUTEX threadLockMutex;    

    child_thread_record_node *child_thread_record_root_node;

    UINT64 osid;

    UINT8 _pad[PAD_SIZE];

    void insertChildThreadRecordNode(
        UINT64 parent_thread_pthread_create_ins_num,        
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

void MLOG::insertChildThreadRecordNode(
    UINT64 parent_thread_pthread_create_ins_num,
    UINT32 parent_thread_ID
    )
{   
    PIN_MutexLock(&threadLockMutex);

    if (child_thread_record_root_node == NULL)
    {
        child_thread_record_root_node = new child_thread_record_node;
        child_thread_record_root_node->parent_thread_pthread_create_ins = parent_thread_pthread_create_ins_num;        
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
        tmpp->pin_assigned_parent_thread_ID = parent_thread_ID;
        tmpp->next = NULL;
    }    

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
    // ThreadDependencyNode* tmpp = threadDependencyNode;
    // cout << "there 5" << endl;
    // threadDependencyNode = threadDependencyNode->next;    
    // // threadDependency << "Thread " 
    // // << setw(5) << child 
    // // << " started by thread " << setw(5) << parent << " with pthread_create"
    // // << " at instruction " << setw(12) << dec << tmpp->instructionNumber 
    // // << ", actually started at instruction " << setw(12) << insNum
    // // << endl;

    // delete tmpp;

    PIN_MutexUnlock(&threadLockMutex);

    PIN_GetLock(&global_lock, child);

    insertThreadDependencyDB(child, parent, tmpp->instructionNumber, insNum);

    PIN_ReleaseLock(&global_lock);
}

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "", "specify dependency record file name");

// ostream threadDependency;

// Force each thread's data to be in its own data cache line so that
// multiple threads do not contend for the same data cache line.
// This avoids the false sharing problem.
#define PADSIZE 56  // 64 byte line size: 64-8

// a running count of the instructions
class thread_data_t
{
  public:
    thread_data_t() : _count(0) {}
    UINT64 _count;
    UINT8 _pad[PADSIZE];
};

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
    //MLOG* mlog = static_cast<MLOG*>(PIN_GetThreadData( mlog_key, threadid));
    
    //fwrite(&(mlog->trace), sizeof(trace_instr_format_t), 1, mlog->traceFile);
}

void Instruction(INS ins, VOID *v)
{
    // numberOfInstructionsFound++;
    // begin each instruction with this function
    UINT32 opcode = INS_Opcode(ins);
    
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)BeginInstruction, IARG_INST_PTR, IARG_UINT32, opcode, IARG_THREAD_ID, IARG_END);

    /*
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
    */

    // finalize each instruction with this function
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)EndInstruction, IARG_THREAD_ID, IARG_END);
}

// Called when thread starts
void ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v)
{   

cout << "Thread " << tid << " created"  << endl;
    MLOG * mlog = new MLOG;

    mlog->osid = PIN_GetTid();    
    
    // const string traceFileName = "named_pipe_" + decstr(tid); // + decstr(getpid()) + "."
    // mlog->traceFile = fopen(traceFileName.c_str(), "ab");

    // if ( ! mlog->traceFile )
    // {
    //     cerr << "Error: could not open output trace file." << endl;
    //     exit(1);
    // }

    mlog->insNum = 0;            

    PIN_MutexInit(&mlog->threadLockMutex);

    mlog->threadDependencyNode = NULL;

    mlog->child_thread_record_root_node = NULL;

    // A thread will need to look up its MLOG, so save pointer in TLS    
    if (PIN_SetThreadData(mlog_key, mlog, tid) == FALSE)
    {
        cerr << "PIN_SetThreadData failed" << endl;
        PIN_ExitProcess(1);
    }

    PIN_GetLock(&global_lock, tid);

    // Set the parent thread ID
    if (tid != 0)
    {
        mlog->parentThreadID = queryThreadMapDB(PIN_GetParentTid());

        MLOG* parentMlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, mlog->parentThreadID));
        parentMlog->popSpaceInThreadCreation(mlog->parentThreadID, tid);
    }
    else
    {
        mlog->parentThreadID = 0;
    }

    insertThreadMapDB(PIN_GetTid(), tid);

    PIN_ReleaseLock(&global_lock);
}

// Called when thread finishes
void ThreadFini(THREADID tid, const CONTEXT *ctxt, INT32 code, VOID *v)
{       
    // MLOG * mlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, tid));

    // volatile MLOG * parentMlog = static_cast<MLOG*>(PIN_GetThreadData(mlog_key, mlog->parentThreadID));

    // // // cout
    // // // << "Thread " << setw(5) << tid 
    // // // << " finished when thread " << setw(5) << mlog->parentThreadID 
    // // // << " reaches instruction " << setw(12) << dec << parentMlog->insNum << endl;

    // if (tid != 0)
    // {
    //     updateThreadDependencyDB(tid, parentMlog->insNum);
    //     updateThreadDependencyDBTerminateInsCount(tid, mlog->insNum);
    //     // threadDependency 
    //     // << "Thread " << setw(5) << tid 
    //     // << " finished when thread " << setw(5) << mlog->parentThreadID 
    //     // << " reaches instruction " << setw(12) << dec << parentMlog->insNum << endl;
    // }   
    
    // // fclose(mlog->traceFile);    

    // // cout << "Thread " << setw(5) << tid << " finishes after " << setw(10) << mlog->insNum << " instructions" << endl;

    // delete mlog;

    cout << "Thread " << tid << " finished"  << endl;
}

// called when the program being traced finishes
void Fini(INT32 code, VOID *v)
{   
    cout << "Program finished" << endl;
    // Dump the thread dependency information
    cout << "============================================================================================================" << endl;

    cout << "Child Thread    Parent Thread    pthread_create at    Thread Start    Thread Terminate    #Instructions Run" << endl;

    cout << "------------------------------------------------------------------------------------------------------------" << endl;

    // threadDependency << "Child Thread    Parent Thread    pthread_create at    Thread Start    Thread Terminate    #Instructions Run" << endl;

    // threadDependency << "------------------------------------------------------------------------------------------------------------" << endl;
    
    map<UINT64, threadDependencyRecord>::iterator itR;
    for (itR = threadDependencyDB.begin(); itR != threadDependencyDB.end(); ++itR) {
        cout << setw(12) << itR->first << "    " 
        << setw(13) << itR->second.parentThread << "    "
        << setw(17) << itR->second.pthreadCreateTime << "    "
        << setw(12) << itR->second.startTime << "    "
        << setw(16) << itR->second.terminateTime  << "    "
        << setw(17) << itR->second.insCount << endl;

        // threadDependency << setw(12) << itR->first << "    " 
        // << setw(13) << itR->second.parentThread << "    "
        // << setw(17) << itR->second.pthreadCreateTime << "    "
        // << setw(12) << itR->second.startTime << "    "
        // << setw(16) << itR->second.terminateTime  << "    "
        // << setw(17) << itR->second.insCount << endl;
    }

    // threadDependency.close();

    // map<UINT64, UINT64>::iterator itr;

    cout << "============================================================================================================" << endl;

    cout << "OS thread ID    PIN thread ID " << endl;

    cout << "------------------------------------------------------------------------------------------------------------" << endl;

    // for (itr = threadMapDB.begin(); itr != threadMapDB.end(); ++itr) {
    //     cout << setw(12) << itr->first << "    " << setw(13) << itr->second << '\n';
    // }

    cout << "============================================================================================================" << endl;

    cout << "Parent thread ID  pthread_create order  start_thread order  Thread ID" << endl;

    cout << "------------------------------------------------------------------------------------------------------------" << endl;

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

    cout << "============================================================================================================" << endl;

    cout << "The end!" << endl;  
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
INT32 Usage()
{
    cerr << "This Pintool prints the routine names and relavent information of each thread" << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[])
{
    // Initialize pin
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
        return Usage();

    // threadDependency = KnobOutputFile.Value().empty() ? &cout : new std::ofstream(KnobOutputFile.Value().c_str());

    // Obtain  a key for TLS storage.
    mlog_key = PIN_CreateThreadDataKey(NULL);
    if (mlog_key == INVALID_TLS_KEY)
    {
        cerr << "number of already allocated keys reached the MAX_CLIENT_TLS_KEYS limit" << endl;
        PIN_ExitProcess(1);
    }

    cout << "Size of MLOG = " << sizeof(MLOG) << endl;

    // open the thread dependency file
    // const string threadDependencyFileName = KnobOutputFile.Value() + ".txt";
    // threadDependency.open(threadDependencyFileName.c_str());

    PIN_InitLock(&global_lock);

    // Register ThreadStart to be called when a thread starts.
    PIN_AddThreadStartFunction(ThreadStart, NULL);

    // Register Fini to be called when thread exits.
    PIN_AddThreadFiniFunction(ThreadFini, NULL);

    // Register Fini to be called when the application exits.
    PIN_AddFiniFunction(Fini, NULL);

    // routines to trace instructions
    INS_AddInstrumentFunction(Instruction, NULL);

    // Start the program, never returns
    PIN_StartProgram();

    return 1;
}
