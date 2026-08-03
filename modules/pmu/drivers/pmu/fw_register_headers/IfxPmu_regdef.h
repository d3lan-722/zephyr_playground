/**
 * \file IfxPmu_regdef.h
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
 * \defgroup IfxSfr_Pmu_Registers Pmu Registers
 * \ingroup IfxSfr
 * 
 * \defgroup IfxSfr_Pmu_Registers_Bitfields Bitfields
 * \ingroup IfxSfr_Pmu_Registers
 * 
 * \defgroup IfxSfr_Pmu_Registers_union Register unions
 * \ingroup IfxSfr_Pmu_Registers
 * 
 * \defgroup IfxSfr_Pmu_Registers_struct Memory map
 * \ingroup IfxSfr_Pmu_Registers
 */
#ifndef IFXPMU_REGDEF_H
#define IFXPMU_REGDEF_H 1
/******************************************************************************/
#include "Ifx_TypesReg.h"
/******************************************************************************/

/******************************************************************************/
/******************************************************************************/


/** \addtogroup IfxSfr_Pmu_Registers_Bitfields
 * \{  */
/** \brief  */
typedef struct _Ifx_PMU_CURRFREQMHZ_Bits
{
    Ifx_UReg_32Bit VALUE:32;          /**< \brief [31:0]  (rh) */
} Ifx_PMU_CURRFREQMHZ_Bits;

/** \brief  */
typedef struct _Ifx_PMU_CURRVOLTMV_Bits
{
    Ifx_UReg_32Bit VALUE:32;          /**< \brief [31:0]  (rh) */
} Ifx_PMU_CURRVOLTMV_Bits;

/** \brief  */
typedef struct _Ifx_PMU_DVFSCTRL_Bits
{
    Ifx_UReg_32Bit DVFSEN:1;          /**< \brief [0:0]  (rwh) */
    Ifx_UReg_32Bit MODESEL:1;         /**< \brief [1:1]  (rwh) */
    Ifx_UReg_32Bit reserved_2:2;      /**< \brief [3:2] \internal Reserved */
    Ifx_UReg_32Bit NUMSTATES:4;       /**< \brief [7:4]  (rwh) */
    Ifx_UReg_32Bit TRANSTIMEOUT:16;    /**< \brief [23:8]  (rwh) */
    Ifx_UReg_32Bit reserved_24:8;     /**< \brief [31:24] \internal Reserved */
} Ifx_PMU_DVFSCTRL_Bits;

/** \brief  */
typedef struct _Ifx_PMU_DVFSPSTATEREQ_Bits
{
    Ifx_UReg_32Bit TARGETPSTATE:4;    /**< \brief [3:0]  (rwh) */
    Ifx_UReg_32Bit reserved_4:28;     /**< \brief [31:4] \internal Reserved */
} Ifx_PMU_DVFSPSTATEREQ_Bits;

/** \brief  */
typedef struct _Ifx_PMU_DVFSPSTATETABLE_PSTATE_Bits
{
    Ifx_UReg_32Bit FREQ:16;           /**< \brief [15:0]  (rwh) */
    Ifx_UReg_32Bit VOLTAGE:16;        /**< \brief [31:16]  (rwh) */
} Ifx_PMU_DVFSPSTATETABLE_PSTATE_Bits;

/** \brief  */
typedef struct _Ifx_PMU_DVFSSTATUS_Bits
{
    Ifx_UReg_32Bit CURRPSTATE:4;      /**< \brief [3:0]  (rh) */
    Ifx_UReg_32Bit TRANSPENDING:1;    /**< \brief [4:4]  (rh) */
    Ifx_UReg_32Bit BUSY:1;            /**< \brief [5:5]  (rh) */
    Ifx_UReg_32Bit TRANSPHASE:3;      /**< \brief [8:6]  (rh) */
    Ifx_UReg_32Bit ERROR:1;           /**< \brief [9:9]  (rwh) */
    Ifx_UReg_32Bit reserved_10:22;    /**< \brief [31:10] \internal Reserved */
} Ifx_PMU_DVFSSTATUS_Bits;

