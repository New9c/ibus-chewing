/*
 * Copyright © 2009,2010  Red Hat, Inc. All rights reserved.
 * Copyright © 2009,2010  Ding-Yi Chen <dchen at redhat.com>
 *
 * This file is part of the ibus-chewing Project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that ill be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */
#include "GSettingsBackend.h"
#include "IBusChewingPreEdit.h"
#include "IBusChewingProperties.h"
#include "IBusChewingUtil.h"
#include <chewing.h>
#include <glib/gi18n.h>
#include <ibus.h>

#include "MakerDialogUtil.h"
#include "ibus-chewing-engine.h"

#include "ibus-chewing-engine-private.h"

static const GEnumValue _engine_flag_values[] = {
    {ENGINE_FLAG_INITIALIZED, (char *)"ENGINE_FLAG_INITIALIZED",
     (char *)"initialized"},
    {ENGINE_FLAG_ENABLED, (char *)"ENGINE_FLAG_ENABLED", (char *)"enabled"},
    {ENGINE_FLAG_FOCUS_IN, (char *)"ENGINE_FLAG_FOCUS_IN", (char *)"focus-in"},
    {ENGINE_FLAG_IS_PASSWORD, (char *)"ENGINE_FLAG_IS_PASSWORD",
     (char *)"is-password"},
    {ENGINE_FLAG_PROPERTIES_REGISTERED,
     (char *)"ENGINE_FLAG_PROPERTIES_REGISTERED",
     (char *)"properties-registered"},
    {0, NULL, NULL}};

GType engine_flag_get_type(void) {
    static GType type = 0;

    if G_UNLIKELY (type == 0)
        type = g_enum_register_static("EngineFlag", _engine_flag_values);
    return type;
}

extern gint ibus_chewing_verbose;

G_DEFINE_TYPE(IBusChewingEngine, ibus_chewing_engine, IBUS_TYPE_ENGINE);

/* here are local prototypes */
static IBusProperty *
ibus_chewing_engine_get_ibus_property_by_name(IBusChewingEngine *self,
                                              const gchar *prop_name);
static void ibus_chewing_engine_page_up(IBusEngine *engine);
static void ibus_chewing_engine_page_down(IBusEngine *engine);
static void ibus_chewing_engine_cursor_up(IBusEngine *engine);
static void ibus_chewing_engine_cursor_down(IBusEngine *engine);
static void ibus_chewing_engine_set_capabilities(IBusEngine *engine,
                                                 guint caps);
static void ibus_chewing_engine_property_show(IBusEngine *engine,
                                              const gchar *prop_name);
static void ibus_chewing_engine_property_hide(IBusEngine *engine,
                                              const gchar *prop_name);

#define ibus_chewing_engine_has_status_flag(self, f)                           \
    mkdg_has_flag(self->statusFlags, f)
#define ibus_chewing_engine_set_status_flag(self, f)                           \
    mkdg_set_flag(self->statusFlags, f)
#define ibus_chewing_engine_clear_status_flag(self, f)                         \
    mkdg_clear_flag(self->statusFlags, f)

#define is_password(self)                                                      \
    ibus_chewing_engine_has_status_flag(self, ENGINE_FLAG_IS_PASSWORD)
#define is_chinese_mode(self)                                                  \
    ibus_chewing_pre_edit_get_chi_eng_mode(self->icPreEdit)

#define ibus_text_is_empty(iText)                                              \
    ((iText == NULL) || STRING_IS_EMPTY(iText->text))

static void ibus_chewing_engine_finalize(GObject *gobject) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(gobject);

    ibus_chewing_pre_edit_free(self->icPreEdit);
    g_clear_object(&self->preEditText);
    g_clear_object(&self->auxText);
    g_clear_object(&self->outgoingText);
    g_clear_object(&self->InputMode);
    g_clear_object(&self->AlnumSize);
    g_clear_object(&self->setup_prop);
    g_clear_object(&self->prop_list);
    G_OBJECT_CLASS(ibus_chewing_engine_parent_class)->finalize(gobject);
}

