#include "picopass_listener_i.h"

#include <furi/furi.h>

typedef enum {
    PicopassListenerCommandProcessed,
    PicopassListenerCommandSilent,
    PicopassListenerCommandSendSoF,
} PicopassListenerCommand;

typedef PicopassListenerCommand (
    *PicopassListenerCommandHandler)(PicopassListener* instance, BitBuffer* buf);

typedef struct {
    uint8_t start_byte_cmd;
    size_t cmd_len_bits;
    PicopassListenerCommandHandler handler;
} PicopassListenerCmd;

PicopassListenerCommand
    picopass_listener_actall_handler(PicopassListener* instance, BitBuffer* buf) {
    UNUSED(buf);

    if(instance->state != PicopassListenerStateHalt) {
        instance->state = PicopassListenerStateActive;
    }

    return PicopassListenerCommandSendSoF;
}

PicopassListenerCommand picopass_listener_act_handler(PicopassListener* instance, BitBuffer* buf) {
    UNUSED(buf);

    PicopassListenerCommand command = PicopassListenerCommandSendSoF;

    if(instance->state != PicopassListenerStateActive) {
        command = PicopassListenerCommandSilent;
    }

    return command;
}

PicopassListenerCommand
    picopass_listener_halt_handler(PicopassListener* instance, BitBuffer* buf) {
    UNUSED(buf);

    PicopassListenerCommand command = PicopassListenerCommandSendSoF;

    // Technically we should go to StateHalt, but since we can't detect the field dropping we drop to idle instead
    instance->state = PicopassListenerStateIdle;

    return command;
}

PicopassListenerCommand
    picopass_listener_identify_handler(PicopassListener* instance, BitBuffer* buf) {
    UNUSED(buf);

    PicopassListenerCommand command = PicopassListenerCommandSilent;

    do {
        if(instance->state != PicopassListenerStateActive) break;
        picopass_listener_write_anticoll_csn(instance, instance->tx_buffer);
        PicopassError error = picopass_listener_send_frame(instance, instance->tx_buffer);
        if(error != PicopassErrorNone) {
            FURI_LOG_D(TAG, "Error sending CSN: %d", error);
            break;
        }

        command = PicopassListenerCommandProcessed;
    } while(false);

    return command;
}

PicopassListenerCommand
    picopass_listener_select_handler(PicopassListener* instance, BitBuffer* buf) {
    PicopassListenerCommand command = PicopassListenerCommandSilent;

    do {
        if((instance->state == PicopassListenerStateHalt) ||
           (instance->state == PicopassListenerStateIdle)) {
            bit_buffer_copy_bytes(
                instance->tmp_buffer,
                instance->data->AA1[PICOPASS_CSN_BLOCK_INDEX].data,
                sizeof(PicopassBlock));
        } else {
            picopass_listener_write_anticoll_csn(instance, instance->tmp_buffer);
        }
        const uint8_t* listener_uid = bit_buffer_get_data(instance->tmp_buffer);
        const uint8_t* received_data = bit_buffer_get_data(buf);

        if(memcmp(listener_uid, &received_data[1], PICOPASS_BLOCK_LEN) != 0) {
            if(instance->state == PicopassListenerStateActive) {
                instance->state = PicopassListenerStateIdle;
            } else if(instance->state == PicopassListenerStateSelected) {
                // Technically we should go to StateHalt, but since we can't detect the field dropping we drop to idle instead
                instance->state = PicopassListenerStateIdle;
            }
            break;
        }

        instance->state = PicopassListenerStateSelected;
        bit_buffer_copy_bytes(
            instance->tx_buffer,
            instance->data->AA1[PICOPASS_CSN_BLOCK_INDEX].data,
            sizeof(PicopassBlock));

        PicopassError error = picopass_listener_send_frame(instance, instance->tx_buffer);
        if(error != PicopassErrorNone) {
            FURI_LOG_D(TAG, "Error sending select response: %d", error);
            break;
        }

        command = PicopassListenerCommandProcessed;
    } while(false);

    return command;
}

