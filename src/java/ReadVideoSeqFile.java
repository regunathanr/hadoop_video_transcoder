import java.io.*;
import org.apache.hadoop.io.*;
import org.apache.hadoop.mapreduce.*;
import org.apache.hadoop.fs.*;
import java.net.URI;
import org.apache.hadoop.conf.*;
import org.apache.hadoop.util.ReflectionUtils;

public class ReadVideoSeqFile
{
	public static void main(String[] args) throws IOException
        {	
		String uri = args[0];
		Configuration conf = new Configuration();
		FileSystem fs =  FileSystem.get(URI.create(uri),conf);
		Path path = new Path(uri);
                 
  		SequenceFile.Reader reader = null;
		try
		{
			reader = new SequenceFile.Reader(fs,path,conf);
			Text key = (Text) ReflectionUtils.newInstance(reader.getKeyClass(),conf);
			BytesWritable value = (BytesWritable) ReflectionUtils.newInstance(reader.getValueClass(),conf);
                        long position = reader.getPosition();
                        int idx = 0;
			while(reader.next(key,value))
			{
				String syncSeen = reader.syncSeen() ? "*":" ";
				String frame_fname = key.toString();
                                byte[] temp_buf = value.getBytes();
                                
                                byte[] frameid_bytes = new byte[8]; 
				System.arraycopy(temp_buf,0,frameid_bytes,0,8);

				ByteArrayInputStream bis = new ByteArrayInputStream(frameid_bytes);
		                DataInputStream dis = new DataInputStream(bis);
                  	        long actual_framenum = dis.readLong();
                   
                               
                   		System.out.println(syncSeen+frameid_bytes[6]+frameid_bytes[7]+idx); 

				frame_fname = frame_fname +"_"+actual_framenum+".jpg";
                                System.out.println(frame_fname); 
                                int length_in_bytes = temp_buf.length;
                                if((idx > 12)&&(idx < 38))
                   		{
                        		Path outFile = new Path(frame_fname);
                        		FSDataOutputStream out = fs.create(outFile);
                        		out.write(temp_buf,8,length_in_bytes-8);
                        		out.close();
                   		}  
				position = reader.getPosition();
				idx = idx + 1;
			}
		}
		finally
		{
			IOUtils.closeStream(reader);	
		}
	}
}
