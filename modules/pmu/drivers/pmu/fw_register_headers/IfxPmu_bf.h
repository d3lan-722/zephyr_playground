/**
 * \file IfxPmu_bf.h
 * \brief
 * \copyright Copyright (c) NA Infineon Technologies AG. All rights reserved.
 *
 *
 * MAY BE CHANGED BY USER [yes/no]: Yes
 *
 *                                 IMPORTANT NOTICE
 *
 *
 * Infineon Technologies AG (Infineon) is supplying this file for use
 * exclusively with Infineon's microcontroller products. This file can be freely
 * distributed within development tools that are supporting such microcontroller
 * products.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
 * OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
 * INFINEON SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL,
 * OR CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
 *
 * \defgroup IfxSfr_Pmu_Registers_BitfieldsMask Bitfields mask and offset
 * \ingroup IfxSfr_Pmu_Registers
 * 
 */
#ifndef IFXPMU_BF_H
#define IFXPMU_BF_H 1

/******************************************************************************/
/******************************************************************************/
/** \addtogroup IfxSfr_Pmu_Registers_BitfieldsMask
 * \{  */
/** \brief Length for Ifx_PMU_DVFSCTRL_Bits.DVFSEN */
#define IFX_PMU_DVFSCTRL_DVFSEN_LEN (1u)

/** \brief Mask for Ifx_PMU_DVFSCTRL_Bits.DVFSEN */
#define IFX_PMU_DVFSCTRL_DVFSEN_MSK (0x1u)

/** \brief Offset for Ifx_PMU_DVFSCTRL_Bits.DVFSEN */
#define IFX_PMU_DVFSCTRL_DVFSEN_OFF (0u)

/** \brief Length for Ifx_PMU_DVFSCTRL_Bits.MODESEL */
#define IFX_PMU_DVFSCTRL_MODESEL_LEN (1u)

/** \brief Mask for Ifx_PMU_DVFSCTRL_Bits.MODESEL */
#define IFX_PMU_DVFSCTRL_MODESEL_MSK (0x1u)

/** \brief Offset for Ifx_PMU_DVFSCTRL_Bits.MODESEL */
#define IFX_PMU_DVFSCTRL_MODESEL_OFF (1u)

/** \brief Length for Ifx_PMU_DVFSCTRL_Bits.NUMSTATES */
#define IFX_PMU_DVFSCTRL_NUMSTATES_LEN (4u)

/** \brief Mask for Ifx_PMU_DVFSCTRL_Bits.NUMSTATES */
#define IFX_PMU_DVFSCTRL_NUMSTATES_MSK (0xfu)

/** \brief Offset for Ifx_PMU_DVFSCTRL_Bits.NUMSTATES */
#define IFX_PMU_DVFSCTRL_NUMSTATES_OFF (4u)

/** \brief Length for Ifx_PMU_DVFSCTRL_Bits.TRANSTIMEOUT */
#define IFX_PMU_DVFSCTRL_TRANSTIMEOUT_LEN (16u)

/** \brief Mask for Ifx_PMU_DVFSCTRL_Bits.TRANSTIMEOUT */
#define IFX_PMU_DVFSCTRL_TRANSTIMEOUT_MSK (0xffffu)

/** \brief Offset for Ifx_PMU_DVFSCTRL_Bits.TRANSTIMEOUT */
#define IFX_PMU_DVFSCTRL_TRANSTIMEOUT_OFF (8u)

/** \brief Length for Ifx_PMU_DVFSPSTATEREQ_Bits.TARGETPSTATE */
#define IFX_PMU_DVFSPSTATEREQ_TARGETPSTATE_LEN (4u)

/** \brief Mask for Ifx_PMU_DVFSPSTATEREQ_Bits.TARGETPSTATE */
#define IFX_PMU_DVFSPSTATEREQ_TARGETPSTATE_MSK (0xfu)

/** \brief Offset for Ifx_PMU_DVFSPSTATEREQ_Bits.TARGETPSTATE */
#define IFX_PMU_DVFSPSTATEREQ_TARGETPSTATE_OFF (0u)

/** \brief Length for Ifx_PMU_DVFSSTATUS_Bits.CURRPSTATE */
#define IFX_PMU_DVFSSTATUS_CURRPSTATE_LEN (4u)

/** \brief Mask for Ifx_PMU_DVFSSTATUS_Bits.CURRPSTATE */
#define IFX_PMU_DVFSSTATUS_CURRPSTATE_MSK (0xfu)

/** \brief Offset for Ifx_PMU_DVFSSTATUS_Bits.CURRPSTATE */
#define IFX_PMU_DVFSSTATUS_CURRPSTATE_OFF (0u)

/** \brief Length for Ifx_PMU_DVFSSTATUS_Bits.TRANSPENDING */
#define IFX_PMU_DVFSSTATUS_TRANSPENDING_LEN (1u)

