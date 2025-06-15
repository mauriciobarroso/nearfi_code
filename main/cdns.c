/**
  ******************************************************************************
  * @file           : cdns.c
  * @author         : Mauricio Barroso Benavides
  * @date           : Jul 23, 2024
  * @brief          : todo: write brief
  ******************************************************************************
  * @attention
  *
  * MIT License
  *
  * Copyright (c) 2024 Mauricio Barroso Benavides
  *
  * Permission is hereby granted, free of charge, to any person obtaining a copy
  * of this software and associated documentation files (the "Software"), to
  * deal in the Software without restriction, including without limitation the
  * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
  * sell copies of the Software, and to permit persons to whom the Software is
  * furnished to do so, subject to the following conditions:
  *
  * The above copyright notice and this permission notice shall be included in
  * all copies or substantial portions of the Software.
  * 
  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  * IN THE SOFTWARE.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#include "lwip/udp.h"
#include "lwip/lwip_napt.h"

/* Private macros ------------------------------------------------------------*/
#define CDNS_MESSAGE_MAX_LEN	512
#define CDNS_PENDING_QR_MAX		20
#define CDNS_PORT 				53
#define CDNS_QR_QUERY 			0
#define CDNS_QR_RESPONSE 		1
#define CDNS_EXT_DNS_NUM		6

/* External variables --------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/
typedef struct {
    uint16_t id;
    unsigned char rd : 1;
    unsigned char tc : 1;
    unsigned char aa : 1;
    unsigned char op_code : 4;
    unsigned char qr : 1;
    unsigned char r_code : 4;
    unsigned char z : 3;
    unsigned char ra : 1;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

typedef struct {
    char *name;
    uint16_t type;
    uint16_t class;
} dns_question_t;

typedef struct {
    char *name;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t data_len;
    struct in_addr addr;
} dns_answer_t;

typedef struct {
	char *domain_name;
	char buffer[CDNS_MESSAGE_MAX_LEN];
	int client_sock;
	int len;
	struct sockaddr_in client_addr;
	socklen_t addr_len;
} dns_query_t;

/* Private variables ---------------------------------------------------------*/
static char **blocked_domains;
static TaskHandle_t dns_task_handle;
static TaskHandle_t dns_ext_task_handle[CDNS_EXT_DNS_NUM];

static const char *ext_dns[CDNS_EXT_DNS_NUM] = {
		"1.1.1.1",
		"1.0.0.1",
		"8.8.8.8",
		"8.8.4.4",
		"1.1.1.1",
		"8.8.8.8",
};

static QueueHandle_t dns_query_queue;

/* Private function prototypes -----------------------------------------------*/
static int is_domain_blocked(const char *domain);
static void parse_domain_name(char *buffer, char *domain_name);
static void dns_task(void *arg);
static void dns_ext_task(void *arg);
static char **read_domains_from_file(const char *filename);

/* Exported functions definitions --------------------------------------------*/
void cdns_init(const char *base_path) /* todo: check names */
{
	/* Create queue to send external DNS requests */
	dns_query_queue = xQueueCreate(CDNS_PENDING_QR_MAX, sizeof(dns_query_t));

	if (dns_query_queue == NULL) {
		return;
	}

	/* Create task to redirect clients DNS requests */
	xTaskCreate(
			dns_task,
			"dns_server_task",
			4096,
			NULL,
			tskIDLE_PRIORITY + 5,
			&dns_task_handle);

	/* Create tasks to manage external DNS requests */
	for (uint8_t i = 0; i < CDNS_EXT_DNS_NUM; i++) {
		xTaskCreate(
				dns_ext_task,
				"dns_server_task",
				4096,
				(void*)ext_dns[i],
				tskIDLE_PRIORITY + 4,
				&dns_ext_task_handle[i]);
	}

	/* Load the blocked domains from file */
	char filepath[32];
	sprintf(filepath, "%s/domains.txt", base_path);
	blocked_domains = read_domains_from_file(filepath);
}

void cdns_deinit(void)
{
	vTaskDelete(dns_task_handle);

	for (uint8_t i = 0; i < CDNS_EXT_DNS_NUM; i++) {
		if (dns_ext_task_handle[i] != NULL) {
			vTaskDelete(dns_ext_task_handle[i]);
		}
	}
}

/* Private function definitions ----------------------------------------------*/
static int IRAM_ATTR is_domain_blocked(const char *domain)
{
	for (uint32_t i = 0; blocked_domains[i] != NULL; i++) {
		if (!strcmp(domain, blocked_domains[i])) {
			return 1;
		}
	}

	return 0;
}

static void IRAM_ATTR parse_domain_name(char *buffer, char *domain_name)
{
	char *p = buffer;
	while (*p != 0) {
		int len = *p;
		p++;
		for (int i = 0; i < len; i++) {
			*domain_name++ = *p++;
		}
		*domain_name++ = '.';
	}
	*--domain_name = '\0';
}

