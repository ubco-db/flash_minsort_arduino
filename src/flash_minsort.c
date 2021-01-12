/******************************************************************************/
/**
@file		flash_minsort.c
@author		Ramon Lawrence
@brief		Flash MinSort (Cossentine/Lawrence 2010) for flash sorting with no writes.
@copyright	Copyright 2020
			The University of British Columbia,
			IonDB Project Contributors (see AUTHORS.md)
@par Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

@par 1.Redistributions of source code must retain the above copyright notice,
	this list of conditions and the following disclaimer.

@par 2.Redistributions in binary form must reproduce the above copyright notice,
	this list of conditions and the following  disclaimer in the documentation
	and/or other materials provided with the distribution.

@par 3.Neither the name of the copyright holder nor the names of its contributors
	may be used to endorse or promote products derived from this software without
	specific prior written permission.

@par THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/
/******************************************************************************/

/*
This is no output sort with block headers and iterator input. Heap used when moving tuples in other blocks.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include <time.h>
#include <math.h>

#include "flash_minsort.h"
#include "in_memory_sort.h"

// #define DEBUG 1
// #define DEBUG_OUTPUT 1
// #define DEBUG_READ 1

void readPage(MinSortState *ms, int pageNum, external_sort_t *es, metrics_t *metric)
{
    file_iterator_state_t* is = (file_iterator_state_t*) ms->iteratorState;
    ION_FILE* fp = is->file;
    
    /* Seek to page location in file */
    unsigned long offset = pageNum;
    offset *= es->page_size;

    int val = fseek(fp, offset, SEEK_SET);
    // printf("Offset: %lu Val: %d\n", offset, val);
    /* Read page into start of buffer */   
    if (0 ==  fread(ms->buffer, es->page_size, 1, fp))
    {	printf("Failed to read page.\n");        
    }
    
    metric->num_reads++;
    
    ms->blocksRead++;     
    ms->lastBlockIdx = pageNum;   
    #ifdef DEBUG_READ
        printf("Reading block: %d",pageNum);    
        printf(" Idx: %d", *((int32_t *) (ms->buffer)));        
        printf(" Rec: %d\r\n", *((int16_t *) (ms->buffer + BLOCK_COUNT_OFFSET)));                
        for (int k = 0; k < 31; k++)
        {
            test_record_t *buf = (void *)(ms->buffer + es->headerSize + k * es->record_size);
            printf("%d: Record: %d\n", k, buf->key);
        }
    #endif
}

/* Returns a value of a tuple given a record number in a block (that has been previously buffered) */
int32_t getValue(MinSortState* ms, int recordNum, external_sort_t *es)
{      
    test_record_t *buf = (test_record_t*) (ms->buffer+es->headerSize+recordNum*es->record_size);
    return buf->key;	    
}

