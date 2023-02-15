#include <stdio.h>
#include <thread>
#include <stdlib.h>
#include <mutex>
#include <time.h>

#define DIMENSION 50

using std::thread;
using std::mutex;

mutex lock;

int **arrayA;
int **arrayB;
int **arrayC;

int counter = 0;

/*
void matrixMultiplication()
{
    lock.lock();

    int column = counter % DIMENSION;
    int row = (counter - column) / DIMENSION;

    int tmpp = 0;

    for (int i = 0; i < DIMENSION; i++)
    {        
        tmpp = tmpp + arrayA[row][i] * arrayB[i][column];              
    }

    arrayC[row][column] = tmpp;
    
    counter++;

    lock.unlock();

    if (counter >= (DIMENSION * DIMENSION - 1))
    {
        return;
    }
    
}
*/
void spawnThreads()
{
    printf("spawn threads\n");
    // thread t1(matrixMultiplication);
    // thread t2(matrixMultiplication);
    // thread t3(matrixMultiplication);
    
    // t1.join();
    // t2.join();
    // t3.join();    
    return;
}

int main() 
{
    printf("Multi-threaded application for matrix multiplication with contension.\n");   
    
    // printf("Lock memory location: %lx\n", (unsigned long)&lock);

    // init
    arrayA = new int*[DIMENSION];
    arrayB = new int*[DIMENSION];
    arrayC = new int*[DIMENSION];

    for (int i = 0; i < DIMENSION; i++)
    {
        arrayA[i] = new int[DIMENSION];
        arrayB[i] = new int[DIMENSION];
        arrayC[i] = new int[DIMENSION];
    }

    // set values
    srand (time(NULL));

    for (int i = 0; i < DIMENSION; i++)
    {
        for (int j = 0; j < DIMENSION; j++)
        {
            arrayA[i][j] = rand() % 1000;
        }        
    }

    for (int i = 0; i < DIMENSION; i++)
    {
        for (int j = 0; j < DIMENSION; j++)
        {
            arrayB[i][j] = rand() % 1000;
        }        
    }

    thread t1(spawnThreads);
    // thread t2(spawnThreads);
    // thread t3(spawnThreads);
    
    t1.join();
    // t2.join();
    // t3.join();

    // free memory
    for (int i = 0; i < DIMENSION; i++)
    {
        delete[] arrayA[i];
        delete[] arrayB[i];
        delete[] arrayC[i];
    }

    delete[] arrayA;
    delete[] arrayB;
    delete[] arrayC;

    printf("Application exits!\n");

    return 0;
}
