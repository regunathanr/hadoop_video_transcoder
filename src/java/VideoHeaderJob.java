import org.apache.hadoop.fs.Path;
import org.apache.hadoop.conf.*;
import org.apache.hadoop.io.*;
import java.util.StringTokenizer;
//import org.apache.hadoop.mapred.*;
//import org.apache.hadoop.mapred.JobConf;
import org.apache.hadoop.util.*;
import org.apache.hadoop.mapreduce.*;
import org.apache.hadoop.mapreduce.InputFormat;
//import org.apache.hadoop.mapred.FileInputFormat;
//import org.apache.hadoop.mapred.FileOutputFormat;
//import org.apache.hadoop.mapred.JobClient;
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat;
import org.apache.hadoop.mapreduce.lib.input.TextInputFormat;
import org.apache.hadoop.mapreduce.lib.output.FileOutputFormat;
import org.apache.hadoop.mapreduce.lib.output.TextOutputFormat;

public class VideoHeaderJob extends Configured implements Tool{
     
     //GenericOptionsParser goptsparser;

      public int run(String[] args) throws Exception
      {
            //creating a JobConf object and assigning a job name for identification purposes
            Job conf = new Job(getConf(),"VideoHeaderJob");
           // conf.setJarByClass(getClass());
	    conf.setJarByClass(VideoHeaderJob.class);
            //conf.setJobName("VideoHeaderJob");

            //Setting configuration object with the Data Type of output Key and Value
            conf.setOutputKeyClass(Text.class);
            conf.setOutputValueClass(IntWritable.class);

            System.out.println("setting mapper and reducer");
	    //Providing the mapper and reducer class names
            conf.setMapperClass(VideoHeaderMapper.class);
            conf.setReducerClass(VideoHeaderReducer.class);
            System.out.println("after setting map and reduce classes");
            System.out.println("arg1: " + args[0]+ "arg2: " +args[1]+" in run");

            StringTokenizer tokenizer = new StringTokenizer(args[0],"/");            
            int numtokens = tokenizer.countTokens();
            if(numtokens > 1)
            {
            	String tempstr;
           	 for(int i = 0; i < numtokens-1;i++)
            	{
			tempstr = tokenizer.nextToken();
                	System.out.println("token:" +i+" "+tempstr);
	    	}
            	tempstr = tokenizer.nextToken();
            	conf.setJobName(tempstr);//set jobname to the same of video filename
	    }
	    else
	    {	
		conf.setJobName(args[0]);
	    }
            
            //StringTokenizer t1 = new StringTokenizer(tempstr,"/");
            //System.out.println("num tokens in "+ tempstr+" :" + t1.countTokens());
             
            conf.setInputFormatClass(WholeFileInputFormat1.class);
            conf.setOutputFormatClass(TextOutputFormat.class);

            //the hdfs input and output directory to be fetched from the command line
            FileInputFormat.addInputPath(conf, new Path(args[0]));
            FileOutputFormat.setOutputPath(conf, new Path(args[1]));

            //JobClient.runJob(conf);
            conf.waitForCompletion(true);
            return 0;
      }
     
      public static void main(String[] args) throws Exception
      {
            
            GenericOptionsParser goptsparser = new GenericOptionsParser(args);
            String [] rem_args = goptsparser.getRemainingArgs(); 
            System.out.println("arg1:" + rem_args[0]);
            System.out.println("arg2:" + rem_args[1]); 
            (goptsparser.getConfiguration()).set("mapred.child.java.opts", "-Xmx1024m"); 
            int res = ToolRunner.run(goptsparser.getConfiguration(), new VideoHeaderJob(),rem_args);
            System.exit(res);
      }
      

}
