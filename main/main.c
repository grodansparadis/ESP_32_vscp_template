//##################################################################
//Projekt: VSCP Very Simple Control Protokoll implementation ESP32 #
//##################################################################
//
// This Project implements the VSCP over Ethernet on the ESP32 Thing
//
//
//Authors:  Gerhard Sabeditsch, Juergen Schoener, Juergen Spandl,
//          Christian Moedelhammer
//
//Version: 1.0
//
//##################################################################

//******************************************************************
// STANDARD INCLUDES - ESP32 - THING
//******************************************************************
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//******************************************************************
// SPECIAL INCLUDES - ESP32 - THING
//******************************************************************

#include "esp_err.h"            //for error handling
#include "esp_intr_alloc.h"     //is used for interrupts
#include "config.h"				/* this has the defines for all the sensors
								 and actors of the esp32_Thing */
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++



//******************************************************************
// DEFINES - ESP32 - THING
//******************************************************************


//Everything for Timer Implementation
#define TIMER_INTR_SEL TIMER_INTR_LEVEL  /*!< Timer level interrupt */
#define TIMER_GROUP    TIMER_GROUP_0     /*!< Test on timer group 0 */
#define TIMER_DIVIDER   16               /*!< Hardware timer clock divider */
#define TIMER_SCALE    (TIMER_BASE_CLK / TIMER_DIVIDER)  /*!< used to calculate counter value */
#define TIMER_FINE_ADJ   (1.4*(TIMER_BASE_CLK / TIMER_DIVIDER)/1000000) /*!< used to compensate alarm value */
#define TIMER_INTERVAL0_SEC   (3.4179)   /*!< test interval for timer 0 */
#define TIMER_INTERVAL1_SEC   (5.78)   /*!< test interval for timer 1 */
#define TEST_WITHOUT_RELOAD   0   /*!< example of auto-reload mode */
#define TEST_WITH_RELOAD   1      /*!< example without auto-reload mode */
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


//******************************************************************
// SPECIAL INCLUDES - ESP32 - VSCP Very Simple Control Protokoll
//******************************************************************
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "vscp_class.h"
#include "vscp_type.h"
#include "vscp_firmware.h"
#include "vscp_millisecond_timer.h"

//******************************************************************
// SPECIAL INCLUDES - ESP32 - HW Lichtschranke + Lichtrelais + Lichtsensor + Jalousie
//******************************************************************
#include "lichtschranke.h"
#include "lichtsensor.h"
#include "lichtrelay.h"
#include "millisekundentimer.h"
#include "jalousie.h"


//******************************************************************
// INCLUDES - FOR http sender
//******************************************************************

#include "httpgetsend.h"

//******************************************************************
// INCLUDES - FOR TCP Connection
//******************************************************************
//#include <stdio.h>
//#include "lwip/err.h"
//#include "lwip/sockets.h"
//#include "lwip/sys.h"
//#include <math.h>
//#include <sys/time.h>
//#include "freertos/task.h"
//#include "freertos/event_groups.h"
//#include "esp_system.h"
//#include "esp_wifi.h"
//#include "esp_event_loop.h"
//#include "esp_log.h"
//#include "nvs_flash.h"
//#include "errno.h"
//#include "string.h"
//#include "error.h"





/**************************************************/


//******************************************************************
// INCLUDES - FOR webserver
//******************************************************************

//#include "webserver.h"

#include "cJSON.h"
#include "lwip/api.h"


/**************************************************/


//******************************************************************
// GLOBAL VARIABLES - FOR webserver
//******************************************************************

char* json_unformatted;

/**************************************************/

//*********************************************************
//************ WEB CONTEND FOR WEB SERVER ++++++++++++++++*/

// STATIC HTTP HEADER
const static char http_html_hdr[] =
"HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";


