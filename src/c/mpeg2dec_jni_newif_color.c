//
//  mpeg2dec_jni.c
//  
//
//  Created by Radhakrishnan, Regunathan on 4/25/13.
//
//

/*
 * mpeg2dec.c
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 * See http://libmpeg2.sourceforge.net/ for updates.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdio.h>
#include <jni.h>
#include "mpeg2decJNIcall.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#ifdef HAVE_IO_H
#include <fcntl.h>
#include <io.h>
#endif
#ifdef LIBVO_SDL
#include <SDL/SDL.h>
#endif
#include <inttypes.h>

#include "mpeg2.h"
#include "../include/attributes.h"
#include "../libmpeg2/mpeg2_internal.h"


#include "video_out.h"
#include "gettimeofday.h"

#include "jpeglib.h"

static int buffer_size = 4096;
static FILE * in_file;
static FILE * in_file_chunk;
static char* mpeg2decstate_fname;//filename for saving mpeg2dec struct
static int chunk_file_present = 0;
static int mpeg2dec_state_ready = 0;
static int myframecnt = 0;
static int demux_track = 0;
static int demux_pid = 0;
static int demux_pva = 0;
static mpeg2dec_t * mpeg2dec;
static mpeg2dec_t * mpeg2dec_copy;
static vo_open_t * output_open = NULL;
static vo_instance_t * output;
static int sigint = 0;
static int total_offset = 0;
static int verbose = 0;

static jint *seq_info_c_array;
static jbyte* bufferPtr;
static int buf_length;
static int tot_bytes_read = 0;

static jbyte* jpg_outputbufferPtr;
static int jpg_outputbuffer_len;
static int tot_bytes_written;
static jint* jpg_indexvec_c_array;
static jint* act_numframes_c_array;
static int written_to_buf =0;
static int not_able_to_write_tobuf = 0;

typedef struct {
struct jpeg_destination_mgr pub; /* base class */
JOCTET* buffer; /* buffer start address */
int bufsize; /* size of buffer */
size_t datasize; /* final size of compressed data */
int* outsize; /* user pointer to datasize */
int errcount; /* counts up write errors due to
buffer overruns */
} memory_destination_mgr;

typedef memory_destination_mgr* mem_dest_ptr;

//variable for colorspace conversion
int Inverse_Table_6_9[8][4]
=
{
  {117504, 138453, 13954, 34903}, /* no sequence_display_extension */
  {117504, 138453, 13954, 34903}, /* ITU-R Rec. 709 (1990) */
  {104597, 132201, 25675, 53279}, /* unspecified */
  {104597, 132201, 25675, 53279}, /* reserved */
  {104448, 132798, 24759, 53109}, /* FCC */
  {104597, 132201, 25675, 53279}, /* ITU-R Rec. 624-4 System B, G */
  {104597, 132201, 25675, 53279}, /* SMPTE 170M */
  {117579, 136230, 16907, 35559}  /* SMPTE 240M (1987) */
};
static unsigned char* Clip;


void dump_state (FILE * f, mpeg2_state_t state, const mpeg2_info_t * info,
                 int offset, int verbose);

void read_data_ascii(char* mpegdec_config_fname,mpeg2dec_t* mpegdec_from_file);

void read_data_seq_array(mpeg2dec_t* mpegdec_from_file);

void read_data(char* mpegdec_config_fname,mpeg2dec_t* mpegdec_from_file);

void write_jpeg_file(uint8_t* pFrame, int w, int h,  int iFrame,int write_to_mem);

void SaveFrame(uint8_t* pFrame, int width, int height, int iFrame);

void write_data(char* mpegdec_config_fname, mpeg2dec_t* data );

void write_data_ascii(char* mpegdec_config_fname, mpeg2dec_t* data );

void conv422to444(unsigned char *src,unsigned char *dst, int Coded_Picture_Width, int Coded_Picture_Height);

void convert_yuv422p_rgb(unsigned char *src[],unsigned char *pPPM[],int offset, int incr, int height,int Coded_Picture_Width, int Coded_Picture_Height);

/* horizontal 1:2 interpolation filter */
void conv422to444(unsigned char *src,unsigned char *dst, int Coded_Picture_Width, int Coded_Picture_Height)
{
  int i, i2, w, j, im3, im2, im1, ip1, ip2, ip3;

  w = Coded_Picture_Width>>1;

    for (j=0; j<Coded_Picture_Height; j++)
    {
      for (i=0; i<w; i++)
      {
        i2 = i<<1;
        im2 = (i<2) ? 0 : i-2;
        im1 = (i<1) ? 0 : i-1;
        ip1 = (i<w-1) ? i+1 : w-1;
        ip2 = (i<w-2) ? i+2 : w-1;
        ip3 = (i<w-3) ? i+3 : w-1;

        /* FIR filter coefficients (*256): 21 0 -52 0 159 256 159 0 -52 0 21 */
        /* even samples (0 0 256 0 0) */
        dst[i2] = src[i];

        /* odd samples (21 -52 159 159 -52 21) */
        dst[i2+1] = Clip[(int)(21*(src[im2]+src[ip3])
                        -52*(src[im1]+src[ip2]) 
                       +159*(src[i]+src[ip1])+128)>>8];
      }
      src+= w;
      dst+= Coded_Picture_Width;
    }
}

void convert_yuv422p_rgb(unsigned char *src[],unsigned char *pPPM[],int offset, int incr, int height,int Coded_Picture_Width, int Coded_Picture_Height)
{
  int i, j;
  int y, u, v, r, g, b;
  int crv, cbu, cgu, cgv;
  unsigned char *py, *pu, *pv;
  static unsigned char *u422, *v422, *u444, *v444;
  int k=0;
  int uloc,vloc;
  int matrix_coefficients = 5;
  int horizontal_size = Coded_Picture_Width;

   if (!(u444 = (unsigned char *)malloc(Coded_Picture_Width*Coded_Picture_Height)))
        Error("malloc failed");

   if (!(v444 = (unsigned char *)malloc(Coded_Picture_Width*Coded_Picture_Height)))
        Error("malloc failed");

    
   uloc = Coded_Picture_Width*Coded_Picture_Height;
   vloc = uloc + (Coded_Picture_Width/2)*Coded_Picture_Height;

   printf("uloc: %d\t vloc: %d\n",uloc,vloc);
   
    conv422to444(src+uloc,u444,Coded_Picture_Width,Coded_Picture_Height);
    conv422to444(src+vloc,v444,Coded_Picture_Width,Coded_Picture_Height);

   printf("after 422 to 444\n");

     /* matrix coefficients */
  crv = Inverse_Table_6_9[matrix_coefficients][0];
  cbu = Inverse_Table_6_9[matrix_coefficients][1];
  cgu = Inverse_Table_6_9[matrix_coefficients][2];
  cgv = Inverse_Table_6_9[matrix_coefficients][3];

  printf("entering yuv to rgb conv\n");
  for (i=0; i<height; i++)
  {
    py = src + offset + incr*i;
    pu = u444 + offset + incr*i;
    pv = v444 + offset + incr*i;

    for (j=0; j<horizontal_size; j++)
    {
      u = *pu++ - 128;
      v = *pv++ - 128;
      y = 76309 * (*py++ - 16); /* (255/219)*65536 */
      r = Clip[(y + crv*v + 32768)>>16];
      g = Clip[(y - cgu*u - cgv*v + 32768)>>16];
      b = Clip[(y + cbu*u + 32786)>>16];

      pPPM[k++] = r;	
      pPPM[k++] = g;	
      pPPM[k++] = b;
    }
  }
  printf("after yuv to rgb\n");




}