static void ibus_chewing_engine_init(IBusChewingEngine *self) {
    self->InputMode_label_chi = g_object_ref_sink(
        ibus_text_new_from_static_string(_("Switch to Alphanumeric Mode")));
    self->InputMode_label_eng = g_object_ref_sink(
        ibus_text_new_from_static_string(_("Switch to Chinese Mode")));
    self->InputMode_tooltip =
        g_object_ref_sink(ibus_text_new_from_static_string(
            _("Click to toggle Chinese/Alphanumeric Mode")));
    self->InputMode_symbol_chi =
        g_object_ref_sink(ibus_text_new_from_static_string("中"));
    self->InputMode_symbol_eng =
        g_object_ref_sink(ibus_text_new_from_static_string("英"));
    self->AlnumSize_label_full = g_object_ref_sink(
        ibus_text_new_from_static_string(_("Fullwidth Form")));
    self->AlnumSize_label_half = g_object_ref_sink(
        ibus_text_new_from_static_string(_("Halfwidth Form")));
    self->AlnumSize_tooltip =
        g_object_ref_sink(ibus_text_new_from_static_string(
            _("Click to toggle Halfwidth/Fullwidth Form")));
    self->AlnumSize_symbol_full =
        g_object_ref_sink(ibus_text_new_from_static_string("全"));
    self->AlnumSize_symbol_half =
        g_object_ref_sink(ibus_text_new_from_static_string("半"));
    self->setup_prop_label = g_object_ref_sink(
        ibus_text_new_from_static_string(_("IBus-Chewing Preferences")));
    self->setup_prop_tooltip = g_object_ref_sink(
        ibus_text_new_from_static_string(_("Click to configure IBus-Chewing")));
    self->setup_prop_symbol =
        g_object_ref_sink(ibus_text_new_from_static_string("訂"));
    self->emptyText = g_object_ref_sink(ibus_text_new_from_static_string(""));

    self->preEditText = NULL;
    self->auxText = NULL;
    self->outgoingText = NULL;
    self->statusFlags = 0;
    self->capabilite = 0;
    self->InputMode = g_object_ref_sink(ibus_property_new(
        "InputMode", PROP_TYPE_NORMAL, self->InputMode_label_chi, NULL,
        self->InputMode_tooltip, TRUE, TRUE, PROP_STATE_UNCHECKED, NULL));
    self->AlnumSize = g_object_ref_sink(ibus_property_new(
        "AlnumSize", PROP_TYPE_NORMAL, self->AlnumSize_label_half, NULL,
        self->AlnumSize_tooltip, TRUE, TRUE, PROP_STATE_UNCHECKED, NULL));
    self->setup_prop = g_object_ref_sink(ibus_property_new(
        "setup_prop", PROP_TYPE_NORMAL, self->setup_prop_label, NULL,
        self->setup_prop_tooltip, TRUE, TRUE, PROP_STATE_UNCHECKED, NULL));
    self->prop_list = g_object_ref_sink(ibus_prop_list_new());
    self->keymap_us = ibus_keymap_get("us");
    IBUS_CHEWING_LOG(INFO, "init() %sinitialized",
                     (self->statusFlags & ENGINE_FLAG_INITIALIZED) ? "" : "un");
    if (self->statusFlags & ENGINE_FLAG_INITIALIZED) {
        return;
    }

    MkdgBackend *backend = mkdg_g_settings_backend_new(
        QUOTE_ME(PROJECT_SCHEMA_ID), QUOTE_ME(PROJECT_SCHEMA_DIR), NULL);

    self->icPreEdit = ibus_chewing_pre_edit_new(backend);

    g_assert(self->icPreEdit);

    self->icPreEdit->engine = IBUS_ENGINE(self);

    /* init properties */
    ibus_prop_list_append(self->prop_list, self->InputMode);
    ibus_prop_list_append(self->prop_list, self->AlnumSize);
    ibus_prop_list_append(self->prop_list, self->setup_prop);

    ibus_chewing_engine_set_status_flag(self, ENGINE_FLAG_INITIALIZED);

    IBUS_CHEWING_LOG(DEBUG, "init() Done");
}

static void ibus_chewing_engine_class_init(IBusChewingEngineClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    IBusEngineClass *ibus_engine_class = IBUS_ENGINE_CLASS(klass);

    object_class->finalize = ibus_chewing_engine_finalize;

    ibus_engine_class->reset = ibus_chewing_engine_reset;
    ibus_engine_class->page_up = ibus_chewing_engine_page_up;
    ibus_engine_class->page_down = ibus_chewing_engine_page_down;
    ibus_engine_class->cursor_up = ibus_chewing_engine_cursor_up;
    ibus_engine_class->cursor_down = ibus_chewing_engine_cursor_down;
    ibus_engine_class->enable = ibus_chewing_engine_enable;
    ibus_engine_class->disable = ibus_chewing_engine_disable;
    ibus_engine_class->focus_in = ibus_chewing_engine_focus_in;
    ibus_engine_class->focus_out = ibus_chewing_engine_focus_out;
    ibus_engine_class->set_capabilities = ibus_chewing_engine_set_capabilities;
    ibus_engine_class->property_show = ibus_chewing_engine_property_show;
    ibus_engine_class->property_hide = ibus_chewing_engine_property_hide;

    ibus_engine_class->property_activate =
        ibus_chewing_engine_property_activate;
    ibus_engine_class->process_key_event =
        ibus_chewing_engine_process_key_event;
    ibus_engine_class->candidate_clicked =
        ibus_chewing_engine_candidate_clicked;
#if IBUS_CHECK_VERSION(1, 5, 4)
    ibus_engine_class->set_content_type = ibus_chewing_engine_set_content_type;
#endif
}

