 OUTPUT_FORMAT("elf32-littlearm")
_region_min_align = 32;
MEMORY
    {
    FLASH (rx) : ORIGIN = ((1616379904)), LENGTH = ((2097152) - 0x0)
    RAM (wx) : ORIGIN = (638582784), LENGTH = (262144)
    DTCM ( rw ) : ORIGIN = (536870912), LENGTH = (262144) ITCM ( rw ) : ORIGIN = (0), LENGTH = (262144) M33SCODE ( rw ) : ORIGIN = (872423424), LENGTH = (217088) M33CODE ( rw ) : ORIGIN = (604340224), LENGTH = (413696) SECSHARED ( rw ) : ORIGIN = (873451520), LENGTH = (4096) M33SHARED ( rw ) : ORIGIN = (605020160), LENGTH = (4096) M55SHARED ( rw ) : ORIGIN = (605024256), LENGTH = (4096)
    IDT_LIST (wx) : ORIGIN = 0xFFFF7FFF, LENGTH = 32K
    }
ENTRY("__start")
SECTIONS
    {
 .rel.plt :
 {
 *(.rel.plt)
 PROVIDE_HIDDEN (__rel_iplt_start = .);
 *(.rel.iplt)
 PROVIDE_HIDDEN (__rel_iplt_end = .);
 }
 .rela.plt :
 {
 *(.rela.plt)
 PROVIDE_HIDDEN (__rela_iplt_start = .);
 *(.rela.iplt)
 PROVIDE_HIDDEN (__rela_iplt_end = .);
 }
 .rel.dyn :
 {
 *(.rel.*)
 }
 .rela.dyn :
 {
 *(.rela.*)
 }
    /DISCARD/ :
 {
 *(.plt)
 }
    /DISCARD/ :
 {
 *(.iplt)
 }
   
 __rom_region_start = ((1616379904));
    rom_start :
 {
HIDDEN(__rom_start_address = .);
FILL(0x00);
. += 0x0 - (. - __rom_start_address);
. = ALIGN(4);
. = ALIGN( 1 << LOG2CEIL(4 * 32) );
. = ALIGN( 1 << LOG2CEIL(4 * (16 + 174)) );
_vector_start = .;
KEEP(*(.exc_vector_table))
KEEP(*(".exc_vector_table.*"))
KEEP(*(.vectors))
_vector_end = .;
. = ALIGN(4);
KEEP(*(.gnu.linkonce.irq_vector_table*))
 _vector_end = .;
 } > FLASH
 .itcm_text_reloc :
        {
                . = ALIGN(4);
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_AssertFailed"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_ClearDSRAMWarmBootEntryStatus"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_ClearResetReason"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_Delay"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_DelayUs"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_FaultHandler"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_GetDevice"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_GetDeviceLCS"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_GetDeviceRevision"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_GetResetReason"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_GetResetStatus"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_GetUniqueId"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_Halt"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_IsDSRAMWarmBootEntry"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_ProcessingFault"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_ResetBackupDomain"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_Rtos_Delay"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_Rtos_DelayUs"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_SetWaitStates"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_Syslib_IsMemCacheable"))
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_Syslib_SetWarmBootEntryPoint"))
                KEEP(*cy_syslib.c.obj(".rel.text._sub_D_00100_1"))
                KEEP(*cy_syslib.c.obj(".rel.text._sub_I_00100_0"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_AssertFailed"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_ClearDSRAMWarmBootEntryStatus"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_ClearResetReason"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_Delay"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_DelayUs"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_FaultHandler"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_GetDevice"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_GetDeviceLCS"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_GetDeviceRevision"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_GetResetReason"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_GetResetStatus"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_GetUniqueId"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_Halt"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_IsDSRAMWarmBootEntry"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_ProcessingFault"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_ResetBackupDomain"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_Rtos_Delay"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_Rtos_DelayUs"))
                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_SetWaitStates"))
                KEEP(*cy_syslib.c.obj(".text.Cy_Syslib_IsMemCacheable"))
                KEEP(*cy_syslib.c.obj(".text.Cy_Syslib_SetWarmBootEntryPoint"))
                KEEP(*cy_syslib.c.obj(".text._sub_D_00100_1"))
                KEEP(*cy_syslib.c.obj(".text._sub_I_00100_0"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_ClearHibernateWakeupCause"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_ClearHibernateWakeupSource"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_Cm33IsDeepSleep"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_CoreBuckGetProfile"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_CoreBuckSetProfile"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_CoreBuckStatus"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_CpuEnterDeepSleep"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_CpuEnterSleep"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_DeepSleepIoIsFrozen"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_DeepSleepIoUnfreeze"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_DeepSleepSetup"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_DeepSleepUpdate"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_ExecuteCallback"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetAppDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetBootMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetFailedCallback"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetHibernateWakeupCause"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetSOCMemSramPartActivePwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetSOCMemSramPartDsPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetSRAMMacroPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetSysDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_Init"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_IoUnfreeze"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_IsLpmReady"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_IsSystemHp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_IsSystemLp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_IsSystemUlp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_LdoSetMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_MiscLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_MiscLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_ReadStatus"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_RegisterCallback"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_RetLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_RetLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetAppDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetHibernateWakeupSource"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetPPUDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetRamTrimsPostDs"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetRamTrimsPreDs"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetSOCMEMDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetSOCMemPartActivePwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetSOCMemPartDsPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetSRAMMacroPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetSRAMPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetSysDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetTrimRamCtl"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SramLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SramLdoEnable"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SramLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterDsToHp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterDsToLp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterDsToUlp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterHibernate"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterHp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterHpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterLp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterLpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterUlp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterUlpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemSetMinRegulatorCurrent"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemSetNormalRegulatorCurrent"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemTransitionHpToLp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemTransitionLpToHp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemTransitionLpToUlp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemTransitionUlpToLp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_TriggerSoftReset"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_TriggerXRes"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_UnregisterCallback"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text._sub_D_00100_1"))
                KEEP(*cy_syspm_v4.c.obj(".rel.text._sub_I_00100_0"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_ClearHibernateWakeupCause"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_ClearHibernateWakeupSource"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_Cm33IsDeepSleep"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_CoreBuckGetProfile"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_CoreBuckSetProfile"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_CoreBuckStatus"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_CpuEnterDeepSleep"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_CpuEnterSleep"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_DeepSleepIoIsFrozen"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_DeepSleepIoUnfreeze"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_DeepSleepSetup"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_DeepSleepUpdate"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_ExecuteCallback"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_GetAppDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_GetBootMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_GetDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_GetFailedCallback"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_GetHibernateWakeupCause"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_GetSOCMemSramPartActivePwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_GetSOCMemSramPartDsPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_GetSRAMMacroPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_GetSysDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_Init"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_IoUnfreeze"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_IsLpmReady"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_IsSystemHp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_IsSystemLp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_IsSystemUlp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_LdoSetMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_MiscLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_MiscLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_ReadStatus"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_RegisterCallback"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_RetLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_RetLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetAppDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetHibernateWakeupSource"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetPPUDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetRamTrimsPostDs"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetRamTrimsPreDs"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetSOCMEMDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetSOCMemPartActivePwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetSOCMemPartDsPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetSRAMMacroPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetSRAMPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetSysDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SetTrimRamCtl"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SramLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SramLdoEnable"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SramLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterDsToHp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterDsToLp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterDsToUlp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterHibernate"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterHp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterHpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterLp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterLpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterUlp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterUlpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemSetMinRegulatorCurrent"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemSetNormalRegulatorCurrent"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemTransitionHpToLp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemTransitionLpToHp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemTransitionLpToUlp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemTransitionUlpToLp"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_TriggerSoftReset"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_TriggerXRes"))
                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_UnregisterCallback"))
                KEEP(*cy_syspm_v4.c.obj(".text._sub_D_00100_1"))
                KEEP(*cy_syspm_v4.c.obj(".text._sub_I_00100_0"))
                . = ALIGN(4);
 }
> ITCM AT > FLASH
        __itcm_text_reloc_end = .;
        __itcm_text_reloc_start = ADDR(.itcm_text_reloc);
        __itcm_text_reloc_size = __itcm_text_reloc_end - __itcm_text_reloc_start;
        __itcm_text_rom_start = LOADADDR(.itcm_text_reloc);
 .itcm_data_reloc :
        {
                . = ALIGN(4);
                KEEP(*cy_syslib.c.obj(".data..LPBX0"))
                KEEP(*cy_syslib.c.obj(".data..LPBX1"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_AssertFailed"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_ClearDSRAMWarmBootEntryStatus"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_ClearResetReason"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_Delay"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_DelayUs"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_FaultHandler"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_GetDevice"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_GetDeviceLCS"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_GetDeviceRevision"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_GetResetReason"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_GetResetStatus"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_GetUniqueId"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_Halt"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_IsDSRAMWarmBootEntry"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_ProcessingFault"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_ResetBackupDomain"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_Rtos_Delay"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_Rtos_DelayUs"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_SysLib_SetWaitStates"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_Syslib_IsMemCacheable"))
                KEEP(*cy_syslib.c.obj(".data.__gcov_.Cy_Syslib_SetWarmBootEntryPoint"))
                KEEP(*cy_syslib.c.obj(".rel.data..LPBX0"))
                KEEP(*cy_syslib.c.obj(".rel.data..LPBX1"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_AssertFailed"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_ClearDSRAMWarmBootEntryStatus"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_ClearResetReason"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_Delay"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_DelayUs"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_FaultHandler"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_GetDevice"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_GetDeviceLCS"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_GetDeviceRevision"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_GetResetReason"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_GetResetStatus"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_GetUniqueId"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_Halt"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_IsDSRAMWarmBootEntry"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_ProcessingFault"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_ResetBackupDomain"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_Rtos_Delay"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_Rtos_DelayUs"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_SysLib_SetWaitStates"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_Syslib_IsMemCacheable"))
                KEEP(*cy_syslib.c.obj(".rel.data.__gcov_.Cy_Syslib_SetWarmBootEntryPoint"))
                KEEP(*cy_syspm_v4.c.obj(".data..LPBX0"))
                KEEP(*cy_syspm_v4.c.obj(".data..LPBX1"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_ClearHibernateWakeupCause"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_ClearHibernateWakeupSource"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_Cm33IsDeepSleep"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_CoreBuckGetProfile"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_CoreBuckSetProfile"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_CoreBuckStatus"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_CpuEnterDeepSleep"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_CpuEnterSleep"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_DeepSleepIoIsFrozen"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_DeepSleepIoUnfreeze"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_DeepSleepSetup"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_DeepSleepUpdate"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_ExecuteCallback"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_GetAppDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_GetBootMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_GetDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_GetFailedCallback"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_GetHibernateWakeupCause"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_GetSOCMemSramPartActivePwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_GetSOCMemSramPartDsPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_GetSRAMMacroPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_GetSysDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_Init"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_IoUnfreeze"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_IsLpmReady"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_IsSystemHp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_IsSystemLp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_IsSystemUlp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_LdoSetMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_MiscLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_MiscLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_ReadStatus"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_RegisterCallback"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_RetLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_RetLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetAppDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetHibernateWakeupSource"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetPPUDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetRamTrimsPostDs"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetRamTrimsPreDs"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetSOCMEMDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetSOCMemPartActivePwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetSOCMemPartDsPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetSRAMMacroPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetSRAMPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetSysDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SetTrimRamCtl"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SramLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SramLdoEnable"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SramLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemEnterDsToHp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemEnterDsToLp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemEnterDsToUlp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemEnterHibernate"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemEnterHp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemEnterHpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemEnterLp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemEnterLpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemEnterUlp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemEnterUlpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemSetMinRegulatorCurrent"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemSetNormalRegulatorCurrent"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemTransitionHpToLp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemTransitionLpToHp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemTransitionLpToUlp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_SystemTransitionUlpToLp"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_TriggerSoftReset"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_TriggerXRes"))
                KEEP(*cy_syspm_v4.c.obj(".data.__gcov_.Cy_SysPm_UnregisterCallback"))
                KEEP(*cy_syspm_v4.c.obj(".data.cy_DeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data..LPBX0"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data..LPBX1"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_ClearHibernateWakeupCause"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_ClearHibernateWakeupSource"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_Cm33IsDeepSleep"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_CoreBuckGetProfile"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_CoreBuckSetProfile"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_CoreBuckStatus"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_CpuEnterDeepSleep"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_CpuEnterSleep"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_DeepSleepIoIsFrozen"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_DeepSleepIoUnfreeze"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_DeepSleepSetup"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_DeepSleepUpdate"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_ExecuteCallback"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_GetAppDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_GetBootMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_GetDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_GetFailedCallback"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_GetHibernateWakeupCause"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_GetSOCMemSramPartActivePwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_GetSOCMemSramPartDsPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_GetSRAMMacroPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_GetSysDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_Init"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_IoUnfreeze"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_IsLpmReady"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_IsSystemHp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_IsSystemLp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_IsSystemUlp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_LdoSetMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_MiscLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_MiscLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_ReadStatus"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_RegisterCallback"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_RetLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_RetLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetAppDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetHibernateWakeupSource"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetPPUDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetRamTrimsPostDs"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetRamTrimsPreDs"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetSOCMEMDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetSOCMemPartActivePwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetSOCMemPartDsPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetSRAMMacroPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetSRAMPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetSysDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SetTrimRamCtl"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SramLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SramLdoEnable"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SramLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemEnterDsToHp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemEnterDsToLp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemEnterDsToUlp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemEnterHibernate"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemEnterHp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemEnterHpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemEnterLp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemEnterLpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemEnterUlp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemEnterUlpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemSetMinRegulatorCurrent"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemSetNormalRegulatorCurrent"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemTransitionHpToLp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemTransitionLpToHp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemTransitionLpToUlp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_SystemTransitionUlpToLp"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_TriggerSoftReset"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_TriggerXRes"))
                KEEP(*cy_syspm_v4.c.obj(".rel.data.__gcov_.Cy_SysPm_UnregisterCallback"))
                . = ALIGN(4);
 }
> ITCM AT > FLASH
        __itcm_data_reloc_end = .;
        __itcm_data_reloc_start = ADDR(.itcm_data_reloc);
        __itcm_data_reloc_size = __itcm_data_reloc_end - __itcm_data_reloc_start;
        __itcm_data_rom_start = LOADADDR(.itcm_data_reloc);
 .itcm_bss_reloc :
        {
                . = ALIGN(4);
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_AssertFailed"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_ClearDSRAMWarmBootEntryStatus"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_ClearResetReason"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_Delay"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_DelayUs"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_FaultHandler"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_GetDevice"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_GetDeviceLCS"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_GetDeviceRevision"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_GetResetReason"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_GetResetStatus"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_GetUniqueId"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_Halt"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_IsDSRAMWarmBootEntry"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_ProcessingFault"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_ResetBackupDomain"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_Rtos_Delay"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_Rtos_DelayUs"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_SysLib_SetWaitStates"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_Syslib_IsMemCacheable"))
                KEEP(*cy_syslib.c.obj(".bss.__gcov0.Cy_Syslib_SetWarmBootEntryPoint"))
                KEEP(*cy_syslib.c.obj(".bss.cy_WakeupFromWarmBootStatus"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_ClearHibernateWakeupCause"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_ClearHibernateWakeupSource"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_Cm33IsDeepSleep"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_CoreBuckGetProfile"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_CoreBuckSetProfile"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_CoreBuckStatus"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_CpuEnterDeepSleep"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_CpuEnterSleep"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_DeepSleepIoIsFrozen"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_DeepSleepIoUnfreeze"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_DeepSleepSetup"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_DeepSleepUpdate"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_ExecuteCallback"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_GetAppDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_GetBootMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_GetDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_GetFailedCallback"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_GetHibernateWakeupCause"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_GetSOCMemSramPartActivePwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_GetSOCMemSramPartDsPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_GetSRAMMacroPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_GetSysDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_Init"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_IoUnfreeze"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_IsLpmReady"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_IsSystemHp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_IsSystemLp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_IsSystemUlp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_LdoSetMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_MiscLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_MiscLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_ReadStatus"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_RegisterCallback"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_RetLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_RetLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetAppDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetHibernateWakeupSource"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetPPUDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetRamTrimsPostDs"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetRamTrimsPreDs"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetSOCMEMDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetSOCMemPartActivePwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetSOCMemPartDsPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetSRAMMacroPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetSRAMPwrMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetSysDeepSleepMode"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SetTrimRamCtl"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SramLdoConfigure"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SramLdoEnable"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SramLdoStatus"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemEnterDsToHp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemEnterDsToLp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemEnterDsToUlp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemEnterHibernate"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemEnterHp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemEnterHpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemEnterLp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemEnterLpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemEnterUlp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemEnterUlpToDs"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemSetMinRegulatorCurrent"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemSetNormalRegulatorCurrent"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemTransitionHpToLp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemTransitionLpToHp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemTransitionLpToUlp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_SystemTransitionUlpToLp"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_TriggerSoftReset"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_TriggerXRes"))
                KEEP(*cy_syspm_v4.c.obj(".bss.__gcov0.Cy_SysPm_UnregisterCallback"))
                KEEP(*cy_syspm_v4.c.obj(".bss.failedCallback"))
                KEEP(*cy_syspm_v4.c.obj(".bss.lastExecutedCallback.0"))
                KEEP(*cy_syspm_v4.c.obj(".bss.pmCallbackRoot"))
                . = ALIGN(4);
 } > ITCM
        __itcm_bss_reloc_end = .;
        __itcm_bss_reloc_start = ADDR(.itcm_bss_reloc);
        __itcm_bss_reloc_size = __itcm_bss_reloc_end - __itcm_bss_reloc_start;
    text :
 {
 __text_region_start = .;
 *(EXCLUDE_FILE(cy_syslib_ext.S.obj) .text)
 *(".text.*")
 *(".TEXT.*")
 *(.gnu.linkonce.t.*)
 *(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)
 . = ALIGN(4);
 } > FLASH
 __text_region_end = .;
 .ARM.exidx :
 {
 __exidx_start = .;
 *(.ARM.exidx* gnu.linkonce.armexidx.*)
 __exidx_end = .;
 } > FLASH
 __rodata_region_start = .;
 initlevel :
 {
  __init_start = .;
  __init_EARLY_start = .; KEEP(*(SORT(.z_init_EARLY_P_?_*))); KEEP(*(SORT(.z_init_EARLY_P_??_*))); KEEP(*(SORT(.z_init_EARLY_P_???_*)));
  __init_PRE_KERNEL_1_start = .; KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_?_*))); KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_??_*))); KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_???_*)));
  __init_PRE_KERNEL_2_start = .; KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_?_*))); KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_??_*))); KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_???_*)));
  __init_POST_KERNEL_start = .; KEEP(*(SORT(.z_init_POST_KERNEL_P_?_*))); KEEP(*(SORT(.z_init_POST_KERNEL_P_??_*))); KEEP(*(SORT(.z_init_POST_KERNEL_P_???_*)));
  __init_APPLICATION_start = .; KEEP(*(SORT(.z_init_APPLICATION_P_?_*))); KEEP(*(SORT(.z_init_APPLICATION_P_??_*))); KEEP(*(SORT(.z_init_APPLICATION_P_???_*)));
  __init_SMP_start = .; KEEP(*(SORT(.z_init_SMP_P_?_*))); KEEP(*(SORT(.z_init_SMP_P_??_*))); KEEP(*(SORT(.z_init_SMP_P_???_*)));
  __init_end = .;
 } > FLASH
 device_area : { _device_list_start = .; KEEP(*(SORT(._device.static.*_?_*))); KEEP(*(SORT(._device.static.*_??_*))); KEEP(*(SORT(._device.static.*_???_*))); KEEP(*(SORT(._device.static.*_????_*))); KEEP(*(SORT(._device.static.*_?????_*))); _device_list_end = .;; } > FLASH
 initlevel_error :
 {
  KEEP(*(SORT(.z_init_*)))
 }
 ASSERT(SIZEOF(initlevel_error) == 0, "Undefined initialization levels used.")
 app_shmem_regions : ALIGN_WITH_INPUT
 {
  __app_shmem_regions_start = .;
  KEEP(*(SORT(.app_regions.*)));
  __app_shmem_regions_end = .;
 } > FLASH
 k_p4wq_initparam_area : { _k_p4wq_initparam_list_start = .; KEEP(*(SORT_BY_NAME(._k_p4wq_initparam.static.*))); _k_p4wq_initparam_list_end = .;; } > FLASH
 k_kernel_init_pre_entry_area : { _k_kernel_init_pre_entry_list_start = .; KEEP(*(SORT_BY_NAME(._k_kernel_init_pre_entry.static.*))); _k_kernel_init_pre_entry_list_end = .;; } > FLASH
 k_kernel_init_post_entry_area : { _k_kernel_init_post_entry_list_start = .; KEEP(*(SORT_BY_NAME(._k_kernel_init_post_entry.static.*))); _k_kernel_init_post_entry_list_end = .;; } > FLASH
 _static_thread_data_area : { __static_thread_data_list_start = .; KEEP(*(SORT_BY_NAME(.__static_thread_data.static.*))); __static_thread_data_list_end = .;; } > FLASH
 device_deps : ALIGN_WITH_INPUT
 {
__device_deps_start = .;
KEEP(*(SORT(.__device_deps_pass2*)));
__device_deps_end = .;
 } > FLASH
