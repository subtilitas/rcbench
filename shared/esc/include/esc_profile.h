/**
 * @file
 * @brief ESC (electronic speed controller) programming profiles: how to enter
 *        an ESC's throttle-stick programming menu, how it sounds a number, and
 *        what the menu holds.
 *
 * One profile describes one family: a set of models that share one entry
 * gesture and one menu.  The profiles of record are the JSON files in
 * shared/esc/profiles/, described in docs/EscProfiles.md.
 * tools/gen_esc_profiles.py compiles them into esc_profiles_gen.c, so every
 * panel carries them without a card.  A file in BENCH/ESC/CONF on the SD
 * card (CARD_ESC_CONF, card_tree.h) is
 * parsed by esc_profile_parse() and replaces the built-in profile of the
 * same id, or adds one when the id is new.
 *
 * Boundaries of this module:
 *
 *   - No file access.  The caller reads the card and hands over bytes, so
 *     the host suite feeds the parser truncated and hostile input with no
 *     card at all.
 *   - No beep timing.  The manuals give none; every time field is 0 until a
 *     bench recording supplies one, and 0 means "not known", never "none".
 *   - Text the bench does not act on (sources, notes, unknowns, the scheme
 *     description) stays in the JSON.  The firmware carries names, numbers
 *     and the operator steps a person has to perform.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_ESC_PROFILE_H
#define RCBENCH_ESC_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The schema version of the JSON this reads.
 */
#define ESC_PROFILE_SCHEMA 1u

/**
 * @brief The largest file the parser takes, in bytes.
 *
 * The largest profile of record is about 21 KiB.
 */
#define ESC_PROFILE_MAX_BYTES 65536u

/**
 * @brief How many profiles the card may add or replace.
 */
#define ESC_PROFILE_MAX_OVERRIDES 32u

/**
 * @brief The most models one maker may list across its profiles, built in and
 *        from the card: what the ESC STICK list holds for one maker.
 *
 * A card profile that would take its maker past it is refused at load.
 */
#define ESC_MAKER_MODELS_MAX 512u

/**
 * @brief How the menu as a whole is driven.
 */
typedef enum {
    ESC_SCHEME_COUNT = 0,       /**< N beeps say item or value N            */
    ESC_SCHEME_SHORT_LONG,      /**< long beeps count several short ones    */
    ESC_SCHEME_MELODY_GROUPS,   /**< a melody per group, repeated           */
    ESC_SCHEME_YES_NO,          /**< each value asked, full = yes, low = no */
    ESC_SCHEME_STICK_POSITION,  /**< the stick's position is the value      */
    ESC_SCHEME_OTHER,
} esc_scheme_t;

/**
 * @brief How one number is sounded.
 */
typedef enum {
    ESC_ENC_COUNT = 0,
    ESC_ENC_SHORT_LONG,
    ESC_ENC_MELODY,
    ESC_ENC_YES_NO,
} esc_encoding_t;

/**
 * @brief Who can run the sequence.
 */
typedef enum {
    ESC_AUTO_FULL = 0,          /**< throttle signal and power alone        */
    ESC_AUTO_ASSISTED,          /**< a person sets a jumper, presses a
                                     button or reads an LED                 */
    ESC_AUTO_NONE,              /**< the manual gives no usable procedure   */
} esc_auto_t;

/** @brief Throttle stick position that a step uses. */
typedef enum {
    ESC_THR_MIN = 0,
    ESC_THR_MID,
    ESC_THR_MAX,
    ESC_THR_NONE,               /**< no throttle move does this             */
} esc_throttle_t;

/**
 * @brief What the ESC announces before a selection.
 */
typedef enum {
    ESC_ANNOUNCE_ITEM = 0,
    ESC_ANNOUNCE_VALUE,
    ESC_ANNOUNCE_ITEM_THEN_VALUE,
} esc_announce_t;

/** @brief One model the profile lists, with its battery and input limits. */
typedef struct {
    const char *name;
    uint8_t     cells_min;      /**< 0: not known                           */
    uint8_t     cells_max;      /**< 0: not known                           */
    bool        nimh;           /**< cells count NiMH, not LiPo             */
    uint32_t    v_max_mv;       /**< 0: not known                           */
    uint16_t    current_a;      /**< continuous; 0: not known               */
    uint32_t    v_min_mv;       /**< lowest input; 0: not known             */
} esc_model_t;