void ibus_chewing_engine_use_setting(IBusChewingEngine *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(IBUS_IS_CHEWING_ENGINE(self));
    IBUS_CHEWING_LOG(INFO, "use_setting()");

    ibus_chewing_pre_edit_use_all_configure(self->icPreEdit);
}

void ibus_chewing_engine_restore_mode(IBusChewingEngine *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(IBUS_IS_CHEWING_ENGINE(self));
    {
        IBUS_CHEWING_LOG(DEBUG, "restore_mode() statusFlags=%x",
                         self->statusFlags);
        GdkDisplay *display = gdk_display_get_default();

        if (display != NULL) {
            GdkSeat *seat = gdk_display_get_default_seat(display);

            if (seat != NULL) {
                GdkDevice *keyboard = gdk_seat_get_keyboard(seat);

                /* Restore Led Mode only make sense if
                 * display is available */
                if (ibus_chewing_pre_edit_has_flag(self->icPreEdit,
                                                   FLAG_SYNC_FROM_IM)) {
                    IBUS_CHEWING_LOG(DEBUG, "restore_mode() "
                                            "FLAG_SYNC_FROM_IM (deprecated)");
                } else if (keyboard != NULL &&
                           ibus_chewing_pre_edit_has_flag(
                               self->icPreEdit, FLAG_SYNC_FROM_KEYBOARD)) {
                    IBUS_CHEWING_LOG(DEBUG, "restore_mode() "
                                            "FLAG_SYNC_FROM_KEYBOARD");
                    gboolean caps_lock_on =
                        gdk_device_get_caps_lock_state(keyboard);
                    chewing_set_ChiEngMode(self->icPreEdit->context,
                                           caps_lock_on ? 0 : CHINESE_MODE);
                }
            }
            ibus_chewing_engine_refresh_property(self, "InputMode");
        }
    }
}

void ibus_chewing_engine_update(IBusChewingEngine *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(IBUS_IS_CHEWING_ENGINE(self));
    {
        IBUS_CHEWING_LOG(DEBUG, "update() statusFlags=%x", self->statusFlags);
        update_pre_edit_text(self);
        commit_text(self);
        update_aux_text(self);

        IBUS_CHEWING_LOG(DEBUG, "update() nPhoneSeq=%d statusFlags=%x",
                         chewing_get_phoneSeqLen(self->icPreEdit->context),
                         self->statusFlags);
        update_lookup_table(self);
    }
}

void ibus_chewing_engine_refresh_property(
    IBusChewingEngine *self, [[maybe_unused]] const gchar *prop_name) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(IBUS_IS_CHEWING_ENGINE(self));
    {
#ifndef UNIT_TEST
        IBUS_CHEWING_LOG(DEBUG, "refresh_property(%s) status=%x", prop_name,
                         self->statusFlags);

        if (STRING_EQUALS(prop_name, "InputMode")) {

            ibus_property_set_label(
                self->InputMode,
                ibus_chewing_pre_edit_get_chi_eng_mode(self->icPreEdit)
                    ? self->InputMode_label_chi
                    : self->InputMode_label_eng);

#if IBUS_CHECK_VERSION(1, 5, 0)
            ibus_property_set_symbol(
                self->InputMode,
                ibus_chewing_pre_edit_get_chi_eng_mode(self->icPreEdit)
                    ? self->InputMode_symbol_chi
                    : self->InputMode_symbol_eng);
#endif

            ibus_engine_update_property(IBUS_ENGINE(self), self->InputMode);

        } else if (STRING_EQUALS(prop_name, "AlnumSize")) {

            ibus_property_set_label(
                self->AlnumSize, chewing_get_ShapeMode(self->icPreEdit->context)
                                     ? self->AlnumSize_label_full
                                     : self->AlnumSize_label_half);

#if IBUS_CHECK_VERSION(1, 5, 0)
            ibus_property_set_symbol(
                self->AlnumSize, chewing_get_ShapeMode(self->icPreEdit->context)
                                     ? self->AlnumSize_symbol_full
                                     : self->AlnumSize_symbol_half);
#endif

            if (self->statusFlags & ENGINE_FLAG_PROPERTIES_REGISTERED)
                ibus_engine_update_property(IBUS_ENGINE(self), self->AlnumSize);

        } else if (STRING_EQUALS(prop_name, "setup_prop")) {
#if IBUS_CHECK_VERSION(1, 5, 0)
            ibus_property_set_symbol(self->setup_prop, self->setup_prop_symbol);
#endif
            ibus_engine_update_property(IBUS_ENGINE(self), self->setup_prop);
        }
#endif
    }
}

