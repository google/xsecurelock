/*
Copyright 2014 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef AUTHPROTO_H
#define AUTHPROTO_H

// Packet format:
//
//   <ptype> <len_hi> <len_lo> <message...>
//
// where
//
//   ptype = one of the below characters.
//   len_hi = high 8 bits of message length.
//   len_lo = low 8 bits of message length.
//   message = (len_hi * 256 + len_lo) bytes that shall be shown to the user.
//
// By convention, uppercase packet types expect a reply and lowercase packet
// types are "terminal".

// PAM-to-user messages:
#define PTYPE_INFO_MESSAGE 'i'
#define PTYPE_ERROR_MESSAGE 'e'
#define PTYPE_PROMPT_LIKE_USERNAME 'U'
#define PTYPE_PROMPT_LIKE_PASSWORD 'P'
// Note: there's no specific message type for successful authentication or
// similar; the caller shall use the exit status of the helper only.

// User-to-PAM messages:
#define PTYPE_RESPONSE_LIKE_USERNAME 'u'
#define PTYPE_RESPONSE_LIKE_PASSWORD 'p'
#define PTYPE_RESPONSE_CANCELLED 'x'

/**
 * \brief Writes a packet in above form.
 *
 * \param fd The file descriptor to write to.
 * \param type The packet type from above macros.
 * \param message The message to include with the packet (NUL-terminated).
 */
void WritePacket(int fd, char type, const char *message);

/**
 * \brief Reads a packet in above form.
 *
 * \param fd The file descriptor to write to.
 * \param message A pointer to store the message (will be mlock()d).
 * \param eof_permitted If enabled, encountering EOF at the beginning will not
 *   count as an error but return 0 silently.
 * \return The packet type, or 0 if no packet has been read. Errors are logged.
 */
char ReadPacket(int fd, char **message, int eof_permitted);

#endif