/**
 * @brief The most stick moves a value asks for after its selection.
 */
#define ESC_AFTER_MAX 4u

/** @brief One value an item can take, with the moves that program it. */
typedef struct {
    const char *name;
    uint8_t     number;         /**< as the ESC sounds it                   */
    bool        is_default;
    /**
     * @brief The stick position the manual programs this value from, where it
     *        differs by value; ESC_THR_NONE: the profile's entry position.
     */
    esc_throttle_t entry_throttle;
    /**
     * @brief Power-on to the menu when this value is programmed, where the
     *        manual gives one other than the entry's; 0: the entry's.
     */
    uint32_t       entry_hold_ms;
    /**
     * @brief The moves this value asks for after its select move, in order,
     *        each once the ESC has answered the one before (Kontronik's car
     *        modes: to the brake after full throttle); 0 entries: none.
     */
    uint8_t        after_count;
    esc_throttle_t after[ESC_AFTER_MAX];
} esc_value_t;

/** @brief One menu item: its name, sounded number and values. */
typedef struct {
    const char        *name;
    const char        *key;     /**< shared across brands: "brake", ...     */
    uint8_t            number;  /**< as the ESC sounds it                   */
    uint8_t            value_count;
    const esc_value_t *values;
    /**
     * @brief Model names this item exists on; 0 entries: every model.
     */
    uint8_t            applies_count;
    const char *const *applies_to;
    /**
     * @brief A condition in words, e.g. "model type heli"; "" when none.
     *
     * Two items may share a number when their conditions differ.
     */
    const char        *applies_when;
} esc_item_t;

/**
 * @brief When an operator's step at the ESC is due, in the order of a run.
 */
typedef enum {
    ESC_MANUAL_BEFORE_POWER = 0,  /**< before the supply comes on          */
    ESC_MANUAL_AT_POWER_UP,       /**< begun before, held while it comes on */
    ESC_MANUAL_BEFORE_MENU,       /**< powered, after the entry, before the
                                       menu sounds                          */
    ESC_MANUAL_DURING_MENU,       /**< while the menu sounds                */
    ESC_MANUAL_BEFORE_POWER_OFF,  /**< powered, after the value is stored,
                                       before the supply goes off: hands
                                       off, the operator watches the ESC  */
    ESC_MANUAL_AFTER_PROGRAMMING, /**< once the run is over                 */
} esc_manual_when_t;

/**
 * @brief The longest manual step's text, in bytes: two lines of a pop-up.
 */
#define ESC_MANUAL_ACTION_MAX 120u
/**
 * @brief The most manual steps one profile holds.
 */
#define ESC_MANUAL_MAX 4u

/**
 * @brief A step a person does at the ESC besides the throttle and the power:
 *        fit or pull a jumper, press a button.
 *
 * The action is the profile's English, as the item and value names are, with
 * its German beside it.
 */
typedef struct {
    esc_manual_when_t when;
    const char       *action;
    uint32_t          hold_ms;  /**< at_power_up: held this long after the
                                     supply comes on; 0: not stated       */
    /**
     * @brief The same step in German, shown when the interface is; "" or NULL
     *        where the profile gives none, and the English is shown.
     */
    const char       *action_de;
    /**
     * @brief The step starts the menu: the ESC answers it and sounds its series
     *        at once (a Kontronik jumper pulled, its button pressed).
     *
     * Only a before_menu step, the last of them, at most one a profile.
     */
    bool              starts_menu;
    /**
     * @brief The ESC locks itself when its supply goes off before the step is
     *        done (a Kontronik KOBY, JIVE Pro, KOLIBRI, KONTROL-X or KOSMIK).
     *
     * Only a before_power_off step.
     */
    bool              locks;
} esc_manual_t;

