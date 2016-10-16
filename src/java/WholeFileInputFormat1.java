import java.io.*;
import org.apache.hadoop.fs.*;
import org.apache.hadoop.mapreduce.InputFormat;
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat;
import org.apache.hadoop.mapreduce.*;
import org.apache.hadoop.io.*;

public class WholeFileInputFormat1 extends FileInputFormat<LongWritable, BytesWritable>
{

	//@Override
	/*protected boolean isSplitable(JobContext context, Path file)
        {
		return false;
        }*/
        
        @Override
	public RecordReader<LongWritable, BytesWritable> createRecordReader(InputSplit split, TaskAttemptContext context) throws IOException,InterruptedException
        {
		WholeFileRecordReader1 reader = new WholeFileRecordReader1();
		reader.initialize(split,context);
		return reader;
	}

}

