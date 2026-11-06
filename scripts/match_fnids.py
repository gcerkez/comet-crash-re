#!/usr/bin/env python3
"""Match Comet Crash's import FNID hashes to PS3 SDK function names.

The ELF's .lib.stub records give, per library, a list of 32-bit FNIDs
(SHA-1 based name hashes). A PS3 FNID is:

    digest = SHA1(utf8(name) + NID_SUFFIX)
    fnid   = first 4 digest bytes, byte-reversed (little-endian read)

with the fixed 16-byte suffix below (documented on the PS3 Dev Wiki's
NID page, with a worked example: FNID('_sys_sprintf') = 0xA1F9EAFE —
this script asserts that example before trusting itself).

Candidate names are the public PS3 SDK / PSL1GHT API surface for each
library. A candidate whose computed hash equals a stub hash identifies
that import: SHA-1 collisions across a few hundred candidates are not a
practical concern, and simultaneous multi-function matches in one
library confirm the method.

Usage: match_fnids.py <EBOOT.ELF> <out imports.tsv>
"""

import hashlib
import os
import struct
import sys

NID_SUFFIX = bytes(
    [0x67, 0x59, 0x65, 0x99, 0x04, 0x25, 0x04, 0x90,
     0x56, 0x64, 0x27, 0x49, 0x94, 0x89, 0x74, 0x1A]
)


def fnid(name: str) -> int:
    digest = hashlib.sha1(name.encode("utf-8") + NID_SUFFIX).digest()
    return int.from_bytes(digest[:4], "little")


assert fnid("_sys_sprintf") == 0xA1F9EAFE, "FNID self-test failed"