#ifdef HAVE_GETTIMEOFDAY

static RETSIGTYPE signal_handler (int sig)
{
    sigint = 1;
    signal (sig, SIG_DFL);
    return (RETSIGTYPE)0;
}

METHODDEF(void)
init_destination (j_compress_ptr cinfo)
{
	mem_dest_ptr dest = (mem_dest_ptr)cinfo->dest;

	dest->pub.next_output_byte = dest->buffer; /* set destination buffer */
	dest->pub.free_in_buffer = dest->bufsize; /* input buffer size */
	dest->datasize = 0; /* reset output size */
	dest->errcount = 0; /* reset error count */
}

METHODDEF(boolean)empty_output_buffer (j_compress_ptr cinfo)
{
	mem_dest_ptr dest = (mem_dest_ptr)cinfo->dest;
	dest->pub.next_output_byte = dest->buffer;
	dest->pub.free_in_buffer = dest->bufsize;
	++dest->errcount; /* need to increase error count */

	return TRUE;
}


METHODDEF(void)term_destination (j_compress_ptr cinfo)
{
	mem_dest_ptr dest = (mem_dest_ptr)cinfo->dest;
	dest->datasize = dest->bufsize - dest->pub.free_in_buffer;
	if (dest->outsize)
		 *dest->outsize += (int)dest->datasize;
}

GLOBAL(void) jpeg_memory_dest (j_compress_ptr cinfo, JOCTET* buffer, int bufsize, int* outsize)
{
	mem_dest_ptr dest;

	/* first call for this instance - need to setup */
	if (cinfo->dest == 0)
	{
		cinfo->dest = (struct jpeg_destination_mgr *)
		(*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
		sizeof (memory_destination_mgr));
	}

	dest = (mem_dest_ptr) cinfo->dest;
	dest->bufsize = bufsize;
	dest->buffer = buffer;
	dest->outsize = outsize;
	/* set method callbacks */
	dest->pub.init_destination = init_destination;
	dest->pub.empty_output_buffer = empty_output_buffer;
	dest->pub.term_destination = term_destination;
}