/** \brief Mask for Ifx_PMU_DVFSSTATUS_Bits.TRANSPENDING */
#define IFX_PMU_DVFSSTATUS_TRANSPENDING_MSK (0x1u)

/** \brief Offset for Ifx_PMU_DVFSSTATUS_Bits.TRANSPENDING */
#define IFX_PMU_DVFSSTATUS_TRANSPENDING_OFF (4u)

/** \brief Length for Ifx_PMU_DVFSSTATUS_Bits.BUSY */
#define IFX_PMU_DVFSSTATUS_BUSY_LEN (1u)

/** \brief Mask for Ifx_PMU_DVFSSTATUS_Bits.BUSY */
#define IFX_PMU_DVFSSTATUS_BUSY_MSK (0x1u)

/** \brief Offset for Ifx_PMU_DVFSSTATUS_Bits.BUSY */
#define IFX_PMU_DVFSSTATUS_BUSY_OFF (5u)

/** \brief Length for Ifx_PMU_DVFSSTATUS_Bits.TRANSPHASE */
#define IFX_PMU_DVFSSTATUS_TRANSPHASE_LEN (3u)

/** \brief Mask for Ifx_PMU_DVFSSTATUS_Bits.TRANSPHASE */
#define IFX_PMU_DVFSSTATUS_TRANSPHASE_MSK (0x7u)

/** \brief Offset for Ifx_PMU_DVFSSTATUS_Bits.TRANSPHASE */
#define IFX_PMU_DVFSSTATUS_TRANSPHASE_OFF (6u)

/** \brief Length for Ifx_PMU_DVFSSTATUS_Bits.ERROR */
#define IFX_PMU_DVFSSTATUS_ERROR_LEN (1u)

/** \brief Mask for Ifx_PMU_DVFSSTATUS_Bits.ERROR */
#define IFX_PMU_DVFSSTATUS_ERROR_MSK (0x1u)

/** \brief Offset for Ifx_PMU_DVFSSTATUS_Bits.ERROR */
#define IFX_PMU_DVFSSTATUS_ERROR_OFF (9u)

/** \brief Length for Ifx_PMU_CURRVOLTMV_Bits.VALUE */
#define IFX_PMU_CURRVOLTMV_VALUE_LEN (32u)

/** \brief Mask for Ifx_PMU_CURRVOLTMV_Bits.VALUE */
#define IFX_PMU_CURRVOLTMV_VALUE_MSK (0xffffffffu)

/** \brief Offset for Ifx_PMU_CURRVOLTMV_Bits.VALUE */
#define IFX_PMU_CURRVOLTMV_VALUE_OFF (0u)

/** \brief Length for Ifx_PMU_CURRFREQMHZ_Bits.VALUE */
#define IFX_PMU_CURRFREQMHZ_VALUE_LEN (32u)

/** \brief Mask for Ifx_PMU_CURRFREQMHZ_Bits.VALUE */
#define IFX_PMU_CURRFREQMHZ_VALUE_MSK (0xffffffffu)

/** \brief Offset for Ifx_PMU_CURRFREQMHZ_Bits.VALUE */
#define IFX_PMU_CURRFREQMHZ_VALUE_OFF (0u)

/** \brief Length for Ifx_PMU_DVFSPSTATETABLE_PSTATE_Bits.FREQ */
#define IFX_PMU_DVFSPSTATETABLE_PSTATE_FREQ_LEN (16u)

/** \brief Mask for Ifx_PMU_DVFSPSTATETABLE_PSTATE_Bits.FREQ */
#define IFX_PMU_DVFSPSTATETABLE_PSTATE_FREQ_MSK (0xffffu)

/** \brief Offset for Ifx_PMU_DVFSPSTATETABLE_PSTATE_Bits.FREQ */
#define IFX_PMU_DVFSPSTATETABLE_PSTATE_FREQ_OFF (0u)

/** \brief Length for Ifx_PMU_DVFSPSTATETABLE_PSTATE_Bits.VOLTAGE */
#define IFX_PMU_DVFSPSTATETABLE_PSTATE_VOLTAGE_LEN (16u)

/** \brief Mask for Ifx_PMU_DVFSPSTATETABLE_PSTATE_Bits.VOLTAGE */
#define IFX_PMU_DVFSPSTATETABLE_PSTATE_VOLTAGE_MSK (0xffffu)

/** \brief Offset for Ifx_PMU_DVFSPSTATETABLE_PSTATE_Bits.VOLTAGE */
#define IFX_PMU_DVFSPSTATETABLE_PSTATE_VOLTAGE_OFF (16u)

/** \}  */

/******************************************************************************/
/******************************************************************************/
#endif /* IFXPMU_BF_H */