void init_MinSort(MinSortState* ms, external_sort_t *es, metrics_t *metric)
{
     unsigned int i = 0, j = 0, val, regionIdx;    

    /* Operator statistics */
    metric->num_reads = 0;
    metric->num_compar = 0;
    metric->num_writes = 0;
    metric->num_memcpys = 0;

    ms->blocksRead    = 0;
    ms->tuplesRead    = 0;
    ms->tuplesOut     = 0; 
    ms->bytesRead     = 0;
            
    ms->record_size       = es->record_size;    
    ms->numBlocks         = es->num_pages;
    ms->records_per_block =  (es->page_size - es->headerSize) / es->record_size;
    // j = (ms->memoryAvailable - 2 * SORT_KEY_SIZE - INT_SIZE) / SORT_KEY_SIZE;  
    j = ms->memoryAvailable/ SORT_KEY_SIZE;  
    printf("Memory overhead: %d  Max regions: %d\r\n",  2 * SORT_KEY_SIZE + INT_SIZE, j);
    ms->blocks_per_region = (unsigned int) ceil((float) ms->numBlocks / j );                      
    ms->numRegions        = (unsigned int) ceil((float) ms->numBlocks / ms->blocks_per_region);    

    // Memory allocation    
    // Allocate minimum index after block 2 (block 0 is input buffer, block 1 is output buffer)
    // ms->min = (unsigned int*) ms->buffer+es->page_size*2;
    ms->min = malloc(ms->numRegions*sizeof(int));
    // TODO: Temporarily switched to malloc for arrays as was having issues at M=3 and above
    
    printf("Page size: %d, Memory size: %d Record size: %d, Number of records: %lu, Number of blocks: %d, Blocks per region: %d  Regions: %d\r\n", 
                   es->page_size, ms->memoryAvailable, ms->record_size, ms->num_records, ms->numBlocks, ms->blocks_per_region, ms->numRegions);
                    
    for (i=0; i < ms->numRegions; i++)
        ms->min[i] = INT_MAX; 
           	
    /* Scan data to populate the minimum in each region */
    for (i=0; i < ms->numBlocks; i++)
    {                                                                                         
        readPage(ms, i, es, metric);                
        regionIdx = i / ms->blocks_per_region;
               
        /* Process first record in block */    		      
        for (j=0; j < ms->records_per_block; j++)
        {      
            if (((i * ms->records_per_block) + j) < ms->num_records)
            {               
                val = getValue(ms, j, es);        
                metric->num_compar++;
                             
                if (val < ms->min[regionIdx])                
                    ms->min[regionIdx] = val;                                    
            }
            else                         
                break;            
        }
    }
       
     #ifdef DEBUG   
        for (i=0; i < ms->numRegions; i++)
            printf("Region: %d  Min: %d\r\n",i,ms->min[i]); 
      #endif
      
    ms->current = INT_MAX;
    ms->next    = INT_MAX; 
    ms->nextIdx = 0;       
    ms->lastBlockIdx = INT_MAX;
}

char* next_MinSort(MinSortState* ms, external_sort_t *es, void *tupleBuffer, metrics_t *metric)
{    
    unsigned int i, curBlk, startBlk, dataVal;                                                     
    unsigned long int startIndex, k;
                 
    // Find the block with the minimum tuple value - otherwise continue on with last block            
    if (ms->nextIdx == 0)
    {    // Find new block as do not know location of next minimum tuple
        ms->current = INT_MAX;
        ms->regionIdx = INT_MAX; 
        ms->next = INT_MAX; 
        for (i=0; i < ms->numRegions; i++)
        {
             metric->num_compar++;

            if (ms->min[i] < ms->current)
            {   ms->current = ms->min[i];
                ms->regionIdx = i;
            }
        }        
        if (ms->regionIdx == INT_MAX)
            return NULL;    // Join complete - no more tuples	            		
    }
     				
    // Search current region for tuple with current minimum value		        
    startIndex = ms->nextIdx;       
    startBlk = ms->regionIdx * ms->blocks_per_region;
    
    for (k=startIndex/ms->records_per_block; k < ms->blocks_per_region; k++)
    {
       curBlk = startBlk + k;
       if (curBlk != ms->lastBlockIdx)
       {    // Read block into buffer   
            readPage(ms, curBlk, es, metric);                       
       }                        
           
       for (i=startIndex%ms->records_per_block; i < ms->records_per_block; i++)
       {     if (curBlk*ms->records_per_block + i >= ms->num_records)
                    break;
            dataVal = getValue(ms,i, es); 
            metric->num_compar++;    
                
            if (dataVal == ms->current)                
            {   memcpy(tupleBuffer, &(ms->buffer[ms->record_size * i+es->headerSize]), ms->record_size);
                metric->num_memcpys++;     
                #ifdef DEBUG
                    test_record_t *buf = (test_record_t*) (ms->buffer+es->headerSize+i*es->record_size);                    
                    buf = (test_record_t*) tupleBuffer;
                    printf("Returning tuple: %d\n", buf->key);                    
                #endif
                i++;                                          
                ms->tuplesOut++;                  
                goto done;	// Found the record we are looking for
            } 
            metric->num_compar++;    
            if (dataVal > ms->current && dataVal < ms->next)
            {
                ms->next = dataVal;
                ms->nextIdx = 0;
            }
       }
    }           

done:   
    #ifdef DEBUG 
        printf("Updating minimum in region\r\n");
    #endif
    // Now update minimum in block - Scan rest of region after what we found to see if can find a smaller record 
    ms->nextIdx = 0;         
    
    for ( ; k < ms->blocks_per_region; k++)
    {
           curBlk = startBlk + k;
           if (curBlk != ms->lastBlockIdx)
           {    // Read block into buffer  
                readPage(ms, curBlk, es, metric); 
                i = 0;               
           }                        
           
           for ( ; i < ms->records_per_block; i++)
           {    if (curBlk*ms->records_per_block + i >= ms->num_records)
                    break;
                dataVal = getValue(ms,i,es); 
                metric->num_compar++;                    
                if (dataVal == ms->current)                
                {   ms->nextIdx = k*ms->records_per_block+i;   
                    #ifdef DEBUG 
                        printf("Next tuple at: %d  k: %d  i: %d\r\n",ms->nextIdx,k,i); 
                    #endif
                    goto done2;
                } 
                metric->num_compar++; 
                if (dataVal > ms->current && dataVal < ms->next)
                {
                    ms->next = dataVal;
                    ms->nextIdx = 0;
                }
           }
    }           

done2:   
						
    if (ms->nextIdx == 0) 		  
    {	               				
        // Update minimum currently in block
        ms->min[ms->regionIdx] = ms->next;	
        #ifdef DEBUG	
            printf("Updated minimum in block to: %d\r\n", ms->min[ms->regionIdx]);
        #endif					
    }
    return tupleBuffer;		    
}