void write_jpeg_file(uint8_t* pFrame, int w, int h,  int iFrame,int write_to_mem )
{
    unsigned char* rgb_buf;
    JSAMPLE* image_buffer;
    int      image_width  = w;
    int      image_height = h;
    char filename[500];
    int quality = 100;
    int x,y,idx = 0;
    
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE* outfile;
    int numBytes_compjpg = 0;
    unsigned char* temp_storage;
    JOCTET *jpgbuff;	
    int expected_jpg_outputsize = 350000;
    
    JSAMPROW row_pointer[1];    /* pointer to JSAMPLE row[s] */
    int row_stride;             /* physical row width in image buffer */
    
    
    rgb_buf = (unsigned char*)calloc(sizeof(unsigned char),w*h*3);

    

    temp_storage = (unsigned char*)calloc(sizeof(unsigned char),expected_jpg_outputsize);//expecting output jpg to be smaller than 350k
    
    printf("after calloc rgb_buf\n");
    
   /* idx = 0;
    for(y=0; y<image_height; y++)
    {
        //for(ln = 0; ln < 3; ln++)
        //{
        for(x=0; x<image_width; x++)
        {
            rgb_buf[idx] = pFrame[x + (y*image_width)];
            idx++;
            rgb_buf[idx] = pFrame[x + (y*image_width)];
            idx++;
            rgb_buf[idx] = pFrame[x + (y*image_width)];
            idx++;
            //fwrite(&(pFrame[x + (y*width)]),1,1,pFile);//r val
            //fwrite(&(pFrame[x + (y*width)]),1,1,pFile);//g val
            //fwrite(&(pFrame[x + (y*width)]),1,1,pFile);//b val
            //printf("%d \t %d\n",x,y);
        }
        //}
        
    }*/

    convert_yuv422p_rgb(pFrame,rgb_buf,0, w, h,w, h);
    
    printf("after yuv422p_rgb\n");

    image_buffer = (JSAMPLE*)rgb_buf;
    
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    if(write_to_mem == 0)
    {
    
    	sprintf(filename,"frame%03d.jpg",iFrame);
    	if ((outfile = fopen(filename, "wb")) == NULL) {
        	fprintf(stderr, "can't open %s\n", filename);
        	exit(1);
    	}
    
    	jpeg_stdio_dest(&cinfo, outfile);
    }
    else
    {
	//write to memory
	jpgbuff = (JOCTET*)temp_storage;
        jpeg_memory_dest(&cinfo,jpgbuff,expected_jpg_outputsize,&numBytes_compjpg);
    }
    cinfo.image_width = image_width;
    cinfo.image_height = image_height;
    cinfo.input_components = 3;         /* # of color components per pixel */
    cinfo.in_color_space = JCS_RGB;//JCS_YCbCr; /* colorspace of input image */
    
    jpeg_set_defaults(&cinfo);
    
    jpeg_set_quality(&cinfo,quality, TRUE /* limit to baseline-JPEG values */);
    
    jpeg_start_compress(&cinfo, TRUE);
    
    row_stride = image_width * 3;       /* JSAMPLEs per row in image_buffer */
    
    while (cinfo.next_scanline < cinfo.image_height)
    {
        row_pointer[0] = & image_buffer[cinfo.next_scanline * row_stride];
        (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    

    jpeg_finish_compress(&cinfo);

    if(write_to_mem == 1)
    {
        if(jpg_outputbuffer_len > tot_bytes_written + numBytes_compjpg)//safe to write
        {
		printf("frame %d compressed jpg size: %d\n",iFrame,numBytes_compjpg);
		memcpy(jpg_outputbufferPtr,jpgbuff,numBytes_compjpg);
                tot_bytes_written = tot_bytes_written + numBytes_compjpg;
                jpg_indexvec_c_array[written_to_buf] = tot_bytes_written;
                written_to_buf++;
                act_numframes_c_array[0] = written_to_buf;
        }
        else
        {
               //jpg outputbuffer not enough
               printf("frame %d: jpg output buffer not enough",iFrame);
               not_able_to_write_tobuf++;  
        }
    }


    jpeg_destroy_compress(&cinfo);
    free(rgb_buf);
    
}


static void print_fps (int final)
{
    static uint32_t frame_counter = 0;
    static struct timeval tv_beg, tv_start;
    static int total_elapsed;
    static int last_count = 0;
    struct timeval tv_end;
    double fps, tfps;
    int frames, elapsed;
    
    if (verbose)
        return;
    
    gettimeofday (&tv_end, NULL);
    
    if (!frame_counter) {
        tv_start = tv_beg = tv_end;
        signal (SIGINT, signal_handler);
    }
    
    elapsed = (tv_end.tv_sec - tv_beg.tv_sec) * 100 +
	(tv_end.tv_usec - tv_beg.tv_usec) / 10000;
    total_elapsed = (tv_end.tv_sec - tv_start.tv_sec) * 100 +
	(tv_end.tv_usec - tv_start.tv_usec) / 10000;
    
    if (final) {
        if (total_elapsed)
            tfps = frame_counter * 100.0 / total_elapsed;
        else
            tfps = 0;
        
        fprintf (stderr,"\n%d frames decoded in %.2f seconds (%.2f fps)\n",
                 frame_counter, total_elapsed / 100.0, tfps);
        
        return;
    }
    
    frame_counter++;
    
    if (elapsed < 50)	/* only display every 0.50 seconds */
        return;
    
    tv_beg = tv_end;
    frames = frame_counter - last_count;
    
    fps = frames * 100.0 / elapsed;
    tfps = frame_counter * 100.0 / total_elapsed;
    
    fprintf (stderr, "%d frames in %.2f sec (%.2f fps), "
             "%d last %.2f sec (%.2f fps)\033[K\r", frame_counter,
             total_elapsed / 100.0, tfps, frames, elapsed / 100.0, fps);
    
    last_count = frame_counter;
}

#else /* !HAVE_GETTIMEOFDAY */

static void print_fps (int final)
{
}

#endif

static void print_usage (char ** argv)
{
    int i;
    vo_driver_t const * drivers;
    
    fprintf (stderr, "usage: "
             "%s [-h] [-o <mode>] [-s [<track>]] [-t <pid>] [-p] [-c] \\\n"
             "\t\t[-v] [-b <bufsize>] <file>\n"
             "\t-h\tdisplay help and available video output modes\n"
             "\t-s\tuse program stream demultiplexer, "
             "track 0-15 or 0xe0-0xef\n"
             "\t-t\tuse transport stream demultiplexer, pid 0x10-0x1ffe\n"
             "\t-p\tuse pva demultiplexer\n"
             "\t-c\tuse c implementation, disables all accelerations\n"
             "\t-v\tverbose information about the MPEG stream\n"
             "\t-b\tset input buffer size, default 4096 bytes\n"
             "\t-o\tvideo output mode\n", argv[0]);
    
    drivers = vo_drivers ();
    for (i = 0; drivers[i].name; i++)
        fprintf (stderr, "\t\t\t%s\n", drivers[i].name);
    
    exit (1);
}

static void handle_args (int argc, char ** argv)
{
    int c;
    vo_driver_t const * drivers;
    int i;
    char * s;
    
    drivers = vo_drivers ();
    while ((c = getopt (argc, argv, "hsa::t:pco:vb::")) != -1)
    {
        printf("after getopt option %c",c);
        switch (c) {
            case 'o':
                for (i = 0; drivers[i].name != NULL; i++)
                    if (strcmp (drivers[i].name, optarg) == 0)
                        output_open = drivers[i].open;
                if (output_open == NULL) {
                    fprintf (stderr, "Invalid video driver: %s\n", optarg);
                    print_usage (argv);
                }
                break;
                
            case 's':
                demux_track = 0xe0;
                if (optarg != NULL) {
                    demux_track = strtol (optarg, &s, 0);
                    if (demux_track < 0xe0)
                        demux_track += 0xe0;
                    if (demux_track < 0xe0 || demux_track > 0xef || *s) {
                        fprintf (stderr, "Invalid track number: %s\n", optarg);
                        print_usage (argv);
                    }
                }
                break;
                
            case 't':
                demux_pid = strtol (optarg, &s, 0);
                if (demux_pid < 0x10 || demux_pid > 0x1ffe || *s) {
                    fprintf (stderr, "Invalid pid: %s\n", optarg);
                    print_usage (argv);
                }
                break;
                
            case 'p':
                demux_pva = 1;
                break;
                
            case 'c':
                mpeg2_accel (0);
                break;
                
            case 'v':
                if (++verbose > 4)
                    print_usage (argv);
                break;
                
            case 'b':
                buffer_size = 1;
                if (optarg != NULL) {
                    buffer_size = strtol (optarg, &s, 0);
                    if (buffer_size < 1 || *s) {
                        fprintf (stderr, "Invalid buffer size: %s\n", optarg);
                        print_usage (argv);
                    }
                }
                break;
            case 'a':
                //regu: additional chunk file
                //in_file_chunk = fopen (optarg, "rb");
               // if (!in_file_chunk)
               // {
               //     printf("chunk file %s cannot be opened\n",optarg);
                //    exit(1);
               // }
                chunk_file_present = 1;
                printf("chunk file opened %s\n",optarg);
                break;
                
            default:
                print_usage (argv);
	}
     }
    
    /* -o not specified, use a default driver */
    if (output_open == NULL)
        output_open = drivers[0].open;
    
    
    /*if (optind < argc) {
        in_file = fopen (argv[optind], "rb");
        
        if (!in_file) {
            fprintf (stderr, "%s - could not open file %s\n", strerror (errno),
                    argv[optind]);
           exit (1);
        }
    } else
       in_file = stdin;*/
}

static void * malloc_hook (unsigned size, mpeg2_alloc_t reason)
{
    void * buf;
    
    /*
     * Invalid streams can refer to fbufs that have not been
     * initialized yet. For example the stream could start with a
     * picture type onther than I. Or it could have a B picture before
     * it gets two reference frames. Or, some slices could be missing.
     *
     * Consequently, the output depends on the content 2 output
     * buffers have when the sequence begins. In release builds, this
     * does not matter (garbage in, garbage out), but in test code, we
     * always zero all our output buffers to:
     * - make our test produce deterministic outputs
     * - hint checkergcc that it is fine to read from all our output
     *   buffers at any time
     */
    if ((int)reason < 0) {
        return NULL;
    }
    buf = mpeg2_malloc (size, (mpeg2_alloc_t)-1);
    if (buf && (reason == MPEG2_ALLOC_YUV || reason == MPEG2_ALLOC_CONVERTED))
        memset (buf, 0, size);
    return buf;
}

void SaveFrame(uint8_t* pFrame, int width, int height, int iFrame) {
    FILE *pFile;
    char szFilename[500];
    int x,y,ln;
    int offset;   
    int widthby2;
 
    // Open file
    sprintf(szFilename, "frame%d_Y.ppm", iFrame);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
        return;
    
    printf("inside save frame %d \t %d\n",width,height);
    
    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width,height);
    
    // Write pixel data
    //for(y=0; y<2*height; y++)
    for(y = 0; y < height; y++)
    {
        //for(ln = 0; ln < 3; ln++)
        //{
        for(x=0; x<width; x++)
        {
            fwrite(&(pFrame[x + (y*width)]),1,1,pFile);//r val
            fwrite(&(pFrame[x + (y*width)]),1,1,pFile);//g val
            fwrite(&(pFrame[x + (y*width)]),1,1,pFile);//b val
            //printf("%d \t %d\n",x,y);
        }
        //}
        
    }
    //fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);
    
    // Close file
    fclose(pFile);


  // Open file
    sprintf(szFilename, "frame%d_U.ppm", iFrame);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
        return;
    
    printf("inside save frame %d \t %d\n",width,height);
    
    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width/2,height);
    
    // Write pixel data
    //for(y=0; y<2*height; y++)
    offset = height*width;
    widthby2 = width/2;
    for(y = 0; y < height; y++)
    {
        //for(ln = 0; ln < 3; ln++)
        //{
        for(x=0; x<widthby2; x++)
        {
            fwrite(&(pFrame[offset+x + (y*width)]),1,1,pFile);//r val
            fwrite(&(pFrame[offset+x + (y*width)]),1,1,pFile);//g val
            fwrite(&(pFrame[offset+x + (y*width)]),1,1,pFile);//b val
            //printf("%d \t %d\n",x,y);
        }
        //}
    
    }
    //fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);
    
    // Close file
    fclose(pFile);

   // Open file
    sprintf(szFilename, "frame%d_V.ppm", iFrame);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
        return;

    printf("inside save frame %d \t %d\n",width,height);

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width/2,height);

    // Write pixel data
    //for(y=0; y<2*height; y++)
    offset = height*width+(height*width/2);
    widthby2 = width/2;
    for(y = 0; y < height; y++)
    {
        //for(ln = 0; ln < 3; ln++)
        //{
        for(x=0; x<widthby2; x++)
        {
            fwrite(&(pFrame[offset+x + (y*width)]),1,1,pFile);//r val
            fwrite(&(pFrame[offset+x + (y*width)]),1,1,pFile);//g val
            fwrite(&(pFrame[offset+x + (y*width)]),1,1,pFile);//b val
            //printf("%d \t %d\n",x,y);
        }
        //}
    
    }
    //fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);
    
    // Close file
    fclose(pFile);

}

