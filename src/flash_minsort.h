#if !defined(NO_OUTPUT_BUFFER_SORT_BLOCK_HEAP_H)
#define NO_OUTPUT_BUFFER_BLOCK_HEAP_H

#if defined(ARDUINO)
#include "serial_c_iface.h"
#include "file/kv_stdio_intercept.h"
#include "file/sd_stdio_c_iface.h"
#endif

#include <stdint.h>

#include "external_sort.h"

#define SORT_KEY_SIZE       4
#define INT_SIZE            4

#if defined(__cplusplus)
extern "C" {
#endif

/**
@brief      Flash Minsort implemented with full tuple reads.
@param      iteratorState
                Structure stores state of iterator (file info etc.)
@param      tupleBuffer
                Pre-allocated space to store one tuple (row) of input being sorted
@param      outputFile
                Already opened file to store sorting output (and in-progress temporary results)
@param      buffer
                Pre-allocated space used by algorithm during sorting
@param      bufferSizeInBytes
                Size of buffer in bytes
@param      es
                Sorting state info (block size, record size, etc.)
@param      resultFilePtr
                Offset within output file of first output record
@param      metric
                Tracks algorithm metrics (I/Os, comparisons, memory swaps)
@param      compareFn
                Record comparison function for record ordering
*/
int flash_minsort(
        void    *iteratorState,
		void    *tupleBuffer,
        ION_FILE *outputFile,		
		char    *buffer,        
		int     bufferSizeInBytes,
		external_sort_t *es,
		long    *resultFilePtr,
		metrics_t *metric,
        int8_t  (*compareFn)(void *a, void *b)
);

typedef struct MinSortState
{
    char* buffer;
    unsigned int* min;
    
    unsigned int current;           // current smallest value
    unsigned int next;              // keep track of next smallest value for next iteration
    unsigned long int nextIdx; 
                       
    unsigned int record_size;
    unsigned long int num_records;
    unsigned int numBlocks;        
    unsigned int records_per_block;
    unsigned int blocks_per_region;
    unsigned int memoryAvailable;
    unsigned int numRegions;          
    unsigned int regionIdx;
    unsigned int lastBlockIdx;    

    void    *iteratorState;

    /* Statistics */
    unsigned int blocksRead;
    unsigned int tuplesRead;
    unsigned int tuplesOut;
    unsigned int bytesRead;    
} MinSortState;


void  init_MinSort(MinSortState* ms, external_sort_t *es, metrics_t *metric);
char* next_MinSort(MinSortState* ms, external_sort_t *es, void *tupleBuffer, metrics_t *metric);
void close_MinSort(MinSortState* ms, external_sort_t *es);

#if defined(__cplusplus)
}
#endif

#endif