PicopassListenerCommand
    picopass_listener_read_handler(PicopassListener* instance, BitBuffer* buf) {
    PicopassListenerCommand command = PicopassListenerCommandSilent;

    do {
        uint8_t block_num = bit_buffer_get_byte(buf, 1);
        if(block_num > PICOPASS_MAX_APP_LIMIT) break;

        bit_buffer_reset(instance->tx_buffer);
        if((block_num == PICOPASS_SECURE_KD_BLOCK_INDEX) ||
           (block_num == PICOPASS_SECURE_KC_BLOCK_INDEX)) {
            for(size_t i = 0; i < PICOPASS_BLOCK_LEN; i++) {
                bit_buffer_append_byte(instance->tx_buffer, 0xff);
            }
        } else {
            bit_buffer_copy_bytes(
                instance->tx_buffer, instance->data->AA1[block_num].data, sizeof(PicopassBlock));
        }
        PicopassError error = picopass_listener_send_frame(instance, instance->tx_buffer);
        if(error != PicopassErrorNone) {
            FURI_LOG_D(TAG, "Failed to tx read block response");
            break;
        }

        command = PicopassListenerCommandProcessed;
    } while(false);

    return command;
}

static const PicopassListenerCmd picopass_listener_cmd_handlers[] = {
    {
        .start_byte_cmd = PICOPASS_CMD_ACTALL,
        .cmd_len_bits = 8,
        .handler = picopass_listener_actall_handler,
    },
    {
        .start_byte_cmd = PICOPASS_CMD_ACT,
        .cmd_len_bits = 8,
        .handler = picopass_listener_act_handler,
    },
    {
        .start_byte_cmd = PICOPASS_CMD_HALT,
        .cmd_len_bits = 8,
        .handler = picopass_listener_halt_handler,
    },
    {
        .start_byte_cmd = PICOPASS_CMD_READ_OR_IDENTIFY,
        .cmd_len_bits = 8,
        .handler = picopass_listener_identify_handler,
    },
    {
        .start_byte_cmd = PICOPASS_CMD_SELECT,
        .cmd_len_bits = 8 * 9,
        .handler = picopass_listener_select_handler,
    },
    {
        .start_byte_cmd = PICOPASS_CMD_READ_OR_IDENTIFY,
        .cmd_len_bits = 8 * 4,
        .handler = picopass_listener_read_handler,
    }

};

PicopassListener* picopass_listener_alloc(Nfc* nfc, const PicopassData* data) {
    furi_assert(nfc);
    furi_assert(data);

    PicopassListener* instance = malloc(sizeof(PicopassListener));
    instance->nfc = nfc;
    instance->data = picopass_protocol_alloc();
    picopass_protocol_copy(instance->data, data);

    instance->tx_buffer = bit_buffer_alloc(PICOPASS_LISTENER_BUFFER_SIZE_MAX);
    instance->tmp_buffer = bit_buffer_alloc(PICOPASS_LISTENER_BUFFER_SIZE_MAX);

    nfc_set_fdt_listen_fc(instance->nfc, PICOPASS_FDT_LISTEN_FC);
    nfc_config(instance->nfc, NfcModeListener, NfcTechIso15693);

    return instance;
}

void picopass_listener_free(PicopassListener* instance) {
    furi_assert(instance);

    bit_buffer_free(instance->tx_buffer);
    bit_buffer_free(instance->tmp_buffer);
    picopass_protocol_free(instance->data);
    free(instance);
}

NfcCommand picopass_listener_start_callback(NfcEvent event, void* context) {
    furi_assert(context);

    NfcCommand command = NfcCommandContinue;
    PicopassListener* instance = context;
    BitBuffer* rx_buf = event.data.buffer;

    PicopassListenerCommand picopass_cmd = PicopassListenerCommandSilent;
    if(event.type == NfcEventTypeRxEnd) {
        for(size_t i = 0; i < COUNT_OF(picopass_listener_cmd_handlers); i++) {
            if(bit_buffer_get_size(rx_buf) != picopass_listener_cmd_handlers[i].cmd_len_bits) {
                continue;
            }
            if(bit_buffer_get_byte(rx_buf, 0) !=
               picopass_listener_cmd_handlers[i].start_byte_cmd) {
                continue;
            }
            picopass_cmd = picopass_listener_cmd_handlers[i].handler(instance, rx_buf);
            break;
        }
        if(picopass_cmd == PicopassListenerCommandSendSoF) {
            nfc_iso15693_listener_tx_sof(instance->nfc);
        }
    }

    return command;
}

void picopass_listener_start(
    PicopassListener* instance,
    PicopassListenerCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(callback);

    instance->callback = callback;
    instance->context = context;

    nfc_start(instance->nfc, picopass_listener_start_callback, instance);
}

void picopass_listener_stop(PicopassListener* instance) {
    furi_assert(instance);

    nfc_stop(instance->nfc);
}

const PicopassData* picopass_listener_get_data(PicopassListener* instance) {
    furi_assert(instance);

    return instance->data;
}