device_api_area : SUBALIGN(4)
{
 _entropy_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._entropy_driver_api.static.*))); _entropy_driver_api_list_end = .;;
 _entropy_driver_api_ext_end = .;
 _gpio_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._gpio_driver_api.static.*))); _gpio_driver_api_list_end = .;;
 _gpio_driver_api_ext_end = .;
 _shared_irq_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._shared_irq_driver_api.static.*))); _shared_irq_driver_api_list_end = .;;
 _shared_irq_driver_api_ext_end = .;
 _audio_codec_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._audio_codec_driver_api.static.*))); _audio_codec_driver_api_list_end = .;;
 _audio_codec_driver_api_ext_end = .;
 _dmic_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._dmic_driver_api.static.*))); _dmic_driver_api_list_end = .;;
 _dmic_driver_api_ext_end = .;
 _crypto_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._crypto_driver_api.static.*))); _crypto_driver_api_list_end = .;;
 _crypto_driver_api_ext_end = .;
 _adc_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._adc_driver_api.static.*))); _adc_driver_api_list_end = .;;
 _adc_driver_api_ext_end = .;
 _auxdisplay_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._auxdisplay_driver_api.static.*))); _auxdisplay_driver_api_list_end = .;;
 _auxdisplay_driver_api_ext_end = .;
 _bbram_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._bbram_driver_api.static.*))); _bbram_driver_api_list_end = .;;
 _bbram_driver_api_ext_end = .;
 _biometric_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._biometric_driver_api.static.*))); _biometric_driver_api_list_end = .;;
 _biometric_driver_api_ext_end = .;
 _bt_hci_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._bt_hci_driver_api.static.*))); _bt_hci_driver_api_list_end = .;;
 _bt_hci_driver_api_ext_end = .;
 _buzzer_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._buzzer_driver_api.static.*))); _buzzer_driver_api_list_end = .;;
 _buzzer_driver_api_ext_end = .;
 _can_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._can_driver_api.static.*))); _can_driver_api_list_end = .;;
 _can_driver_api_ext_end = .;
 _cellular_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._cellular_driver_api.static.*))); _cellular_driver_api_list_end = .;;
 _cellular_driver_api_ext_end = .;
 _charger_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._charger_driver_api.static.*))); _charger_driver_api_list_end = .;;
 _charger_driver_api_ext_end = .;
 _clock_control_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._clock_control_driver_api.static.*))); _clock_control_driver_api_list_end = .;;
 _nrf_clock_control_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._nrf_clock_control_driver_api.static.*))); _nrf_clock_control_driver_api_list_end = .;;
 _nrf_clock_control_driver_api_ext_end = .;
 _clock_control_driver_api_ext_end = .;
 _clock_monitor_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._clock_monitor_driver_api.static.*))); _clock_monitor_driver_api_list_end = .;;
 _clock_monitor_driver_api_ext_end = .;
 _comparator_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._comparator_driver_api.static.*))); _comparator_driver_api_list_end = .;;
 _comparator_driver_api_ext_end = .;
 _coredump_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._coredump_driver_api.static.*))); _coredump_driver_api_list_end = .;;
 _coredump_driver_api_ext_end = .;
 _counter_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._counter_driver_api.static.*))); _counter_driver_api_list_end = .;;
 _counter_driver_api_ext_end = .;
 _crc_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._crc_driver_api.static.*))); _crc_driver_api_list_end = .;;
 _crc_driver_api_ext_end = .;
 _dac_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._dac_driver_api.static.*))); _dac_driver_api_list_end = .;;
 _dac_driver_api_ext_end = .;
 _dai_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._dai_driver_api.static.*))); _dai_driver_api_list_end = .;;
 _dai_driver_api_ext_end = .;
 _dali_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._dali_driver_api.static.*))); _dali_driver_api_list_end = .;;
 _dali_driver_api_ext_end = .;
 _display_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._display_driver_api.static.*))); _display_driver_api_list_end = .;;
 _display_driver_api_ext_end = .;
 _dma_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._dma_driver_api.static.*))); _dma_driver_api_list_end = .;;
 _dma_driver_api_ext_end = .;
 _edac_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._edac_driver_api.static.*))); _edac_driver_api_list_end = .;;
 _edac_driver_api_ext_end = .;
 _eeprom_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._eeprom_driver_api.static.*))); _eeprom_driver_api_list_end = .;;
 _eeprom_driver_api_ext_end = .;
 _emul_bbram_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._emul_bbram_driver_api.static.*))); _emul_bbram_driver_api_list_end = .;;
 _emul_bbram_driver_api_ext_end = .;
 _fuel_gauge_emul_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._fuel_gauge_emul_driver_api.static.*))); _fuel_gauge_emul_driver_api_list_end = .;;
 _fuel_gauge_emul_driver_api_ext_end = .;
 _emul_sensor_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._emul_sensor_driver_api.static.*))); _emul_sensor_driver_api_list_end = .;;
 _emul_sensor_driver_api_ext_end = .;
 _espi_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._espi_driver_api.static.*))); _espi_driver_api_list_end = .;;
 _emul_espi_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._emul_espi_driver_api.static.*))); _emul_espi_driver_api_list_end = .;;
 _emul_espi_driver_api_ext_end = .;
 _espi_driver_api_ext_end = .;
 _espi_saf_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._espi_saf_driver_api.static.*))); _espi_saf_driver_api_list_end = .;;
 _espi_saf_driver_api_ext_end = .;
 _flash_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._flash_driver_api.static.*))); _flash_driver_api_list_end = .;;
 _flash_driver_api_ext_end = .;
 _fpga_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._fpga_driver_api.static.*))); _fpga_driver_api_list_end = .;;
 _fpga_driver_api_ext_end = .;
 _fuel_gauge_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._fuel_gauge_driver_api.static.*))); _fuel_gauge_driver_api_list_end = .;;
 _fuel_gauge_driver_api_ext_end = .;
 _gnss_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._gnss_driver_api.static.*))); _gnss_driver_api_list_end = .;;
 _gnss_driver_api_ext_end = .;
 _haptics_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._haptics_driver_api.static.*))); _haptics_driver_api_list_end = .;;
 _haptics_driver_api_ext_end = .;
 _hwspinlock_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._hwspinlock_driver_api.static.*))); _hwspinlock_driver_api_list_end = .;;
 _hwspinlock_driver_api_ext_end = .;
 _i2c_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._i2c_driver_api.static.*))); _i2c_driver_api_list_end = .;;
 _i3c_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._i3c_driver_api.static.*))); _i3c_driver_api_list_end = .;;
 _i3c_driver_api_ext_end = .;
 _i2c_driver_api_ext_end = .;
 _i2c_target_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._i2c_target_driver_api.static.*))); _i2c_target_driver_api_list_end = .;;
 _i2c_target_driver_api_ext_end = .;
 _i2s_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._i2s_driver_api.static.*))); _i2s_driver_api_list_end = .;;
 _i2s_driver_api_ext_end = .;
 _ipm_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._ipm_driver_api.static.*))); _ipm_driver_api_list_end = .;;
 _ipm_driver_api_ext_end = .;
 _led_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._led_driver_api.static.*))); _led_driver_api_list_end = .;;
 _led_driver_api_ext_end = .;
 _led_strip_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._led_strip_driver_api.static.*))); _led_strip_driver_api_list_end = .;;
 _led_strip_driver_api_ext_end = .;
 _lora_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._lora_driver_api.static.*))); _lora_driver_api_list_end = .;;
 _lora_driver_api_ext_end = .;
 _mbox_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._mbox_driver_api.static.*))); _mbox_driver_api_list_end = .;;
 _mbox_driver_api_ext_end = .;
 _mdio_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._mdio_driver_api.static.*))); _mdio_driver_api_list_end = .;;
 _mdio_driver_api_ext_end = .;
 _memc_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._memc_driver_api.static.*))); _memc_driver_api_list_end = .;;
 _memc_driver_api_ext_end = .;
 _mipi_dbi_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._mipi_dbi_driver_api.static.*))); _mipi_dbi_driver_api_list_end = .;;
 _mipi_dbi_driver_api_ext_end = .;
 _mipi_dsi_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._mipi_dsi_driver_api.static.*))); _mipi_dsi_driver_api_list_end = .;;
 _mipi_dsi_driver_api_ext_end = .;
 _mspi_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._mspi_driver_api.static.*))); _mspi_driver_api_list_end = .;;
 _emul_mspi_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._emul_mspi_driver_api.static.*))); _emul_mspi_driver_api_list_end = .;;
 _emul_mspi_driver_api_ext_end = .;
 _mspi_driver_api_ext_end = .;
 _mux_control_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._mux_control_driver_api.static.*))); _mux_control_driver_api_list_end = .;;
 _mux_control_driver_api_ext_end = .;
 _opamp_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._opamp_driver_api.static.*))); _opamp_driver_api_list_end = .;;
 _opamp_driver_api_ext_end = .;
 _otp_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._otp_driver_api.static.*))); _otp_driver_api_list_end = .;;
 _otp_driver_api_ext_end = .;
 _peci_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._peci_driver_api.static.*))); _peci_driver_api_list_end = .;;
 _peci_driver_api_ext_end = .;
 _ps2_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._ps2_driver_api.static.*))); _ps2_driver_api_list_end = .;;
 _ps2_driver_api_ext_end = .;
 _ptp_clock_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._ptp_clock_driver_api.static.*))); _ptp_clock_driver_api_list_end = .;;
 _ptp_clock_driver_api_ext_end = .;
 _pulse_io_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._pulse_io_driver_api.static.*))); _pulse_io_driver_api_list_end = .;;
 _pulse_io_driver_api_ext_end = .;
 _pwm_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._pwm_driver_api.static.*))); _pwm_driver_api_list_end = .;;
 _pwm_driver_api_ext_end = .;
 _regulator_parent_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._regulator_parent_driver_api.static.*))); _regulator_parent_driver_api_list_end = .;;
 _regulator_parent_driver_api_ext_end = .;
 _regulator_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._regulator_driver_api.static.*))); _regulator_driver_api_list_end = .;;
 _regulator_driver_api_ext_end = .;
 _reset_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._reset_driver_api.static.*))); _reset_driver_api_list_end = .;;
 _reset_driver_api_ext_end = .;
 _retained_mem_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._retained_mem_driver_api.static.*))); _retained_mem_driver_api_list_end = .;;
 _retained_mem_driver_api_ext_end = .;
 _rtc_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._rtc_driver_api.static.*))); _rtc_driver_api_list_end = .;;
 _rtc_driver_api_ext_end = .;
 _sdhc_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._sdhc_driver_api.static.*))); _sdhc_driver_api_list_end = .;;
 _sdhc_driver_api_ext_end = .;
 _sensor_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._sensor_driver_api.static.*))); _sensor_driver_api_list_end = .;;
 _sensor_driver_api_ext_end = .;
 _smbus_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._smbus_driver_api.static.*))); _smbus_driver_api_list_end = .;;
 _smbus_driver_api_ext_end = .;
 _spi_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._spi_driver_api.static.*))); _spi_driver_api_list_end = .;;
 _spi_driver_api_ext_end = .;
 _syscon_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._syscon_driver_api.static.*))); _syscon_driver_api_list_end = .;;
 _syscon_driver_api_ext_end = .;
 _tee_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._tee_driver_api.static.*))); _tee_driver_api_list_end = .;;
 _tee_driver_api_ext_end = .;
 _tgpio_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._tgpio_driver_api.static.*))); _tgpio_driver_api_list_end = .;;
 _tgpio_driver_api_ext_end = .;
 _uaol_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._uaol_driver_api.static.*))); _uaol_driver_api_list_end = .;;
 _uaol_driver_api_ext_end = .;
 _video_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._video_driver_api.static.*))); _video_driver_api_list_end = .;;
 _video_driver_api_ext_end = .;
 _virtio_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._virtio_driver_api.static.*))); _virtio_driver_api_list_end = .;;
 _virtio_driver_api_ext_end = .;
 _w1_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._w1_driver_api.static.*))); _w1_driver_api_list_end = .;;
 _w1_driver_api_ext_end = .;
 _wdt_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._wdt_driver_api.static.*))); _wdt_driver_api_list_end = .;;
 _wdt_driver_api_ext_end = .;
 _wuc_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._wuc_driver_api.static.*))); _wuc_driver_api_list_end = .;;
 _wuc_driver_api_ext_end = .;
 _can_transceiver_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._can_transceiver_driver_api.static.*))); _can_transceiver_driver_api_list_end = .;;
 _can_transceiver_driver_api_ext_end = .;
 _i3c_target_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._i3c_target_driver_api.static.*))); _i3c_target_driver_api_list_end = .;;
 _i3c_target_driver_api_ext_end = .;
 _its_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._its_driver_api.static.*))); _its_driver_api_list_end = .;;
 _its_driver_api_ext_end = .;
 _vtd_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._vtd_driver_api.static.*))); _vtd_driver_api_list_end = .;;
 _vtd_driver_api_ext_end = .;
 _devmux_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._devmux_driver_api.static.*))); _devmux_driver_api_list_end = .;;
 _devmux_driver_api_ext_end = .;
 _renesas_elc_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._renesas_elc_driver_api.static.*))); _renesas_elc_driver_api_list_end = .;;
 _renesas_elc_driver_api_ext_end = .;
 _pcie_ctrl_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._pcie_ctrl_driver_api.static.*))); _pcie_ctrl_driver_api_list_end = .;;
 _pcie_ctrl_driver_api_ext_end = .;
 _pcie_ep_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._pcie_ep_driver_api.static.*))); _pcie_ep_driver_api_list_end = .;;
 _pcie_ep_driver_api_ext_end = .;
 _psi5_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._psi5_driver_api.static.*))); _psi5_driver_api_list_end = .;;
 _psi5_driver_api_ext_end = .;
 _sent_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._sent_driver_api.static.*))); _sent_driver_api_list_end = .;;
 _sent_driver_api_ext_end = .;
 _svc_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._svc_driver_api.static.*))); _svc_driver_api_list_end = .;;
 _svc_driver_api_ext_end = .;
 _stepper_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._stepper_driver_api.static.*))); _stepper_driver_api_list_end = .;;
 _stepper_driver_api_ext_end = .;
 _stepper_ctrl_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._stepper_ctrl_driver_api.static.*))); _stepper_ctrl_driver_api_list_end = .;;
 _stepper_ctrl_driver_api_ext_end = .;
 _uart_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._uart_driver_api.static.*))); _uart_driver_api_list_end = .;;
 _uart_driver_api_ext_end = .;
 _bc12_emul_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._bc12_emul_driver_api.static.*))); _bc12_emul_driver_api_list_end = .;;
 _bc12_emul_driver_api_ext_end = .;
 _uhc_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._uhc_driver_api.static.*))); _uhc_driver_api_list_end = .;;
 _uhc_driver_api_ext_end = .;
 _bc12_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._bc12_driver_api.static.*))); _bc12_driver_api_list_end = .;;
 _bc12_driver_api_ext_end = .;
 _usbc_ppc_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._usbc_ppc_driver_api.static.*))); _usbc_ppc_driver_api_list_end = .;;
 _usbc_ppc_driver_api_ext_end = .;
 _tcpc_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._tcpc_driver_api.static.*))); _tcpc_driver_api_list_end = .;;
 _tcpc_driver_api_ext_end = .;
 _usbc_vbus_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._usbc_vbus_driver_api.static.*))); _usbc_vbus_driver_api_list_end = .;;
 _usbc_vbus_driver_api_ext_end = .;
 _ivshmem_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._ivshmem_driver_api.static.*))); _ivshmem_driver_api_list_end = .;;
 _ivshmem_driver_api_ext_end = .;
 _ethphy_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._ethphy_driver_api.static.*))); _ethphy_driver_api_list_end = .;;
 _ethphy_driver_api_ext_end = .;
} > FLASH
ztest :
{
 _ztest_expected_result_entry_list_start = .; KEEP(*(SORT_BY_NAME(._ztest_expected_result_entry.static.*))); _ztest_expected_result_entry_list_end = .;;
 _ztest_suite_node_list_start = .; KEEP(*(SORT_BY_NAME(._ztest_suite_node.static.*))); _ztest_suite_node_list_end = .;;
 _ztest_unit_test_list_start = .; KEEP(*(SORT_BY_NAME(._ztest_unit_test.static.*))); _ztest_unit_test_list_end = .;;
 _ztest_param_inst_list_start = .; KEEP(*(SORT_BY_NAME(._ztest_param_inst.static.*))); _ztest_param_inst_list_end = .;;
 _ztest_test_rule_list_start = .; KEEP(*(SORT_BY_NAME(._ztest_test_rule.static.*))); _ztest_test_rule_list_end = .;;
} > FLASH
 init_array :
 {
  __zephyr_init_array_start = .;
  KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*)
   SORT_BY_INIT_PRIORITY(.ctors.*)))
  KEEP (*(.init_array .ctors))
  __zephyr_init_array_end = .;
 } > FLASH
 bt_l2cap_fixed_chan_area : { _bt_l2cap_fixed_chan_list_start = .; KEEP(*(SORT_BY_NAME(._bt_l2cap_fixed_chan.static.*))); _bt_l2cap_fixed_chan_list_end = .;; } > FLASH
 bt_gatt_service_static_area : { _bt_gatt_service_static_list_start = .; KEEP(*(SORT_BY_NAME(._bt_gatt_service_static.static.*))); _bt_gatt_service_static_list_end = .;; } > FLASH
 log_strings_area : { _log_strings_list_start = .; KEEP(*(SORT_BY_NAME(._log_strings.static.*))); _log_strings_list_end = .;; } > FLASH
 log_stmesp_ptr_area : { _log_stmesp_ptr_list_start = .; KEEP(*(SORT_BY_NAME(._log_stmesp_ptr.static.*))); _log_stmesp_ptr_list_end = .;; } > FLASH
 log_stmesp_str_area : { _log_stmesp_str_list_start = .; KEEP(*(SORT_BY_NAME(._log_stmesp_str.static.*))); _log_stmesp_str_list_end = .;; } > FLASH
 log_const_area : { _log_const_list_start = .; KEEP(*(SORT_BY_NAME(._log_const.static.*))); _log_const_list_end = .;; } > FLASH
 log_backend_area : { _log_backend_list_start = .; KEEP(*(SORT_BY_NAME(._log_backend.static.*))); _log_backend_list_end = .;; } > FLASH
 log_link_area : { _log_link_list_start = .; KEEP(*(SORT_BY_NAME(._log_link.static.*))); _log_link_list_end = .;; } > FLASH
 tracing_backend_area : { _tracing_backend_list_start = .; KEEP(*(SORT_BY_NAME(._tracing_backend.static.*))); _tracing_backend_list_end = .;; } > FLASH
 zephyr_dbg_info : ALIGN_WITH_INPUT
 {
  KEEP(*(".dbg_thread_info"));
 } > FLASH
 intc_table_area : { _intc_table_list_start = .; KEEP(*(SORT_BY_NAME(._intc_table.static.*))); _intc_table_list_end = .;; } > FLASH
 symbol_to_keep : ALIGN_WITH_INPUT
 {
  __symbol_to_keep_start = .;
  KEEP(*(SORT(.symbol_to_keep*)));
  __symbol_to_keep_end = .;
 } > FLASH
 shell_area : { _shell_list_start = .; KEEP(*(SORT_BY_NAME(._shell.static.*))); _shell_list_end = .;; } > FLASH
 shell_root_cmds_area : { _shell_root_cmds_list_start = .; KEEP(*(SORT_BY_NAME(._shell_root_cmds.static.*))); _shell_root_cmds_list_end = .;; } > FLASH
 shell_subcmds_area : { _shell_subcmds_list_start = .; KEEP(*(SORT_BY_NAME(._shell_subcmds.static.*))); _shell_subcmds_list_end = .;; } > FLASH
 shell_dynamic_subcmds_area : { _shell_dynamic_subcmds_list_start = .; KEEP(*(SORT_BY_NAME(._shell_dynamic_subcmds.static.*))); _shell_dynamic_subcmds_list_end = .;; } > FLASH
 shell_remote_area : { _shell_remote_list_start = .; KEEP(*(SORT_BY_NAME(._shell_remote.static.*))); _shell_remote_list_end = .;; } > FLASH
 cfb_font_area : { _cfb_font_list_start = .; KEEP(*(SORT_BY_NAME(._cfb_font.static.*))); _cfb_font_list_end = .;; } > FLASH
 clock_nodes :
 {
  _clk_list_start = .;
  _clk_root_list_start = .;
  *(SORT(.clk_node_root*))
  _clk_root_list_end = .;
  _clk_standard_list_start = .;
  *(SORT(.clk_node_standard*))
  _clk_standard_list_end = .;
  _clk_mux_list_start = .;
  *(SORT(.clk_node_mux*))
  _clk_mux_list_end = .;
  _clk_leaf_list_start = .;
  *(SORT(.clk_node_leaf*))
  _clk_leaf_list_end = .;
  _clk_list_end = .;
  _clk_output_36_list_start = .; *(SORT(.clock_output_36*)) _clk_output_36_list_end = .; _clk_output_21_list_start = .; *(SORT(.clock_output_21*)) _clk_output_21_list_end = .; _clk_output_27_list_start = .; *(SORT(.clock_output_27*)) _clk_output_27_list_end = .;
 } > FLASH
 tdata : ALIGN_WITH_INPUT
 {
  *(.tdata .tdata.* .gnu.linkonce.td.*);
 } > FLASH
 tbss (NOLOAD) : ALIGN_WITH_INPUT
 {
  *(.tbss .tbss.* .gnu.linkonce.tb.* .tcommon);
 } > FLASH
 PROVIDE(__tdata_start = LOADADDR(tdata));
 PROVIDE(__tdata_align = ALIGNOF(tdata));
 PROVIDE(__tdata_size = (SIZEOF(tdata) + __tdata_align - 1) & ~(__tdata_align - 1));
 PROVIDE(__tdata_end = __tdata_start + __tdata_size);
 PROVIDE(__tbss_align = ALIGNOF(tbss));
 PROVIDE(__tbss_start = ADDR(tbss));
 PROVIDE(__tbss_size = (SIZEOF(tbss) + __tbss_align - 1) & ~(__tbss_align - 1));
 PROVIDE(__tbss_end = __tbss_start + __tbss_size);
 PROVIDE(__tls_start = __tdata_start);
 PROVIDE(__tls_size = __tbss_end - __tdata_start);
 PROVIDE(__tls_end = __tbss_end);
    rodata :
 {
 *(.rodata)
 *(".rodata.*")
 *(.gnu.linkonce.r.*)
. = ALIGN(4);
PROVIDE_HIDDEN (__init_array_start = .);
KEEP (*(SORT(.init_array.*)))
KEEP (*(.init_array*))
PROVIDE_HIDDEN (__init_array_end = .);
 . = ALIGN(4);
 } > FLASH
 PROVIDE(__eh_frame_start = 0);
 PROVIDE(__eh_frame_end = 0);
 PROVIDE(__eh_frame_hdr_start = 0);
 PROVIDE(__eh_frame_hdr_end = 0);
 /DISCARD/ : { *(.eh_frame) }
 __rodata_region_end = .;
 . = ALIGN(_region_min_align);
 __rom_region_end = __rom_region_start + . - ADDR(rom_start);
   
    /DISCARD/ : {
 *(.got.plt)
 *(.igot.plt)
 *(.got)
 *(.igot)
 }
   
 . = (638582784);
 . = ALIGN(_region_min_align);
 _image_ram_start = .;
