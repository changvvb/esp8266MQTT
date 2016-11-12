/*
 * File	: at_port.c
 * This file is part of Espressif's AT+ command set program.
 * Copyright (C) 2013 - 2016, Espressif Systems
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of version 3 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "ets_sys.h"
#include "at.h"
#include "config.h"
#include "debug.h"
#include "user_interface.h"
#include "osapi.h"
#include "gpio.h"
#include "driver/uart.h"
#include "mqtt.h"
#include "my.h"
#include <string.h>
#include <stdlib.h>

/** @defgroup AT_PORT_Defines
  * @{
  */
#define at_cmdLenMax 128
#define at_dataLenMax 2048
/**
  * @}
  */

/** @defgroup AT_PORT_Extern_Variables
  * @{
  */
extern uint16_t at_sendLen;
extern uint16_t at_tranLen;
//extern UartDevice UartDev;
//extern bool IPMODE;
extern os_timer_t at_delayCheck;
extern uint8_t ipDataSendFlag;
/**
  * @}
  */

/** @defgroup AT_PORT_Extern_Functions
  * @{
  */
extern void at_ipDataSending(uint8_t *pAtRcvData);
extern void at_ipDataSendNow(void);
/**
  * @}
  */

os_event_t    recvTaskQueue[recvTaskQueueLen];
os_event_t    cmdTaskQueue[cmdTaskQueueLen];
//os_event_t    at_busyTaskQueue[at_busyTaskQueueLen];
os_event_t    onTaskQueue[at_procTaskQueueLen];

BOOL specialAtState = TRUE;
at_stateType  at_state;
uint8_t *pDataLine;
BOOL echoFlag = TRUE;

static uint8_t at_cmdLine[at_cmdLenMax];
uint8_t at_dataLine[at_dataLenMax];/////

static void at_procTask(os_event_t *events);
static void cmdTask(os_event_t *events);

extern MQTT_Client mqtt_client;
extern UartDevice  UartDev;

static void ICACHE_FLASH_ATTR 
recvTask(os_event_t *events)
{
    static uint8_t mqtt_is_init = 0;
	uint8_t* p_topic;
    uint8_t* p_data;
    uint8_t data_length;
    uint8_t* p_buf;
	uint8_t* p;
    uint8_t qos;
	uint8_t* p_ip;
	uint8_t* p_port;
	uint32_t port;
	uint8_t* p_ssid;
	uint8_t* p_key;
	MQTT_Client *client = p_mqttClient;

	
	p_buf = UartDev.rcv_buff.pRcvMsgBuff;
	INFO("%s\r\n",p_buf);
	
	if(p = (uint8_t*)os_strstr(p_buf,"RESTART"))
	{
		system_restart();
	}
	
    //unsubscribe                          
    else if(p_buf == (uint8_t*)os_strstr(p_buf,"unsub:"))
    {
        if (NULL == client)
        {
            INFO("MQTT HAVE NOT CONNECTED\r\n");
            return;
        }
        if(p = (uint8_t*)os_strstr(p_buf,"topic="))
        {
            p_topic = p+os_strlen("topic=");
            for(;*p!= ',' && *p != '\0'; p++);
            *p = '\0';
            INFO("UnSubscribe topic %s OK\r\n",p_topic);
            MQTT_UnSubscribe(client,p_topic);
        }
        else 
        {
            INFO("ERROR\r\n");
        }

    } 

    //subscrib
    else if(p_buf == (uint8_t*)os_strstr(p_buf,"sub:"))
    {
        if (NULL == client)
        {
            INFO("MQTT HAVE NOT CONNECTED\r\n");
            return;
        }
        //p = p + os_strlen("subscribe:");
        if(p = (uint8_t*)os_strstr(p_buf,"topic="))
        {
            p_topic = p+os_strlen("topic=");
            for(;*p!= ',' && *p != '\0'; p++);
            *p = '\0';
            qos = 0;
            if(p = (uint8_t*)os_strstr(p+1,"qos"));
                qos=*(p+4)-48;
            if(qos > 2)
                qos = 0;
            MQTT_Subscribe(client,p_topic,qos);
            INFO("Subscribe topic OK   %s   %d\r\n",p_topic,qos);
        }
    } 


    //publish
    else if(p_buf == (uint8_t*)os_strstr(p_buf,"pub:"))
    {
        if (NULL == client)
        {
            INFO("MQTT HAVE NOT CONNECTED\r\n");
            return;
        }
        p = p + os_strlen("pub:");
        if(p = (uint8_t*)os_strstr(p_buf,"topic="))
        {
            p_topic = p + os_strlen("topic=");
            for(;*p!= ',' && *p != '\0'; p++);*p='\0';
            p_data = NULL;
            qos = 0;
            if(p = (uint8_t*)strstr(p+1,"data="))
            {
                p_data = p+strlen("data=");
                for(;*p!=','&&*p!='\0';p++);*p='\0';
                if(p = strstr(p+1,"qos"))
                    qos=*(p+4)-'0';
                if(qos > 2)
                    qos = 0;
            }
            MQTT_Publish(client,p_topic,p_data,p_data?os_strlen(p_data):0,qos,0);
            INFO("Publish topic=%s  data=%s  qos=%d \r\n",p_topic,p_data,qos);
        } 
        else 
        {
            INFO("ERROR\r\n");    
        }
    }

    //server
    else if(p_buf == (uint8_t*)os_strstr(p_buf,"mqtt:"))
    {
        //p = p + os_strlen("subscribe:");
        if(p = (uint8_t*)os_strstr(p_buf,"server="))
        {
            p_ip = p+os_strlen("server=");
            for(;*p!= ',' && *p != '\0'; p++);
            *p = '\0';
            if(p = (uint8_t*)os_strstr(p+1,"port="))
            {
                p_port = p+os_strlen("port=");
                for(;*p!=','&&*p!='\0';p++);*p='\0';
                port = atoi(p_port);
            }
            else 
                port = 1883;
            MQTT_Init(p_ip,port);
            mqtt_is_init = 1;
            INFO("mqtt server=%s port=%d\r\n",p_ip,port);
        }
        else
        {
            INFO("ERROR\r\n");
        }
    } 

    //wifi
    else if(p_buf == (uint8_t*)os_strstr(p_buf,"wifi:"))
    {
        if (0 == mqtt_is_init) 
        {
            INFO("MQTT HAVE NOT INIT\r\n");
            return;
        }
        p = p + os_strlen("wifi:");
        if(p = (uint8_t*)os_strstr(p_buf,"ssid="))
        {
            p_ssid = p + os_strlen("ssid=");
            for(;*p!= ',' && *p != '\0'; p++);*p='\0';

            if(p = (uint8_t*)strstr(p+1,"key="))
            {
                p_key = p+strlen("key=");
                for(;*p!=','&&*p!='\0';p++);*p='\0';
            }
            else 
                p_key = NULL;
            INFO("ssid=%s key=%d\r\n",p_ssid,p_key);
            WIFI_Connect(p_ssid,p_key,wifiConnectCb);
        } 
        else 
        {
            INFO("ERROR\r\n");
        }
    }
    else 
    {
        INFO("Commond not found: %s\r\n",p_buf);
    }

}