# ---------------------------------------------------------------------------
# Candidate SDK function names per library. Broad on purpose: extra names
# cost nothing, missing names are the only failure mode.
# ---------------------------------------------------------------------------
CANDIDATES = {
    "cellAudio": """
        cellAudioInit cellAudioQuit cellAudioAddData cellAudioAdd2chData
        cellAudioAdd6chData cellAudioGetPortConfig cellAudioPortOpen
        cellAudioPortClose cellAudioPortStart cellAudioPortStop
        cellAudioPortWrite cellAudioGetPortTimestamp cellAudioGetPortStatus
        cellAudioSetPortLevel cellAudioCreateNotifyEventQueue
        cellAudioSetNotifyEventQueue cellAudioRemoveNotifyEventQueue
        cellAudioOutConfigure cellAudioOutGetState cellAudioOutSetCopyControl
        cellAudioOutSetVolumeDevice cellAudioGetDeviceList cellAudioSetDevice
    """,
    "cellSpurs": """
        cellSpursInitialize cellSpursFinalize cellSpursGetSpuThreadGroupId
        cellSpursCreateTaskset cellSpursDestroyTaskset cellSpursCreateTask
        cellSpursJoinTask cellSpursYieldTask cellSpursExitTask
        cellSpursShutdownTaskset cellSpursWakeupTaskset
        cellSpursAttachLv2EventQueue cellSpursDetachLv2EventQueue
        cellSpursSetGlobalExceptionEventHandler
        cellSpursSetExceptionEventHandler cellSpursGetTaskInfo
        cellSpursGetTasksetInfo cellSpursSetTaskPriority
        cellSpursEventFlagInitialize cellSpursEventFlagAttachLv2EventQueue
        cellSpursEventFlagWait cellSpursEventFlagSet cellSpursEventFlagClear
        cellSpursSemaphoreInitialize cellSpursSemaphoreP cellSpursSemaphoreV
        cellSpursQueueInitialize cellSpursQueuePushBody cellSpursQueuePopBody
        cellSpursBarrierInitialize cellSpursBarrierWait
        cellSpursLFQueueInitialize cellSpursLFQueuePushBody
        cellSpursLFQueuePopBody cellSpursCreateTasksetWithAttribute
        cellSpursCreateTaskWithAttribute cellSpursGetNumSpuThreads
        cellSpursGetSpuStatus cellSpursSetSpuStatus cellSpursRequestShutdown
        cellSpursWaitTasksetSignal cellSpursSignalTaskset
        cellSpursGetTaskList cellSpursGetTasksetList cellSpursSendSignal
        cellSpursReceiveSignal _cellSpursInitialize _cellSpursFinalize
        cellSpursInitializeWithAttribute cellSpursTasksetCreateJobChain
        cellSpursDestroyJobChain cellSpursRunJobChain cellSpursJoinJobChain
     _cellSpursCreateTaskset _cellSpursDestroyTaskset _cellSpursJoinTask _cellSpursExitTask cellSpursCreateTaskset2 cellSpursGetTasksetAddress cellSpursCreateTasksetOnJobChain cellSpursBarrierGetTasksetAddress cellSpursEventFlagGetTasksetAddress cellSpursQueueGetTasksetAddress cellSpursSemaphoreGetTasksetAddress cellSpursLFQueueGetTasksetAddress cellSpursGetAlignmentExceptionStatus cellSpursSetPrepareJoinTaskMode cellSpursGetReadyCount cellSpursSetReadyCount""",
    "cellSysmodule": """
        cellSysmoduleLoadModule cellSysmoduleUnloadModule
        cellSysmoduleIsLoaded cellSysmoduleInitialize
        cellSysmoduleLoadModuleDemo cellSysmoduleUnloadModuleDemo
     cellSysmoduleIsLoadedDemo""",
    "cellGcmSys": """
        cellGcmInit cellGcmTerm cellGcmSetDefaultFifoSize
        cellGcmGetConfiguration cellGcmSetDisplayMode cellGcmSetBuffer
        cellGcmGetFlipStatus cellGcmResetFlipStatus cellGcmSetFlipMode
        cellGcmSetFlip cellGcmSetSecondFlip cellGcmFlush cellGcmFinish
        cellGcmGetLabelAddress cellGcmSetWaitForLabel cellGcmGetTimeStamp
        cellGcmSetTimeStamp cellGcmSetTimeStampLocation
        cellGcmGetControlRegister cellGcmMapMainMemory
        cellGcmMapEaIoAddress cellGcmMapLocalMemory cellGcmGetMaxIoMapSize
        cellGcmUnmapEaIoAddress cellGcmUnmapLocalMemory
        cellGcmReserveIoMapSize cellGcmUnreserveIoMapSize cellGcmGetIoAddress
        cellGcmAllocateLocalMemory cellGcmFreeLocalMemory
        cellGcmSetInvalidateTile cellGcmSetTileInfo cellGcmBindTile
        cellGcmUnbindTile cellGcmSetZcull cellGcmGetTiledPitchSize
        cellGcmSetSecondVHandler cellGcmSetVBlankHandler
        cellGcmSetGraphicsHandler cellGcmSysGetLastSecondVHandler
        cellGcmSetFlipHandler cellGcmSetQueueHandler cellGcmSetDebugHandler
        cellGcmGetDisplayInfo cellGcmGetCurrentBuffer
        cellGcmGetCurrentField cellGcmGetTileInfo cellGcmGetZcullInfo
        cellGcmSetUserHandler cellGcmRecoverFromError
        cellGcmSetPrepareFlip cellGcmGetFlipTime cellGcmGetLastFlipTime
        cellGcmGetLastSecondFlipTime cellGcmSetTileCompression
        cellGcmMMapLocalMemory cellGcmAddressToOffset cellGcmIoOffsetToAddress
        cellGcmGetOffsetSize cellGcmSortRemapEaIoAddress
        cellGcmSetDefaultCommandBufferSize cellGcmGetDriverInfo
     cellGcmSetFlipImmediate cellGcmSetSecondFlipImmediate cellGcmSetVBlankFrequency cellGcmInitBody cellGcmGetControlRegisterAddress cellGcmGetTimeStampLocation cellGcmMapEaIoAddressWithFlags cellGcmGetDisplayInfoByIndex cellGcmSetDefaultFifoSizeEx cellGcmSetRenderTarget cellGcmGetFlipCount cellGcmGetSecondFlipCount cellGcmSetUserCommandHandler cellGcmSetPerfMonEnabled _cellGcmInit _cellGcmTerm cellGcmSetTileCompressionByIndex cellGcmGetTimeStampMid""",
    "cellSysutil": """
        cellSysutilRegisterCallback cellSysutilUnregisterCallback
        cellSysutilCheckCallback cellSysutilGetSystemParamInt
        cellSysutilGetSystemParamString cellSysutilGetBgmPlaybackStatus
        cellSysutilEnableBgmPlayback cellSysutilDisableBgmPlayback
        cellSysutilSetBgmPlaybackExtraParam cellSysutilGetHomeDirectory
        cellSysutilGameBootInvite cellSysutilGameDataOnStore
        cellSysutilGameGetInvalidUser cellSysutilDrawCustomData
        cellMsgDialogOpen cellMsgDialogOpen2 cellMsgDialogClose
        cellMsgDialogAbort cellMsgDialogProgressBarInc
        cellMsgDialogProgressBarSetMsg cellMsgDialogProgressBarReset
        cellMsgDialogDisableSystemButtonCheck cellSaveDataListSave
        cellSaveDataFixedSave cellSaveDataAutoSave cellSaveDataListLoad
        cellSaveDataFixedLoad cellSaveDataAutoLoad cellSaveDataDelete
        cellSaveDataBufSave cellSaveDataBufLoad cellSaveDataGetListItem
        cellSaveDataSetListItem cellSaveDataGetStat cellSaveDataSetStat
        cellSaveDataSetCallback cellSaveDataGetCallback
        cellSaveDataEnableOverlay cellOskDialogLoadAsync
        cellOskDialogUnloadAsync cellOskDialogGetInputText
        cellOskDialogSetInitialInputDevice cellOskDialogSetKeyLayoutOption
        cellOskDialogSetLayoutMode cellOskDialogGetSize
        cellOskDialogSetSeparateWindowOption cellSysutilAvcInit
        cellSysutilAvcTerm cellSysutilAvcSetVoiceChatProperty
        cellSysutilAvcStartVoiceChat cellSysutilAvcStopVoiceChat
        cellSysutilAvcJoinVoiceChat cellSysutilAvcLeaveVoiceChat
        cellSysutilAvcMuteVoiceChat cellSysutilAvcUnmuteVoiceChat
        cellSysutilPhotoImport cellSysutilPhotoExport
        cellSysutilMusicImport cellSysutilMusicExport
        cellSysutilVideoImport cellSysutilVideoExport cellSysutilPrintInit
        cellSysutilPrintOpen cellSysutilPrintClose cellSysutilApInit
        cellSysutilGetSystemParamGameData cellSysutilCheckXpMode
     cellSaveDataListSave2 cellSaveDataFixedSave2 cellSaveDataAutoSave2 cellSaveDataListLoad2 cellSaveDataFixedLoad2 cellSaveDataAutoLoad2 cellSaveDataUserListSave cellSaveDataUserFixedSave cellSaveDataUserAutoSave cellSaveDataUserListLoad cellSaveDataUserFixedLoad cellSaveDataUserAutoLoad cellSaveDataUserDelete cellMsgDialogOpenErrorCode cellSysutilGetSystemParamIntForApp cellSysutilSetSystemParamInt cellSysutilSetSystemParamString cellSysutilGetChannelSize cellSysutilNpGetInvitationSelectedData cellSysutilAvcGetPlayersVoiceData2""",
    "cellResc": """
        cellRescInit cellRescExit cellRescSetDsts cellRescSetDisplayMode
        cellRescSetBufferAddress cellRescSetFlipHandler
        cellRescSetVBlankHandler cellRescGcmSurface2RescSrc
        cellRescSetWaitFlip cellRescFlip cellRescResetFlipStatus
        cellRescGetFlipStatus cellRescGetNumColorBuffers
        cellRescGetBufferSize cellRescSetInterval cellRescGetLastRsxFlipTime
        cellRescAdjustAspectRatio cellRescSetPalInterpolate
        cellRescCreateInterlaceTable cellRescSetInterlaceTable
        cellRescGetInterlaceTable cellRescVideoOutResolutionId2RescBufferMode
        cellRescGetDisplayInfoFromResolutionId cellRescSetSrcFip
        cellRescSetSrcFlip cellRescSetConvertAndFlip cellRescGetRegisterCount
        cellRescSetRegisterCount cellRescGetDsts
     cellRescGetDstsByResolutionId cellRescSetVideoOutInfo2RescDsts cellRescSetGraphicsParam cellRescGetGraphicsParam""",
    "sys_io": """
        ioPadInit ioPadEnd ioPadGetInfo ioPadGetData ioPadGetDataExtra
        ioPadInfoPressMode ioPadSetPressMode ioPadSetSensorMode
        ioPadSetActDirect ioPadGetCapability ioPadPeriphGetInfo
        ioPadPeriphGetData ioPadSetPort ioPadGetRawData ioPadClearBuf
        ioKbInit ioKbEnd ioKbGetInfo ioKbRead ioKbSetCodeType
        ioKbSetLEDStatus ioKbGetConfiguration ioKbAssignDevice
        ioKbSetReadMode ioKbConnect ioKbGetControlState
        ioMouseInit ioMouseEnd ioMouseGetInfo ioMouseGetData
        ioMouseGetTabletData ioMouseSetTabletMode ioMouseConnect
        ioMouseAssignDevice cellPadInit cellPadEnd cellPadGetData
        cellPadGetInfo cellPadGetRawData cellPadSetSensorMode
     cellPadGetInfo2 cellPadGetButtonInfo cellPadInfoIrMode cellPadSetIrMode cellPadSetFiltering cellPadGetDataExtra cellPadSetPort cellPadGetDeviceInfo cellPadGetPortAssignment cellPadSetClearDeviceInfo cellPadGetNumberOfDevice cellPadCalibrateAnalogStick cellPadSetAnalogDeadZone ioPadGetPortAssignment cellKbInit cellKbEnd cellKbGetInfo cellKbRead cellKbClearBuf cellKbCnvMode cellKbGetConfiguration cellKbSetCodeType cellKbSetLEDStatus cellKbSetReadMode cellKbAssignDevice cellKbConnect cellKbGetStatus cellKbSetBufferingMode cellMouseInit cellMouseEnd cellMouseGetInfo cellMouseGetData cellMouseGetDataList cellMouseGetTabletData cellMouseSetTabletMode cellMouseGetAxes cellMouseGetButtons cellMouseGetPointer cellMouseCheckData cellMouseAssignDevice cellMouseConnect cellPadSetSensorMode2 cellPadGetAssignment cellPadInit2 cellPadGetInfoEx""",
    "sys_fs": """
        cellFsOpen cellFsRead cellFsWrite cellFsClose cellFsOpendir
        cellFsReaddir cellFsClosedir cellFsStat cellFsFstat cellFsLseek
        cellFsMkdir cellFsRmdir cellFsUnlink cellFsRename cellFsUtime
        cellFsSync cellFsTruncate cellFsFtruncate cellFsGetBlockSize
        cellFsGetDirectoryEntries cellFsReadWithOffset cellFsWriteWithOffset
        cellFsSdataOpen cellFsSdataOpenByFd cellFsGetDirectoryEntry
        cellFsChmod cellFsAccess cellFsGetFreeSize cellFsMmap
        cellFsChangeFileSize cellFsFsync cellFsLink cellFsFgetXattr
    """,
    "sys_net": """
        socket socketpair bind listen accept connect send recv sendto
        recvfrom shutdown closesocket setsockopt getsockopt gethostbyname
        gethostbyaddr inet_addr inet_ntoa htons ntohs htonl ntohl select
        poll getpeername getsockname listen sys_net_initialize_network
        sys_net_finalize_network sys_net_abort sys_net_free_thread_context
        sys_net_allocate_thread_context sendmsg recvmsg ioctl if_nametoindex
        gethostname freeaddrinfo getaddrinfo getnameinfo gai_strerror
        acceptconnect sconnect _sys_net_socket _sys_net_bind
     sys_net_initialize_network_ex sys_net_get_sockinfo sys_net_set_libinfo sys_net_get_libinfo""",
    "sceNp": """
        sceNpInit sceNpTerm sceNpManagerReadClock
        sceNpManagerRegisterCallback sceNpManagerUnregisterCallback
        sceNpManagerGetStatus sceNpManagerGetNetworkTime
        sceNpManagerGetAccountRegion sceNpManagerGetNpId
        sceNpManagerGetOnlineId sceNpManagerGetOnlineName
        sceNpManagerGetAvatarUrl sceNpManagerGetLanguage
        sceNpManagerGetCddaCount sceNpManagerGetTitleNpId
        sceNpManagerGetChatLanguage sceNpManagerRequestTicket
        sceNpManagerRequestTicket2 sceNpBasicSetPresence
        sceNpBasicRegisterHandler sceNpBasicUnregisterHandler
        sceNpBasicSendMessage sceNpBasicRecvMessage
        sceNpBasicRecvMessageCustom sceNpBasicRecvPresence
        sceNpBasicGetFriendPresenceByIndex sceNpLookupInit sceNpLookupTerm
        sceNpLookupCreateTitleCtx sceNpLookupDestroyTitleCtx
        sceNpLookupUserProfile sceNpLookupNpId sceNpLookupAvatarImage
        sceNpLookupTitleUserStorageData sceNpMatching2Init
        sceNpMatching2Term sceNpScoreInit sceNpScoreTerm
        sceNpScoreCreateTitleCtx sceNpScoreDestroyTitleCtx
        sceNpScoreCreateTransactionCtx sceNpScoreDestroyTransactionCtx
        sceNpScoreSetPlayerCharacterId sceNpScoreRecordScore
        sceNpScoreRecordScoreAsync sceNpScoreGetRankingByNpId
        sceNpScoreGetRankingByNpIdAsync sceNpScoreGetRanking
        sceNpScoreGetRankingAsync sceNpScoreGetRankingByRange
        sceNpScoreGetRankingByRangeAsync sceNpScoreAbortTransaction
        sceNpScoreWaitAsync sceNpScorePollAsync sceNpScoreGetStatus
        sceNpSignalingInit sceNpSignalingTerm sceNpDrmIsAvailable
        sceNpDrmVerifyUpgradeLicense sceNpDrmExecuteGamePurchase
        sceNpEntitlementAccessInit sceNpGetGameProductCode
        sceNpGetOnlineId sceNpGetNpId sceNpIdMgrGetNpId
     sceNpManagerGetContentRatingFlag sceNpManagerGetEntitlementById sceNpManagerGetPsHandle sceNpBasicSetPresenceDetails sceNpBasicGetFriendPresence sceNpSignalingCreateCtx sceNpSignalingDestroyCtx sceNpSignalingGetConnectionStatus sceNpSignalingGetLocalNetAddr sceNpLookupCreateRequest sceNpLookupDestroyRequest sceNpGetGameProductCode sceNpManagerGetAccountAge sceNpManagerGetBirthDate sceNpManagerGetCreditCardInfo""",
    "cellRtc": """
        cellRtcGetCurrentTick cellRtcGetCurrentClock
        cellRtcGetCurrentDateTime cellRtcTickAddTicks cellRtcTickAddYears
        cellRtcTickAddMonths cellRtcTickAddDays cellRtcTickAddHours
        cellRtcTickAddMinutes cellRtcTickAddSeconds
        cellRtcTickAddMicroseconds cellRtcConvertDateTimeToTick
        cellRtcConvertTickToDateTime cellRtcFormatRFC2822
        cellRtcFormatRFC3339 cellRtcParseDateTime cellRtcGetDaysInMonth
        cellRtcIsLeapYear cellRtcCompareTick cellRtcSetTime_t
        cellRtcGetTime_t cellRtcCheckValid cellRtcGetTick
        cellRtcDateTimeToTick cellRtcTickToDateTime
     cellRtcGetCurrentSystemTime cellRtcSetCurrentSystemTime cellRtcGetSystemTime cellRtcGetTickCount cellRtcGetCurrentTick2 _cellRtcGetCurrentTick cellRtcGetCurrentClockLocalTime cellRtcConvertUtcToLocalTime cellRtcConvertLocalTimeToUtc cellRtcGetSecForTimeZone cellRtcSetSecForTimeZone""",
    "sceNpTrophy": """
        sceNpTrophyInit sceNpTrophyTerm sceNpTrophyCreateContext
        sceNpTrophyDestroyContext sceNpTrophyCreateHandle
        sceNpTrophyDestroyHandle sceNpTrophyRegisterContext
        sceNpTrophyGetTrophyUnlockState sceNpTrophyUnlockTrophy
        sceNpTrophyGetGameInfo sceNpTrophyGetGroupInfo
        sceNpTrophyGetTrophyInfo sceNpTrophyGetGameIcon
        sceNpTrophyGetGroupIcon sceNpTrophyGetTrophyIcon
        sceNpTrophySetDescription sceNpTrophyGetRequiredDiskSpace
        sceNpTrophyGetProgress sceNpTrophyAbortHandle
    """,
    "cellNetCtl": """
        cellNetCtlInit cellNetCtlTerm cellNetCtlGetInfo
        cellNetCtlAddHandler cellNetCtlDelHandler cellNetCtlGetState
        cellNetCtlGetNatType cellNetCtlNetStartLoad
        cellNetCtlNetFinishLoad cellNetCtlNetStartUnload
        cellNetCtlNetFinishUnload cellNetCtlGetInetInfo
        cellNetCtlGetBandwidthInfoMeterBw cellNetCtlAbortTest
        cellNetCtlTestConnect cellNetCtlGetScanResult
     cellNetCtlGetLinkStatus cellNetCtlGetDnsServerAddress cellNetCtlGetCurrentConnectionType cellNetCtlJoinAp cellNetCtlDisconnect cellNetCtlScanAp""",
    "cellPngDec": """
        cellPngDecCreate cellPngDecDestroy cellPngDecOpen cellPngDecClose
        cellPngDecReadHeader cellPngDecSetParameter cellPngDecDecodeData
        cellPngDecGetDateTime cellPngDecCreateExt cellPngDecDestroyExt
    """,
    "cellVideoExportUtility": """
        cellVideoExportUtilityInitialize cellVideoExportUtilityFinalize
        cellVideoExportUtilityPhotoExportFromFile
        cellVideoExportUtilityVideoExport cellVideoExportUtilityCancel
        cellVideoExportUtilitySetPhotoThumbParam
        cellVideoExportUtilityInitializeEx cellVideoExportInitialize
     cellVideoExportUtilityInit cellVideoExportUtilityPhotoExport cellVideoExportUtilityMovieExport cellVideoExportUtilitySetPhotoThumbnailParam cellVideoExportUtilityGetPhotoInfo cellVideoExportUtilityAbort cellVideoExportUtilityCheckAvailability cellVideoExportUtilityRegisterEventHandler""",
    "cellVideoUpload": """
        cellVideoUploadInitialize cellVideoUploadFinalize
        cellVideoUploadStart cellVideoUploadUpload cellVideoUploadInit
    """,
    "cellRec": """
        cellRecInit cellRecEnd cellRecOpen cellRecClose cellRecStart
        cellRecStop cellRecGetInfo cellRecQueryMemSize cellRecSetInfo
        cellRecNotify cellRecGetStatus cellRecSetQuality cellRecAbort
        cellRecSetParam cellRecGetParam
    """,
    "cellGame": """
        cellGameGetParamInt cellGameGetParamString cellGameGetSizeKB
        cellGameContentPermit cellGameDataCheckCreate
        cellGameDataCheckCreate2 cellGameBootCheck
        cellGameContentErrorDialog cellGameSetMemDebugMode
        cellGameStrSearchGameDataContentId cellGamePatchCheck
        cellGameCreateGameData cellGameDeleteGameData
        cellGameSetGameDataPath cellGameGetGameDataPath
        cellGameDataCheck cellGameInit
    """,
    "sceNpCommerce2": """
        sceNpCommerce2Init sceNpCommerce2Term sceNpCommerce2CreateCtx
        sceNpCommerce2DestroyCtx sceNpCommerce2CreateContentCtrl
        sceNpCommerce2DestroyContentCtrl sceNpCommerce2DoProductBrowseStart
        sceNpCommerce2DoDlListStart sceNpCommerce2DoCheckoutStart
        sceNpCommerce2DoSkuGuideStart sceNpCommerce2GetProductInfoStart
        sceNpCommerce2GetCategoryContentsStart
        sceNpCommerce2GetContentRatingDescriptor
        sceNpCommerce2SetReqNetInfo sceNpCommerce2ResetReqNetInfo
        sceNpCommerce2GetResult sceNpCommerce2AbortReq
     sceNpCommerce2GetReqStatus sceNpCommerce2GetProductInfo sceNpCommerce2DoContentBrowseStart sceNpCommerce2InitWithParams sceNpCommerce2GetCurrencyInfo""",
    "sysPrxForUser": """
        sys_prx_load_module sys_prx_start_module sys_prx_stop_module
        sys_prx_unload_module sys_prx_register_library
        sys_prx_query_module sys_prx_get_module_info
        sys_prx_get_module_id_by_name sys_prx_get_module_id_by_address
        sys_prx_load_module_by_name sys_prx_load_module_list
        sys_prx_load_module_list_by_name sys_prx_get_module_list
        sys_prx_register_export sys_prx_start sys_prx_stop
        _sys_printf _sys_sprintf _sys_snprintf _sys_vsprintf
        _sys_vsnprintf _sys_vprintf _sys_putchar _sys_getchar
        console_write console_putc sys_process_exit sys_process_exit2
        _sys_process_atExitspi _sys_process_at_Exit _sys_mallopt
        _sys_malloc _sys_free _sys_calloc _sys_realloc sys_memset
        sys_time_get_system_time sys_timebase_frequency
        sys_get_random_number sys_initialize_tls sys_deinitialize_tls
        sys_ppu_thread_get_id sys_raw_spu_load sys_interrupt_thread_eoi
     sys_prx_load_module_on_memorycontainer sys_prx_start_module_with_options sys_prx_stop_module_with_options sys_prx_get_module_info_with_options sys_prx_exitspawn_with_notification sys_time_get_current_time sys_time_set_current_time _sys_time_get_system_time sys_get_timebase_frequency console_getc console_init sys_lwmutex_create sys_lwmutex_lock sys_lwmutex_unlock sys_lwmutex_destroy _sys_memcpy _sys_memset _sys_strcpy _sys_strlen sys_random_number sys_prx_load_module_by_name_with_options""",
}

