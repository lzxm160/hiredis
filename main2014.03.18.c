#define _GNU_SOURCE
#include "fmacros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include "hiredis.h"
#include "conf.h"
#include <mysql.h>
#include "async.h"
#include "adapters/ae.h"
#include <signal.h>
/**下面两条语句将日志模块的实现代码加入进来
#define IMPLEMENT_LOG4C语句一定要在#include "log4c_amalgamation.h"语句之前。
*/
#define linux
#define HAVE_PTHREAD_H
#define LOG4C_ENABLE //注释此宏就可以将日志关掉
#define IMPLEMENT_LOG4C
#include "log4c_amalgamation.h"
//#define DEBUG	//实际用时关掉debug

enum connection_type {
    CONN_TCP,
    CONN_UNIX
};
struct config {
    enum connection_type type;
    struct {
        char *host;
        int port;
        struct timeval timeout;
    } tcp;
    struct {
        const char *path;
    } unixl;
};
int GetRequestLogNewName(char *RequestLogNewName)
{//这个是获得requestlog位置及名字：/F5_log/request_log/history_log/201401060920.log
	    struct tm  *ptm;
		long   ts;
		int    y,m,d,h,n,s;
		ts = time(NULL);
		ptm = localtime(&ts);
		y = ptm->tm_year+1900;  //年
		m = ptm->tm_mon+1;      //月
		d = ptm->tm_mday;       //日
		h = ptm->tm_hour;       //时
		n = ptm->tm_min;        //分
		s = ptm->tm_sec;        //秒
		//requestlog位置及名字：/F5_log/request_log/history_log/201401060920.log，计算ip地址的日志
		if(n%5==0)//每5分钟
		{
			sprintf(RequestLogNewName,"%d%02d%02d%02d%02d.log",y,m,d,h,n);	
			return 0;
	    }
		else
		{
	  		return -1;  		
		}	
}
int GetF5LogLocationNewName(char* F5LogNewLocation,char* F5LogNewName)
{   // 获得f5日志位置及名字：
	///F5_log/2014_5m/20140109/2014010916/caccess_f5_20140109_1625.log
	struct tm *ptm=NULL;
    struct tm *ptm1=NULL;
    long ts=0;
    long ts1=0;
    int year=0,mon=0,day=0,hour=0,min=0,sec=0;
    int year1=0,mon1=0,day1=0,hour1=0,min1=0,sec1=0;
    ts=time(NULL);
    ts1=ts-20;
		ptm = localtime(&ts);
   		year = ptm->tm_year+1900;  //年
		mon = ptm->tm_mon+1;      //月
		day = ptm->tm_mday;       //日
		hour = ptm->tm_hour;       //时
		min = ptm->tm_min;        //分
		sec = ptm->tm_sec;        //秒

		ptm1 = localtime(&ts1);
   		year1 = ptm1->tm_year+1900;  //年
		mon1 = ptm1->tm_mon+1;      //月
		day1 = ptm1->tm_mday;       //日
		hour1 = ptm1->tm_hour;       //时
		min1 = ptm1->tm_min;        //分
		sec1 = ptm1->tm_sec;        //秒

		if(min%5==0)//每5分钟
		{		//用10s之前的时间拼起文件目录	
			sprintf(F5LogNewLocation,"%d_5m/%d%02d%02d/%d%02d%02d%02d",year1,year1,mon1,day1,year1,mon1,day1,hour1);	
			//现在的时间拼起文件名
			sprintf(F5LogNewName,"caccess_f5_%d%02d%02d_%02d%02d.log",year,mon,day,hour,min);	
			return 0;
		}
		else
		{
	  		return -1;  		
		}
	
}
int del_str_line(char *str)
{
    for(;*str!=0;str++)
    {
		if(*str=='\n'||*str=='\r')
    	{	
    		*str='\0';
  			return 0;//只得到一行数据
  		}
    }
    	return 0;
}
void GetUrlBeforeQuestionMark(char *buf)
{
	while(*buf!='\0')
	{
		if(*buf=='?')
		{
			*buf='\0';
			break;
		}
		buf++;
	}	
}
void GetSqlBeforeWhere(char *buf)
{
	char *temp=strcasestr(buf,"where");
	//char *temp=strstr(buf,"where");
	if(temp)
		*temp='\0';
}
void LogInitWithParam()
{
	///设置日志配置文件名
	LOG4C_PARAM_CFG_FILE_NAME("log.cfg");
	///设置生成日志文件名
	LOG4C_PARAM_LOG_FILE_NAME("log");
	///设置日志级别
	LOG4C_PARAM_LOG_LEVEL("unknown");
	///设置日志文件大小
	LOG4C_PARAM_LOG_FILE_SIZE(209715200);//单位是字节，200M一个
	///设置生成日志文件个数，达到最大个数将自动覆盖最旧的日志
	LOG4C_PARAM_LOG_FILE_NUM(500);
	///设置每次记录日志都重新读取日志配置文件
	LOG4C_PARAM_REREAD_LOG_CFG_FILE(1);
	///带参数日志模块初始化,以上所有设置了的参数都将生效，没有设置的采用缺省值
	LOG4C_INIT_WITH_PARAM();
}
int ConnectToRedis(const char *host,int port,redisContext **c)
{
	redisReply *reply;
	struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    *c = redisConnectWithTimeout(host,port,timeout);
	    if (*c == NULL) {
        printf("Redis connection error: can't allocate redis context!\n");
		LOG4C((LOG_ERROR,"Redis connection error: can't allocate redis context!"));
		redisFree(*c);
        return -1;
    } else if ((*c)->err) {
		LOG4C((LOG_ERROR,"Redis connection error: %s!", (*c)->errstr));
        printf("Redis connection error: %s!\n", (*c)->errstr);
		redisFree(*c);
        return -1;
    }
	reply = redisCommand(*c,"PING");    
    if (reply->type == REDIS_REPLY_STATUS &&strcasecmp(reply->str,"pong") == 0)
	{
		printf("Redis connection success\n");
		LOG4C((LOG_NOTICE,"Redis connection success"));
		freeReplyObject(reply); 
		return 0;
	}
	else
	{       
		freeReplyObject(reply); 
        return -1;
    }      	
}
int ConnectToMysql(MYSQL** conn_ptr,char* Server,char* User,char* Password,char* DataBase)
{
	if (!mysql_init(NULL)) 
	{  
    	printf("mysql init error!\n");
		LOG4C((LOG_ERROR,"mysql init error!"));
        return -1;  
    }       
    if (mysql_real_connect(*conn_ptr, Server, User, Password, DataBase, 0, NULL, 0)) 
	{  
    	#ifdef DEBUG
		LOG4C((LOG_NOTICE,"Connection to mysql success"));
	    printf("Connection to mysql success\n");
	    #endif    
		return 0;
    } 
	else 
	{  
        printf("Connection mysql failed:%s!\n",mysql_error(*conn_ptr)); 
		LOG4C((LOG_ERROR,"Connection mysql failed:%s!",mysql_error(*conn_ptr)));
    	#ifdef DEBUG
        printf("host:%s\n",(*conn_ptr)->host);
        printf("user:%s\n",(*conn_ptr)->user);
        printf("passwd:%s\n",(*conn_ptr)->passwd);
        printf("db:%s\n",(*conn_ptr)->db);
        printf("info:%s\n",(*conn_ptr)->info);

		LOG4C((LOG_ERROR,"host:%s",(*conn_ptr)->host));
		LOG4C((LOG_ERROR,"user:%s",(*conn_ptr)->user));
		LOG4C((LOG_ERROR,"passwd:%s",(*conn_ptr)->passwd));
		LOG4C((LOG_ERROR,"db:%s",(*conn_ptr)->db));
		LOG4C((LOG_ERROR,"info:%s",(*conn_ptr)->info));
	    #endif  
		return -1;
    }  
}
int main(int argc, char **argv) {
	
	//程序成为Daemon
	//1、读配置，找到需要分析ip的日志，及F5的日志位置
	//if ip module ipmodule处理
	//if sql模块  sql模块处理   //后面加什么模块就转到相应模块处理
	//ip和url需要联动，首先找出ip访问最多的，然后再在其中找出哪个url最多，在mysql中找到相应的表
	//2、解析日志
	//3、存入redis中
	//4、redis信息定时放入mysql中
	//redis挂掉会5s重连一次,mysql同样

	LogInitWithParam();//日志系统初始化
	
	char Debug[10]={0};
	char Daemon[10]={0};
	if(GetProfileString("./cls.conf", "system", "debug", Debug)==-1)
	{	
		printf("Get debug error!\n");
		LOG4C((LOG_ERROR, "Get debug error!"));
	   	exit(-1);
	}
	else if(strcasecmp(Debug,"on")==0)
	{	
		printf("Debug:%s\n",Debug);	
		LOG4C((LOG_NOTICE, "Debug:%s",Debug));		
	}
	else if(strcasecmp(Debug,"off")==0)
	{	
		printf("Debug:%s\n",Debug);	
		LOG4C((LOG_NOTICE, "Debug:%s",Debug));		
	}
	else
	{
		printf("Debug must be on or off!\n");	
		LOG4C((LOG_NOTICE, "Debug must be on or off!"));//log自动增加回车换行	
	}
	
	if(GetProfileString("./cls.conf", "system", "daemon", Daemon)==-1)
	{	 
		printf("Get daemon error!\n");
		LOG4C((LOG_ERROR, "Get daemon error!"));
	   	exit(-1);
	}
	else if(strcasecmp(Daemon,"on")==0)
	{
		printf("Daemon:%s\n",Daemon);
		LOG4C((LOG_NOTICE, "Daemon:%s",Daemon));	
		if(daemon(1,1)<0)
			exit(-1);  //此处为退出父进程继续子进程
	}
	else if(strcasecmp(Daemon,"off")==0)
	{
		printf("Daemon:%s\n",Daemon);
		LOG4C((LOG_NOTICE, "Daemon:%s",Daemon));	
	}
	else
	{
		printf("Daemon must be on or off!\n");	
		LOG4C((LOG_NOTICE, "Daemon must be on or off!"));//log自动增加回车换行	
	}
	char RequestLogLocation[50]={0};//5分钟切一次，可以分析出ip访问量，然后存入到redis中
	char F5LogLocation[50]={0};//分析更多详细信息
	char ip[16]={0};
	char url[16]={0};
	//1、读取配置///////////////////////////////////////////
	
	if(GetProfileString("./cls.conf", "loglocation", "request_log", RequestLogLocation)==-1)
	{	 
		printf("Request_log location error!\n");
		LOG4C((LOG_ERROR, "Request_log location error!"));
	   	exit(-1);
	}
#ifdef DEBUG
	printf("Request_log location:%s\n",RequestLogLocation);
	LOG4C((LOG_NOTICE, "Request_log location:%s",RequestLogLocation));
#endif
	if(GetProfileString("./cls.conf", "loglocation", "F5_log", F5LogLocation)==-1)
	{		
		printf("F5Log Location error!\n");
		LOG4C((LOG_ERROR, "F5Log Location error!"));
	   	exit(-1);
	}
#ifdef DEBUG
	printf("F5_log location:%s\n",F5LogLocation);
	LOG4C((LOG_NOTICE,"F5_log location:%s",F5LogLocation));
#endif
   if(GetProfileString("./cls.conf", "ip", "top", ip)==-1)
	{
		printf("get ip top error!\n");
		LOG4C((LOG_ERROR, "get ip top error!"));
		exit(-1);
	}
#ifdef DEBUG
   printf("ip top:%s\n",ip);
   LOG4C((LOG_NOTICE,"ip top:%s",ip));
#endif
   ///////////////////////url
    if(GetProfileString("./cls.conf", "url", "url", url)==-1)
	{
		printf("get url error!\n");
		LOG4C((LOG_ERROR, "get url error!"));
		exit(-1);
	}
#ifdef DEBUG
   printf("url:%s\n",url);
   LOG4C((LOG_NOTICE,"url:%s",url));
#endif


   char redisServer[20]={0};
   char redisPort[10]={0};
   char redisDatabase[10]={0};
   char urldatabase[10]={0};
  if(GetProfileString("./cls.conf", "redis", "server", redisServer)==-1)
	{
		printf("redisServer error!\n");
		LOG4C((LOG_ERROR, "redisServer error!"));
		exit(-1);
	}
#ifdef DEBUG
   printf("redisServer:%s\n",redisServer);
   LOG4C((LOG_NOTICE,"redisServer:%s",redisServer));
#endif
    if(GetProfileString("./cls.conf", "redis", "port", redisPort)==-1)
	{
		printf("redisPort error!\n");
		LOG4C((LOG_ERROR, "redisPort error!"));
		exit(-1);
	}
#ifdef DEBUG
   printf("redisPort:%s\n",redisPort);
   LOG4C((LOG_NOTICE,"redisPort:%s",redisPort));
#endif
   if(GetProfileString("./cls.conf", "redis", "database", redisDatabase)==-1)
	{
		printf("redisDatabase error!\n");
		LOG4C((LOG_ERROR, "redisDatabase error!"));
		exit(-1);
	}
#ifdef DEBUG
   printf("redisDatabase:%s\n",redisDatabase);
   LOG4C((LOG_NOTICE,"redisDatabase:%s",redisDatabase));
#endif
    if(GetProfileString("./cls.conf", "redis", "urldatabase", urldatabase)==-1)
	{
		printf("urldatabase error!\n");
		LOG4C((LOG_ERROR, "urldatabase error!"));
		exit(-1);
	}
#ifdef DEBUG
   printf("urldatabase:%s\n",urldatabase);
   LOG4C((LOG_NOTICE,"urldatabase:%s",urldatabase));
#endif
   ////////////////////////////////连接redis开始
    redisContext ct;
	redisContext *c=&ct;
    redisReply *reply;
    struct config cfg = {
        .tcp = {
            .host = "127.0.0.1",
            .port = 6379
        },
        .unixl = {
            .path = "/tmp/redis.sock"
        }
    };
    //int throughput = 1;
    // Ignore broken pipe signal (for I/O error tests). 
    //signal(SIGPIPE, SIG_IGN);
	#ifdef DEBUG
	printf("cfg.tcp.host:%s\n",cfg.tcp.host);
	printf("cfg.tcp.port:%d\n",cfg.tcp.port);
	LOG4C((LOG_NOTICE,"cfg.tcp.host:%s\n",cfg.tcp.host)); 
	LOG4C((LOG_NOTICE,"cfg.tcp.port:%d\n",cfg.tcp.port)); 
	#endif
	if(strlen(redisServer)!=0)
            cfg.tcp.host=redisServer;
	if(strlen(redisPort)!=0)
            cfg.tcp.port = atoi(redisPort);
	#ifdef DEBUG
	printf("cfg.tcp.host:%s\n",cfg.tcp.host);
	printf("cfg.tcp.port:%d\n",cfg.tcp.port);
	LOG4C((LOG_NOTICE,"cfg.tcp.host:%s\n",cfg.tcp.host)); 
	LOG4C((LOG_NOTICE,"cfg.tcp.port:%d\n",cfg.tcp.port)); 
	#endif
	for(;;)//程序启动时检测redis是否可以连接
	{
		if(!ConnectToRedis(cfg.tcp.host,cfg.tcp.port,&c))
		{	
			printf("Connect to redis success\n");
			LOG4C((LOG_NOTICE, "Connect to redis success"));			
			//要插入或查询数据时再选择数据库
//			reply = (redisReply *)redisCommand(c,"select %s",database);	
//#ifdef DEBUG  
//		php_printf("select: %s<br>", reply->str);
//		//LOG4C((LOG_NOTICE,"lpush: %s", reply->str));
//#endif
			break;
		}
		else
		{
			printf("failed to connect to redis,reconnect after 5s...\n");
			LOG4C((LOG_ERROR,"failed to connect to redis,reconnect after 5s..."));	
			sleep(5);
			continue;
		}
	}
///////////////////////连接redis结束,获得连接上下文c可以用///////////////////////////////////////////////

//////////////////////连接mysql开始////////////////////////////////////////////////
//char Server[20]={0};
//char DataBase[30]={0};
//char User[20]={0};
//char Password[20]={0};
//char Table[20]={0};
//	if(GetProfileString("./cls.conf", "mysql", "server", Server)==-1)
//	{	 
//		printf("get Server in conf error!\n");
//		LOG4C((LOG_ERROR,"get Server in conf error!"));
//	   	exit(-1);
//	}
//	#ifdef DEBUG
//	printf("Server:%s\n",Server);
//	LOG4C((LOG_NOTICE,"Server:%s",Server));
//	#endif
//	
//	if(GetProfileString("./cls.conf", "mysql", "database", DataBase)==-1)
//	{		
//		printf("DataBase error!\n");
//		LOG4C((LOG_ERROR,"DataBase error!"));
//	   	exit(-1);
//	}
//	#ifdef DEBUG
//	LOG4C((LOG_NOTICE,"DataBase:%s",DataBase));
//	printf("DataBase:%s\n",DataBase);
//	#endif
//	
//	if(GetProfileString("./cls.conf", "mysql", "table", Table)==-1)
//	{		
//		printf("Table error!\n");
//		LOG4C((LOG_ERROR,"Table error!"));
//	   	exit(-1);
//	}
//	#ifdef DEBUG
//	printf("Table:%s\n",Table);
//	LOG4C((LOG_NOTICE,"Table:%s",Table));
//	#endif
//	
//	if(GetProfileString("./cls.conf", "mysql", "user", User)==-1)
//	{
//		printf("User error!\n");
//		LOG4C((LOG_ERROR,"User error!"));
//		exit(-1);
//	}
//	#ifdef DEBUG
//	LOG4C((LOG_NOTICE,"User:%s",User));
//	printf("User:%s\n",User);
//	#endif
//	
//	if(GetProfileString("./cls.conf", "mysql", "password", Password)==-1)
//	{
//		printf("Password error!\n");
//		LOG4C((LOG_ERROR,"Password error!"));
//		exit(-1);
//	}
//	#ifdef DEBUG
//	printf("Password:%s\n",Password);
//	LOG4C((LOG_NOTICE,"Password:%s",Password));
//	#endif
//	
/////////////////////开始连接mysql
//    MYSQL Conn_ptr={0};
//    MYSQL *conn_ptr=&Conn_ptr;  
//    MYSQL_RES *res_ptr=NULL;  
//    MYSQL_ROW sqlrow=0;  
//    int res=0;  
    /*for(;;)
	{
		if(!ConnectToMysql(&conn_ptr,Server, User, Password, DataBase))
		{	
#ifdef DEBUG
			printf("Connect to mysql success\n");
			LOG4C((LOG_NOTICE, "Connect to mysql success"));			
#endif
			break;
		}
		else
		{
			printf("Connect to mysql fail,after 5s reconnect...\n");
			LOG4C((LOG_ERROR,"Connect to mysql fail,after 5s reconnect..."));	
			sleep(5);
			continue;
		}
	}     		*/	
//////////////////////连接mysql结束//////////////////////////////////////////////////  

	//变量及配置解析放大循环外面
	char RequestLogNewName[30]={0};
	char RequestLogName[30]={0};
	char F5LogNewLocation[50]={0};
	char F5LogNewName[40]={0};
	char cmdip[100]={0};//request log bash
	FILE *stream;//用于获得bash脚本的输出
	FILE *streamSearchF5;
	char SmallBuf[50]={0};
	char streamSearchF5buf[1024]={0};//用于存储从stream中读取的数据，即search脚本执行的结果
	char SelectBuf[100]={0};
	char streamSearchF5bufTemp[1024]={0};
	char InsertToList[1024]={0};
	for(;;)//////////////////////////////////整个大循环
	{
	//2、解析日志///////////////////////////////////
		memset(RequestLogNewName,0,sizeof(RequestLogNewName));
		memset(F5LogNewLocation,0,sizeof(F5LogNewLocation));
		memset(F5LogNewName,0,sizeof(F5LogNewName));
		if(!GetRequestLogNewName(RequestLogNewName))//获得文件名
		{
#ifdef DEBUG
			printf("RequestLogNewName=%s\n",RequestLogNewName);
			LOG4C((LOG_NOTICE,"RequestLogNewName=%s",RequestLogNewName));
#endif
		}
		else
		{
			printf("cant get RequestLogNewName,sleep 5 sec!\n");
			LOG4C((LOG_NOTICE,"cant get RequestLogNewName,sleep 5 sec!"));
			sleep(5);
			continue;//大循环从头开始
		}
		if(!GetF5LogLocationNewName(F5LogNewLocation,F5LogNewName))
		{
#ifdef DEBUG
			printf("F5LogNewName=%s%s/%s\n",F5LogLocation,F5LogNewLocation,F5LogNewName);
			LOG4C((LOG_NOTICE,"F5LogNewName=%s%s/%s",F5LogLocation,F5LogNewLocation,F5LogNewName));
#endif
		}
		else
		{
			printf("Get F5LogNew location and name error,sleep 5 sec!\n");
			LOG4C((LOG_NOTICE,"Get F5LogNew location and name error,sleep 5 sec!"));
			sleep(5);
			continue;//大循环从头开始
		}
 //新文件名生成，但实际文件有可能还未生成，需sleep(4),实际文件是5分或10分过2、3秒后生成的。
			sleep(4);
			memset(cmdip,0,sizeof(cmdip));
			sprintf(cmdip, "bash ip.sh %s%s",RequestLogLocation,RequestLogNewName);//解析出ip及count
#ifdef DEBUG
			printf("cmdip=%s\n",cmdip);	
			LOG4C((LOG_NOTICE,"cmdip=%s",cmdip));
#endif
    		stream=popen(cmdip,"r");
			if(stream==NULL)//命令未执行成功进入下一次循环
			{
				printf("Stream popen bash ip.sh:%s!\n",strerror(errno));
				LOG4C((LOG_ERROR,"Stream popen bash ip.sh:%s!",strerror(errno)));
				continue;//继续外层大循环
			}
			char *buff[2] = {0}; 
			for (int i = 0; i < atoi(ip); i++) //此处循环是要解析几个ip地址的信息
			{  
     			memset(SmallBuf,0,sizeof(SmallBuf)); 
				//从stream中读取一行，992 222.73.133.32
     			if(fgets(SmallBuf,sizeof(SmallBuf),stream) != NULL)
				{
					//去掉换行符
					del_str_line(SmallBuf);
					//将992 222.73.133.32拆分成两个字符串              
					buff[0] = strtok(SmallBuf," " );  //count
					buff[1] = strtok(NULL," ");  //ip

					//将前10ip对应的f5日志条目筛选出来。直接分析5分钟内的access_f5.log
					memset(cmdip,0,sizeof(cmdip));
					//获得系统时间，拼出目录和文件名，
					///F5_log/2014_5m/20140109/2014010916/caccess_f5_20140109_1615.log
					sprintf(cmdip, "nohup bash SearchIpInF5.sh %s%s/%s %s",F5LogLocation,F5LogNewLocation,F5LogNewName,buff[1]);//解析出ip及count,buff[1]为ip地址
#ifdef DEBUG
					printf("Searchipinf5=%s\n",cmdip);	
					LOG4C((LOG_NOTICE,"Searchipinf5=%s",cmdip));
#endif
    				streamSearchF5=popen(cmdip,"r");
    				if(streamSearchF5==NULL)//查找ip失败，继续下次循环，查找下一个ip的信息，583行的ip的循环
					{
						printf("StreamSearchF5 popen:%s!\n",strerror(errno));
						LOG4C((LOG_ERROR,"StreamSearchF5 popen:%s!",strerror(errno)));
						continue;
					}
				   //得到的流是基于前20个ip的url信息，例子如下： 
				   // 101.226.180.132 [08/Jan/2014:11:11:22 +0800] 
				   ///login?URL=%2Fdiskall%2Fdownloadfile.php%3Ffileid%3D2707567%26type%3DEclass%26diskid%3D46783 
				   //http://www.yiban.cn/eclass/home.php?id=46783
					//将信息放入redis里保存，最终存储到mysql中
				  memset(streamSearchF5buf,0,sizeof(streamSearchF5buf));
				  for(int ii=0;ii<atoi(url);ii++)
				  {
					memset(streamSearchF5buf,0,sizeof(streamSearchF5buf));
				  if(fgets(streamSearchF5buf,sizeof(streamSearchF5buf),streamSearchF5) != NULL)//取出一条分析
				  {
      				del_str_line(streamSearchF5buf);//去掉回车换行
        
					//在streamSearchF5buf中增加url对应的数据库表信息,

					///////////url数据库中查找url对应表信息-start
					memset(streamSearchF5bufTemp,0,sizeof(streamSearchF5bufTemp));
					strcpy(streamSearchF5bufTemp,streamSearchF5buf);//复制一份取出url
					strtok(streamSearchF5bufTemp," " );  
					strtok(NULL," ");
					strtok(NULL," ");
					buff[1] = strtok(NULL," ");//第四个字段是url
			#ifdef DEBUG
					printf("Url:%s\n",buff[1]);		
					LOG4C((LOG_NOTICE,"Url:%s",buff[1]));
			#endif
			///这里处理下url，截取问号前面的部分，若没有问号则不变//////////////////////
			//GetUrlBeforeQuestionMark(buff[1]);//这里要不要去除问号后面的url语句？
			/*#ifdef DEBUG		
					printf("Url after del question mark:%s\n",buff[1]);
					LOG4C((LOG_NOTICE,"Url after del question mark:%s",buff[1]));
			#endif	*/	
//////////////查询database1 获得url及sql信息//start/////////////////////
	///* Let's check what we have inside the list */
 //   reply = redisCommand(c,"LRANGE mylist 0 -1");
 //   if (reply->type == REDIS_REPLY_ARRAY) {
 //       for (j = 0; j < reply->elements; j++) {
 //           printf("%u) %s\n", j, reply->element[j]->str);
 //       }
 //   }
		redisCommand(c,"select %s",urldatabase);//选择url的数据库	
		reply = (redisReply *)redisCommand(c,"smembers %s",buff[1]);
		redisCommand(c,"select %s",redisDatabase);//回到原来的数据库
		char temp[1024]={0};
		char *p=temp;//用于将多个url拼接成一个
		if (reply->type == REDIS_REPLY_ARRAY) 
		{
			for (int j = 0; j < reply->elements; j++) 
			{
#ifdef DEBUG  
				printf("%u) %s\n", j, reply->element[j]->str);
				LOG4C((LOG_NOTICE,"%u) %s",j, reply->element[j]->str));
#endif	
				sprintf(p,"%s;",reply->element[j]->str);
				p+=strlen(reply->element[j]->str)+1;
			}
		}
///////////////////////////////插入开始///////////////
	{
					memset(InsertToList,0,sizeof(InsertToList));
					sprintf(InsertToList,"%s %s %s",buff[0],streamSearchF5buf,temp);
					{////////////写入redis///start////////////////////
						//先判断redis是否还在线，如果不在线循环连接
						reply = redisCommand(c,"PING");
#ifdef DEBUG
						printf("PING: %s\n", reply->str);//返回pong为连接成功
						LOG4C((LOG_NOTICE,"PING: %s", reply->str));
#endif								 
						//redis在线
						if(reply->type == REDIS_REPLY_STATUS &&strcasecmp(reply->str,"pong") == 0)
						{
							freeReplyObject(reply);
							reply = redisCommand(c,"rpush iplist%s %s",RequestLogNewName,InsertToList);	
#ifdef DEBUG
							printf("rpush iplist%s %s\n",RequestLogNewName,InsertToList);
							printf("rpush: %s\n", reply->str);
							LOG4C((LOG_NOTICE,"rpush iplist%s %s",RequestLogNewName,InsertToList));
							LOG4C((LOG_NOTICE,"rpush: %s", reply->str));
#endif
							freeReplyObject(reply);
						}
						else//lpush问题，redis挂掉，每5s重连一次
						{
								for(;;)
								{
									if(!ConnectToRedis(cfg.tcp.host,cfg.tcp.port,&c))
									{	
							#ifdef DEBUG
										printf("Connect to redis success\n");
										LOG4C((LOG_NOTICE, "Connect to redis success"));			
							#endif
										break;
									}
									else
									{
										printf("Connect to redis fail,after 5s reconnect...\n");
										LOG4C((LOG_ERROR,"Connect to redis fail,after 5s reconnect..."));	
										sleep(5);
										continue;
									}
								}
						}
					}////////////写入redis///end////////////////////
				}///////////////////////////////插入结束///////////////				
//////////////查询database1 获得url及sql信息//end/////////////////////										
  }		
} 
   pclose(streamSearchF5); 
   }
}     	
    	pclose(stream);   
    	sleep(250);//解析完一次，250s后再继续    	
} 
		
    /* Disconnects and frees the context */
    redisFree(c);	
	LOG4C_FINI();//日志模块扫尾代码，程序退出时调用扫尾代码防止内存/资源泄漏。
    return 0;
}
