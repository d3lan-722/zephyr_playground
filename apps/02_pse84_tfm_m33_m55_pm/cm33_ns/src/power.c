/*
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2 stub: Zephyr's PM subsystem requires a pm_state_set symbol
 * once CONFIG_PM=y. The Zephyr PSE84 SoC ships a default in
 * soc/infineon/edge/pse84/power.c, but its PRE_KERNEL_1 SYS_INIT
 * pokes protected SRSS / SOCMEM PPU registers — under TF-M those
 * writes bus-fault from NS and trap the CPU in a reset loop. The
 * app's CMakeLists removes that file from zephyr_sources; this stub
 * satisfies the linker until our own dispatcher arrives in Phase 4.
 *
 * With no cpu-power-states declared in DT yet, the policy never
 * picks a state and these functions are never actually called.
 */

#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);
}