# ---------------------------------------------------------------------------
# .lib.stub parsing (record layout established in NATIVE-PORT-001 work):
#   u8 size=0x2C, u8 pad, u16 version, u16 attributes, u16 nfunc,
#   u16 nvar, u16 ntlsvar, u32 hashinfo, u32 libname_ptr, u32 fnid_ptr,
#   u32 functable_ptr, u32 var_ptr, u32 tls_ptr, u32 pad, u32 pad
# The stub array is the 0x370-byte section at VA 0x175C3C.
# ---------------------------------------------------------------------------
STUB_VA = 0x175C3C
STUB_SIZE = 0x370
STUB_REC = 0x2C


def va2off(va: int) -> int:
    if 0x10000 <= va < 0x10000 + 0x1D81E8:
        return va - 0x10000
    if 0x1F0000 <= va < 0x1F0000 + 0xCCC4C:
        return 0x1E0000 + (va - 0x1F0000)
    raise ValueError(f"VA not mapped: {va:#x}")


# Extra candidates mined from RPCS3's HLE module sources
# (scripts/rpcs3_candidates.txt: library<TAB>name). RPCS3 implements
# these libraries by name, so its registration tables are a
# high-quality dictionary for the FNID hash.
_rpc_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "rpcs3_candidates.txt")
if os.path.exists(_rpc_path):
    for _line in open(_rpc_path):
        _parts = _line.rstrip("\n").split("\t")
        if len(_parts) == 2 and _parts[0] in CANDIDATES:
            if _parts[1] not in CANDIDATES[_parts[0]].split():
                CANDIDATES[_parts[0]] += " " + _parts[1]


