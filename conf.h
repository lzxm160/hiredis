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
//�����ļ��Ⱥ����ҿ����пո�Ҳ����û��
//ip=192.16.31.2

/*   ɾ����ߵĿո�   */
char * l_trim(char * szOutput, const char *szInput);

/*   ɾ���ұߵĿո�   */
char *r_trim(char *szOutput, const char *szInput);
 
/*   ɾ�����ߵĿո�   */
char * a_trim(char * szOutput, const char * szInput);
 
 
int GetProfileString(char *profile, char *AppName, char *KeyName, char *KeyVal );

#endif