/**
 * ibus_chewing_engine_refresh_property_list:
 * @self: this instances.
 *
 * Refresh the property list (language bar).
 **/
void ibus_chewing_engine_refresh_property_list(IBusChewingEngine *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(IBUS_IS_CHEWING_ENGINE(self));
    {
#ifndef UNIT_TEST
        ibus_chewing_engine_refresh_property(self, "InputMode");
        ibus_chewing_engine_refresh_property(self, "AlnumSize");
        ibus_chewing_engine_refresh_property(self, "setup_prop");
#endif
    }
}

/**
 * ibus_chewing_engine_hide_property_list:
 * @self: this instances.
 *
 * Hide the property list (language bar).
 **/
void ibus_chewing_engine_hide_property_list(IBusChewingEngine *self) {
    g_return_if_fail(self != NULL);
    g_return_if_fail(IBUS_IS_CHEWING_ENGINE(self));
    {
#ifndef UNIT_TEST
        IBUS_ENGINE_GET_CLASS(self)->property_hide(IBUS_ENGINE(self),
                                                   "AlnumSize");
#endif
    }
}

static IBusProperty *
ibus_chewing_engine_get_ibus_property_by_name(IBusChewingEngine *self,
                                              const gchar *prop_name) {
    g_return_val_if_fail(self != NULL, (IBusProperty *)0);
    g_return_val_if_fail(IBUS_IS_CHEWING_ENGINE(self), (IBusProperty *)0);
    {
        if (STRING_EQUALS(prop_name, "InputMode")) {
            return self->InputMode;
        } else if (STRING_EQUALS(prop_name, "AlnumSize")) {
            return self->AlnumSize;
        } else if (STRING_EQUALS(prop_name, "setup_prop")) {
            return self->setup_prop;
        }
        IBUS_CHEWING_LOG(MSG, "get_ibus_property_by_name(%s): NULL is returned",
                         prop_name);
        return NULL;
    }
}

static void ibus_chewing_engine_page_up(IBusEngine *engine G_GNUC_UNUSED) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);

    if (is_password(self))
        return;
    ibus_chewing_pre_edit_process_key(self->icPreEdit, IBUS_KEY_Page_Up, 0);
    ibus_chewing_engine_update(self);
}

static void ibus_chewing_engine_page_down(IBusEngine *engine G_GNUC_UNUSED) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);

    if (is_password(self))
        return;
    ibus_chewing_pre_edit_process_key(self->icPreEdit, IBUS_KEY_Page_Down, 0);
    ibus_chewing_engine_update(self);
}

static void ibus_chewing_engine_cursor_up(IBusEngine *engine G_GNUC_UNUSED) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);

    if (is_password(self))
        return;
    ibus_chewing_pre_edit_process_key(self->icPreEdit, IBUS_KEY_Up, 0);
    ibus_chewing_engine_update(self);
}

static void ibus_chewing_engine_cursor_down(IBusEngine *engine G_GNUC_UNUSED) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);

    if (is_password(self))
        return;
    ibus_chewing_pre_edit_process_key(self->icPreEdit, IBUS_KEY_Down, 0);
    ibus_chewing_engine_update(self);
}

static void
ibus_chewing_engine_set_capabilities(IBusEngine *engine G_GNUC_UNUSED,
                                     guint caps) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);

    self->capabilite = caps;
    IBUS_CHEWING_LOG(MSG, "***** set_capabilities(%x): statusFlags=%x", caps,
                     self->statusFlags);
}