void close_MinSort(MinSortState* ms, external_sort_t *es)
{
    free(ms->min);
    /*
    printf("Tuples out:  %lu\r\n", ms->op.tuples_out); 
    printf("Blocks read: %lu\r\n", ms->op.blocks_read);
    printf("Tuples read: %lu\r\n", ms->op.tuples_read);  
    printf("Bytes read:  %lu\r\n", ms->op.bytes_read);
    */
}

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
)
{
    printf("*Flash Minsort*\n");
  
    MinSortState ms;
    ms.buffer = buffer;
    ms.iteratorState = iteratorState;
    ms.memoryAvailable = bufferSizeInBytes;
    ms.num_records = ((file_iterator_state_t*) iteratorState)->totalRecords;

    init_MinSort(&ms, es, metric);
    int16_t count = 0;  
    int32_t blockIndex = 0;
    int16_t values_per_page = (es->page_size - es->headerSize) / es->record_size;
    char* outputBuffer = buffer+es->page_size;        

    // Write 
    while (next_MinSort(&ms, es, (char*) (outputBuffer+count*es->record_size+es->headerSize), metric) != NULL)
    {         
        // Store record in block (already done during call to next)                     
        count++;

        if (count == values_per_page)
        {   // Write block
            *((int32_t *) outputBuffer) = blockIndex;                             /* Block index */
            *((int16_t *) (outputBuffer + BLOCK_COUNT_OFFSET)) = count;            /* Block record count */
            count=0;
           
            if (0 == fwrite(outputBuffer, es->page_size, 1, outputFile))
                return 9;

        #ifdef DEBUG_OUTPUT
            printf("Wrote output block. Block index: %d\n", blockIndex);
            for (int k = 0; k < values_per_page; k++)
            {
                test_record_t *buf = (void *)(outputBuffer + es->headerSize + k * es->record_size);
                printf("%d: Output Record: %d\n", k, buf->key);
            }
        #endif
            blockIndex++;
        }       
    }

    if (count > 0)
    {
        // Write last block        
        *((int32_t *) buffer) = blockIndex;                             /* Block index */
        *((int16_t *)(buffer + BLOCK_COUNT_OFFSET)) = count;            /* Block record count */
        count=0;
        blockIndex++;
        if (0 == fwrite(buffer, es->page_size, 1, outputFile))
            return 9;
    }
     
    close_MinSort(&ms, es);

    *resultFilePtr = 0;
    return 0;
}
