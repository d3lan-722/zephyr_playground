

/* Linker section for memory region M33SCODE for TEXT section  */

	SECTION_PROLOGUE(.m33scode_text_reloc,,)
        {
                . = ALIGN(4);
                
                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_AssertFailed"))

                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_ClearDSRAMWarmBootEntryStatus"))

                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_Delay"))

                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_DelayUs"))

                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_FaultHandler"))

                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_IsDSRAMWarmBootEntry"))

                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_Rtos_Delay"))

                KEEP(*cy_syslib.c.obj(".rel.text.Cy_SysLib_Rtos_DelayUs"))

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

                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_GetUniqueId"))

                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_Halt"))

                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_IsDSRAMWarmBootEntry"))

                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_ProcessingFault"))

                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_ResetBackupDomain"))

                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_Rtos_Delay"))

                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_Rtos_DelayUs"))

                KEEP(*cy_syslib.c.obj(".text.Cy_SysLib_SetWaitStates"))

                KEEP(*cy_syslib.c.obj(".text.Cy_Syslib_SetWarmBootEntryPoint"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_CoreBuckStatus"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_CpuEnterDeepSleep"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_CpuEnterSleep"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_DeepSleepIoUnfreeze"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_DeepSleepUpdate"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_ExecuteCallback"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetAppDeepSleepMode"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetBootMode"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetDeepSleepMode"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetFailedCallback"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_GetSysDeepSleepMode"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_Init"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_IoUnfreeze"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_LdoSetMode"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_MiscLdoConfigure"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_MiscLdoStatus"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_RegisterCallback"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_RetLdoConfigure"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_RetLdoStatus"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetAppDeepSleepMode"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetDeepSleepMode"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetPPUDeepSleepMode"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetRamTrimsPostDs"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetRamTrimsPreDs"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetSOCMEMDeepSleepMode"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetSRAMPwrMode"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SetSysDeepSleepMode"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SramLdoConfigure"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SramLdoStatus"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterHibernate"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterHp"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterLp"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemEnterUlp"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemSetMinRegulatorCurrent"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemSetNormalRegulatorCurrent"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemTransitionHpToLp"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemTransitionLpToHp"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemTransitionLpToUlp"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_SystemTransitionUlpToLp"))

                KEEP(*cy_syspm_v4.c.obj(".rel.text.Cy_SysPm_UnregisterCallback"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_ClearHibernateWakeupSource"))

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

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SramLdoConfigure"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SramLdoStatus"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterHibernate"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterHp"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterLp"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemEnterUlp"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemSetMinRegulatorCurrent"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemSetNormalRegulatorCurrent"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemTransitionHpToLp"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemTransitionLpToHp"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemTransitionLpToUlp"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_SystemTransitionUlpToLp"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_TriggerSoftReset"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_TriggerXRes"))

                KEEP(*cy_syspm_v4.c.obj(".text.Cy_SysPm_UnregisterCallback"))

                . = ALIGN(4);
	} 
#ifdef CONFIG_XIP
GROUP_DATA_LINK_IN(M33SCODE , ROMABLE_REGION)
#else
GROUP_DATA_LINK_IN(M33SCODE , M33SCODE )
#endif

        __m33scode_text_reloc_end = .;
        __m33scode_text_reloc_start = ADDR(.m33scode_text_reloc);
        __m33scode_text_reloc_size = __m33scode_text_reloc_end - __m33scode_text_reloc_start;

        __m33scode_text_rom_start = LOADADDR(.m33scode_text_reloc);


/* Linker section for memory region M33SCODE for RODATA section  */

	SECTION_PROLOGUE(.m33scode_rodata_reloc,,)
        {
                __m33scode_rodata_reloc_start = .;
                
                KEEP(*cy_syspm_v4.c.obj(".rodata.CSWTCH.45"))

#if 0
                . = ALIGN(0);
#else
                MPU_ALIGN(__m33scode_rodata_reloc_size);
#endif
                __m33scode_rodata_reloc_end = .;
	} 
#ifdef CONFIG_XIP
GROUP_DATA_LINK_IN(M33SCODE , ROMABLE_REGION)
#else
GROUP_DATA_LINK_IN(M33SCODE , M33SCODE )
#endif

        __m33scode_rodata_reloc_size = __m33scode_rodata_reloc_end - __m33scode_rodata_reloc_start;

        __m33scode_rodata_rom_start = LOADADDR(.m33scode_rodata_reloc);


/* Linker section for memory region M33SCODE for BSS section  */

	SECTION_PROLOGUE(.m33scode_bss_reloc,,)
        {
                . = ALIGN(4);
                
                KEEP(*cy_syslib.c.obj(".bss.cy_WakeupFromWarmBootStatus"))

                KEEP(*cy_syspm_v4.c.obj(".bss.failedCallback"))

                KEEP(*cy_syspm_v4.c.obj(".bss.lastExecutedCallback.0"))

                KEEP(*cy_syspm_v4.c.obj(".bss.pmCallbackRoot"))

                . = ALIGN(4);
	} GROUP_LINK_IN(M33SCODE )
        __m33scode_bss_reloc_end = .;
        __m33scode_bss_reloc_start = ADDR(.m33scode_bss_reloc);
        __m33scode_bss_reloc_size = __m33scode_bss_reloc_end - __m33scode_bss_reloc_start;