static void ibus_chewing_engine_property_show(IBusEngine *engine G_GNUC_UNUSED,
                                              const gchar *prop_name) {
    IBUS_CHEWING_LOG(INFO, "property_show(-, %s)", prop_name);
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);
    IBusProperty *prop =
        ibus_chewing_engine_get_ibus_property_by_name(self, prop_name);

    ibus_property_set_visible(prop, TRUE);
}

static void ibus_chewing_engine_property_hide(IBusEngine *engine G_GNUC_UNUSED,
                                              const gchar *prop_name) {
    IBUS_CHEWING_LOG(INFO, "property_hide(-, %s)", prop_name);
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);
    IBusProperty *prop =
        ibus_chewing_engine_get_ibus_property_by_name(self, prop_name);

    ibus_property_set_visible(prop, FALSE);
}

/**
 * ibus_chewing_engine_start:
 * @self: IBusChewingEngine instance.
 *
 * This is different with init. This will be called in the
 * beginning of reset, enable, and focus_in for setup.
 */
void ibus_chewing_engine_start(IBusChewingEngine *self) {
#ifndef UNIT_TEST
    if (!ibus_chewing_engine_has_status_flag(
            self, ENGINE_FLAG_PROPERTIES_REGISTERED)) {
        IBUS_ENGINE_GET_CLASS(self)->property_show(IBUS_ENGINE(self),
                                                   "InputMode");
        IBUS_ENGINE_GET_CLASS(self)->property_show(IBUS_ENGINE(self),
                                                   "AlnumSize");
        IBUS_ENGINE_GET_CLASS(self)->property_show(IBUS_ENGINE(self),
                                                   "setup_prop");
        ibus_engine_register_properties(IBUS_ENGINE(self), self->prop_list);
        ibus_chewing_engine_set_status_flag(self,
                                            ENGINE_FLAG_PROPERTIES_REGISTERED);
    }
#endif
    ibus_chewing_engine_use_setting(self);
    ibus_chewing_engine_restore_mode(self);
    ibus_chewing_engine_refresh_property_list(self);
}

/**
 * ibus_chewing_engine_reset:
 * @self: IBusChewingEngine instance.
 *
 * Reset the outgoing and pre_edit buffer and cursor
 * chewing_reset will NOT called here, as it will chnage
 * the KBType and input mode.
 */
void ibus_chewing_engine_reset(IBusEngine *engine) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);
    IBUS_CHEWING_LOG(MSG, "* reset");

    /* Always clean buffer */
    ibus_chewing_pre_edit_clear(self->icPreEdit);
#ifndef UNIT_TEST

    ibus_engine_hide_auxiliary_text(engine);
    ibus_engine_hide_lookup_table(engine);
    ibus_engine_update_preedit_text(engine, self->emptyText, 0, FALSE);

#endif
}

void ibus_chewing_engine_enable(IBusEngine *engine) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);
    IBUS_CHEWING_LOG(MSG, "* enable(): statusFlags=%x", self->statusFlags);
    ibus_chewing_engine_start(self);
    ibus_chewing_engine_set_status_flag(self, ENGINE_FLAG_ENABLED);
}

void ibus_chewing_engine_disable(IBusEngine *engine) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);
    IBUS_CHEWING_LOG(MSG, "* disable(): statusFlags=%x", self->statusFlags);
    ibus_chewing_engine_clear_status_flag(self, ENGINE_FLAG_ENABLED);
}

void ibus_chewing_engine_focus_in(IBusEngine *engine) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);
    IBUS_CHEWING_LOG(MSG, "* focus_in(): statusFlags=%x", self->statusFlags);
    ibus_chewing_engine_start(self);
    /* Shouldn't have anything to commit when Focus-in */
    ibus_chewing_pre_edit_clear(self->icPreEdit);
    refresh_pre_edit_text(self);
    refresh_aux_text(self);
    refresh_outgoing_text(self);

    ibus_chewing_engine_set_status_flag(self, ENGINE_FLAG_FOCUS_IN);
    IBUS_CHEWING_LOG(INFO, "focus_in() statusFlags=%x: return",
                     self->statusFlags);
}

void ibus_chewing_engine_focus_out(IBusEngine *engine) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);
    IBUS_CHEWING_LOG(MSG, "* focus_out(): statusFlags=%x", self->statusFlags);
    ibus_chewing_engine_clear_status_flag(
        self, ENGINE_FLAG_FOCUS_IN | ENGINE_FLAG_PROPERTIES_REGISTERED);
    ibus_chewing_engine_hide_property_list(self);

    if (ibus_chewing_pre_edit_get_property_boolean(self->icPreEdit,
                                                   "clean-buffer-focus-out")) {
        /* Clean the buffer when focus out */
        ibus_chewing_pre_edit_clear(self->icPreEdit);
        refresh_pre_edit_text(self);
        refresh_aux_text(self);
    }

    IBUS_CHEWING_LOG(DEBUG, "focus_out(): return");
}