nocache (NOLOAD) : ALIGN_WITH_INPUT
{
 . = ALIGN(_region_min_align);
 _nocache_ram_start = .;
 _nocache_noload_ram_start = .;
 *(.nocache)
 *(".nocache.*")
 _nocache_noload_ram_end = .;
} > RAM AT > RAM
_nocache_noload_ram_size = _nocache_noload_ram_end - _nocache_noload_ram_start;
nocache_load : ALIGN_WITH_INPUT
{
 _nocache_load_ram_start = .;
 *(.nocache_load)
 *(".nocache_load.*")
 . = ALIGN(_region_min_align);
 _nocache_load_ram_end = .;
 _nocache_ram_end = .;
} > RAM AT > FLASH
_nocache_load_ram_size = _nocache_load_ram_end - _nocache_load_ram_start;
_nocache_load_rom_start = LOADADDR(nocache_load);
_nocache_ram_size = _nocache_ram_end - _nocache_ram_start;
.ramfunc : ALIGN_WITH_INPUT
{
 __ramfunc_region_start = .;
 . = ALIGN(_region_min_align);
 __ramfunc_start = .;
 *(.ramfunc)
 *(".ramfunc.*")
 . = ALIGN(_region_min_align);
 __ramfunc_end = .;
} > RAM AT > FLASH
__ramfunc_size = __ramfunc_end - __ramfunc_start;
__ramfunc_load_start = LOADADDR(.ramfunc);
_GCOV_BSS_SECTION_NAME (NOLOAD) : ALIGN_WITH_INPUT
{
 . = ALIGN(_region_min_align);
 __gcov_bss_start = .;
 KEEP(*(".bss.__gcov0.*"));
 . = ALIGN(_region_min_align);
 __gcov_bss_end = .;
} > RAM AT > RAM
__gcov_bss_num_words = ((__gcov_bss_end - __gcov_bss_start) >> 2);
__gcov_bss_size = __gcov_bss_end - __gcov_bss_start;
   
    datas : ALIGN_WITH_INPUT
 {
 __data_region_start = .;
 __data_start = .;
 *(.data)
 *(".data.*")
 *(".kernel.*")
KEEP(*(.cy_socmem_data))
 __data_end = .;
 } > RAM AT > FLASH
    __data_size = __data_end - __data_start;
    __data_load_start = LOADADDR(datas);
    __data_region_load_start = LOADADDR(datas);
 sw_isr_table : ALIGN_WITH_INPUT
 {
  . = ALIGN(4);
  *(.gnu.linkonce.sw_isr_table*)
 } > RAM AT > FLASH
        device_states : ALIGN_WITH_INPUT
        {
  . = ALIGN(4);
                __device_states_start = .;
  KEEP(*(".z_devstate"));
  KEEP(*(".z_devstate.*"));
                __device_states_end = .;
  . = ALIGN(4);
        } > RAM AT > FLASH
 log_mpsc_pbuf_area : ALIGN_WITH_INPUT { _log_mpsc_pbuf_list_start = .; *(SORT_BY_NAME(._log_mpsc_pbuf.static.*)); _log_mpsc_pbuf_list_end = .;; } > RAM AT > FLASH
 log_msg_ptr_area : ALIGN_WITH_INPUT { _log_msg_ptr_list_start = .; KEEP(*(SORT_BY_NAME(._log_msg_ptr.static.*))); _log_msg_ptr_list_end = .;; } > RAM AT > FLASH
 log_dynamic_area : ALIGN_WITH_INPUT { _log_dynamic_list_start = .; KEEP(*(SORT_BY_NAME(._log_dynamic.static.*))); _log_dynamic_list_end = .;; } > RAM AT > FLASH
 k_timer_area : ALIGN_WITH_INPUT { _k_timer_list_start = .; *(SORT_BY_NAME(._k_timer.static.*)); _k_timer_list_end = .;; } > RAM AT > FLASH
 k_mem_slab_area : ALIGN_WITH_INPUT { _k_mem_slab_list_start = .; *(SORT_BY_NAME(._k_mem_slab.static.*)); _k_mem_slab_list_end = .;; } > RAM AT > FLASH
 k_heap_area : ALIGN_WITH_INPUT { _k_heap_list_start = .; *(SORT_BY_NAME(._k_heap.static.*)); _k_heap_list_end = .;; } > RAM AT > FLASH
 k_mutex_area : ALIGN_WITH_INPUT { _k_mutex_list_start = .; *(SORT_BY_NAME(._k_mutex.static.*)); _k_mutex_list_end = .;; } > RAM AT > FLASH
 k_stack_area : ALIGN_WITH_INPUT { _k_stack_list_start = .; *(SORT_BY_NAME(._k_stack.static.*)); _k_stack_list_end = .;; } > RAM AT > FLASH
 k_msgq_area : ALIGN_WITH_INPUT { _k_msgq_list_start = .; *(SORT_BY_NAME(._k_msgq.static.*)); _k_msgq_list_end = .;; } > RAM AT > FLASH
 k_mbox_area : ALIGN_WITH_INPUT { _k_mbox_list_start = .; *(SORT_BY_NAME(._k_mbox.static.*)); _k_mbox_list_end = .;; } > RAM AT > FLASH
 k_pipe_area : ALIGN_WITH_INPUT { _k_pipe_list_start = .; *(SORT_BY_NAME(._k_pipe.static.*)); _k_pipe_list_end = .;; } > RAM AT > FLASH
 k_sem_area : ALIGN_WITH_INPUT { _k_sem_list_start = .; *(SORT_BY_NAME(._k_sem.static.*)); _k_sem_list_end = .;; } > RAM AT > FLASH
 k_event_area : ALIGN_WITH_INPUT { _k_event_list_start = .; *(SORT_BY_NAME(._k_event.static.*)); _k_event_list_end = .;; } > RAM AT > FLASH
 k_queue_area : ALIGN_WITH_INPUT { _k_queue_list_start = .; *(SORT_BY_NAME(._k_queue.static.*)); _k_queue_list_end = .;; } > RAM AT > FLASH
 k_fifo_area : ALIGN_WITH_INPUT { _k_fifo_list_start = .; *(SORT_BY_NAME(._k_fifo.static.*)); _k_fifo_list_end = .;; } > RAM AT > FLASH
 k_lifo_area : ALIGN_WITH_INPUT { _k_lifo_list_start = .; *(SORT_BY_NAME(._k_lifo.static.*)); _k_lifo_list_end = .;; } > RAM AT > FLASH
 k_condvar_area : ALIGN_WITH_INPUT { _k_condvar_list_start = .; *(SORT_BY_NAME(._k_condvar.static.*)); _k_condvar_list_end = .;; } > RAM AT > FLASH
 sys_mem_blocks_ptr_area : ALIGN_WITH_INPUT { _sys_mem_blocks_ptr_list_start = .; *(SORT_BY_NAME(._sys_mem_blocks_ptr.static.*)); _sys_mem_blocks_ptr_list_end = .;; } > RAM AT > FLASH
 net_buf_pool_area : ALIGN_WITH_INPUT { _net_buf_pool_list_start = .; KEEP(*(SORT_BY_NAME(._net_buf_pool.static.*))); _net_buf_pool_list_end = .;; } > RAM AT > FLASH
    __data_region_end = .;

 .itcm : SUBALIGN(4)
 {
  __itcm_start = .;
  *(.itcm)
  *(".itcm.*")
KEEP(*(.cy_ramfunc))
KEEP(*(.cy_itcm))
*cy_syslib_ext.S.obj(.text*)
  __itcm_end = .;
 } > ITCM AT> FLASH
 __itcm_size = __itcm_end - __itcm_start;
 __itcm_load_start = LOADADDR(.itcm);


 .dtcm_bss (NOLOAD) : SUBALIGN(4)
 {
  __dtcm_start = .;
  __dtcm_bss_start = .;
  *(.dtcm_bss)
  *(".dtcm_bss.*")
  __dtcm_bss_end = .;
 } > DTCM
 .dtcm_noinit (NOLOAD) : SUBALIGN(4)
 {
  __dtcm_noinit_start = .;
  *(.dtcm_noinit)
  *(".dtcm_noinit.*")
  __dtcm_noinit_end = .;
 } > DTCM
 .dtcm_data : SUBALIGN(4)
 {
  __dtcm_data_start = .;
  *(.dtcm_data)
  *(".dtcm_data.*")
  __dtcm_data_end = .;
 } > DTCM AT> FLASH
 __dtcm_end = .;
 __dtcm_data_load_start = LOADADDR(.dtcm_data);


 .cy_sharedmem (NOLOAD) :
 {
  . = ALIGN(4);
  __public_ram_start__ = .;
  KEEP(*(.cy_sharedmem))
  . = ALIGN(4);
  __public_ram_end__ = .;
 } > M55SHARED