static void IRAM_ATTR dns_task(void *arg)
{
	int sock;
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		printf("Error creating socket\r\n");
		vTaskDelete(NULL);
		return;
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(CDNS_PORT);

	if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		printf("Error binding socket\r\n");
		close(sock);
		vTaskDelete(NULL);
		return;
	}

	for (;;) {
		uint8_t buffer[CDNS_MESSAGE_MAX_LEN];
		int len = recvfrom(sock, buffer, CDNS_MESSAGE_MAX_LEN, 0,
				(struct sockaddr*)&client_addr, &client_addr_len);

		if (len < 0) {
			printf("Failed to receive from client socket\r\n");
		}

		if (len > 0) {
			dns_header_t *dns_header = (dns_header_t*)buffer;
			if (dns_header->qr == CDNS_QR_QUERY) {
				char domain_name[256] = {0};
				parse_domain_name((char*)buffer + sizeof(dns_header_t),
						domain_name);

				if (is_domain_blocked(domain_name)) {
					dns_header->qr = CDNS_QR_RESPONSE;
					dns_header->an_count = htons(1);

					uint8_t *response = buffer + len;
					dns_answer_t dns_answer;
					dns_answer.name = NULL;
					dns_answer.type = htons(1);
					dns_answer.class = htons(1);
					dns_answer.ttl = htonl(60);
					dns_answer.data_len = htons(4);
					dns_answer.addr.s_addr = inet_addr("0.0.0.0");

					memcpy(response, &dns_answer, sizeof(dns_answer));
					response += sizeof(dns_answer);
					sendto(sock, buffer, response - buffer, 0,
							(struct sockaddr*)&client_addr, client_addr_len);
				} else {
					dns_query_t query = {
							.client_sock = sock,
							.domain_name = domain_name,
							.len = len,
							.client_addr = client_addr,
							.addr_len = client_addr_len
					};

					memcpy(query.buffer, buffer, len);
					xQueueSend(dns_query_queue, &query, portMAX_DELAY);
				}
			}
		}
	}

	close(sock);
	vTaskDelete(NULL);
}

static void dns_ext_task(void *arg)
{
	const char *ext_dns = (const char *)arg;

	struct sockaddr_in dns_addr;
	memset(&dns_addr, 0, sizeof(dns_addr));
	dns_addr.sin_family = AF_INET;
	dns_addr.sin_addr.s_addr = inet_addr(ext_dns);
	dns_addr.sin_port = htons(CDNS_PORT);

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		printf("Error creating socket\r\n");
		vTaskDelete(NULL);
		return;
	}
	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout,
			sizeof(timeout)) < 0) {
		close(sock);
		vTaskDelete(NULL);
	}

	dns_query_t query;

	for (;;) {
		if (xQueueReceive(dns_query_queue, &query, portMAX_DELAY)) {
			sendto(sock, query.buffer, query.len, 0,
					(struct sockaddr*)&dns_addr, sizeof(dns_addr));

			int len = recvfrom(sock, query.buffer, CDNS_MESSAGE_MAX_LEN, 0,
					NULL, NULL);

			if (len < 0) {
				printf("Failed to receive from DNS server socket\r\n");
			}

			if (len > 0) {
				sendto(query.client_sock, query.buffer, len, 0,
						(struct sockaddr*)&query.client_addr, query.addr_len);
			}
		}
	}
	close(sock);
	vTaskDelete(NULL);
}

static char **read_domains_from_file(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Failed to open file\n");
        return NULL;
    }

    char **domains = NULL;
    size_t domain_count = 0;
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), file)) {
        /* Skip lines that start with '#' */
        if (buffer[0] == '#' || buffer[0] == '\n') {
            continue;
        }

        /* Split the line to get the domain name (skip the IP address) */
        char *domain = strchr(buffer, ' ');
        if (domain) {
            domain = domain + 1; /* Move past the space */
            domain[strcspn(domain, "\n")] = 0; /* Remove newline character */

            /* Allocate or reallocate memory for the domain array */
            char **new_domains = (char **)heap_caps_realloc(domains, (domain_count + 1) * sizeof(char *), MALLOC_CAP_SPIRAM);
            if (!new_domains) {
                printf("Failed to allocate memory\n");
                free(domains);
                fclose(file);
                return NULL;
            }
            domains = new_domains;

            /* Allocate memory for the new domain string */
            domains[domain_count] = (char *)heap_caps_malloc(strlen(domain) + 1, MALLOC_CAP_SPIRAM);
            if (!domains[domain_count]) {
                printf("Failed to allocate memory for domain\n");

                /* Free already allocated domains and close the file */
                for (size_t i = 0; i < domain_count; i++) {
                    free(domains[i]);
                }
                free(domains);
                fclose(file);
                return NULL;
            }

            /* Copy the domain to the array */
            strcpy(domains[domain_count], domain);
            domain_count++;
        }
    }

    fclose(file);

    /* Null-terminate the array */
	char **final_domains = (char**)heap_caps_realloc(domains,
			(domain_count + 1) * sizeof(char *), MALLOC_CAP_SPIRAM);

    if (final_domains) {
        domains = final_domains;
        domains[domain_count] = NULL;
    } else {
        printf("Failed to reallocate memory for final NULL termination\n");
        free(domains);
        return NULL;
    }

    return domains;
}

/***************************** END OF FILE ************************************/
