/**
 * @file
 * @brief Writing a new panel application. See fw_install.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "fw_install.h"

#include <string.h>

#include "flash_gate.h"

fw_gate_t fw_gate(bool armed, bool supply, bool run_open, bool drawing,
                  bool coproc)
{
    if (!flash_gate_open(armed, supply)) {
        return armed ? FW_GATE_ARMED : FW_GATE_SUPPLY;
    }
    if (run_open) {
        return FW_GATE_RUN;
    }
    if (drawing) {
        return FW_GATE_DRAWING;
    }
    return coproc ? FW_GATE_COPROC : FW_GATE_OPEN;
}

bool fw_coproc_may_start(bool radio_down, bool installing)
{
    return radio_down && !installing;
}

const fw_coproc_image_t *fw_coproc_image(const fw_coproc_image_t *tab,
                                         size_t n, uint16_t board)
{
    if (tab == NULL || board == 0u) {
        return NULL;
    }
    for (size_t i = 0; i < n; ++i) {
        if (tab[i].board == board && tab[i].image != NULL
            && tab[i].len != 0u) {
            return &tab[i];
        }
    }
    return NULL;
}

fw_err_t fw_install_begin(fw_install_t *in, const fw_flash_t *f,
                          const uint8_t *img, uint32_t bytes, uint32_t slot,
                          const fw_image_info_t *running)
{
    memset(in, 0, sizeof(*in));
    if (f == NULL || f->begin == NULL || f->write == NULL
        || f->finish == NULL || running == NULL) {
        return FW_ERR_FLASH;
    }
    if (img == NULL) {
        return FW_ERR_MEMORY;
    }
    const fw_err_t e = fw_image_check(img, bytes, slot, &in->info);
    if (e != FW_OK) {
        return e;
    }
    in->offer = fw_offer(running, &in->info);
    if (!fw_offer_installable(in->offer)) {
        return FW_ERR_RUNNING;
    }
    in->f     = f;
    in->img   = img;
    in->bytes = bytes;
    in->busy  = true;
    return FW_OK;
}

/**
 * @brief Ends an install that went wrong: the slot's write abandoned and
 *        the error FW_ERR_FLASH.
 *
 * @param in  The install.
 * @return    FW_INSTALL_FAILED.
 */
static fw_install_status_t fail(fw_install_t *in)
{
    if (in->opened && in->f->abort != NULL) {
        in->f->abort(in->f->ctx);
    }
    in->busy = false;
    in->err  = FW_ERR_FLASH;
    return FW_INSTALL_FAILED;
}

fw_install_status_t fw_install_step(fw_install_t *in, bool quiet)
{
    if (!in->busy) {
        return FW_INSTALL_IDLE;
    }
    if (!quiet) {
        return FW_INSTALL_WAITING;
    }
    in->touched = true;
    if (!in->opened) {
        if (!in->f->begin(in->f->ctx, in->bytes)) {
            return fail(in);
        }
        in->opened = true;
        return FW_INSTALL_RUNNING;
    }
    if (in->done < in->bytes) {
        uint32_t n = in->bytes - in->done;
        if (n > FW_WRITE_CHUNK) {
            n = FW_WRITE_CHUNK;
        }
        if (!in->f->write(in->f->ctx, in->done, in->img + in->done, n)) {
            return fail(in);
        }
        in->done += n;
        return FW_INSTALL_RUNNING;
    }
    if (!in->f->finish(in->f->ctx)) {
        return fail(in);
    }
    in->busy = false;
    in->err  = FW_OK;
    return FW_INSTALL_DONE;
}

bool fw_install_finishing(const fw_install_t *in)
{
    return in->busy && in->opened && in->done == in->bytes;
}