#if IBUS_CHECK_VERSION(1, 5, 4)
void ibus_chewing_engine_set_content_type(IBusEngine *engine, guint purpose,
                                          guint hints) {
    IBUS_CHEWING_LOG(DEBUG, "ibus_chewing_set_content_type(%d, %d)", purpose,
                     hints);

    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);

    if (purpose == IBUS_INPUT_PURPOSE_PASSWORD ||
        purpose == IBUS_INPUT_PURPOSE_PIN) {
        ibus_chewing_engine_set_status_flag(self, ENGINE_FLAG_IS_PASSWORD);
    } else {
        ibus_chewing_engine_clear_status_flag(self, ENGINE_FLAG_IS_PASSWORD);
    }
}
#endif

/*=================================================
 * Display text update routines
 *
 */

void parent_commit_text(IBusEngine *iEngine) {
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(iEngine);

    IBUS_CHEWING_LOG(MSG, "* parent_commit_text(-): outgoingText=%s",
                     self->outgoingText->text);
#ifdef UNIT_TEST
    printf("* parent_commit_text(-, %s)\n", self->outgoingText->text);
#else
    ibus_engine_commit_text(iEngine, self->outgoingText);
#endif
}

void parent_update_pre_edit_text([[maybe_unused]] IBusEngine *iEngine,
                                 IBusText *iText, guint cursor_pos,
                                 gboolean visible) {
#ifdef UNIT_TEST
    printf("* parent_update_pre_edit_text(-, %s, %u, %x)\n", iText->text,
           cursor_pos, visible);
#else
    ibus_engine_update_preedit_text(iEngine, iText, cursor_pos, visible);
#endif
}

void parent_update_pre_edit_text_with_mode([[maybe_unused]] IBusEngine *iEngine,
                                           IBusText *iText, guint cursor_pos,
                                           gboolean visible,
                                           IBusPreeditFocusMode mode) {
#ifdef UNIT_TEST
    printf("* parent_update_pre_edit_text_with_mode(-, %s, %u, %x, %x)\n",
           iText->text, cursor_pos, visible, mode);
#else
    ibus_engine_update_preedit_text_with_mode(iEngine, iText, cursor_pos,
                                              visible, mode);
#endif
}

void parent_update_auxiliary_text([[maybe_unused]] IBusEngine *iEngine,
                                  IBusText *iText, gboolean visible) {
#ifdef UNIT_TEST
    printf("* parent_update_auxiliary_text(-, %s, %x)\n",
           (iText) ? iText->text : "NULL", visible);
#else
    if (!visible || ibus_text_is_empty(iText)) {
        ibus_engine_hide_auxiliary_text(iEngine);
        return;
    }
    ibus_engine_update_auxiliary_text(iEngine, iText, visible);
    ibus_engine_show_auxiliary_text(iEngine);
#endif
}

IBusText *decorate_pre_edit(IBusChewingPreEdit *icPreEdit,
                            [[maybe_unused]] IBusCapabilite capabilite) {
    gchar *preEdit = ibus_chewing_pre_edit_get_pre_edit(icPreEdit);
    IBusText *iText = ibus_text_new_from_string(preEdit);
    gint chiSymbolCursor = chewing_cursor_Current(icPreEdit->context);
    gint charLen = (gint)g_utf8_strlen(preEdit, -1);

    IBUS_CHEWING_LOG(DEBUG,
                     "decorate_pre_edit() cursor=%d "
                     "preEdit=%s charLen=%d",
                     chiSymbolCursor, preEdit, charLen);

    /* Use single underline to mark whole pre-edit buffer
     */
    ibus_text_append_attribute(iText, IBUS_ATTR_TYPE_UNDERLINE,
                               IBUS_ATTR_UNDERLINE_SINGLE, 0, charLen);

    /* Use background color to show current cursor */
    if (chiSymbolCursor < charLen) {
        ibus_text_append_attribute(iText, IBUS_ATTR_TYPE_BACKGROUND, 0x00c8c8f0,
                                   chiSymbolCursor, chiSymbolCursor + 1);
        ibus_text_append_attribute(iText, IBUS_ATTR_TYPE_FOREGROUND, 0x00000000,
                                   chiSymbolCursor, chiSymbolCursor + 1);
    }

    return iText;
}

