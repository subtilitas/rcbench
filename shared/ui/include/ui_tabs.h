/**
 * @file
 * @brief The pane selector: one bench, several views of it.
 *
 * Readings and controls stay in place while the pane switches, so a change of
 * view never moves a control that is about to be pressed.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "touch_types.h"
#include "ui_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The most tabs one row holds. */
#define UI_TABS_MAX 6

/** @brief A row of tabs: their labels, where they are, which is selected
 *         and the press under way. */
typedef struct {
    const char *label[UI_TABS_MAX]; /**< as given, for a -1 text      */
    int16_t     text[UI_TABS_MAX];  /**< a ui_text_id_t, or -1 for label */
    gfx_rect_t  rect[UI_TABS_MAX];  /**< each tab, screen coordinates  */
    int         count;              /**< tabs in the row               */
    int         selected;           /**< the tab shown                 */
    int         pressed;    /**< index, or -1 */
    uint8_t     press_id;   /**< the touch id of that press */
} ui_tabs_t;

/**
 * @brief Tabs whose labels are drawn as given, in every language: @p row
 *        split into @p count tabs 4 px apart, the first selected.
 * @param t      The tabs; NULL does nothing.
 * @param labels The labels; NULL does nothing.
 * @param count  The tabs, at most UI_TABS_MAX.
 * @param row    The row.
 */
void ui_tabs_init(ui_tabs_t *t, const char *const *labels, int count,
                  gfx_rect_t row);
/**
 * @brief Tabs whose labels are looked up at every draw, so they follow the
 *        language.
 * @param t     The tabs; NULL does nothing.
 * @param ids   The labels' texts; NULL does nothing.
 * @param count The tabs, at most UI_TABS_MAX.
 * @param row   The row.
 */
void ui_tabs_init_text(ui_tabs_t *t, const ui_text_id_t *ids, int count,
                       gfx_rect_t row);
/**
 * @brief One touch event: a tab is selected on the release, as a tap, of
 *        the press that began on it.
 * @param t   The tabs.
 * @param evt The event.
 * @return True when the selection changed.
 */
bool ui_tabs_event(ui_tabs_t *t, const touch_event_t *evt);
/**
 * @brief Abandon a press in progress.
 *
 * A screen calls it when told its record of the glass is stale: a latched
 * press owns a track id the controller reuses, so a later contact that
 * began elsewhere and lifts over the tab would otherwise be taken for the
 * missing release and switch the pane.
 * @param t The tabs; NULL does nothing.
 */
void ui_tabs_cancel(ui_tabs_t *t);
/**
 * @brief The tabs' labels in the face of ui_tabs_font(), centred in each
 *        tab; the selected tab raised in a sunk trough 3 px around the row.
 * @param t The tabs.
 * @param c Canvas.
 */
void ui_tabs_render(const ui_tabs_t *t, gfx_canvas_t *c);

/**
 * @brief A row UI_TABS_LABEL_H (32 px) tall or taller whose every label
 *        fits its tab in the 10 x 20 face, UI_TABS_PAD px clear each side,
 *        draws its labels in that face; another row in UI_FONT_LABEL
 *        (8 x 16).  The test runs in the language shown, so one row draws
 *        in one face.
 */
#define UI_TABS_LABEL_H 32
/** @brief The room a tab keeps left and right of a 10 x 20 label: 6 px. */
#define UI_TABS_PAD     6

/**
 * @brief The face the tabs' labels are drawn in now.
 * @param t The tabs.
 * @return The 10 x 20 face or UI_FONT_LABEL, as UI_TABS_LABEL_H says;
 *         UI_FONT_LABEL for NULL or no tab.
 */
const gfx_font_t *ui_tabs_font(const ui_tabs_t *t);
/**
 * @brief The tabs as ui_tabs_render() draws them, or with @p locked as a
 *        control that takes no press: the trough outlined, the selected
 *        tab's label dim rather than in the accent, a press under way not
 *        drawn.  The screen feeds a locked control no event.
 * @param t      The tabs.
 * @param c      Canvas.
 * @param locked Draw them as taking no press.
 */
void ui_tabs_render_locked(const ui_tabs_t *t, gfx_canvas_t *c, bool locked);

#ifdef __cplusplus
}
#endif