/* Writing */

void write_data(char* mpegdec_config_fname, mpeg2dec_t* data )
{
    int fd = 0;
    FILE * fp;
    // open the file in read/write mode
    //fd = open( mpegdec_config_fname, O_RDWR|O_CREAT|O_APPEND );
    
    fp = fopen(mpegdec_config_fname,"wb");
    
    //if(fd == -1)
    if(fp == NULL)
    {
        printf("cannot open %s for saving mpegdec struct\n",mpegdec_config_fname);
        exit(1);
    }
    
    // write the binary structure right to the file
    //write(fd, (mpeg2dec_t *) data, sizeof (mpeg2dec_t));
    //close(fd);
    fwrite(&(data->sequence),1,sizeof (mpeg2_sequence_t),fp);
    fclose(fp);
}

void write_data_ascii(char* mpegdec_config_fname, mpeg2dec_t* data )
{
    int fd = 0;
    FILE * fp;
    // open the file in read/write mode
    //fd = open( mpegdec_config_fname, O_RDWR|O_CREAT|O_APPEND );
    
    fp = fopen(mpegdec_config_fname,"w");
    
    //if(fd == -1)
    if(fp == NULL)
    {
        printf("cannot open %s for saving mpegdec struct\n",mpegdec_config_fname);
        exit(1);
    }
    
    // write the binary structure right to the file
    //write(fd, (mpeg2dec_t *) data, sizeof (mpeg2dec_t));
    //close(fd);
    //fwrite(&(data->sequence),1,sizeof (mpeg2_sequence_t),fp);
    fprintf(fp,"%d\n",data->sequence.width);//width
    fprintf(fp,"%d\n",data->sequence.height);//height
    fprintf(fp,"%d\n",data->sequence.chroma_width);//chroma_width
    fprintf(fp,"%d\n",data->sequence.chroma_height);//chroma_height
    fprintf(fp,"%d\n",data->sequence.byte_rate); //byte_rate
    fprintf(fp,"%d\n",data->sequence.vbv_buffer_size); //vbv_buffer_size
    fprintf(fp,"%d\n",data->sequence.flags); //flags
    fprintf(fp,"%d\n",data->sequence.picture_width);//picture_width
    fprintf(fp,"%d\n",data->sequence.picture_height);//picture_height
    fprintf(fp,"%d\n",data->sequence.display_width);//display_width
    fprintf(fp,"%d\n",data->sequence.display_height);//display_height
    fprintf(fp,"%d\n",data->sequence.pixel_width);//pixel_width
    fprintf(fp,"%d\n",data->sequence.pixel_height);//pixel_height
    fprintf(fp,"%d\n",data->sequence.frame_period);//frame_period
    fprintf(fp,"%u\n",data->sequence.profile_level_id);//profile_level_id uint8_t
    fprintf(fp,"%u\n",data->sequence.colour_primaries);//colour_primaries uint8_t
    fprintf(fp,"%u\n",data->sequence.transfer_characteristics);//transfer_characteristics uint8_t
    fprintf(fp,"%u\n",data->sequence.matrix_coefficients);//matrix_coefficients uint8_t
    fclose(fp);
}


void read_data(char* mpegdec_config_fname,mpeg2dec_t* mpegdec_from_file)
{
    int fd = 0;
    FILE * fp;
    
    // open the file
    //fd = open( mpegdec_config_fname, O_RDONLY );
    fp = fopen( mpegdec_config_fname,"rb");
    
    //if(fd == -1)
    if(fp == NULL)
    {
        printf("cannot open %s for reading mpegdec struct\n",mpegdec_config_fname);
        exit(1);
    }
    
    
    // read the data into 'data' until we read the end of the file
    /*while( read( fd,(mpeg2dec_t *)  mpegdec_from_file, sizeof (mpeg2dec_t )))
     {
     //printf("Just read:\nfirst: %s\nlast\nage: %d\ncreate time: %d\n",
     //      data.first_name, data.last_name, data.age, data.create_time );
     }
     
     // close file
     close( fd );*/
    printf("before fread in read_data\n");
    fread(&(mpegdec_from_file->sequence),1,sizeof(mpeg2_sequence_t),fp);
    printf("after fread in read_data\n");
    fclose(fp);
}

