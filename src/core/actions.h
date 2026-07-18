#pragma once
#include "core/assets.h"

/* Executes a chosen menu entry (chainload / bootnext / uefi / submenu-less
 * leaf). Platform-provided: the UEFI app implements it in src/efi/actions_efi.c
 * on top of LoadImage/StartImage and the firmware variable services.
 * `live` = 0 resolves and validates the action without performing it. */
int action_execute(const entry_t *e, int live);
