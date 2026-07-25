#ifndef CAPTIVE_DNS_H
#define CAPTIVE_DNS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Starts a minimal UDP DNS responder on port 53 that answers every query
 * with the AP's own IP address, so phones/laptops get redirected to the
 * local dashboard (captive-portal behavior). */
void start_captive_dns(void);

#ifdef __cplusplus
}
#endif

#endif