/** @brief One ESC programming profile: its family, menu and steps. */
typedef struct {
    const char        *id;      /**< the file name without .json            */
    const char        *brand;
    const char        *family;
    esc_scheme_t       scheme;
    esc_encoding_t     encoding;
    esc_announce_t     announce;
    esc_auto_t         automatable;
    const char        *automatable_note;   /**< "" when full             */
    esc_throttle_t     entry_throttle;
    bool               entry_after_power;  /**< else before power is on  */
    uint32_t           entry_hold_ms;      /**< 0: not known             */
    esc_throttle_t     select_throttle;
    uint32_t           select_within_ms;   /**< 0: not known             */
    /**
     * @brief In a two-stage menu, the move that stores the value sounded, after
     *        select_throttle picked the item; ESC_THR_NONE in a one-stage menu,
     *        where the select move stores it.
     */
    esc_throttle_t     value_select_throttle;
    uint32_t           value_select_within_ms; /**< 0: not known         */
    esc_throttle_t     skip_throttle;
    /**
     * @brief Where the stick rests while the menu sounds; ESC_THR_NONE where
     *        the entry left it.
     */
    esc_throttle_t     listen_throttle;
    /**
     * @brief The move that stores a selection once the ESC has answered it;
     *        ESC_THR_NONE where the selection itself stores.
     */
    esc_throttle_t     store_throttle;
    uint8_t            long_equals_short;  /**< 0 unless short_long      */
    uint32_t           beep_ms;            /**< 0: not known             */
    uint32_t           gap_ms;             /**< 0: not known             */
    uint32_t           group_gap_ms;       /**< 0: not known             */
    int16_t            repeat;             /**< -1 not known, 0 until a
                                                choice is made           */
    bool               one_change_per_entry;
    bool               verified;           /**< a bench run confirmed it */
    uint8_t            step_count;
    const char *const *steps;              /**< operator actions, in order */
    uint16_t           model_count;
    const esc_model_t *models;
    uint8_t            item_count;
    const esc_item_t  *items;
    /**
     * @brief The operator's steps at the ESC, in the order they are due; 0
     *        entries on every profile that is not assisted.
     */
    uint8_t            manual_count;
    const esc_manual_t *manual;
} esc_profile_t;

/* ------------------------------------------------------------ built in */

/**
 * @brief The profiles compiled in, sorted by id.
 */
extern const esc_profile_t esc_profiles_builtin[];
extern const size_t        esc_profiles_builtin_count;

/* ------------------------------------------------------------ the card */

/**
 * @brief Parse one profile file.
 *
 * Everything the profile points at -- strings, models, items, values -- is
 * placed in one block from malloc(); *block receives it and free(*block)
 * releases the lot.  On failure nothing is allocated and @p err (when not
 * NULL) holds a line for the operator, e.g. "items[2].values: empty".
 *
 * @param json      The file's bytes.
 * @param len       Length of @p json in bytes.
 * @param out       Receives the parsed profile.
 * @param block     Receives the malloc() block that holds the profile.
 * @param err       Message for the operator, or NULL.
 * @param err_size  Size of @p err in bytes.
 * @return true when @p out is a usable profile
 */
bool esc_profile_parse(const char *json, size_t len, esc_profile_t *out,
                       void **block, char *err, size_t err_size);

/**
 * @brief Whether @p file_name is the name the profile @p id belongs in: the id
 *        with ".json" after it, letters compared without regard to case, since
 *        a card written on another computer may change them.
 *
 * A card file whose name and id differ is refused, as tools/gen_esc_profiles.py
 * refuses one in the repository: a renamed copy would otherwise replace a
 * profile its name does not mention.
 *
 * @param file_name  The file name as found on the card.
 * @param id         The profile id to compare against.
 * @return true when the names match
 */
bool esc_profile_file_is(const char *file_name, const char *id);

/* -------------------------------------------------------- the registry */

/**
 * @brief Add a parsed profile from the card.
 *
 * Ownership of @p block passes to the registry.  A second file with the same id
 * replaces the first.
 *
 * @param p      The parsed profile.
 * @param block  The malloc() block that holds @p p.
 * @return false when the registry is full; @p block is freed then too
 */
bool esc_profiles_override(const esc_profile_t *p, void *block);

/**
 * @brief esc_profiles_override(), with why it refused in @p why (a static
 *        string; may be NULL): "no profile", "more than 32 on the card", or
 *        "its maker would list more than 512 models" -- the maker's models
 *        across every profile, built in and from the card, with this one in
 *        place of any it replaces, makers compared with the letters A to Z
 *        folded.
 *
 * @param p      The parsed profile.
 * @param block  The malloc() block that holds @p p; freed when the profile is
 *               refused.
 * @param why    Receives a static reason string when refused, or NULL.
 * @return true when @p p is registered, false when it is refused.
 */
