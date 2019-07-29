/*
Copyright 2018 Google Inc. All rights reserved.

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

#include <locale.h>             // for NULL, setlocale, LC_CTYPE
#include <security/pam_appl.h>  // for pam_end, pam_start, pam_acct_mgmt
#include <stdlib.h>             // for free, calloc, exit, getenv
#include <string.h>             // for strchr

#include "../env_info.h"      // for GetHostName, GetUserName
#include "../env_settings.h"  // for GetStringSetting
#include "../logging.h"       // for Log
#include "../util.h"          // for explicit_bzero
#include "authproto.h"        // for WritePacket, ReadPacket, PTYPE_ERRO...

// IWYU pragma: no_include <security/_pam_types.h>

//! Set if a conversation error has happened during the last PAM call.
static int conv_error = 0;

/*! \brief Perform a single PAM conversation step.
 *
 * \param msg The PAM message.
 * \param resp The PAM response to store the output in.
 * \return The PAM status (PAM_SUCCESS in case of success, or anything else in
 *   case of error).
 */
int ConverseOne(const struct pam_message *msg, struct pam_response *resp) {
  resp->resp_retcode = 0;  // Unused but should be set to zero.
  switch (msg->msg_style) {
    case PAM_PROMPT_ECHO_OFF: {
      WritePacket(1, PTYPE_PROMPT_LIKE_PASSWORD, msg->msg);
      char type = ReadPacket(0, &resp->resp, 0);
      return type == PTYPE_RESPONSE_LIKE_PASSWORD ? PAM_SUCCESS : PAM_CONV_ERR;
    }
    case PAM_PROMPT_ECHO_ON: {
      WritePacket(1, PTYPE_PROMPT_LIKE_USERNAME, msg->msg);
      char type = ReadPacket(0, &resp->resp, 0);
      return type == PTYPE_RESPONSE_LIKE_USERNAME ? PAM_SUCCESS : PAM_CONV_ERR;
    }
    case PAM_ERROR_MSG:
      WritePacket(1, PTYPE_ERROR_MESSAGE, msg->msg);
      return PAM_SUCCESS;
    case PAM_TEXT_INFO:
      WritePacket(1, PTYPE_INFO_MESSAGE, msg->msg);
      return PAM_SUCCESS;
    default:
      return PAM_CONV_ERR;
  }
}

/*! \brief Perform a PAM conversation.
 *
 * \param num_msg The number of conversation steps to execute.
 * \param msg The PAM messages.
 * \param resp The PAM responses to store the output in.
 * \param appdata_ptr Unused.
 * \return The PAM status (PAM_SUCCESS in case of success, or anything else in
 *   case of error).
 */
int Converse(int num_msg, const struct pam_message **msg,
             struct pam_response **resp, void *appdata_ptr) {
  (void)appdata_ptr;

  if (conv_error) {
    Log("Converse() got called again with %d messages (first: %s) after "
        "having failed before - this is very likely a bug in the PAM "
        "module having made the call. Bailing out",
        num_msg, num_msg <= 0 ? "(none)" : msg[0]->msg);
    exit(1);
  }

  *resp = calloc(num_msg, sizeof(struct pam_response));

  int i;
  for (i = 0; i < num_msg; ++i) {
    int status = ConverseOne(msg[i], &(*resp)[i]);
    if (status != PAM_SUCCESS) {
      for (i = 0; i < num_msg; ++i) {
        if ((*resp)[i].resp != NULL) {
          explicit_bzero((*resp)[i].resp, strlen((*resp)[i].resp));
        }
        free((*resp)[i].resp);
      }
      free(*resp);
      *resp = NULL;
      conv_error = 1;
      return status;
    }
  }

  return PAM_SUCCESS;
}

/*! \brief Perform a single PAM operation with retrying logic.
 */
int CallPAMWithRetries(int (*pam_call)(pam_handle_t *, int), pam_handle_t *pam,
                       int flags) {
  int attempt = 0;
  for (;;) {
    conv_error = 0;

    int status = pam_call(pam, flags);
    if (conv_error) {
      return status;
    }
    switch (status) {
      // Never retry these:
      case PAM_ABORT:             // This is fine.
      case PAM_MAXTRIES:          // D'oh.
      case PAM_NEW_AUTHTOK_REQD:  // hunter2 no longer good enough.
      case PAM_SUCCESS:           // Duh.
        return status;
      default:
        // Let's try again then.
        ++attempt;
        if (attempt >= 3) {
          return status;
        }
        break;
    }
  }
}

