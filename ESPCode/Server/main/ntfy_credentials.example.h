#ifndef NTFY_CREDENTIALS_H
#define NTFY_CREDENTIALS_H

/* Copy this file to ntfy_credentials.h (same folder) and set a real topic.
 * ntfy_credentials.h is gitignored, your real topic never gets committed.
 *
 * ntfy.sh is a free push-notification service: whatever the ESP posts to
 * https://ntfy.sh/<topic> gets pushed instantly to anyone subscribed to
 * that topic in the ntfy app (iOS/Android) or a browser at ntfy.sh/<topic>.
 * No account, no API key.
 *
 * IMPORTANT: ntfy topics are public by default, anyone who knows or
 * guesses this exact string can subscribe and read every notification,
 * including medication reminders. Use something private and hard to
 * guess (a random word string, a UUID, etc), NOT something like
 * "portapill" or a family name. */

#define NTFY_TOPIC "YOUR_PRIVATE_TOPIC_NAME"

#endif