bool esc_profiles_override_why(const esc_profile_t *p, void *block,
                               const char **why);

/**
 * @brief Whether @p a and @p b name one maker: the letters A to Z folded, every
 *        other byte itself, as the list groups them.
 *
 * @param a  A maker name, or NULL.
 * @param b  A maker name, or NULL.
 * @return true when the names are equal with A to Z folded.  Two NULL names
 * are equal; a NULL name and a name are not.
 */
bool esc_brand_same(const char *a, const char *b);

/**
 * @brief Drop every card profile; the built-in ones remain.
 */
void esc_profiles_clear_overrides(void);

/**
 * @brief Card profiles held, for a status line.
 *
 * @return The number of card profiles held.
 */
size_t esc_profiles_override_count(void);

/**
 * @brief Every profile the bench offers: the built-in ones with card files in
 *        their place, then card profiles with new ids.
 *
 * @return The number of profiles the bench offers.
 */
size_t esc_profiles_count(void);
/**
 * @brief The profile at index @p i of the list the bench offers.
 *
 * @param i  Index into the list, from 0.
 * @return The profile at @p i, or NULL when @p i is past the last profile.
 */
const esc_profile_t *esc_profiles_at(size_t i);

/**
 * @brief NULL when no profile has this id.
 *
 * @param id  The profile id.
 * @return The card profile with this id if there is one, else the built-in
 * profile, or NULL when @p id is NULL or no profile has it.
 */
const esc_profile_t *esc_profiles_find(const char *id);

/**
 * @brief Whether the profile at @p p came from the card.
 *
 * @param p  The profile.
 * @return true when @p p is one of the card profiles.
 */
bool esc_profiles_is_override(const esc_profile_t *p);

/* ------------------------------------------------------------- search */

/**
 * @brief Whether @p pattern is found in @p text.
 *
 * Letters A to Z match without regard to case; every other byte matches itself,
 * so "Ü" does not match "ü".  "*" stands for any run of characters, none
 * included, and the pattern is found anywhere in the text: "sky*v2" is in
 * "Skywalker V2". An empty pattern, or one of "*" only, is found in every text.
 * A NULL pattern counts as empty and a NULL text as "".
 *
 * @param text     The text to search.
 * @param pattern  The pattern, with "*" as a wildcard.
 * @return true when the pattern is found
 */
bool esc_text_matches(const char *text, const char *pattern);

/**
 * @brief Whether @p pattern (as esc_text_matches()) is found in the profile's
 *        manufacturer and name read as one text, "brand family", or in its
 *        manufacturer and one of its models' names, "brand model": a family
 *        such as "JAZZ / MINIJAZZ" names its sizes only in its models ("JAZZ 55
 *        LV").
 *
 * @param p        The profile to search.
 * @param pattern  The pattern, with "*" as a wildcard.
 * @return true when the pattern is found
 */
bool esc_profile_matches(const esc_profile_t *p, const char *pattern);

/**
 * @brief Whether @p pattern finds model @p model of @p p: in the maker and the
 *        family read as one text, which every model of the family shares, or in
 *        the maker and the model's own name, as esc_profile_matches() reads
 *        them.
 *
 * @param p        The profile.
 * @param model    Index into the profile's models.
 * @param pattern  The pattern, with "*" as a wildcard.
 * @return true when the pattern finds the model
 */
bool esc_model_matches(const esc_profile_t *p, unsigned model,
                       const char *pattern);

/**
 * @brief Whether item @p item of @p p is on model @p model: an item without
 *        applies_to is on every model; one with it only on the models it names.
 *
 * For -1 -- the family, no model picked -- only an item on every model, as the
 * one item the ESC is sure to have.
 *
 * @param p      The profile.
 * @param item   Index into the profile's items.
 * @param model  Index into the profile's models, or -1 for none picked.
 * @return true when the item applies
 */
bool esc_item_applies(const esc_profile_t *p, unsigned item, int model);

/**
 * @brief How many of the profile's manual steps are due at @p when.
 *
 *  @param p     The profile.
 *  @param when  The point in a run to count for.
 *  @return The number of manual steps due at @p when.
 */
unsigned esc_profile_manual_count(const esc_profile_t *p,
                                  esc_manual_when_t when);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ESC_PROFILE_H */