/*! \brief Perform PAM authentication.
 *
 * \param conv The PAM conversation handler.
 * \param pam The PAM handle will be returned here.
 * \return The PAM status (PAM_SUCCESS after successful authentication, or
 *   anything else in case of error).
 */
int Authenticate(struct pam_conv *conv, pam_handle_t **pam) {
  const char *service_name =
      GetStringSetting("XSECURELOCK_PAM_SERVICE", PAM_SERVICE_NAME);
  if (strchr(service_name, '/')) {
    // As this binary might be running with setuid privileges, we should better
    // refuse potentially dangerous parameters. This works around PAM
    // implementations being potentially vulnerable to someone passing
    // "../shadow" as service name and then getting an error message containing
    // the encrypted root password. I am not aware of any implementations that
    // do fall for that - nevertheless let's better be safe.
    Log("PAM service name (%s) contains a slash - refusing", service_name);
    return 1;
  }
  char username[256];
  if (!GetUserName(username, sizeof(username))) {
    return 1;
  }
  int status = pam_start(service_name, username, conv, pam);
  if (status != PAM_SUCCESS) {
    Log("pam_start: %d",
        status);  // Or can one call pam_strerror on a NULL handle?
    return status;
  }

  if (!GetIntSetting("XSECURELOCK_NO_PAM_RHOST", 0)) {
    // This is a local login - by convention PAM_RHOST should be "localhost":
    // http://www.linux-pam.org/Linux-PAM-html/adg-security-user-identity.html
    status = pam_set_item(*pam, PAM_RHOST, "localhost");
    if (status != PAM_SUCCESS) {
      Log("pam_set_item: %s", pam_strerror(*pam, status));
      return status;
    }
  }

  status = pam_set_item(*pam, PAM_RUSER, username);
  if (status != PAM_SUCCESS) {
    Log("pam_set_item: %s", pam_strerror(*pam, status));
    return status;
  }

  const char *display = getenv("DISPLAY");
  status = pam_set_item(*pam, PAM_TTY, display);
  if (status != PAM_SUCCESS) {
    Log("pam_set_item: %s", pam_strerror(*pam, status));
    return status;
  }

  status = CallPAMWithRetries(pam_authenticate, *pam, 0);
  if (status != PAM_SUCCESS) {
    if (!conv_error) {
      Log("pam_authenticate: %s", pam_strerror(*pam, status));
    }
    return status;
  }

  int status2 = CallPAMWithRetries(pam_acct_mgmt, *pam, 0);
  if (status2 == PAM_NEW_AUTHTOK_REQD) {
    status2 =
        CallPAMWithRetries(pam_chauthtok, *pam, PAM_CHANGE_EXPIRED_AUTHTOK);
#ifdef PAM_CHECK_ACCOUNT_TYPE
    if (status2 != PAM_SUCCESS) {
      if (!conv_error) {
        Log("pam_chauthtok: %s", pam_strerror(*pam, status2));
      }
      return status2;
    }
#else
    (void)status2;
#endif
  }

#ifdef PAM_CHECK_ACCOUNT_TYPE
  if (status2 != PAM_SUCCESS) {
    // If this one is true, it must be coming from pam_acct_mgmt, as
    // pam_chauthtok's result already has been checked against PAM_SUCCESS.
    if (!conv_error) {
      Log("pam_acct_mgmt: %s", pam_strerror(*pam, status2));
    }
    return status2;
  }
#endif

  return status;
}

/*! \brief The main program.
 *
 * Usage: ./authproto_pam; status=$?
 *
 * \return 0 if authentication successful, anything else otherwise.
 */
int main() {
  setlocale(LC_CTYPE, "");

  struct pam_conv conv;
  conv.conv = Converse;
  conv.appdata_ptr = NULL;

  pam_handle_t *pam = NULL;
  int status = Authenticate(&conv, &pam);
  int status2 = pam == NULL ? PAM_SUCCESS : pam_end(pam, status);

  if (status != PAM_SUCCESS) {
    // The caller already displayed an error.
    return 1;
  }
  if (status2 != PAM_SUCCESS) {
    Log("pam_end: %s", pam_strerror(pam, status2));
    return 1;
  }

  return 0;
}
