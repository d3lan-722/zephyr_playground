/**
 * \file IfxPmu_reg.h
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
 * \defgroup IfxSfr_Pmu_Registers_Cfg Pmu address
 * \ingroup IfxSfr_Pmu_Registers
 * 
 * \defgroup IfxSfr_Pmu_Registers_Cfg_BaseAddress Base address
 * \ingroup IfxSfr_Pmu_Registers_Cfg
 *
 * \defgroup IfxSfr_Pmu_Registers_Cfg_Pmu 2-PMU
 * \ingroup IfxSfr_Pmu_Registers_Cfg
 *
 *
 */
#ifndef IFXPMU_REG_H
#define IFXPMU_REG_H 1
/******************************************************************************/
#include "IfxPmu_regdef.h"
/******************************************************************************/

/******************************************************************************/
/******************************************************************************/
/** \addtogroup IfxSfr_Pmu_Registers_Cfg_BaseAddress
 * \{  */

/** \brief PMU object */
#define MODULE_PMU /*lint --e(923, 9078)*/ ((*(Ifx_PMU*)0x0u))
/** \}  */


/******************************************************************************/
/******************************************************************************/
/** \addtogroup IfxSfr_Pmu_Registers_Cfg_Pmu
 * \{  */
/** \brief 4,  */
#define PMU_DVFSCTRL /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSCTRL*)0x4u)

/** \brief 8,  */
#define PMU_DVFSPSTATEREQ /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSPSTATEREQ*)0x8u)

/** \brief C,  */
#define PMU_DVFSSTATUS /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSSTATUS*)0xCu)

/** \brief 10,  */
#define PMU_CURRVOLTMV /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_CURRVOLTMV*)0x10u)

/** \brief 14,  */
#define PMU_CURRFREQMHZ /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_CURRFREQMHZ*)0x14u)

/** \brief 100,  */
#define PMU_DVFSPSTATETABLE_PSTATE0 /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSPSTATETABLE_PSTATE*)0x100u)
/** Alias (User Manual Name) for PMU_DVFSPSTATETABLE_PSTATE0, Base Mirror Register */
#define PMU_DVFSPSTATETABLE_PSTATE (PMU_DVFSPSTATETABLE_PSTATE0)

/** \brief 104,  */
#define PMU_DVFSPSTATETABLE_PSTATE1 /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSPSTATETABLE_PSTATE*)0x104u)

/** \brief 108,  */
#define PMU_DVFSPSTATETABLE_PSTATE2 /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSPSTATETABLE_PSTATE*)0x108u)

/** \brief 10C,  */
#define PMU_DVFSPSTATETABLE_PSTATE3 /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSPSTATETABLE_PSTATE*)0x10Cu)

/** \brief 110,  */
#define PMU_DVFSPSTATETABLE_PSTATE4 /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSPSTATETABLE_PSTATE*)0x110u)

/** \brief 114,  */
#define PMU_DVFSPSTATETABLE_PSTATE5 /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSPSTATETABLE_PSTATE*)0x114u)

/** \brief 118,  */
#define PMU_DVFSPSTATETABLE_PSTATE6 /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSPSTATETABLE_PSTATE*)0x118u)

/** \brief 11C,  */
#define PMU_DVFSPSTATETABLE_PSTATE7 /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSPSTATETABLE_PSTATE*)0x11Cu)

/** \brief 120,  */
#define PMU_DVFSPSTATETABLE_PSTATE8 /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSPSTATETABLE_PSTATE*)0x120u)

/** \brief 124,  */
#define PMU_DVFSPSTATETABLE_PSTATE9 /*lint --e(923, 9078)*/ (*(volatile Ifx_PMU_DVFSPSTATETABLE_PSTATE*)0x124u)


/** \}  */

/******************************************************************************/
/******************************************************************************/
#endif /* IFXPMU_REG_H */