void read_data_seq_array(mpeg2dec_t* mpegdec_from_file)
{

    mpegdec_from_file->sequence.width = seq_info_c_array[0];//width
    mpegdec_from_file->sequence.height = seq_info_c_array[1];//height
    mpegdec_from_file->sequence.chroma_width = seq_info_c_array[2];//chroma_width
    mpegdec_from_file->sequence.chroma_height = seq_info_c_array[3];//chroma_height
    mpegdec_from_file->sequence.byte_rate = seq_info_c_array[4]; //byte_rate
    mpegdec_from_file->sequence.vbv_buffer_size = seq_info_c_array[5]; //vbv_buffer_size
    mpegdec_from_file->sequence.flags = seq_info_c_array[6]; //flags
    mpegdec_from_file->sequence.picture_width = seq_info_c_array[7];//picture_width
    mpegdec_from_file->sequence.picture_height = seq_info_c_array[8];//picture_height
    mpegdec_from_file->sequence.display_width = seq_info_c_array[9];//display_width
    mpegdec_from_file->sequence.display_height = seq_info_c_array[10];//display_height
    mpegdec_from_file->sequence.pixel_width = seq_info_c_array[11];//pixel_width
    mpegdec_from_file->sequence.pixel_height = seq_info_c_array[12];//pixel_height
    mpegdec_from_file->sequence.frame_period = seq_info_c_array[13];//frame_period
    mpegdec_from_file->sequence.profile_level_id = (uint8_t)seq_info_c_array[14];//profile_level_id uint8_t
    mpegdec_from_file->sequence.colour_primaries = (uint8_t)seq_info_c_array[15];//colour_primaries uint8_t
    mpegdec_from_file->sequence.transfer_characteristics = (uint8_t)seq_info_c_array[16] ;//transfer_characteristics uint8_t
    mpegdec_from_file->sequence.matrix_coefficients = (uint8_t)seq_info_c_array[17];//matrix_coefficients uint8_t

   printf("after filling seq info from java array\n");

}

void read_data_ascii(char* mpegdec_config_fname,mpeg2dec_t* mpegdec_from_file)
{
    int fd = 0;
    FILE * fp;
    
    // open the file
    //fd = open( mpegdec_config_fname, O_RDONLY );
    fp = fopen( mpegdec_config_fname,"r");
    
    //if(fd == -1)
    if(fp == NULL)
    {
        printf("cannot open %s for reading mpegdec struct\n",mpegdec_config_fname);
        exit(1);
    }
    
    
    // read the data into 'data' until we read the end of the file
    /*while( read( fd,(mpeg2dec_t *)  mpegdec_from_file, sizeof (mpeg2dec_t )))
     {
     //printf("Just read:\nfirst: %s\nlast\nage: %d\ncreate time: %d\n",
     //      data.first_name, data.last_name, data.age, data.create_time );
     }
     
     // close file
     close( fd );*/
    printf("before fread in read_data\n");
    //fread(&(mpegdec_from_file->sequence),1,sizeof(mpeg2_sequence_t),fp);
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.width));//width
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.height));//height
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.chroma_width));//chroma_width
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.chroma_height));//chroma_height
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.byte_rate)); //byte_rate
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.vbv_buffer_size)); //vbv_buffer_size
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.flags)); //flags
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.picture_width));//picture_width
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.picture_height));//picture_height
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.display_width));//display_width
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.display_height));//display_height
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.pixel_width));//pixel_width
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.pixel_height));//pixel_height
    fscanf(fp,"%d\n",&(mpegdec_from_file->sequence.frame_period));//frame_period
    fscanf(fp,"%u\n",&(mpegdec_from_file->sequence.profile_level_id));//profile_level_id uint8_t
    fscanf(fp,"%u\n",&(mpegdec_from_file->sequence.colour_primaries));//colour_primaries uint8_t
    fscanf(fp,"%u\n",&(mpegdec_from_file->sequence.transfer_characteristics));//transfer_characteristics uint8_t
    fscanf(fp,"%u\n",&(mpegdec_from_file->sequence.matrix_coefficients));//matrix_coefficients uint8_t
    
    printf("after fread in read_data\n");
    fclose(fp);
}


static void decode_mpeg2 (uint8_t * current, uint8_t * end)
{
    const mpeg2_info_t * info;
    mpeg2_state_t state;
    vo_setup_result_t setup_result;
    
    mpeg2_buffer (mpeg2dec, current, end);
    printf("after mpeg2 buffer\n");
    total_offset += end - current;
    
    info = mpeg2_info (mpeg2dec);
    printf("after mpeg2 info\n");
    while (1) {
        state = mpeg2_parse (mpeg2dec);
        if (verbose)
            dump_state (stderr, state, info,
                        total_offset - mpeg2_getpos (mpeg2dec), verbose);
        switch (state) {
            case STATE_BUFFER:
                printf("state buffer\n");
                return;
            case STATE_SEQUENCE:
                /* might set nb fbuf, convert format, stride */
                /* might set fbufs */
                if (output->setup (output, info->sequence->width,
                                   info->sequence->height,
                                   info->sequence->chroma_width,
                                   info->sequence->chroma_height, &setup_result)) {
                    fprintf (stderr, "display setup failed\n");
                    exit (1);
                }
                if (setup_result.convert &&
                    mpeg2_convert (mpeg2dec, setup_result.convert, NULL)) {
                    fprintf (stderr, "color conversion setup failed\n");
                    exit (1);
                }
                if (output->set_fbuf) {
                    uint8_t * buf[3];
                    void * id;
                    
                    mpeg2_custom_fbuf (mpeg2dec, 1);
                    output->set_fbuf (output, buf, &id);
                    mpeg2_set_buf (mpeg2dec, buf, id);
                    output->set_fbuf (output, buf, &id);
                    mpeg2_set_buf (mpeg2dec, buf, id);
                } else if (output->setup_fbuf) {
                    uint8_t * buf[3];
                    void * id;
                    
                    output->setup_fbuf (output, buf, &id);
                    mpeg2_set_buf (mpeg2dec, buf, id);
                    output->setup_fbuf (output, buf, &id);
                    mpeg2_set_buf (mpeg2dec, buf, id);
                    output->setup_fbuf (output, buf, &id);
                    mpeg2_set_buf (mpeg2dec, buf, id);
                }
                mpeg2_skip (mpeg2dec, (output->draw == NULL));
                printf("state sequence\n");
                if(mpeg2dec_state_ready == 0)
                {
                    //write_data(mpeg2decstate_fname,mpeg2dec);
                    write_data_ascii(mpeg2decstate_fname,mpeg2dec);
                    mpeg2dec_state_ready = 1;
                }
                break;
            case STATE_PICTURE:
                /* might skip */
                /* might set fbuf */
                if (output->set_fbuf) {
                    uint8_t * buf[3];
                    void * id;
                    
                    output->set_fbuf (output, buf, &id);
                    mpeg2_set_buf (mpeg2dec, buf, id);
                    
                    
                }
                if (output->start_fbuf)
                {
                    output->start_fbuf (output, info->current_fbuf->buf,
                                        info->current_fbuf->id);
                    
                    
                    
                }
                printf("state picture\n");
                //write_data(mpeg2decstate_fname,mpeg2dec);
                //mpeg2dec_state_ready = 1;
                
                
                break;
            case STATE_SLICE:
            case STATE_END:
            case STATE_INVALID_END:
                /* draw current picture */
                /* might free frame buffer */
                if (info->display_fbuf) {
                    if (output->draw)
                        output->draw (output, info->display_fbuf->buf,
                                      info->display_fbuf->id);
                    print_fps (0);
                }
                if (output->discard && info->discard_fbuf)
                    output->discard (output, info->discard_fbuf->buf,
                                     info->discard_fbuf->id);
                if(myframecnt > 3)
                {
                    printf("before save frame %d \t %d \t%d\n",info->sequence->height,info->sequence->width,myframecnt);
                    SaveFrame(info->display_fbuf->buf[0],info->sequence->width,info->sequence->height,myframecnt);
                    //write_jpeg_file(info->display_fbuf->buf[0],info->sequence->width,info->sequence->height,myframecnt,1);
                }
                myframecnt++;
                printf("state invalid end\n");
                
                break;
            default:
                break;
        }
    }
}

