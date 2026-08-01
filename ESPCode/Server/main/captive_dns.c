/* ============================================================================
 * CAPTIVE_DNS.C - Minimal DNS Responder for Captive Portal Behavior
 * ----------------------------------------------------------------------------
 * A deliberately tiny hand-rolled DNS server, not a general-purpose one.
 * It answers every single query, regardless of what hostname was actually
 * asked, with the ESP's own AP address. That's exactly what's needed for
 * a captive portal: it makes the OS's own connectivity check on a newly
 * joined device resolve to this device, triggering the "sign in to this
 * network" prompt automatically.
 * ============================================================================ */

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

/* ----------------------------------------------------------------------------
 * dns_server_task()
 * ----------------------------------------------------------------------------
 * Listens for DNS queries on UDP port 53 and answers each one with a single
 * A record pointing at the AP's address, built by hand rather than through
 * a DNS library since the responses needed here are so simple. Runs
 * forever once started.
 *
 * Per received packet:
 *   - The first 12 bytes are the DNS header. Byte 2's top bit tells us
 *     whether this is a query (0) or a response (1), only queries get
 *     answered.
 *   - Right after the header is the "question" section: the hostname
 *     being asked about, encoded as length-prefixed labels (e.g.
 *     3"www"6"google"3"com"0), followed by 4 bytes for the query type and
 *     class. The loop below just walks past the labels to find where the
 *     question ends, the actual hostname text is never inspected, since
 *     every query gets the same answer regardless.
 *   - The response is built by copying the header and question back
 *     verbatim (with the header's flags changed to mark it as a reply),
 *     then appending one answer record: "same name as the question, type
 *     A, 60 second TTL, followed by the 4 address bytes".
 * ---------------------------------------------------------------------------- */
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
			continue; /* shorter than a DNS header, not a real query */
		}

		if (request[2] & 0x80) {
			continue; /* this is itself a response, not a query, ignore it */
		}

		/* Walk the question's label sequence to find its end (the 0x00
		 * terminator byte), without needing to decode the hostname text. */
		int index = 12;
		while (index < req_len && request[index] != 0) {
			index += request[index] + 1;
		}

		if ((index + 5) >= req_len) {
			continue; /* truncated, missing the type/class fields */
		}

		int question_len = (index + 1) - 12 + 4;
		if ((12 + question_len) > req_len) {
			continue;
		}

		/* Copy the header + question back verbatim, then flip the flag
		 * bytes to mark this as a reply: response bit set, "recursion
		 * available", 1 answer record, 0 authority/additional records. */
		memcpy(response, request, 12 + question_len);
		response[2] = 0x81;
		response[3] = 0x80;
		response[6] = 0x00;
		response[7] = 0x01;
		response[8] = 0x00;
		response[9] = 0x00;
		response[10] = 0x00;
		response[11] = 0x00;

		/* Append one answer record: a pointer back to the question's name
		 * (0xC0 0x0C = compression pointer to offset 12), type A, class
		 * IN, 60s TTL, 4-byte address length, then the address itself. */
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

/* Starts dns_server_task() on its own FreeRTOS task. Call once at boot. */
void start_captive_dns(void)
{
	xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 4, NULL);
}
