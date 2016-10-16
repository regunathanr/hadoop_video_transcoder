import java.io.IOException;
import java.util.Iterator;

import org.apache.hadoop.io.*;
import org.apache.hadoop.mapreduce.*;

public class VideoHeaderReducer extends Reducer<Text, IntWritable, Text, IntWritable>
//public class WordCountReducer extends Reducer<Text, BytesWritable, Text, BytesWritable>
{
      //reduce method accepts the Key Value pairs from mappers, do the aggregation based on keys and produce the final out put
     public void reduce(Text key, Iterable<IntWritable> values, Context context) throws IOException,InterruptedException
     // public void reduce(Text key, Iterable<BytesWritable> values, Context context) throws IOException,InterruptedException
      {
            int sum = 0;
            int idx = 0;
            /*iterates through all the values available with a key and add them together and give the
            final result as the key and sum of its values*/
          //while (values.hasNext())
         for (IntWritable val:values)
         // for(BytesWritable val:values)
          {
		/*if(idx == 0)
		{
			System.err.println("reducer key: "+key);
		}*/
                idx = idx + 1;
               sum += val.get();
               //context.write(key,new BytesWritable(val.get()));
          }
	  //System.err.println("reducer key numvals: "+idx);
          context.write(key, new IntWritable(sum));
      }
}
