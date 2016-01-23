#include "simcom.h"
#include <stm32f4xx.h>
#include <string.h>
#include "tm_stm32f4_usart.h"

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

#define WAITTIME 2000000

SemaphoreHandle_t xMutex;

//#ifdef DBG
static void dbg_puts(char *str)
{
    TM_USART_Puts(USART6, str);
}
//#else
//static void dbg_puts(char *str)
//{

//}
//#endif

static void RecvResponse(char *response)
{
    int wait = WAITTIME;
    int recv = 0;
    char c;

    while(wait--) {
        if((c = TM_USART_Getc(USART1)) != 0) {
            if(c == '\r') {

            }
            else {
                response[recv++] = c;
            }
        }
    }

    response[recv] = '\0';
}

/*
 * Send AT command.
 */
static void SendCmd(char *cmd)
{
    dbg_puts("Send: ");
    dbg_puts(cmd);
    dbg_puts("\n\r");

    TM_USART_Puts(USART1, cmd);
    TM_USART_Puts(USART1, "\r\n");
}

/*
 * Send AT command to SIMCOM module and check response immediately.
 */
static int SendCmd_Check(char *cmd, char *check)
{
    int result = 1;
    char recv_str[32];

    dbg_puts("Send: ");
    dbg_puts(cmd);
    dbg_puts("\n\r");
    TM_USART_Puts(USART1, cmd);
    TM_USART_Puts(USART1, "\r\n");

    if(check) {
        RecvResponse(recv_str);

        if(strlen(recv_str) > 0) {
            dbg_puts("Recv: ");
            dbg_puts(recv_str);
            dbg_puts("\n\r");
        }

        if(!strstr(recv_str, check)) {
            result = 0;
        }
    }

    return result;
}

