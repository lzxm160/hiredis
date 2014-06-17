#include <stdio.h>
#include <time.h>


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

int main()
{
    struct tm *ptm;
    struct tm *ptm1;
    long ts;
    long ts1;
    int year,mon,day,hour,min,sec;
    int year1,mon1,day1,hour1,min1,sec1;
    ts=time(NULL);
    ts1=ts-10;
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

		printf("ts=%ld\n",ts);
		printf("ts1=%ld\n",ts1);
		printf("ptm=%p\n",ptm);
		printf("ptm1=%p\n",ptm1);
		printf("%d_5m/%d%02d%02d/%d %02d %02d %02d %02d %02d\n",year,year,mon,day,year,mon,day,hour,min,sec);
		printf("%d_5m/%d%02d%02d/%d %02d %02d %02d %02d %02d\n",year1,year1,mon1,day1,year1,mon1,day1,hour1,min1,sec1);

		char buf[]="/function/download.php?eclass_id=48079&dir=20140113&filename=14495922246_83.xls&fn=13%BA%A3%BF%C6%C6%BD%BE%F9%D1%A7%B7%D6%BC%A8%B5%E3%C5%C5%C3%FB%A3%A8%B5%DA%D2%BB%D1%A7%C6%DA%A3%A9.xls";
		GetUrlBeforeQuestionMark(buf);
		printf("%s\n",buf);
}