// STATIC HTTP PAGE
const static char http_index_hml[] = "<!DOCTYPE html>"
"<html>\n"
"<head>\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"  <style type=\"text/css\">\n"
"    html, body, iframe { margin: 0; padding: 0; height: 100%; }\n"
"    iframe { display: block; width: 100%; border: none; }\n"
"  </style>\n"
"<title>Better Than VSCP</title>\n"
"</head>\n"
"<body>\n"
"<h1>Hello World, from ESP32!</h1>\n"
"</body>\n"
"</html>\n";

//************ WEB CONTEND FOR WEB SERVER ++++++++++++++++*/
//*********************************************************/



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//******************************************************************
// Prototypes - ESP32 - THING - Overview ;-)
//******************************************************************


void IRAM_ATTR btn0_isr_handler(void* arg);

/* +++++++++++++++++++++++++++ śtruct for timevalue (for test porposes) +++++++++++++++++++++++++++++ */
struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };   /* btw settimeofday() is helpfull here too*/





/**
 * WIFI Event Handler, falls die Verbindung abreist oder so ;-)
 */
static esp_err_t event_handler(void *ctx, system_event_t *event){
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        printf("SYSTEM_EVENT_STA_START\n");
		esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
    	printf("SYSTEM_EVENT_STA_GOT_IP\n");
    	xEventGroupSetBits(wifi_event_group, STA_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
    	printf("SYSTEM_EVENT_STA_DISCONNECTED\n");
    	esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, STA_CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* Button 0 ISR
 *
 * This is the ISR for Button 0 on falling edge
 *
 * \author: Christian Mödlhammer
 */
void IRAM_ATTR btn0_isr_handler(void* arg)
{
	uint32_t sec, us;
	uint64_t ts,ts_init,ts_next;

	// Null für den Ausgangswert
	vscp_initbtncnt=0;

	if (gpio_get_level(GPIO_NUM_0)==0){

		gettimeofday(&tv, NULL);
			(sec) = tv.tv_sec;
			(us) = tv.tv_usec;
			(ts) = (uint64_t)(sec*1000000+us);
			ts_init=ts;
			ts_next=ts;
			while ((ts_next-ts_init)<250000){ // wait for 250 milliseconds
				gettimeofday(&tv, NULL);
				(sec) = tv.tv_sec;
				(us) = tv.tv_usec;
				(ts) = (uint64_t)(sec*1000000+us);
				ts_next=ts;
			}

			//if button pressed for more than 250 milliseconds
			if (gpio_get_level(GPIO_NUM_0)==0) vscp_initbtncnt=251;

	}

}


/**
 * initializes the WIFI and the connection to the in config.h specified Access Point
 *
 *
 */
static void initialise_wifi(void){

	STA_CONNECTED_BIT = BIT0;
	tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t sta_config = {
        .sta = {
            .ssid = WIFIROUTER,
            .password = WIFIPASS,
			.bssid_set = 0
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}


/*
 * EVERYTHING FOR THE WEB-SERVER
 */

static void http_server_netconn_serve(struct netconn *conn)
{
    struct netbuf *inbuf;
    char *buf;
    uint16_t buflen=1000;
    err_t err;
    char channelId[3]="3";  // this are just assigned testvalues and should go away
    char commandType[3]="0"; // this are just assigned testvalues and should go away
    char value[3]="0"; // this are just assigned testvalues and should go away
    uint32_t status;
    
    /* Read the data from the port, blocking if nothing yet there.
     We assume the request (the part we care about) is in one netbuf */
    err = netconn_recv(conn, &inbuf);
    

    if (err == ERR_OK) {

    	netbuf_data(inbuf, (void**)&buf, &buflen);
        
        // strncpy(_mBuffer, buf, buflen);
        
        /* Is this an HTTP GET command? (only check the first 5 chars, since
         there are other formats for GET, and we're keeping it very simple )*/
        printf("buffer = %s \n", buf);
        if (buflen>=5 &&
            buf[0]=='G' &&
            buf[1]=='E' &&
            buf[2]=='T' &&
            buf[3]==' ' &&
            buf[4]=='/' ) {
            printf("buf[5] = %c\n", buf[5]);
            /* Send the HTML header
             * subtract 1 from the size, since we dont send the \0 in the string
             * NETCONN_NOCOPY: our data is const static, so no need to copy it
             */
            
            netconn_write(conn, http_html_hdr, sizeof(http_html_hdr)-1, NETCONN_NOCOPY);
            
            if(buf[5]=='h') {
                /* Send our HTML page */
                netconn_write(conn, http_index_hml, sizeof(http_index_hml)-1, NETCONN_NOCOPY);
            }
            else if(buf[5]=='l') {
                /* Send our HTML page */
                netconn_write(conn, http_index_hml, sizeof(http_index_hml)-1, NETCONN_NOCOPY);
            }
            else if(buf[5]=='j') {
                netconn_write(conn, json_unformatted, strlen(json_unformatted), NETCONN_NOCOPY);
            }
            else {
                netconn_write(conn, http_index_hml, sizeof(http_index_hml)-1, NETCONN_NOCOPY);
            }
        }
        
    }
    /* Close the connection (server closes in HTTP) */
    netconn_close(conn);
    ESP_LOGI(TAG, "... Webserver connection closed.");
    
    /* Delete the buffer (netconn_recv gives us ownership,
     so we have to make sure to deallocate the buffer) */
    netbuf_delete(inbuf);

    /* pasring should be done here --> ToDo Gerhard */


    /* logic for sending to control queue */

    printf("Rückgabe von verlgeich (%s : %s) compare: %d",channelId,CHANNEL3,strcmp(channelId,CHANNEL3));
    if ((strcmp(channelId,CHANNEL3)==0) && (strcmp(commandType,COMMANDTYPE_OnOffType)==0)){
    	if (strcmp(value,OnOffType_Off)==0){
    		status=LIGHT_OFF;
    		xQueueSendToBack(relay_queue, &status, 0);
    	}
    	if (strcmp(value,OnOffType_On)==0){
    		status=LIGHT_ON;
    		xQueueSendToBack(relay_queue, &status, 0);
    	}
    }

    if ((strcmp(channelId,CHANNEL4)==0) && (strcmp(commandType,COMMANDTYPE_OnOffType)==0)){
    	if (strcmp(value,OnOffType_Off)==0){
        	status=JALOUSIE_OFF;
        	xQueueSendToBack(relay_queue, &status, 0);
        }
    }

    if ((strcmp(channelId,CHANNEL4)==0) && (strcmp(commandType,COMMANDTYPE_UpDownType)==0)){
    	if (strcmp(value,UpDownType_Up)==0){
      		status=JALOUSIE_UP;
      		xQueueSendToBack(relay_queue, &status, 0);
      	}
      	if (strcmp(value,UpDownType_Down)==0){
      		status=JALOUSIE_DOWN;
      		xQueueSendToBack(relay_queue, &status, 0);
      	}
    }
    /* end of logic for sending control messages to internal queue */

}




/**
 * open http listening Server at specified port
 *
 *
 */


static void http_server(void *pvParameters)
{
    struct netconn *conn, *newconn;
    err_t err;
    conn = netconn_new(NETCONN_TCP);
    netconn_bind(conn, NULL, WEBSERVER_PORT);
    netconn_listen(conn);
    do {
        err = netconn_accept(conn, &newconn);
        if (err == ERR_OK) {
            http_server_netconn_serve(newconn);
            netconn_delete(newconn);
        }
    } while(err == ERR_OK);
    netconn_close(conn);
    netconn_delete(conn);
}


static void generate_json() {
    cJSON *root, *info, *d;
    root = cJSON_CreateObject();
    
    cJSON_AddItemToObject(root, "d", d = cJSON_CreateObject());
    cJSON_AddItemToObject(root, "info", info = cJSON_CreateObject());
    
    cJSON_AddStringToObject(d, "myName", "CMMC-ESP32-NANO");
    cJSON_AddNumberToObject(d, "temperature", 30.100);
    cJSON_AddNumberToObject(d, "humidity", 70.123);
    
    cJSON_AddStringToObject(info, "ssid", "dummy");
    //cJSON_AddNumberToObject(info, "heap", system_get_free_heap_size());
    //cJSON_AddStringToObject(info, "sdk", system_get_sdk_version());
    //cJSON_AddNumberToObject(info, "time", system_get_time());
    
    while (1) {
        //cJSON_ReplaceItemInObject(info, "heap",
        //													cJSON_CreateNumber(system_get_free_heap_size()));
        //cJSON_ReplaceItemInObject(info, "time",
        //													cJSON_CreateNumber(system_get_time()));
        //cJSON_ReplaceItemInObject(info, "sdk",
        //													cJSON_CreateString(system_get_sdk_version()));
        
        json_unformatted = cJSON_PrintUnformatted(root);
        printf("[len = %d]  ", strlen(json_unformatted));
        
        for (int var = 0; var < strlen(json_unformatted); ++var) {
            putc(json_unformatted[var], stdout);
        }
        
        printf("\n");
        fflush(stdout);
        delay_ms(2000);
        free(json_unformatted);
    }
}


/**
 * Task for register and heartbeat
 *
 *
 */
static void http_send_heartbeat(void *pvParameters)
{

	char request[500];

   while(1) {

   	//register all channels
	   	   	strcpy(request, "GET /api/Thing/register?thingId=");
	  	   	        strcat(request, THING_ID);
	  	   	        strcat(request, "&channelId=");
	  	   	        strcat(request, CHANNEL1);
	  	   	        strcat(request, "&thingType=");
	  	   	        strcat(request, "0");
	  	   	        strcat(request, "&channelType=");
	  	   	        strcat(request, CHANNELTYPE1);
	  	   	        strcat(request, " HTTP/1.0\r\n");
	 	   	        strcat(request, "Host: ");
	 	   	        strcat(request, DEAMON_SERVER);
	  	   	        strcat(request, "\r\n");
	  	   	        strcat(request, "User-Agent: esp-idf/1.0 esp32\r\n");
	  	   	        strcat(request, "\r\n");
	  	   	     xQueueSendToBack(send_queue, &request, 0);
	  	   	     ESP_LOGI(TAG,"Registering Lightsensor at Deamon!");
	  	   	     delay_ms(1000);

	  	   	 strcpy(request, "GET /api/Thing/register?thingId=");
	  	   	        strcat(request, THING_ID);
	  	   	        strcat(request, "&channelId=");
	  	   	        strcat(request, CHANNEL2);
	  	   	        strcat(request, "&thingType=");
	  	   	        strcat(request, "0");
	  	   	        strcat(request, "&channelType=");
	  	   	        strcat(request, CHANNELTYPE2);
	  	   	        strcat(request, " HTTP/1.0\r\n");
	 	   	        strcat(request, "Host: ");
	 	   	        strcat(request, DEAMON_SERVER);
	  	   	        strcat(request, "\r\n");
	  	   	        strcat(request, "User-Agent: esp-idf/1.0 esp32\r\n");
	  	   	        strcat(request, "\r\n");
	  	   	     xQueueSendToBack(send_queue, &request, 0);
	  	   	     ESP_LOGI(TAG,"Registering Lightbarrier at Deamon!");
	  	   	     delay_ms(1000);

	  	   	 strcpy(request, "GET /api/Thing/register?thingId=");
	   	        strcat(request, THING_ID);
	   	        strcat(request, "&channelId=");
	   	        strcat(request, CHANNEL3);
	   	        strcat(request, "&thingType=");
	   	        strcat(request, "0");
	   	        strcat(request, "&channelType=");
	   	        strcat(request, CHANNELTYPE3);
	   	        strcat(request, " HTTP/1.0\r\n");
	   	        strcat(request, "Host: ");
	   	        strcat(request, DEAMON_SERVER);
	   	        strcat(request, "\r\n");
	   	        strcat(request, "User-Agent: esp-idf/1.0 esp32\r\n");
	   	        strcat(request, "\r\n");
	   	     xQueueSendToBack(send_queue, &request, 0);
	   	     ESP_LOGI(TAG,"Registering Switch at Deamon!");
	   	     delay_ms(1000);

	   	     strcpy(request, "GET /api/Thing/register?thingId=");
  	   	        strcat(request, THING_ID);
  	   	        strcat(request, "&channelId=");
  	   	        strcat(request, CHANNEL4);
  	   	        strcat(request, "&thingType=");
  	   	        strcat(request, "0");
  	   	        strcat(request, "&channelType=");
  	   	        strcat(request, CHANNELTYPE4);
  	   	        strcat(request, " HTTP/1.0\r\n");
 	   	        strcat(request, "Host: ");
 	   	        strcat(request, DEAMON_SERVER);
  	   	        strcat(request, "\r\n");
  	   	        strcat(request, "User-Agent: esp-idf/1.0 esp32\r\n");
  	   	        strcat(request, "\r\n");
  	   	     xQueueSendToBack(send_queue, &request, 0);
  	   	     ESP_LOGI(TAG,"Registering Rollershutter at Deamon!");



  	  delay_ms(60000);  // send heartbeat every 60 seconds!
   }
}

/**
 * Task for translating queues and sending messages
 *
 *
 */
static void http_send_queue_translator(void *pvParameters)
{

	messageparameters mpara;
	char request[500];

   while(1) {


	xQueueReceive(sensor_queue, &mpara, portMAX_DELAY);
   	strcpy(request, "GET /api/Thing/set?thingId=");
   	        strcat(request, THING_ID);
   	        strcat(request, "&channelId=");
   	        strcat(request, mpara.channelId);
   	        strcat(request, "&commandType=");
   	        strcat(request, mpara.commandType);
   	        strcat(request, "&value=");
   	        char tmp[sizeof(uint32_t)];
   	        sprintf(tmp,"%d", mpara.value);
   	        strcat(request, tmp);
   	        strcat(request, " HTTP/1.0\r\n");
   	        strcat(request, "Host: ");
   	        strcat(request, DEAMON_SERVER);
   	        strcat(request, "\r\n");
   	        strcat(request, "User-Agent: esp-idf/1.0 esp32\r\n");
   	        strcat(request, "\r\n");

   	     ESP_LOGI(TAG,"Sending value to send_queue: %s",request);
   	     xQueueSendToBack(send_queue, &request, 0);

   }
}

/* app_main()
 *
 *  This Function is the Main Funktion of the Project
 *
 */
void app_main(void)
{
	//uint32_t UpOrDownMain;
	nvs_flash_init(); //Initialize NVS flash storage with layout given in the partition table


	//create a sensor queue for sending values
	    sensor_queue = xQueueCreate(10, sizeof(messageparameters));
	// Start HW Components and millisecond timer
	app_lichtsensor(); // starting light sensor
	app_timer(); // starting the timer with the queues 1 ms and 50 ms
	app_lichtschranke(); // starting the light barrier logic
	app_lichtrelay(); // starting the light relay logic
	app_jalousie(); // starting the jalousie task


	// Start WIFI connection
	initialise_wifi();
	app_httpgetsend(); // starts the http-get sending task
	xTaskCreate(&http_send_heartbeat, "http_send_heartbeat", 4096, NULL, 5, NULL);
	xTaskCreate(&http_send_queue_translator, "http_send_queue_translator", 4096, NULL, 5, NULL);
    xTaskCreate(&http_server, "http_server", 4096, NULL, 6, NULL);
    
    //--VSCP------------------------//
    init_vscp_millisecond_timer();

    

    gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT);
    int level = 0;
    while (true) {
    		gpio_set_level(GPIO_NUM_5, level);
    	        level = !level;


    	        vTaskDelay(300 / portTICK_PERIOD_MS);

    }
}

