#ifndef CONF_H
#define CONF_H

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
#define KEYVALLEN 100

//[cls_server]
//配置文件等号左右可以有空格也可以没有
//ip=192.16.31.2

/*   删除左边的空格   */
char * l_trim(char * szOutput, const char *szInput);

/*   删除右边的空格   */
char *r_trim(char *szOutput, const char *szInput);
 
/*   删除两边的空格   */
char * a_trim(char * szOutput, const char * szInput);
 
 
int GetProfileString(char *profile, char *AppName, char *KeyName, char *KeyVal );

#endif
