#include "captive_dns.h"

#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "time_server";

#define DNS_PORT 53
#define AP_IP_OCTET_1 192
#define AP_IP_OCTET_2 168
#define AP_IP_OCTET_3 4
#define AP_IP_OCTET_4 1

static void dns_server_task(void *arg)
{
	int sock;
	struct sockaddr_in listen_addr;

	(void)arg;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sock < 0) {
		ESP_LOGE(TAG, "DNS socket create failed");
		vTaskDelete(NULL);
		return;
	}

	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_port = htons(DNS_PORT);
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
		ESP_LOGE(TAG, "DNS socket bind failed");
		close(sock);
		vTaskDelete(NULL);
		return;
	}

	ESP_LOGI(TAG, "Captive DNS started on UDP 53");

	while (1) {
		uint8_t request[512];
		uint8_t response[512];
		struct sockaddr_in source_addr;
		socklen_t source_addr_len = sizeof(source_addr);
		ssize_t req_len = recvfrom(sock,
							   request,
							   sizeof(request),
							   0,
							   (struct sockaddr *)&source_addr,
							   &source_addr_len);

		if (req_len < 12) {
			continue;
		}

		if (request[2] & 0x80) {
			continue;
		}

		int index = 12;
		while (index < req_len && request[index] != 0) {
			index += request[index] + 1;
		}

		if ((index + 5) >= req_len) {
			continue;
		}

		int question_len = (index + 1) - 12 + 4;
		if ((12 + question_len) > req_len) {
			continue;
		}

		memcpy(response, request, 12 + question_len);
		response[2] = 0x81;
		response[3] = 0x80;
		response[6] = 0x00;
		response[7] = 0x01;
		response[8] = 0x00;
		response[9] = 0x00;
		response[10] = 0x00;
		response[11] = 0x00;

		int resp_len = 12 + question_len;
		response[resp_len++] = 0xC0;
		response[resp_len++] = 0x0C;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x01;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x01;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x3C;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x04;
		response[resp_len++] = AP_IP_OCTET_1;
		response[resp_len++] = AP_IP_OCTET_2;
		response[resp_len++] = AP_IP_OCTET_3;
		response[resp_len++] = AP_IP_OCTET_4;

		sendto(sock,
			   response,
			   resp_len,
			   0,
			   (struct sockaddr *)&source_addr,
			   source_addr_len);
	}
}

void start_captive_dns(void)
{
	xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 4, NULL);
}
