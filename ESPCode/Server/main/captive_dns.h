/* ============================================================================
 * CAPTIVE_DNS.H - Captive Portal DNS Responder
 * ----------------------------------------------------------------------------
 * A minimal DNS server that always answers with the ESP's own address, so
 * a phone or laptop that joins the "PortaPill" hotspot gets automatically
 * prompted to open the dashboard, the same behavior as hotel or airport
 * Wi-Fi login pages.
 * ============================================================================ */

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