/** \}  */
/******************************************************************************/
/******************************************************************************/
/** \addtogroup IfxSfr_pmu_Registers_union
 * \{   */
/** \brief    */
typedef union
{
    Ifx_UReg_32Bit U;                 /**< \brief Unsigned access */
    Ifx_SReg_32Bit I;                 /**< \brief Signed access */
    Ifx_PMU_CURRFREQMHZ_Bits B;       /**< \brief Bitfield access */
} Ifx_PMU_CURRFREQMHZ;

/** \brief    */
typedef union
{
    Ifx_UReg_32Bit U;                 /**< \brief Unsigned access */
    Ifx_SReg_32Bit I;                 /**< \brief Signed access */
    Ifx_PMU_CURRVOLTMV_Bits B;        /**< \brief Bitfield access */
} Ifx_PMU_CURRVOLTMV;

/** \brief    */
typedef union
{
    Ifx_UReg_32Bit U;                 /**< \brief Unsigned access */
    Ifx_SReg_32Bit I;                 /**< \brief Signed access */
    Ifx_PMU_DVFSCTRL_Bits B;          /**< \brief Bitfield access */
} Ifx_PMU_DVFSCTRL;

/** \brief    */
typedef union
{
    Ifx_UReg_32Bit U;                 /**< \brief Unsigned access */
    Ifx_SReg_32Bit I;                 /**< \brief Signed access */
    Ifx_PMU_DVFSPSTATEREQ_Bits B;     /**< \brief Bitfield access */
} Ifx_PMU_DVFSPSTATEREQ;

/** \brief    */
typedef union
{
    Ifx_UReg_32Bit U;                 /**< \brief Unsigned access */
    Ifx_SReg_32Bit I;                 /**< \brief Signed access */
    Ifx_PMU_DVFSPSTATETABLE_PSTATE_Bits B;    /**< \brief Bitfield access */
} Ifx_PMU_DVFSPSTATETABLE_PSTATE;

/** \brief    */
typedef union
{
    Ifx_UReg_32Bit U;                 /**< \brief Unsigned access */
    Ifx_SReg_32Bit I;                 /**< \brief Signed access */
    Ifx_PMU_DVFSSTATUS_Bits B;        /**< \brief Bitfield access */
} Ifx_PMU_DVFSSTATUS;

/** \}  */

/******************************************************************************/
/** \addtogroup IfxSfr_Pmu_Registers_struct
 * \{  */
/******************************************************************************/
/** \name Object L0
 * \{  */

/** \brief PMU object */
typedef volatile struct _Ifx_PMU
{
       Ifx_UReg_8Bit                       reserved_0[4];          /**< \brief 0, \internal Reserved */
       Ifx_PMU_DVFSCTRL                    DVFSCTRL;               /**< \brief 4, */
       Ifx_PMU_DVFSPSTATEREQ               DVFSPSTATEREQ;          /**< \brief 8, */
       Ifx_PMU_DVFSSTATUS                  DVFSSTATUS;             /**< \brief C, */
       Ifx_PMU_CURRVOLTMV                  CURRVOLTMV;             /**< \brief 10, */
       Ifx_PMU_CURRFREQMHZ                 CURRFREQMHZ;            /**< \brief 14, */
       Ifx_UReg_8Bit                       reserved_18[232];       /**< \brief 18, \internal Reserved */
       Ifx_PMU_DVFSPSTATETABLE_PSTATE      DVFSPSTATETABLE_PSTATE[10];    /**< \brief 100, MIRROR, */
       Ifx_UReg_8Bit                       reserved_128[65240];    /**< \brief 128, \internal Reserved */
} Ifx_PMU;

/** \}  */
/******************************************************************************/
/** \}  */


/******************************************************************************/
/******************************************************************************/

#endif /* IFXPMU_REGDEF_H */