void refresh_pre_edit_text(IBusChewingEngine *self) {
    IBusText *iText = decorate_pre_edit(self->icPreEdit, self->capabilite);

    if (self->preEditText) {
        g_object_unref(self->preEditText);
    }
    self->preEditText = g_object_ref_sink(iText);
}

void update_pre_edit_text(IBusChewingEngine *self) {
    refresh_pre_edit_text(self);
    gboolean visible = TRUE;
    gint bpmfLen = self->icPreEdit->bpmfLen;

    IBusPreeditFocusMode mode;

    if (STRING_IS_EMPTY(self->preEditText->text)) {
        mode = IBUS_ENGINE_PREEDIT_CLEAR;
        visible = FALSE;
    } else {
        mode = IBUS_ENGINE_PREEDIT_COMMIT;
    }

    parent_update_pre_edit_text_with_mode(IBUS_ENGINE(self), self->preEditText,
                                          cursor_current + bpmfLen, visible,
                                          mode);
}

void refresh_aux_text(IBusChewingEngine *self) {
    IBUS_CHEWING_LOG(INFO, "refresh_aux_text()");

    if (self->auxText != NULL) {
        g_object_unref(self->auxText);
    }

    /* Make auxText (text to be displayed in auxiliary
     * candidate window). Use auxText to show messages
     * from libchewing, such as "已有：".
     */

    gboolean showPageNumber = ibus_chewing_pre_edit_get_property_boolean(
        self->icPreEdit, "show-page-number");

    if (chewing_aux_Length(self->icPreEdit->context) > 0) {
        IBUS_CHEWING_LOG(INFO, "update_aux_text() chewing_aux_Length=%x",
                         chewing_aux_Length(self->icPreEdit->context));
        gchar *auxStr = chewing_aux_String(self->icPreEdit->context);

        IBUS_CHEWING_LOG(INFO, "update_aux_text() auxStr=%s", auxStr);
        self->auxText = g_object_ref_sink(ibus_text_new_from_string(auxStr));
        g_free(auxStr);
    } else if (showPageNumber &&
               (chewing_cand_TotalPage(self->icPreEdit->context) > 0)) {
        int TotalPage = chewing_cand_TotalPage(self->icPreEdit->context);
        int currentPage =
            chewing_cand_CurrentPage(self->icPreEdit->context) + 1;
        self->auxText = g_object_ref_sink(
            ibus_text_new_from_printf("(%i/%i)", currentPage, TotalPage));
    } else {
        /* clear out auxText, otherwise it will be
         * displayed continually. */
        self->auxText = g_object_ref_sink(ibus_text_new_from_static_string(""));
    }
}

void update_aux_text(IBusChewingEngine *self) {
    IBUS_CHEWING_LOG(DEBUG, "update_aux_text()");
    refresh_aux_text(self);
    parent_update_auxiliary_text(IBUS_ENGINE(self), self->auxText, TRUE);
}

void update_lookup_table(IBusChewingEngine *self) {
    IBUS_CHEWING_LOG(DEBUG, "update_lookup_table() CurrentPage=%d",
                     chewing_cand_CurrentPage(self->icPreEdit->context));

    gboolean isShow =
        ibus_chewing_pre_edit_has_flag(self->icPreEdit, FLAG_TABLE_SHOW);

    if (isShow) {
#ifndef UNIT_TEST
        ibus_engine_update_lookup_table(IBUS_ENGINE(self),
                                        self->icPreEdit->iTable, isShow);
        ibus_engine_show_lookup_table(IBUS_ENGINE(self));
#endif
    } else {
#ifndef UNIT_TEST
        ibus_engine_update_lookup_table(IBUS_ENGINE(self),
                                        self->icPreEdit->iTable, isShow);
        ibus_engine_hide_lookup_table(IBUS_ENGINE(self));
#endif
    }
}

void refresh_outgoing_text(IBusChewingEngine *self) {
    gchar *outgoingStr = ibus_chewing_pre_edit_get_outgoing(self->icPreEdit);

    IBUS_CHEWING_LOG(INFO, "refresh_outgoing_text() outgoingStr=|%s|",
                     outgoingStr);

    if (self->outgoingText) {
        g_object_unref(self->outgoingText);
    }
    self->outgoingText =
        g_object_ref_sink(ibus_text_new_from_string(outgoingStr));
    IBUS_CHEWING_LOG(DEBUG, "refresh_outgoing_text() outgoingText=|%s|",
                     self->outgoingText->text);
}

