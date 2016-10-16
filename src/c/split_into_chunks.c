//
//  split_into_chunks.c
//  
//
//  Created by Radhakrishnan, Regunathan on 4/22/13.
//
//

#include <stdio.h>
#include <stdlib.h>




int main (int argc, char ** argv)
{
    char* input_filename;
    char* out_filename;
    int chunk_size;
    FILE *input_fptr;
    FILE *out_fptr;
    //struct stat st;
    long file_size;
    int numbytes_read = 0;
    int numchunks;
    int idx;
    unsigned char *buffer;
    int rem;
    
    input_filename = (char*)calloc(sizeof(char),500);
    out_filename = (char*)calloc(sizeof(char),500);
    
    sprintf(input_filename,"%s",argv[1]);
    chunk_size = atoi(argv[2]);
    
    buffer = (unsigned char*)calloc(sizeof(unsigned char),chunk_size);
    
    input_fptr = fopen(input_filename,"rb");
    
    fseek(input_fptr, 0, SEEK_END); // seek to end of file
    file_size = ftell(input_fptr); // get current file pointer
    fseek(input_fptr, 0, SEEK_SET); // seek back to beginning of file
    
    printf("file size = %ld\n",file_size);
    
    printf("num chunks = %f\n",file_size/(float)chunk_size);
    
    printf("splitting into chunks...\n");
    
    numchunks = file_size/chunk_size;
    
    idx = 0;
    
    while(idx < numchunks)
    {
        numbytes_read = fread(buffer,1,chunk_size,input_fptr);
        sprintf(out_filename,"%s_chunk%d",input_filename,idx+1);
        out_fptr = fopen(out_filename,"wb");
        fwrite(buffer,1,chunk_size,out_fptr);
        fclose(out_fptr);
        idx = idx + 1;
        
    }
    rem = file_size - (numchunks*chunk_size);
    numbytes_read = fread(buffer,1,rem,input_fptr);
    sprintf(out_filename,"%s_chunk%d",input_filename,idx+1);
    out_fptr = fopen(out_filename,"wb");
    fwrite(buffer,1,rem,out_fptr);
    fclose(out_fptr);
    fclose(input_fptr);
    
}