.intList :
{
 KEEP(*(.irq_info*))
 KEEP(*(.intList*))
} > IDT_LIST
 .stab 0 : { *(.stab) }
 .stabstr 0 : { *(.stabstr) }
 .stab.excl 0 : { *(.stab.excl) }
 .stab.exclstr 0 : { *(.stab.exclstr) }
 .stab.index 0 : { *(.stab.index) }
 .stab.indexstr 0 : { *(.stab.indexstr) }
 .gnu.build.attributes 0 : { *(.gnu.build.attributes .gnu.build.attributes.*) }
 .comment 0 : { *(.comment) }
 .debug 0 : { *(.debug) }
 .line 0 : { *(.line) }
 .debug_srcinfo 0 : { *(.debug_srcinfo) }
 .debug_sfnames 0 : { *(.debug_sfnames) }
 .debug_aranges 0 : { *(.debug_aranges) }
 .debug_pubnames 0 : { *(.debug_pubnames) }
 .debug_info 0 : { *(.debug_info .gnu.linkonce.wi.*) }
 .debug_abbrev 0 : { *(.debug_abbrev) }
 .debug_line 0 : { *(.debug_line .debug_line.* .debug_line_end ) }
 .debug_frame 0 : { *(.debug_frame) }
 .debug_str 0 : { *(.debug_str) }
 .debug_loc 0 : { *(.debug_loc) }
 .debug_macinfo 0 : { *(.debug_macinfo) }
 .debug_weaknames 0 : { *(.debug_weaknames) }
 .debug_funcnames 0 : { *(.debug_funcnames) }
 .debug_typenames 0 : { *(.debug_typenames) }
 .debug_varnames 0 : { *(.debug_varnames) }
 .debug_pubtypes 0 : { *(.debug_pubtypes) }
 .debug_ranges 0 : { *(.debug_ranges) }
 .debug_addr 0 : { *(.debug_addr) }
 .debug_line_str 0 : { *(.debug_line_str) }
 .debug_loclists 0 : { *(.debug_loclists) }
 .debug_macro 0 : { *(.debug_macro) }
 .debug_names 0 : { *(.debug_names) }
 .debug_rnglists 0 : { *(.debug_rnglists) }
 .debug_str_offsets 0 : { *(.debug_str_offsets) }
 .debug_sup 0 : { *(.debug_sup) }
    /DISCARD/ : { *(.note.GNU-stack) }
    .ARM.attributes 0 :
 {
 KEEP(*(.ARM.attributes))
 KEEP(*(.gnu.attributes))
 }
    DTCM (NOLOAD) : { __DTCM_start = .; KEEP(*(DTCM)) KEEP(*(DTCM.*)) __DTCM_end = .; } > DTCM __DTCM_size = __DTCM_end - __DTCM_start; __DTCM_load_start = LOADADDR(DTCM); ITCM (NOLOAD) : { __ITCM_start = .; KEEP(*(ITCM)) KEEP(*(ITCM.*)) __ITCM_end = .; } > ITCM __ITCM_size = __ITCM_end - __ITCM_start; __ITCM_load_start = LOADADDR(ITCM); M33SCODE (NOLOAD) : { __M33SCODE_start = .; KEEP(*(M33SCODE)) KEEP(*(M33SCODE.*)) __M33SCODE_end = .; } > M33SCODE __M33SCODE_size = __M33SCODE_end - __M33SCODE_start; __M33SCODE_load_start = LOADADDR(M33SCODE); M33CODE (NOLOAD) : { __M33CODE_start = .; KEEP(*(M33CODE)) KEEP(*(M33CODE.*)) __M33CODE_end = .; } > M33CODE __M33CODE_size = __M33CODE_end - __M33CODE_start; __M33CODE_load_start = LOADADDR(M33CODE); SECSHARED (NOLOAD) : { __SECSHARED_start = .; KEEP(*(SECSHARED)) KEEP(*(SECSHARED.*)) __SECSHARED_end = .; } > SECSHARED __SECSHARED_size = __SECSHARED_end - __SECSHARED_start; __SECSHARED_load_start = LOADADDR(SECSHARED); M33SHARED (NOLOAD) : { __M33SHARED_start = .; KEEP(*(M33SHARED)) KEEP(*(M33SHARED.*)) __M33SHARED_end = .; } > M33SHARED __M33SHARED_size = __M33SHARED_end - __M33SHARED_start; __M33SHARED_load_start = LOADADDR(M33SHARED); M55SHARED (NOLOAD) : { __M55SHARED_start = .; KEEP(*(M55SHARED)) KEEP(*(M55SHARED.*)) __M55SHARED_end = .; } > M55SHARED __M55SHARED_size = __M55SHARED_end - __M55SHARED_start; __M55SHARED_load_start = LOADADDR(M55SHARED);
.last_section :
{
  KEEP(*(.last_section))
} > FLASH
_flash_used = LOADADDR(.last_section) + SIZEOF(.last_section) - __rom_region_start;
    bss (NOLOAD) : ALIGN_WITH_INPUT
 {
        . = ALIGN(4);
 __bss_start = .;
 __kernel_ram_start = .;
 *(.bss)
 *(".bss.*")
 *(COMMON)
 *(".kernel_bss.*")
 __bss_end = ALIGN(4);
 } > RAM AT > RAM
noinit (NOLOAD) :
{
        *(.noinit)
        *(".noinit.*")
} > RAM AT > RAM
    __kernel_ram_end = (638582784) + (262144);
    __kernel_ram_size = __kernel_ram_end - __kernel_ram_start;
    .last_ram_section (NOLOAD) :
    {
 _image_ram_end = .;
 _image_ram_size = _image_ram_end - _image_ram_start;
 _end = .;
 z_mapped_end = .;
    } > RAM AT > RAM
   
    }