def main() -> int:
    elf_path, out_path = sys.argv[1], sys.argv[2]
    data = open(elf_path, "rb").read()

    def cstr(va: int) -> str:
        off = va2off(va)
        end = data.index(b"\x00", off)
        return data[off:end].decode("ascii", "replace")

    # Precompute candidate hashes per library.
    tables = {}
    for lib, blob in CANDIDATES.items():
        names = set(blob.split())
        tables[lib] = {fnid(n): n for n in names}

    rows = []
    base = va2off(STUB_VA)
    nrecords = STUB_SIZE // STUB_REC
    total = matched = 0
    for i in range(nrecords):
        r = base + i * STUB_REC
        if data[r] != STUB_REC:
            print(f"warning: record {i} has size byte {data[r]:#x}", file=sys.stderr)
            break
        _, _, nfunc, _, _ = struct.unpack(">HHHHH", data[r + 2 : r + 12])
        libname_ptr, fnid_ptr, table_ptr = struct.unpack(">III", data[r + 16 : r + 28])
        lib = cstr(libname_ptr)
        table = tables.get(lib, {})
        for j in range(nfunc):
            (hashval,) = struct.unpack(">I", data[va2off(fnid_ptr) + 4 * j : va2off(fnid_ptr) + 4 * j + 4])
            name = table.get(hashval, "")
            total += 1
            matched += bool(name)
            rows.append((lib, j, table_ptr + 4 * j, hashval, name))

    with open(out_path, "w") as f:
        f.write("library\tindex\ttable_address\tfnid\tname\n")
        for lib, j, addr, hashval, name in rows:
            f.write(f"{lib}\t{j}\t{addr:08x}\t{hashval:08x}\t{name}\n")

    print(f"matched {matched}/{total} imports")
    # Per-library coverage.
    from collections import defaultdict

    cov = defaultdict(lambda: [0, 0])
    for lib, _j, _a, _h, name in rows:
        cov[lib][1] += 1
        cov[lib][0] += bool(name)
    for lib in sorted(cov):
        m, t = cov[lib]
        print(f"  {lib:24s} {m:3d}/{t:3d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
