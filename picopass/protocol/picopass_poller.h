#pragma once

#include <nfc/nfc.h>
#include "picopass_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PicopassPollerEventTypeRequestKey,
    PicopassPollerEventTypeSuccess,
    PicopassPollerEventTypeFail,
} PicopassPollerEventType;

typedef struct {
    uint8_t key[PICOPASS_KEY_LEN];
    bool is_key_provided;
    bool is_elite_key;
} PicopassPollerEventDataRequestKey;

typedef union {
    PicopassPollerEventDataRequestKey req_key;
} PicopassPollerEventData;

typedef struct {
    PicopassPollerEventType type;
    PicopassPollerEventData* data;
} PicopassPollerEvent;

typedef NfcCommand (*PicopassPollerCallback)(PicopassPollerEvent event, void* context);

typedef struct PicopassPoller PicopassPoller;

PicopassPoller* picopass_poller_alloc(Nfc* nfc);

void picopass_poller_free(PicopassPoller* instance);

void picopass_poller_start(
    PicopassPoller* instance,
    PicopassPollerCallback callback,
    void* context);

void picopass_poller_stop(PicopassPoller* instance);

const PicopassData* picopass_poller_get_data(PicopassPoller* instance);

#ifdef __cplusplus
}
#endif