/*
static void ICACHE_FLASH_ATTR 
cmdTask(os_event_t *events)
{
    static uint8_t *pCmdLine;
    uint8_t* p_topic;
    uint8_t* p_data;
    uint8_t data_length;
    uint8_t* p_buf;
	uint8_t* p;
    uint8_t qos;

    MQTT_Client* client = p_mqttClient;
	p_buf = (uint8_t*)events;
	
	
	
    // while(READ_PERI_REG(UART_STATUS(UART0)) & (UART_RXFIFO_CNT << UART_RXFIFO_CNT_S))
    // {
   //temp = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF
        // WRITE_PERI_REG(0X60000914, 0x73); //WTD
        // if(at_state != at_statIpTraning)
        // {
            // temp = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
            // if((temp != '\n') && (echoFlag))
            // {
          //     uart_tx_one_char(temp); //display back
            //   uart_tx_one_char(UART0, temp);
                // *p++ = temp;
            // }
        // }

    // }
	
    *p = '\0';

	INFO  ("\r\nrev_buf: %s\r\n",p);
    if(p = strstr(p_buf,"subscrib:"))
    {
        p = p + strlen("subscribi:");
        if(p = strstr(p_buf,"topic="))
        {
            p_topic = p+strlen("topic=");
            for(;*p!= ',' || *p != '\0'; p++);
            *p = '\0';
            if(p = strstr(p+1,"qos"))
                if(1 == sscanf(p,"qos=%d",&qos))
                    uart0_sendStr("Subscrib topic OK\r\n");
            

        }
       
    } 
    else if(p = strstr(p_buf,"publish:"))
    {
        p = p + strlen("publish:");
        if(p = strstr(p_buf,"topic="))
        {
            p_topic = p + strlen("topic=");
            for(;*p!=','||*p!='\0';p++);*p='\0';
            if(p == strstr(p+1,"message"))
            {
                p_data = p+strlen("message");
                for(;*p!=' '||*p!='\0';p++);*p='\0';
                    if(p = strstr(p+1,"qos"))
                    if(1 == sscanf(p,"qos=%d",&qos))
                    uart0_sendStr("Publish topic OK\r\n");

            }
        } 
    }
	
	  if(UART_RXFIFO_FULL_INT_ST == (READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_FULL_INT_ST))
	  {
		WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);
	  }
	  else if(UART_RXFIFO_TOUT_INT_ST == (READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_TOUT_INT_ST))
	  {
		WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_TOUT_INT_CLR);
	  }
	  ETS_UART_INTR_ENABLE();
}*/

/**
  * @brief  Task of process command or txdata.
  * @param  events: no used
  * @retval None
  */
static void ICACHE_FLASH_ATTR
at_procTask(os_event_t *events)
{
  if(at_state == at_statProcess)
  {
    at_cmdProcess(at_cmdLine);
    if(specialAtState)
    {
      at_state = at_statIdle;
    }
  }
  else if(at_state == at_statIpSended)
  {
    at_ipDataSending(at_dataLine);//UartDev.rcv_buff.pRcvMsgBuff);
    if(specialAtState)
    {
      at_state = at_statIdle;
    }
  }
  else if(at_state == at_statIpTraning)
  {
    at_ipDataSendNow();//UartDev.rcv_buff.pRcvMsgBuff);
  }
}

//static void ICACHE_FLASH_ATTR
//at_busyTask(os_event_t *events)
//{
//  switch(events->par)
//  {
//  case 1:
//    uart0_sendStr("\r\nbusy p...\r\n");
//    break;
//
//  case 2:
//    uart0_sendStr("\r\nbusy s...\r\n");
//    break;
//  }
//}

/**
  * @brief  Initializes build two tasks.
  * @param  None
  * @retval None
  */
  
#define cmdTaskPrio         1
#define cmdTaskQueueLen     64

void ICACHE_FLASH_ATTR
task_init()
{
  system_os_task(recvTask, recvTaskPrio, recvTaskQueue, recvTaskQueueLen);
  //system_os_task(cmdTask, cmdTaskPrio, cmdTaskQueue, cmdTaskQueueLen);
//  system_os_task(at_procTask, at_procTaskPrio, at_procTaskQueue, at_procTaskQueueLen);
}

/**
  * @}
  */
