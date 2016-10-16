import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.io.*;
import org.apache.hadoop.mapreduce.*;
import org.apache.hadoop.mapreduce.lib.input.FileSplit;
import org.apache.hadoop.fs.*;
import java.io.*;

class WholeFileRecordReader1 extends RecordReader<LongWritable, BytesWritable>
{
	private FileSplit fileSplit;
	private Configuration conf;
        private BytesWritable value = new BytesWritable();
	private boolean processed = false;

        private long chunk_start;
        private LongWritable key = null;

	@Override
	public void initialize(InputSplit split, TaskAttemptContext context) throws IOException,InterruptedException
	{
		this.fileSplit = (FileSplit) split;
		this.conf = context.getConfiguration();
	}

	@Override
	public boolean nextKeyValue() throws IOException,InterruptedException
	{
	        if(!processed)
		{
			byte[] contents = new byte[(int) fileSplit.getLength()];
			Path file =  fileSplit.getPath();
			FileSystem fs = file.getFileSystem(conf);
			FSDataInputStream in = null;
			try
			{
				in = fs.open(file);
                                System.err.println("split start in record reader: "+fileSplit.getStart());
                                in.seek(fileSplit.getStart());
                                if(key == null)
				{
					key = new LongWritable();
				}
                                key.set(fileSplit.getStart());
				IOUtils.readFully(in, contents,0, contents.length);
				value.set(contents,0,contents.length);
			}
			finally
			{
				IOUtils.closeStream(in);
			}
		        processed = true;
			return true;

		}
		return false;		
	}

	@Override
	public LongWritable getCurrentKey() throws IOException, InterruptedException
	{
		//return NullWritable.get();
                return key;
	}
	
	@Override
	public BytesWritable getCurrentValue() throws IOException, InterruptedException
	{
		return value;

	}
	
	@Override
	public float getProgress() throws IOException
	{
		return processed ? 1.0f:0.0f;
	}

	@Override
	public void close() throws IOException
	{
		//do nothing
	}


}