void commit_text(IBusChewingEngine *self) {
    refresh_outgoing_text(self);
    if (!ibus_text_is_empty(self->outgoingText) ||
        !ibus_chewing_engine_has_status_flag(self, ENGINE_FLAG_FOCUS_IN)) {
        parent_commit_text(IBUS_ENGINE(self));
    }

    ibus_chewing_pre_edit_clear_outgoing(self->icPreEdit);
}

gboolean ibus_chewing_engine_process_key_event(IBusEngine *engine, KSym keySym,
                                               guint keycode,
                                               KeyModifiers unmaskedMod) {
    IBUS_CHEWING_LOG(MSG, "******** process_key_event(-,%x(%s),%x,%x) %s",
                     keySym, key_sym_get_name(keySym), keycode, unmaskedMod,
                     modifiers_to_string(unmaskedMod));

    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);

    if (unmaskedMod & IBUS_MOD4_MASK)
        return FALSE;

    if (is_password(self))
        return FALSE;

    KSym kSym = ibus_chewing_pre_edit_key_code_to_key_sym(
        self->icPreEdit, keySym, keycode, unmaskedMod);

    gboolean result =
        ibus_chewing_pre_edit_process_key(self->icPreEdit, kSym, unmaskedMod);

    IBUS_CHEWING_LOG(MSG, "process_key_event() result=%d", result);
    ibus_chewing_engine_update(self);

    if (kSym == IBUS_KEY_Shift_L || kSym == IBUS_KEY_Shift_R ||
        kSym == IBUS_KEY_Caps_Lock) {
        /* Refresh property list (language bar) only when
         * users toggle Chi-Eng Mode or Shape Mode with
         * Shift or Caps Lock, otherwise the bar will
         * stick to the cursor and block the candidats
         * list.
         */
        ibus_chewing_engine_refresh_property_list(self);
    }

    return result;
}

/*===================================================
 * Mouse events
 */
void ibus_chewing_engine_candidate_clicked(IBusEngine *engine, guint index,
                                           guint button, guint state) {
    IBUS_CHEWING_LOG(INFO, "*** candidate_clicked(-, %x, %x, %x) ... proceed.",
                     index, button, state);
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);

    if (is_password(self))
        return;
    if ((gint)index >= chewing_get_candPerPage(self->icPreEdit->context)) {
        IBUS_CHEWING_LOG(DEBUG, "candidate_clicked() index out of ranged");
        return;
    }
    if (ibus_chewing_pre_edit_has_flag(self->icPreEdit, FLAG_TABLE_SHOW)) {
        gint *selKeys = chewing_get_selKey(self->icPreEdit->context);
        KSym k = (KSym)selKeys[index];

        ibus_chewing_pre_edit_process_key(self->icPreEdit, k, 0);
        g_free(selKeys);
        ibus_chewing_engine_update(self);
    } else {
        IBUS_CHEWING_LOG(DEBUG,
                         "candidate_clicked() ... candidates are not showing");
    }
}

void ibus_chewing_engine_property_activate(IBusEngine *engine,
                                           const gchar *prop_name,
                                           guint prop_state) {
    IBUS_CHEWING_LOG(INFO, "property_activate(-, %s, %u)", prop_name,
                     prop_state);
    IBusChewingEngine *self = IBUS_CHEWING_ENGINE(engine);

    if (STRING_EQUALS(prop_name, "InputMode")) {
        /* Toggle Chinese <-> English */
        ibus_chewing_pre_edit_toggle_chi_eng_mode(self->icPreEdit);
        IBUS_CHEWING_LOG(INFO, "property_activate chinese=%d",
                         is_chinese_mode(self));
        ibus_chewing_engine_refresh_property(self, prop_name);
    } else if (STRING_EQUALS(prop_name, "AlnumSize")) {
        /* Toggle Full <-> Half */
        chewing_set_ShapeMode(self->icPreEdit->context,
                              !chewing_get_ShapeMode(self->icPreEdit->context));
        ibus_chewing_engine_refresh_property(self, prop_name);
    } else if (STRING_EQUALS(prop_name, "setup_prop")) {
        /* open preferences window */
        system(QUOTE_ME(LIBEXEC_DIR) "/ibus-setup-chewing");
    } else {
        IBUS_CHEWING_LOG(DEBUG, "property_activate(-, %s, %u) not recognized",
                         prop_name, prop_state);
    }
}
