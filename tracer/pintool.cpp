/*
 * Copyright 2002-2019 Intel Corporation.
 * 
 * This software is provided to you as Sample Source Code as defined in the accompanying
 * End User License Agreement for the Intel(R) Software Development Products ("Agreement")
 * section 1.L.
 * 
 * This software and the related documents are provided as is, with no express or implied
 * warranties, other than those that are expressly stated in the License.
 */

#include <iostream>
#include <fstream>
#include "pin.H"
using std::ostream;
using std::cout;
using std::cerr;
using std::string;
using std::endl;

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

    UINT32 pthread_create_counter;

    UINT32 running_child_thread_counter;

    child_thread_record_node *child_thread_record_root_node;

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
        child_thread_record_root_node->parent_thread_pthread_create_order = pthread_create_counter;
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
    cout << "there 5" << endl;
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

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "", "specify dependency record file name");

INT32 Usage()
{
    cerr << "This Pintool prints the routine names and relavent information of each thread" << endl;
    return -1;
}

INT32 numThreads = 0;
ostream* threadDependency = NULL;

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
    MLOG* mlog = static_cast<MLOG*>(PIN_GetThreadData( mlog_key, threadid));
    
    fwrite(&(mlog->trace), sizeof(trace_instr_format_t), 1, mlog->traceFile);
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


// key for accessing TLS storage in the threads. initialized once in main()
static  TLS_KEY tls_key = INVALID_TLS_KEY;

// This function is called before every block
VOID PIN_FAST_ANALYSIS_CALL docount(UINT32 c, THREADID threadid)
{
    thread_data_t* tdata = static_cast<thread_data_t*>(PIN_GetThreadData(tls_key, threadid));
    tdata->_count += c;
}

VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    numThreads++;
    thread_data_t* tdata = new thread_data_t;
    if (PIN_SetThreadData(tls_key, tdata, threadid) == FALSE)
    {
        cerr << "PIN_SetThreadData failed" << endl;
        PIN_ExitProcess(1);
    }
}


// Pin calls this function every time a new basic block is encountered.
// It inserts a call to docount.
VOID Trace(TRACE trace, VOID *v)
{
    // Visit every basic block  in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // Insert a call to docount for every bbl, passing the number of instructions.

        BBL_InsertCall(bbl, IPOINT_ANYWHERE, (AFUNPTR)docount, IARG_FAST_ANALYSIS_CALL,
                       IARG_UINT32, BBL_NumIns(bbl), IARG_THREAD_ID, IARG_END);
    }
}

// This function is called when the thread exits
VOID ThreadFini(THREADID threadIndex, const CONTEXT *ctxt, INT32 code, VOID *v)
{
    thread_data_t* tdata = static_cast<thread_data_t*>(PIN_GetThreadData(tls_key, threadIndex));
    *threadDependency << "Count[" << decstr(threadIndex) << "] = " << tdata->_count << endl;
    delete tdata;
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    *threadDependency << "Total number of threads = " << numThreads << endl;
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This tool counts the number of dynamic instructions executed" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return 1;
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

    threadDependency = KnobOutputFile.Value().empty() ? &cout : new std::ofstream(KnobOutputFile.Value().c_str());

    // Obtain  a key for TLS storage.
    tls_key = PIN_CreateThreadDataKey(NULL);
    if (tls_key == INVALID_TLS_KEY)
    {
        cerr << "number of already allocated keys reached the MAX_CLIENT_TLS_KEYS limit" << endl;
        PIN_ExitProcess(1);
    }

    // Register ThreadStart to be called when a thread starts.
    PIN_AddThreadStartFunction(ThreadStart, NULL);

    // Register Fini to be called when thread exits.
    PIN_AddThreadFiniFunction(ThreadFini, NULL);

    // Register Fini to be called when the application exits.
    PIN_AddFiniFunction(Fini, NULL);

    // Register Instruction to be called to instrument instructions.
    TRACE_AddInstrumentFunction(Trace, NULL);

    // Start the program, never returns
    PIN_StartProgram();

    return 1;
}
