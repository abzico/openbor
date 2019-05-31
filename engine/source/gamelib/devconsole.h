/*
 * OpenBOR - http://www.chronocrash.com
 * -----------------------------------------------------------------------
 * All rights reserved, see LICENSE in OpenBOR root for details.
 *
 * Copyright (c) 2004 - 2014 OpenBOR Team
 */

#ifndef DEVCONSOLE_H
#define DEVCONSOLE_H

#include <SDL_stdinc.h>
#include <SDL_events.h>
#include <stdbool.h>

/**
 * Support interactive features of dev.txt.
 */
extern bool is_dev_console_triggered;
extern bool is_dev_console_shouldbe_visible;
extern bool is_dev_console_prevframe_update_bypass;
#define DEVCONSOLE_MAX_CHAR_INPUT 95
// TODO: probably prevent this definition at compile time if users don't need it
extern char dev_console_inputstr[DEVCONSOLE_MAX_CHAR_INPUT];

/**
 * Detect and handle key control
 *
 * \param keystate Current keystate
 * \param event current key event
 *
 * \return true if event is consumed, otherwise return false. If subsequent event handler
 * receives false as result from this function, then it should handle event further, otherwise
 * just ignore it.
 */
bool devconsole_control_update(Uint8 *keystate, const SDL_Event event);

/**
 * Update its logic and rendering per-frame.
 */
void devconsole_perframe_update();

/**
 * Init devconsole preparing it to be used
 */
void devconsole_init();

/**
 * Shutdown devconsole
 */
void devconsole_shutdown();

/**
 * Reset state of devconsole
 * This will empty all related string, and its state.
 */
void devconsole_reset();

#endif