#define DEMUX_PAYLOAD_START 1
static int demux (uint8_t * buf, uint8_t * end, int flags)
{
    static int mpeg1_skip_table[16] = {
        0, 0, 4, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    
    /*
     * the demuxer keeps some state between calls:
     * if "state" = DEMUX_HEADER, then "head_buf" contains the first
     *     "bytes" bytes from some header.
     * if "state" == DEMUX_DATA, then we need to copy "bytes" bytes
     *     of ES data before the next header.
     * if "state" == DEMUX_SKIP, then we need to skip "bytes" bytes
     *     of data before the next header.
     *
     * NEEDBYTES makes sure we have the requested number of bytes for a
     * header. If we dont, it copies what we have into head_buf and returns,
     * so that when we come back with more data we finish decoding this header.
     *
     * DONEBYTES updates "buf" to point after the header we just parsed.
     */
    
#define DEMUX_HEADER 0
#define DEMUX_DATA 1
#define DEMUX_SKIP 2
    static int state = DEMUX_SKIP;
    static int state_bytes = 0;
    static uint8_t head_buf[264];
    
    uint8_t * header;
    int bytes;
    int len;
    
#define NEEDBYTES(x)						\
do {							\
int missing;						\
\
missing = (x) - bytes;					\
if (missing > 0) {					\
if (header == head_buf) {				\
if (missing <= end - buf) {			\
memcpy (header + bytes, buf, missing);	\
buf += missing;				\
bytes = (x);				\
} else {					\
memcpy (header + bytes, buf, end - buf);	\
state_bytes = bytes + end - buf;		\
return 0;					\
}						\
} else {						\
memcpy (head_buf, header, bytes);		\
state = DEMUX_HEADER;				\
state_bytes = bytes;				\
return 0;					\
}							\
}							\
} while (0)
    
#define DONEBYTES(x)		\
do {			\
if (header != head_buf)	\
buf = header + (x);	\
} while (0)
    
    if (flags & DEMUX_PAYLOAD_START)
        goto payload_start;
    switch (state) {
        case DEMUX_HEADER:
            if (state_bytes > 0) {
                header = head_buf;
                bytes = state_bytes;
                goto continue_header;
            }
            break;
        case DEMUX_DATA:
            if (demux_pid || (state_bytes > end - buf)) {
                decode_mpeg2 (buf, end);
                state_bytes -= end - buf;
                return 0;
            }
            decode_mpeg2 (buf, buf + state_bytes);
            buf += state_bytes;
            break;
        case DEMUX_SKIP:
            if (demux_pid || (state_bytes > end - buf)) {
                state_bytes -= end - buf;
                return 0;
            }
            buf += state_bytes;
            break;
    }
    
    while (1) {
        if (demux_pid) {
            state = DEMUX_SKIP;
            return 0;
        }
    payload_start:
        header = buf;
        bytes = end - buf;
    continue_header:
        NEEDBYTES (4);
        if (header[0] || header[1] || (header[2] != 1)) {
            if (demux_pid) {
                state = DEMUX_SKIP;
                return 0;
            } else if (header != head_buf) {
                buf++;
                goto payload_start;
            } else {
                header[0] = header[1];
                header[1] = header[2];
                header[2] = header[3];
                bytes = 3;
                goto continue_header;
            }
        }
        if (demux_pid) {
            if ((header[3] >= 0xe0) && (header[3] <= 0xef))
                goto pes;
            fprintf (stderr, "bad stream id %x\n", header[3]);
            exit (1);
        }
        switch (header[3]) {
            case 0xb9:	/* program end code */
                /* DONEBYTES (4); */
                /* break;         */
                return 1;
            case 0xba:	/* pack header */
                NEEDBYTES (5);
                if ((header[4] & 0xc0) == 0x40) {	/* mpeg2 */
                    NEEDBYTES (14);
                    len = 14 + (header[13] & 7);
                    NEEDBYTES (len);
                    DONEBYTES (len);
                    /* header points to the mpeg2 pack header */
                } else if ((header[4] & 0xf0) == 0x20) {	/* mpeg1 */
                    NEEDBYTES (12);
                    DONEBYTES (12);
                    /* header points to the mpeg1 pack header */
                } else {
                    fprintf (stderr, "weird pack header\n");
                    DONEBYTES (5);
                }
                break;
            default:
                if (header[3] == demux_track) {
                pes:
                    NEEDBYTES (7);
                    if ((header[6] & 0xc0) == 0x80) {	/* mpeg2 */
                        NEEDBYTES (9);
                        len = 9 + header[8];
                        NEEDBYTES (len);
                        /* header points to the mpeg2 pes header */
                        if (header[7] & 0x80) {
                            uint32_t pts, dts;
                            
                            pts = (((header[9] >> 1) << 30) |
                                   (header[10] << 22) | ((header[11] >> 1) << 15) |
                                   (header[12] << 7) | (header[13] >> 1));
                            dts = (!(header[7] & 0x40) ? pts :
                                   (uint32_t)(((header[14] >> 1) << 30) |
                                              (header[15] << 22) |
                                              ((header[16] >> 1) << 15) |
                                              (header[17] << 7) | (header[18] >> 1)));
                            mpeg2_tag_picture (mpeg2dec, pts, dts);
                        }
                    } else {	/* mpeg1 */
                        int len_skip;
                        uint8_t * ptsbuf;
                        
                        len = 7;
                        while (header[len - 1] == 0xff) {
                            len++;
                            NEEDBYTES (len);
                            if (len > 23) {
                                fprintf (stderr, "too much stuffing\n");
                                break;
                            }
                        }
                        if ((header[len - 1] & 0xc0) == 0x40) {
                            len += 2;
                            NEEDBYTES (len);
                        }
                        len_skip = len;
                        len += mpeg1_skip_table[header[len - 1] >> 4];
                        NEEDBYTES (len);
                        /* header points to the mpeg1 pes header */
                        ptsbuf = header + len_skip;
                        if ((ptsbuf[-1] & 0xe0) == 0x20) {
                            uint32_t pts, dts;
                            
                            pts = (((ptsbuf[-1] >> 1) << 30) |
                                   (ptsbuf[0] << 22) | ((ptsbuf[1] >> 1) << 15) |
                                   (ptsbuf[2] << 7) | (ptsbuf[3] >> 1));
                            dts = (((ptsbuf[-1] & 0xf0) != 0x30) ? pts :
                                   (uint32_t)(((ptsbuf[4] >> 1) << 30) |
                                              (ptsbuf[5] << 22) | ((ptsbuf[6] >> 1) << 15) |
                                              (ptsbuf[7] << 7) | (ptsbuf[18] >> 1)));
                            mpeg2_tag_picture (mpeg2dec, pts, dts);
                        }
                    }
                    DONEBYTES (len);
                    bytes = 6 + (header[4] << 8) + header[5] - len;
                    if (demux_pid || (bytes > end - buf)) {
                        decode_mpeg2 (buf, end);
                        state = DEMUX_DATA;
                        state_bytes = bytes - (end - buf);
                        return 0;
                    } else if (bytes > 0) {
                        decode_mpeg2 (buf, buf + bytes);
                        buf += bytes;
                    }
                } else if (header[3] < 0xb9) {
                    fprintf (stderr,
                             "looks like a video stream, not system stream\n");
                    DONEBYTES (4);
                } else {
                    NEEDBYTES (6);
                    DONEBYTES (6);
                    bytes = (header[4] << 8) + header[5];
                    if (bytes > end - buf) {
                        state = DEMUX_SKIP;
                        state_bytes = bytes - (end - buf);
                        return 0;
                    }
                    buf += bytes;
                }
        }
    }
}

static void ps_loop (void)
{
    uint8_t * buffer = (uint8_t *) malloc (buffer_size);
    uint8_t * end;
    
    if (buffer == NULL)
        exit (1);
    do {
        end = buffer + fread (buffer, 1, buffer_size, in_file);
        if (demux (buffer, end, 0))
            break;	/* hit program_end_code */
    } while (end == buffer + buffer_size && !sigint);
    free (buffer);
}

static int pva_demux (uint8_t * buf, uint8_t * end)
{
    static int state = DEMUX_SKIP;
    static int state_bytes = 0;
    static uint8_t head_buf[15];
    
    uint8_t * header;
    int bytes;
    int len;
    
    switch (state) {
        case DEMUX_HEADER:
            if (state_bytes > 0) {
                header = head_buf;
                bytes = state_bytes;
                goto continue_header;
            }
            break;
        case DEMUX_DATA:
            if (state_bytes > end - buf) {
                decode_mpeg2 (buf, end);
                state_bytes -= end - buf;
                return 0;
            }
            decode_mpeg2 (buf, buf + state_bytes);
            buf += state_bytes;
            break;
        case DEMUX_SKIP:
            if (state_bytes > end - buf) {
                state_bytes -= end - buf;
                return 0;
            }
            buf += state_bytes;
            break;
    }
    
    while (1) {
    payload_start:
        header = buf;
        bytes = end - buf;
    continue_header:
        NEEDBYTES (2);
        if (header[0] != 0x41 || header[1] != 0x56) {
            if (header != head_buf) {
                buf++;
                goto payload_start;
            } else {
                header[0] = header[1];
                bytes = 1;
                goto continue_header;
            }
        }
        NEEDBYTES (8);
        if (header[2] != 1) {
            DONEBYTES (8);
            bytes = (header[6] << 8) + header[7];
            if (bytes > end - buf) {
                state = DEMUX_SKIP;
                state_bytes = bytes - (end - buf);
                return 0;
            } 
            buf += bytes; 
        } else {
            len = 8;
            if (header[5] & 0x10) {
                len = 12 + (header[5] & 3);
                NEEDBYTES (len);
                decode_mpeg2 (header + 12, header + len);
                mpeg2_tag_picture (mpeg2dec,
                                   ((header[8] << 24) | (header[9] << 16) |
                                    (header[10] << 8) | header[11]), 0);
            }
            DONEBYTES (len);
            bytes = (header[6] << 8) + header[7] + 8 - len;
            if (bytes > end - buf) {
                decode_mpeg2 (buf, end);
                state = DEMUX_DATA;
                state_bytes = bytes - (end - buf);
                return 0;
            } else if (bytes > 0) {
                decode_mpeg2 (buf, buf + bytes);
                buf += bytes;
            }
        }
    }
}

static void pva_loop (void)
{
    uint8_t * buffer = (uint8_t *) malloc (buffer_size);
    uint8_t * end;
    
    if (buffer == NULL)
        exit (1);
    do {
        end = buffer + fread (buffer, 1, buffer_size, in_file);
        pva_demux (buffer, end);
    } while (end == buffer + buffer_size && !sigint);
    free (buffer);
}

static void ts_loop (void)
{
    uint8_t * buffer = (uint8_t *) malloc (buffer_size);
    uint8_t * buf;
    uint8_t * nextbuf;
    uint8_t * data;
    uint8_t * end;
    int pid;
    
    if (buffer == NULL || buffer_size < 188)
        exit (1);
    buf = buffer;
    do {
        end = buf + fread (buf, 1, buffer + buffer_size - buf, in_file);
        buf = buffer;
        for (; (nextbuf = buf + 188) <= end; buf = nextbuf) {
            if (*buf != 0x47) {
                fprintf (stderr, "bad sync byte\n");
                nextbuf = buf + 1;
                continue;
            }
            pid = ((buf[1] << 8) + buf[2]) & 0x1fff;
            if (pid != demux_pid)
                continue;
            data = buf + 4;
            if (buf[3] & 0x20) {	/* buf contains an adaptation field */
                data = buf + 5 + buf[4];
                if (data > nextbuf)
                    continue;
            }
            if (buf[3] & 0x10)
                demux (data, nextbuf,
                       (buf[1] & 0x40) ? DEMUX_PAYLOAD_START : 0);
        }
        if (end != buffer + buffer_size)
            break;
        memcpy (buffer, buf, end - buf);
        buf = buffer + (end - buf);
    } while (!sigint);
    free (buffer);
}

static void es_loop (void)
{
    uint8_t * buffer = (uint8_t *) malloc (buffer_size);
    uint8_t * end;
    
    if (buffer == NULL)
        exit (1);
    do {
        end = buffer + fread (buffer, 1, buffer_size, in_file);
        decode_mpeg2 (buffer, end);
    } while (end == buffer + buffer_size && !sigint);
    free (buffer);
}

static void es_loop_chunk(void)
{
    uint8_t * buffer = (uint8_t *) malloc (buffer_size);
    uint8_t * end;
    int gop_cnt=0;
    int num_bytes_read;
    int bytes_remaining;
    
    if (buffer == NULL)
        exit (1);
    do {
        //if(gop_cnt > 3)
        if(1)
        {
            if(mpeg2dec_state_ready == 1)
            {
                mpeg2dec_copy = mpeg2_init ();
                if (mpeg2dec_copy == NULL)
                {
                    printf("cannot mpeg2_init\n");
                    exit (1);
                }
                
                //read_data(mpeg2decstate_fname,mpeg2dec_copy);
                //read_data_ascii(mpeg2decstate_fname,mpeg2dec_copy);
                read_data_seq_array(mpeg2dec_copy);
                 printf("after reading config file\n");
                printf("width = %d \t \t height = %d\n",mpeg2dec_copy->sequence.width,mpeg2dec_copy->sequence.height);
                //exit(1);
                //mpeg2dec_copy = mpeg2dec;
                mpeg2dec = mpeg2dec_copy;
                mpeg2_reset(mpeg2dec,1);
                mpeg2dec_state_ready = 2;//need to read only once from the file
            }
            
            //end = buffer + fread (buffer, 1, buffer_size, in_file_chunk);
            bytes_remaining = buf_length - tot_bytes_read;
            if(bytes_remaining > buffer_size)
            {
                  memcpy(buffer,bufferPtr,buffer_size);
                  num_bytes_read =  buffer_size;
            }
            else
            {
		 memcpy(buffer,bufferPtr,bytes_remaining);
                 num_bytes_read = bytes_remaining;
            }
            tot_bytes_read = tot_bytes_read + num_bytes_read;
            bufferPtr = bufferPtr + num_bytes_read;
            end = buffer + num_bytes_read; 
            printf("after fread in es_loop_chunk\n");
        }
        else
        {
            //end = buffer + fread (buffer, 1, buffer_size, in_file);
        }
        gop_cnt++;
        printf("%d\n",gop_cnt);
        decode_mpeg2 (buffer, end);
    } while (end == buffer + buffer_size && !sigint);
    printf("finished decoding chunk\n"); 
   free (buffer);
}

//int main (int argc, char ** argv)
JNIEXPORT void JNICALL
Java_mpeg2decJNIcall_mpeg2decMain(JNIEnv *env, jobject obj, jbyteArray buffer_java,jint bufsize,jintArray sequence_info_arr,jbyteArray jpg_outputbuffer_java, jintArray jpg_indexvec, jint output_bufsize,jintArray act_numframes_written)
{
    int i;
    int argc;
    char** argv;
    //const char *str_chunkfname;
    char str_chunkfname[500];
#ifdef HAVE_IO_H
    setmode (fileno (stdin), O_BINARY);
    setmode (fileno (stdout), O_BINARY);
#endif
    
    printf("regu print\n");
    fprintf (stderr, PACKAGE"-"VERSION
             " - by Michel Lespinasse <walken@zoy.org> and Aaron Holtzman\n");
    
    //str_chunkfname = (*env)->GetStringUTFChars(env, chunk_fname_java, 0);
    
    sprintf(str_chunkfname,"dummy_chunkfname");
    printf("chunk fname from java %s", str_chunkfname);
    
    
    argc = 5;
    argv = (char**)calloc(sizeof(char*),argc);
    for (i = 0; i < argc; i++)
    {
        argv[i] = (char*)calloc(sizeof(char),500);
        if(i == 0)
        {
            sprintf(argv[0],"mpeg2dec");
            
        }
        else if (i == 1)
        {
            sprintf(argv[1],"%s",str_chunkfname);
        }
        else if(i == 2)
        {
            sprintf(argv[2],"-a%s",str_chunkfname);
        }
        else if(i == 3)
        {
            sprintf(argv[3],"-o");
        }
        else
        {
            //i == 4
            sprintf(argv[4],"pgm");
        }
        
    }
    
    
    printf("argc = %d\n",argc);
    for (i = 0; i < argc; i++)
    {
        printf("arg1: %s\n",argv[i]);
        
    }
    
    //clip table for color space conversion
    if (!(Clip=(unsigned char *)malloc(1024)))
          Error("Clip[] malloc failed\n");

    Clip += 384;

    for (i=-384; i<640; i++)
       Clip[i] = (i<0) ? 0 : ((i>255) ? 255 : i);
     // clip table

   handle_args (argc, argv);

    printf("after handle_args\n");
    
    output = output_open ();
    if (output == NULL) {
        fprintf (stderr, "Can not open output\n");
        return 1;
    }
    
    mpeg2decstate_fname = (char*)calloc(sizeof(char),500);
    //sprintf(mpeg2decstate_fname,"/Users/radhar5/Downloads/mpeg2dec_config_forseq.dat");
    sprintf(mpeg2decstate_fname,"mpeg2dec_config_forseq_ascii.txt");
    
    seq_info_c_array = (*env)->GetIntArrayElements(env,sequence_info_arr,NULL);
    if(seq_info_c_array == NULL)
    {
	printf("sequence info array not passed correctly\n");
        exit(1);
    }

    mpeg2dec_state_ready = 1;//if file mpeg2decstate_fname is already present

    bufferPtr = (*env)->GetByteArrayElements(env,buffer_java, NULL);
    buf_length = bufsize;    

    jpg_outputbufferPtr = (*env)->GetByteArrayElements(env,jpg_outputbuffer_java, NULL);
    jpg_outputbuffer_len = output_bufsize;

    jpg_indexvec_c_array = (*env)->GetIntArrayElements(env,jpg_indexvec,NULL);
    if(jpg_indexvec_c_array == NULL)
    {
        printf("jpg indexvec array not passed correctly\n");
        exit(1);
    }    

    act_numframes_c_array = (*env)->GetIntArrayElements(env,act_numframes_written,NULL);

    mpeg2dec = mpeg2_init ();
    if (mpeg2dec == NULL)
        exit (1);
    mpeg2_malloc_hooks (malloc_hook, NULL);
    
    if (demux_pva)
        pva_loop ();
    else if (demux_pid)
        ts_loop ();
    else if (demux_track)
        ps_loop ();
    else if (chunk_file_present == 1)
        es_loop_chunk();
    else
        es_loop ();
    
    printf("myframecnt %d\n",myframecnt);
    mpeg2_close (mpeg2dec);
    printf("after mpeg2declose\n");
    //if (output->close)
      //  output->close (output);
    //print_fps (1);
   // fclose (in_file);
    //(*env)->ReleaseStringUTFChars(env, chunk_fname_java, str_chunkfname);
    (*env)->ReleaseIntArrayElements(env,sequence_info_arr,seq_info_c_array,0);
     printf("after releasing seq inf\n"); 
   // (*env)->ReleaseByteArrayElements(env, buffer_java, bufferPtr, 0);
    printf("after releasing java input buffer\n");
   (*env)->ReleaseByteArrayElements(env, jpg_outputbuffer_java,jpg_outputbufferPtr, 0);
    printf("after releasing java output buffer\n");
    (*env)->ReleaseIntArrayElements(env,jpg_indexvec,jpg_indexvec_c_array,0);
    printf("after releasing index vec\n");
    (*env)->ReleaseIntArrayElements(env,act_numframes_written,act_numframes_c_array,0);
    //return 0;
}

