//
//  mpeg2decJNIcall.java
//  
//
//  Created by Radhakrishnan, Regunathan on 4/25/13.
//
//
import java.io.*;

class mpeg2decJNIcall
{
    private native void mpeg2decMain(byte[] bitstream_buffer,int buf_size,int[] sequence_info_arr,byte[] jpeg_output_buffer_java, int[] jpg_indexvec, int output_bufsize,int[] actual_num_frames_written_to_buf);


    
    static {
        System.loadLibrary("mpeg2dec_jni_local");//This is firstJNI.DLL

        /*if generated by borland
         System.loadLibrary("firstjni");//This is firstjni.dll
         */
    }

  private void write_bytes_tofile(byte[] aInput, String aOutputFileName){
    //log("Writing binary file...");
    try {
      OutputStream output = null;
      try {
        output = new BufferedOutputStream(new FileOutputStream(aOutputFileName));
        output.write(aInput);
      }
      finally {
        output.close();
      }
    }
    catch(FileNotFoundException ex){
      System.out.println("File not found.");
    }
    catch(IOException ex){
      System.out.println(ex);
    }
  }
  

    
    public static void main(String[] args)
    {
        mpeg2decJNIcall JN=new mpeg2decJNIcall();
        
        
        //String input_chunk_fname = "/Users/radhar5/Documents/SZTRA101a08.m2v_chunk2";
        String input_chunk_fname = args[0];
	System.out.println("You Entered " + input_chunk_fname);
        
        //sequence info
        int[] sequence_info = new int[18];
       sequence_info[0] = 720;
       sequence_info[1] = 576;
       sequence_info[2] = 360;
       sequence_info[3] = 576;
       sequence_info[4] = 13107150;
       sequence_info[5] = 356352;
       sequence_info[6] = 165;
       sequence_info[7] = 720;
       sequence_info[8] = 576;
       sequence_info[9] = 720;
       sequence_info[10] = 576;
       sequence_info[11] = 1;
       sequence_info[12] = 1;
       sequence_info[13] = 1080000;
       sequence_info[14] = 133;
       sequence_info[15] = 0;
       sequence_info[16] = 0;
       sequence_info[17] = 0;

        //int fileDatalen = value.getLength();
       // byte[] fileData = new byte[fileDatalen];
       // fileData = value.getBytes();
       // total_len = total_len + fileDatalen;

        int size_est_one_jpeg = 320000;//320kbytes is estimated size of one jpeg file
        int est_num_jpg_frames = 1260;
        byte[] jpg_outputbuffer_java = new byte[size_est_one_jpeg*est_num_jpg_frames];
        int[] jpg_indexvec = new int[est_num_jpg_frames*2];
        int[] actual_num_frames_written_to_buf=new int[1];

       File file = new File(input_chunk_fname);
       byte [] fileData = new byte[(int)file.length()];
       int fileDatalen = (int)file.length();

       try
       {
       		DataInputStream dis = new DataInputStream((new FileInputStream(file)));
      
       		dis.readFully(fileData);
       		dis.close();
	}
	catch(IOException e)
	{
		System.out.println("Error message: " + e.getMessage());
	}


        JN.mpeg2decMain(fileData,fileDatalen,sequence_info,jpg_outputbuffer_java,jpg_indexvec,size_est_one_jpeg*est_num_jpg_frames,actual_num_frames_written_to_buf);
        
       int end_ind;
       int length_in_bytes;
       int start_ind = 0;

       //write to jpgs
       // for(int i = 0;i < actual_num_frames_written_to_buf[0];i++)
        for (int i = 0; i < 12; i++)
        {
                   end_ind = jpg_indexvec[i]-1;
                   length_in_bytes = end_ind - start_ind+1;
                   byte [] temp_buf = new byte[end_ind-start_ind+1];
                   System.arraycopy(jpg_outputbuffer_java,start_ind,temp_buf,0,length_in_bytes);
                   start_ind = end_ind+1;
                   String temp_filename ="frame_"+i+".jpg";
                   JN.write_bytes_tofile(temp_buf,temp_filename);

         }

        
    }
}