void SIMCOM_Init()
{
    /* Init USART communication between STM and SIMCOM module */
    TM_USART_Init(USART1, TM_USART_PinsPack_1, 115200);

//#ifdef DBG
    /* Init USART Communication between PC and STM, use for debug */
    TM_USART_Init(USART6, TM_USART_PinsPack_1, 115200);
//#endif

    while(1) {
        dbg_puts("Try initilize communication between STM32 & SIMCOM module\n\r");
        if(SendCmd_Check("AT", "OK")) {
            dbg_puts("Initilization successful!\n\r");
            break;
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

    while(1) {
        dbg_puts("Try close ATE function\n\r");
        if(SendCmd_Check("ATE0", "OK")) {
            dbg_puts("Close successful!\n\r");
            break;
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

    while(1) {
        dbg_puts("Try disable calling number\n\r");
        if(SendCmd_Check("AT+CLIP=0", "OK")) {
            dbg_puts("Disable successful!\n\r");
            break;
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

    while(1) {
        dbg_puts("Try enable ATH\n\r");
        if(SendCmd_Check("AT+CVHU=0", "OK")) {
            dbg_puts("Enable successful!\n\r");
            break;
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

    while(1) {
        dbg_puts("Set SMS text mode\n\r");
        if(SendCmd_Check("AT+CMGF=1", "OK")) {
            dbg_puts("Set successful!\n\r");
            break;
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

    xMutex = xSemaphoreCreateMutex();
    if(xMutex == NULL) {
        dbg_puts("Mutex false\n\r");
    }
}

void SIMCOM_Dial(char *number)
{
    char cmd[16] = {0};
    char recv[64];

    strcpy(cmd, "ATD");
    strcat(cmd, number);
    strcat(cmd, ";");

    dbg_puts("Dial to ");
    dbg_puts(number);
    dbg_puts("\n\r");
    SendCmd(cmd);
    while(1) {
        RecvResponse(recv);

        if(strlen(recv) == 0) {
            continue;
        }

        if(strstr(recv, "NO CARRIER")) {
            dbg_puts("NO CARRIER\n\r");
        }
        else if(strstr(recv, "NO ANSWER")) {
            dbg_puts("NO ANSWER\n\r");
        }
        else if(strstr(recv, "ERROR")) {
            dbg_puts("ERROR\n\r");
        }
        else if(strstr(recv, "OK")) {
            dbg_puts("Dial successful!\n\r");
        }

        break;
    }
}

void SIMCOM_Answer()
{
    char recv[64];

    dbg_puts("Answer phone\n\r");
    SendCmd("ATA");

    while(1) {
        RecvResponse(recv);

        if(strlen(recv) == 0) {
            continue;
        }

        if(strstr(recv, "NO CARRIER")) {
            dbg_puts("NO CARRIER\n\r");
        }
        else if(strstr(recv, "OK")) {
            dbg_puts("Answer successful!\n\r");
        }

        break;
    }
}

void SIMCOM_HangUp()
{
    int try;

    for(try = 5; try >= 0; try--) {
        if(SendCmd_Check("ATH", "OK")) {
            break;
        }
    }
}

int SIMCOM_CheckPhone()
{
    char recv[64];
    int result = 0;

    if(xSemaphoreTake(xMutex, (TickType_t) 10000) == pdTRUE) {
        dbg_puts("Check coming call\n\r");
        RecvResponse(recv);
        xSemaphoreGive(xMutex);
    }
    else {
        dbg_puts("Block mutex\n\r");
        return 0;
    }

    if(strstr(recv, "RING")) {
        dbg_puts("Has coming call!\n\r");
        result = 1;
    }

    return result;
}

int SIMCOM_ReadSMS(SMS_STRUCT sms[3])
{
    int count = 0;
    char recv[128];
    char *pch;

    while(1) {
        if(xSemaphoreTake(xMutex, (TickType_t) 10) == pdTRUE) {
            dbg_puts("Receive SMS\n\r");
            SendCmd("AT+CMGL=\"ALL\"");

            vTaskDelay(5000 / portTICK_PERIOD_MS);

            RecvResponse(recv);
            xSemaphoreGive(xMutex);

            break;
        }
    }
    pch = recv;

    while((pch = strstr(pch, "+CMGL")) != NULL && count < 3) {
        /* Cut Number */
        pch = strchr(pch, '"');
        pch = strchr(pch + 1, '\"');
        pch = strchr(pch + 1, '\"');
        strncpy(sms[count].number, pch + 1, strchr(pch + 1, '\"') - pch - 1);
        sms[count].number[(int)(strchr(pch + 1, '\"') - pch) - 1] = '\0';

        // Skip until message content
        pch = strchr(pch + 1, '\n');

        /* Cut message content */
        if(strstr(pch + 1, "+CMGL")) {
            strncpy(sms[count].content, pch + 1, strstr(pch + 1, "+CMGL") - pch - 1);
            sms[count].content[(int)(strstr(pch + 1, "+CMGL") - pch - 1)] = '\0';
            pch = strstr(pch + 1, "+CMGL");
        }
        else {
            strncpy(sms[count].content, pch + 1, strstr(pch + 1, "OK") - pch - 1);
            sms[count].content[(int)(strstr(pch + 1, "OK") - pch - 1)] = '\0';
            pch = strstr(pch + 1, "OK");
        }

        count++;
    }

//#ifdef DBG
    int i;

    dbg_puts("Message\n\r");
    for(i = 0; i < count; i++) {
        dbg_puts("From: ");
        dbg_puts(sms[i].number);
        dbg_puts("\n\rContent: ");
        dbg_puts(sms[i].content);
        dbg_puts("\n\r");
    }
//#endif

    return count;
}

void SIMCOM_SendSMS(char *number, char *content)
{
    char cmd[128] = {0};
    char recv[64];

    strcpy(cmd, "AT+CMGSO=\"");
    strcat(cmd, number);
    strcat(cmd, "\",\"");
    strcat(cmd, content);
    strcat(cmd, "\"");

    dbg_puts("Send message to ");
    dbg_puts(number);
    dbg_puts("\n\r");
    SendCmd(cmd);
    while(1) {
        RecvResponse(recv);

        if(strlen(recv) == 0) {
            continue;
        }

        if(strstr(recv, "ERROR")) {
            dbg_puts("ERROR\n\r");
        }
        else if(strstr(recv, "OK")) {
            dbg_puts("Send successful!\n\r");
        }

        break;
    }

}

/* Test API */
void SIMCOM_Test()
{
    char c;
    SMS_STRUCT sms[3];

    while(1) {
        while((c = TM_USART_Getc(USART6)) != 0)
        {
            switch(c) {
                case 'd':
                    SIMCOM_Dial("0973439084");
                    break;
                case 'h':
                    SIMCOM_HangUp();
                    break;
                case 'c':
                    SIMCOM_Answer();
                    break;
                case 'e':
                    SIMCOM_CheckPhone();
                    break;
                case 's':
                    SIMCOM_ReadSMS(sms);
                    break;
                case 'i':
                    SIMCOM_SendSMS("0973439084", "Test\n123");
                    break;
                default:
                    break;
            }
            dbg_puts("Test\n\r");
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

int SIMCOM_OpenNetwork()
{
    if(!(SendCmd_check("AT+CGSOCKCONT=1,\"IP\",\"internet\"","OK") && SendCmd_check("AT+CSOCKSETPN=1","OK") && SendCmd_check("AT+CIPMODE=1","OK") ))
    {
        dbg_puts("Failed to initialize APN\n");
        return 0;
    }
    if(!SendCmd_check("AT+NETOPEN","OK\n\n+NETOPEN:0\n"))
    {
        dbg_puts("Failed to open network\n");
        return 0;
    }
    SendCmd("AT+IPADDR");
    SendCmd("AT+CHTTPSSTART");
    return 1;

}
//Ex simcom_http_request("http://www.tutorialspoint.com/index.htm",80)
void simcom_http_request(const char * url,int port)
{
    char request[512];
    char response[512];
    char cmd[64];
    char host[32];
    char relative_path[128];
    char *test=url;
    if(port>65535 || port<0)
    {
        dbg_puts("Port number is out of range\n");
        return ;
    }
    if(!strncmp("http://",test,7))
    {
        test+=7;
        dbg_puts("It's HTTP protocol\n");
    }
    else if(!strncmp("https://",test,8))
    {
        test+=8;
        dbg_puts("It's HTTPS protocol\n");
        return ;
    }
    int length=strstr(test,"/")-test;
    strncpy(host,test,length);
    strncpy(relative_path,test+length,strlen(test+length));
    dbg_puts("host: %s\npath: %s\n",host,relative_path);

    sprintf(cmd,"AT+CHTTPACT=0,\"TCP\",\"%s\",%d",host,port)

    SendCmd(cmd);

    //FIXME:this only check if error or not 
    while(strlen(response)==0)   //wait for response : +CHTTPACT:REQUEST/ERROR
        RecvResponse(response);
    if(!strncmp(response,"+CHTTPACT: REQUEST",18))//ERROR
    {
        dbg_puts("Something wrong on AT+CHTTPACT\n");
        return ;
    }
    sprintf(request,"GET %s HTTP/1.1\r\nHost: %s\r\n\r\n\r\n",relative_path,host)

        SendCmd(request);//though it sends two additional characters,the sim module will discard.

    //Start to receive packet
    //FIXME:this only output to debug channel 
    while(RecvResponse(response))
    {
        dbg_puts(response);
        if(!strncmp(response[strlen(response)-13],"+CHTTPACT: 0",12))//+CHTTPACT: 0
            break;
        response="\0";
    }
}

void simcom_https_request(const char * url,int port)
{
    char request[512];
    char response[512];
    char cmd[64];
    char host[32];
    char relative_path[128];
    char *test=url;
    if(port>65535 || port<0)
    {
        dbg_puts("Port number is out of range\n");
        return ;
    }
    if(!strncmp("http://",test,7))
    {
        test+=7;
        dbg_puts("It's HTTP protocol\n");
    }
    else if(!strncmp("https://",test,8))
    {
        test+=8;
        dbg_puts("It's HTTPS protocol\n");
        return ;
    }
    int length=strstr(test,"/")-test;
    strncpy(host,test,length);
    strncpy(relative_path,test+length,strlen(test+length));
    dbg_puts("host: %s\npath: %s\n",host,relative_path);
    sprintf(cmd,"AT+CHTTPACT=0,\"TCP\",\"%s\",%d",host,port)

    if(!SendCmd_Check(cmd,"OK"))
    {
        dbg_puts("Fail to connect to host\n");
        return ;
    }

    sprintf(request,"GET %s HTTP/1.1\r\nHost: %s\r\n\r\n\r\n",relative_path,host)
    sprintf(cmd,"AT+CHTTPSSEND=%d",strlen(request))
    SemdCmd(cmd);

    while(strlen(response)==0)   //wait for response : >
        RecvResponse(response);
    if(!strncmp(response,">",))
    {
        dbg_puts("Something wrong on AT+CHTTPSSEND\n");
        return ;
    }

    SendCmd_Check(request,"OK");//though it sends two additional characters,the sim module will discard.
    //FIXME: The check string maybe wrong 
    SendCmd_Check("AT+CHTTPSSEND","OK\r\n\r\nCHTTPSSEND: 0");

    SendCmd("AT+CHTTPSRECV=450");

    //Start to receive packet

    //FIXME:this only output to debug channel 
    while(RecvResponse(response))
    {
        dbg_puts(response);
        if(!strncmp(response[strlen(response)-15],"+CHTTPSRECV: 0",14))//+CHTTPACT: 0
        {
            SendCmd("AT+CHTTPSRECV=450");
            break;
        }response="\0";
    }
}

