// SPDX-License-Identifier: Apache-2.0
/*
 * WFx hardware interface definitions
 *
 * Copyright (c) 2018-2019, Silicon Laboratories Inc.
 */

#ifndef _WSM_MIB_API_H_
#define _WSM_MIB_API_H_

#include "general_api.h"

#define WSM_API_IPV4_ADDRESS_SIZE                       4
#define WSM_API_IPV6_ADDRESS_SIZE                       16

typedef enum WsmMibIds_e {
	WSM_MIB_ID_GL_OPERATIONAL_POWER_MODE       = 0x2000,
	WSM_MIB_ID_GL_BLOCK_ACK_INFO               = 0x2001,
	WSM_MIB_ID_GL_SET_MULTI_MSG                = 0x2002,
	WSM_MIB_ID_CCA_CONFIG                      = 0x2003,
	WSM_MIB_ID_ETHERTYPE_DATAFRAME_CONDITION   = 0x2010,
	WSM_MIB_ID_PORT_DATAFRAME_CONDITION        = 0x2011,
	WSM_MIB_ID_MAGIC_DATAFRAME_CONDITION       = 0x2012,
	WSM_MIB_ID_MAC_ADDR_DATAFRAME_CONDITION    = 0x2013,
	WSM_MIB_ID_IPV4_ADDR_DATAFRAME_CONDITION   = 0x2014,
	WSM_MIB_ID_IPV6_ADDR_DATAFRAME_CONDITION   = 0x2015,
	WSM_MIB_ID_UC_MC_BC_DATAFRAME_CONDITION    = 0x2016,
	WSM_MIB_ID_CONFIG_DATA_FILTER              = 0x2017,
	WSM_MIB_ID_SET_DATA_FILTERING              = 0x2018,
	WSM_MIB_ID_ARP_IP_ADDRESSES_TABLE          = 0x2019,
	WSM_MIB_ID_NS_IP_ADDRESSES_TABLE           = 0x201A,
	WSM_MIB_ID_RX_FILTER                       = 0x201B,
	WSM_MIB_ID_BEACON_FILTER_TABLE             = 0x201C,
	WSM_MIB_ID_BEACON_FILTER_ENABLE            = 0x201D,
	WSM_MIB_ID_GRP_SEQ_COUNTER                 = 0x2030,
	WSM_MIB_ID_TSF_COUNTER                     = 0x2031,
	WSM_MIB_ID_STATISTICS_TABLE                = 0x2032,
	WSM_MIB_ID_COUNTERS_TABLE                  = 0x2033,
	WSM_MIB_ID_MAX_TX_POWER_LEVEL              = 0x2034,
	WSM_MIB_ID_EXTENDED_COUNTERS_TABLE         = 0x2035,
	WSM_MIB_ID_DOT11_MAC_ADDRESS               = 0x2040,
	WSM_MIB_ID_DOT11_MAX_TRANSMIT_MSDU_LIFETIME = 0x2041,
	WSM_MIB_ID_DOT11_MAX_RECEIVE_LIFETIME      = 0x2042,
	WSM_MIB_ID_DOT11_WEP_DEFAULT_KEY_ID        = 0x2043,
	WSM_MIB_ID_DOT11_RTS_THRESHOLD             = 0x2044,
	WSM_MIB_ID_SLOT_TIME                       = 0x2045,
	WSM_MIB_ID_CURRENT_TX_POWER_LEVEL          = 0x2046,
	WSM_MIB_ID_NON_ERP_PROTECTION              = 0x2047,
	WSM_MIB_ID_TEMPLATE_FRAME                  = 0x2048,
	WSM_MIB_ID_BEACON_WAKEUP_PERIOD            = 0x2049,
	WSM_MIB_ID_RCPI_RSSI_THRESHOLD             = 0x204A,
	WSM_MIB_ID_BLOCK_ACK_POLICY                = 0x204B,
	WSM_MIB_ID_OVERRIDE_INTERNAL_TX_RATE       = 0x204C,
	WSM_MIB_ID_SET_ASSOCIATION_MODE            = 0x204D,
	WSM_MIB_ID_SET_UAPSD_INFORMATION           = 0x204E,
	WSM_MIB_ID_SET_TX_RATE_RETRY_POLICY        = 0x204F,
	WSM_MIB_ID_PROTECTED_MGMT_POLICY           = 0x2050,
	WSM_MIB_ID_SET_HT_PROTECTION               = 0x2051,
	WSM_MIB_ID_KEEP_ALIVE_PERIOD               = 0x2052,
	WSM_MIB_ID_ARP_KEEP_ALIVE_PERIOD           = 0x2053,
	WSM_MIB_ID_INACTIVITY_TIMER                = 0x2054,
	WSM_MIB_ID_INTERFACE_PROTECTION            = 0x2055,
	WSM_MIB_ID_BEACON_STATS                    = 0x2056,
} WsmMibIds;

#define WSM_OP_POWER_MODE_MASK                     0xf

typedef enum WsmOpPowerMode_e {
	WSM_OP_POWER_MODE_ACTIVE                   = 0x0,
	WSM_OP_POWER_MODE_DOZE                     = 0x1,
	WSM_OP_POWER_MODE_QUIESCENT                = 0x2
} WsmOpPowerMode;

typedef struct WsmHiMibGlOperationalPowerMode_s {
	uint8_t    PowerMode:4;
	uint8_t    Reserved1:3;
	uint8_t    WupIndActivation:1;
	uint8_t    Reserved2[3];
} __packed WsmHiMibGlOperationalPowerMode_t;

typedef struct WsmHiMibGlBlockAckInfo_s {
	uint8_t    RxBufferSize;
	uint8_t    RxMaxNumAgreements;
	uint8_t    TxBufferSize;
	uint8_t    TxMaxNumAgreements;
} __packed WsmHiMibGlBlockAckInfo_t;

typedef struct WsmHiMibGlSetMultiMsg_s {
	uint8_t    EnableMultiTxConf:1;
	uint8_t    Reserved1:7;
	uint8_t    Reserved2[3];
} __packed WsmHiMibGlSetMultiMsg_t;

typedef enum WsmCcaThrMode_e {
	WSM_CCA_THR_MODE_RELATIVE = 0x0,
	WSM_CCA_THR_MODE_ABSOLUTE = 0x1
} WsmCcaThrMode;

typedef struct WsmHiMibGlCcaConfig_s {
	uint8_t  CcaThrMode;
	uint8_t  Reserved[3];
} __packed WsmHiMibGlCcaConfig_t;

#define MAX_NUMBER_DATA_FILTERS             0xA

#define MAX_NUMBER_IPV4_ADDR_CONDITIONS     0x4
#define MAX_NUMBER_IPV6_ADDR_CONDITIONS     0x4
#define MAX_NUMBER_MAC_ADDR_CONDITIONS      0x4
#define MAX_NUMBER_UC_MC_BC_CONDITIONS      0x4
#define MAX_NUMBER_ETHER_TYPE_CONDITIONS    0x4
#define MAX_NUMBER_PORT_CONDITIONS          0x4
#define MAX_NUMBER_MAGIC_CONDITIONS         0x4
#define MAX_NUMBER_ARP_CONDITIONS           0x2
#define MAX_NUMBER_NS_CONDITIONS            0x2

typedef struct WsmHiMibEthertypeDataFrameCondition_s {
	uint8_t    ConditionIdx;
	uint8_t    Reserved;
	uint16_t   EtherType;
} __packed WsmHiMibEthertypeDataFrameCondition_t;

typedef enum WsmUdpTcpProtocol_e {
	WSM_PROTOCOL_UDP                       = 0x0,
	WSM_PROTOCOL_TCP                       = 0x1,
	WSM_PROTOCOL_BOTH_UDP_TCP              = 0x2
} WsmUdpTcpProtocol;

typedef enum WsmWhichPort_e {
	WSM_PORT_DST                           = 0x0,
	WSM_PORT_SRC                           = 0x1,
	WSM_PORT_SRC_OR_DST                    = 0x2
} WsmWhichPort;

typedef struct WsmHiMibPortsDataFrameCondition_s {
	uint8_t    ConditionIdx;
	uint8_t    Protocol;
	uint8_t    WhichPort;
	uint8_t    Reserved1;
	uint16_t   PortNumber;
	uint8_t    Reserved2[2];
} __packed WsmHiMibPortsDataFrameCondition_t;

#define WSM_API_MAGIC_PATTERN_SIZE                 32

typedef struct WsmHiMibMagicDataFrameCondition_s {
	uint8_t    ConditionIdx;
	uint8_t    Offset;
	uint8_t    MagicPatternLength;
	uint8_t    Reserved;
	uint8_t    MagicPattern[WSM_API_MAGIC_PATTERN_SIZE];
} __packed WsmHiMibMagicDataFrameCondition_t;

typedef enum WsmMacAddrType_e {
	WSM_MAC_ADDR_A1                            = 0x0,
	WSM_MAC_ADDR_A2                            = 0x1,
	WSM_MAC_ADDR_A3                            = 0x2
} WsmMacAddrType;

typedef struct WsmHiMibMacAddrDataFrameCondition_s {
	uint8_t    ConditionIdx;
	uint8_t    AddressType;
	uint8_t    MacAddress[ETH_ALEN];
} __packed WsmHiMibMacAddrDataFrameCondition_t;

typedef enum WsmIpAddrMode_e {
	WSM_IP_ADDR_SRC                            = 0x0,
	WSM_IP_ADDR_DST                            = 0x1
} WsmIpAddrMode;

typedef struct WsmHiMibIpv4AddrDataFrameCondition_s {
	uint8_t    ConditionIdx;
	uint8_t    AddressMode;
	uint8_t    Reserved[2];
	uint8_t    IPv4Address[WSM_API_IPV4_ADDRESS_SIZE];
} __packed WsmHiMibIpv4AddrDataFrameCondition_t;

typedef struct WsmHiMibIpv6AddrDataFrameCondition_s {
	uint8_t    ConditionIdx;
	uint8_t    AddressMode;
	uint8_t    Reserved[2];
	uint8_t    IPv6Address[WSM_API_IPV6_ADDRESS_SIZE];
} __packed WsmHiMibIpv6AddrDataFrameCondition_t;

typedef union WsmHiAddrType_u {
	uint8_t value;
	struct {
		uint8_t    TypeUnicast:1;
		uint8_t    TypeMulticast:1;
		uint8_t    TypeBroadcast:1;
		uint8_t    Reserved:5;
	} bits;
} __packed WsmHiAddrType_t;

typedef struct WsmHiMibUcMcBcDataFrameCondition_s {
	uint8_t    ConditionIdx;
	WsmHiAddrType_t Param;
	uint8_t    Reserved[2];
} __packed WsmHiMibUcMcBcDataFrameCondition_t;

typedef struct WsmHiMibConfigDataFilter_s {
	uint8_t    FilterIdx;
	uint8_t    Enable;
	uint8_t    Reserved1[2];
	uint8_t    EthTypeCond;
	uint8_t    PortCond;
	uint8_t    MagicCond;
	uint8_t    MacCond;
	uint8_t    Ipv4Cond;
	uint8_t    Ipv6Cond;
	uint8_t    UcMcBcCond;
	uint8_t    Reserved2;
} __packed WsmHiMibConfigDataFilter_t;

typedef struct WsmHiMibSetDataFiltering_s {
	uint8_t    DefaultFilter;
	uint8_t    Enable;
	uint8_t    Reserved[2];
} __packed WsmHiMibSetDataFiltering_t;

typedef enum WsmArpNsFrameTreatment_e {
	WSM_ARP_NS_FILTERING_DISABLE                  = 0x0,
	WSM_ARP_NS_FILTERING_ENABLE                   = 0x1,
	WSM_ARP_NS_REPLY_ENABLE                       = 0x2
} WsmArpNsFrameTreatment;

typedef struct WsmHiMibArpIpAddrTable_s {
	uint8_t    ConditionIdx;
	uint8_t    ArpEnable;
	uint8_t    Reserved[2];
	uint8_t    Ipv4Address[WSM_API_IPV4_ADDRESS_SIZE];
} __packed WsmHiMibArpIpAddrTable_t;

typedef struct WsmHiMibNsIpAddrTable_s {
	uint8_t    ConditionIdx;
	uint8_t    NsEnable;
	uint8_t    Reserved[2];
	uint8_t    Ipv6Address[WSM_API_IPV6_ADDRESS_SIZE];
} __packed WsmHiMibNsIpAddrTable_t;

typedef struct WsmHiMibRxFilter_s {
	uint8_t    Reserved1:1;
	uint8_t    BssidFilter:1;
	uint8_t    Reserved2:1;
	uint8_t    FwdProbeReq:1;
	uint8_t    KeepAliveFilter:1;
	uint8_t    Reserved3:3;
	uint8_t    Reserved4[3];
} __packed WsmHiMibRxFilter_t;

#define WSM_API_OUI_SIZE                                3
#define WSM_API_MATCH_DATA_SIZE                         3

typedef struct WsmHiIeTableEntry_s {
	uint8_t    IeId;
	uint8_t    HasChanged:1;
	uint8_t    NoLonger:1;
	uint8_t    HasAppeared:1;
	uint8_t    Reserved:1;
	uint8_t    NumMatchData:4;
	uint8_t    Oui[WSM_API_OUI_SIZE];
	uint8_t    MatchData[WSM_API_MATCH_DATA_SIZE];
} __packed WsmHiIeTableEntry_t;

typedef struct WsmHiMibBcnFilterTable_s {
	uint32_t   NumOfInfoElmts;
	WsmHiIeTableEntry_t IeTable[];
} __packed WsmHiMibBcnFilterTable_t;

typedef enum WsmBeaconFilter_e {
	WSM_BEACON_FILTER_DISABLE                  = 0x0,
	WSM_BEACON_FILTER_ENABLE                   = 0x1,
	WSM_BEACON_FILTER_AUTO_ERP                 = 0x2
} WsmBeaconFilter;

typedef struct WsmHiMibBcnFilterEnable_s {
	uint32_t   Enable;
	uint32_t   BcnCount;
} __packed WsmHiMibBcnFilterEnable_t;

typedef struct WsmHiMibGroupSeqCounter_s {
	uint32_t   Bits4716;
	uint16_t   Bits1500;
	uint16_t   Reserved;
} __packed WsmHiMibGroupSeqCounter_t;

typedef struct WsmHiMibTsfCounter_s {
	uint32_t   TSFCounterlo;
	uint32_t   TSFCounterhi;
} __packed WsmHiMibTsfCounter_t;

typedef struct WsmHiMibStatsTable_s {
	int16_t    LatestSnr;
	uint8_t    LatestRcpi;
	int8_t     LatestRssi;
} __packed WsmHiMibStatsTable_t;

typedef struct WsmHiMibExtendedCountTable_s {
	uint32_t   CountPlcpErrors;
	uint32_t   CountFcsErrors;
	uint32_t   CountTxPackets;
	uint32_t   CountRxPackets;
	uint32_t   CountRxPacketErrors;
	uint32_t   CountRxDecryptionFailures;
	uint32_t   CountRxMicFailures;
	uint32_t   CountRxNoKeyFailures;
	uint32_t   CountTxMulticastFrames;
	uint32_t   CountTxFramesSuccess;
	uint32_t   CountTxFrameFailures;
	uint32_t   CountTxFramesRetried;
	uint32_t   CountTxFramesMultiRetried;
	uint32_t   CountRxFrameDuplicates;
	uint32_t   CountRtsSuccess;
	uint32_t   CountRtsFailures;
	uint32_t   CountAckFailures;
	uint32_t   CountRxMulticastFrames;
	uint32_t   CountRxFramesSuccess;
	uint32_t   CountRxCMACICVErrors;
	uint32_t   CountRxCMACReplays;
	uint32_t   CountRxMgmtCCMPReplays;
	uint32_t   CountRxBIPMICErrors;
	uint32_t   CountRxBeacon;
	uint32_t   CountMissBeacon;
	uint32_t   Reserved[15];
} __packed WsmHiMibExtendedCountTable_t;

typedef struct WsmHiMibCountTable_s {
	uint32_t   CountPlcpErrors;
	uint32_t   CountFcsErrors;
	uint32_t   CountTxPackets;
	uint32_t   CountRxPackets;
	uint32_t   CountRxPacketErrors;
	uint32_t   CountRxDecryptionFailures;
	uint32_t   CountRxMicFailures;
	uint32_t   CountRxNoKeyFailures;
	uint32_t   CountTxMulticastFrames;
	uint32_t   CountTxFramesSuccess;
	uint32_t   CountTxFrameFailures;
	uint32_t   CountTxFramesRetried;
	uint32_t   CountTxFramesMultiRetried;
	uint32_t   CountRxFrameDuplicates;
	uint32_t   CountRtsSuccess;
	uint32_t   CountRtsFailures;
	uint32_t   CountAckFailures;
	uint32_t   CountRxMulticastFrames;
	uint32_t   CountRxFramesSuccess;
	uint32_t   CountRxCMACICVErrors;
	uint32_t   CountRxCMACReplays;
	uint32_t   CountRxMgmtCCMPReplays;
	uint32_t   CountRxBIPMICErrors;
} __packed WsmHiMibCountTable_t;

typedef struct WsmHiMibMaxTxPowerLevel_s {
	int32_t       MaxTxPowerLevelRfPort1;
	int32_t       MaxTxPowerLevelRfPort2;
} __packed WsmHiMibMaxTxPowerLevel_t;

typedef struct WsmHiMibBeaconStats_s {
	int32_t     LatestTbttDiff;
	uint32_t    Reserved[4];
} __packed WsmHiMibBeaconStats_t;

typedef struct WsmHiMibMacAddress_s {
	uint8_t    MacAddr[ETH_ALEN];
	uint16_t   Reserved;
} __packed WsmHiMibMacAddress_t;

typedef struct WsmHiMibDot11MaxTransmitMsduLifetime_s {
	uint32_t   MaxLifeTime;
} __packed WsmHiMibDot11MaxTransmitMsduLifetime_t;

typedef struct WsmHiMibDot11MaxReceiveLifetime_s {
	uint32_t   MaxLifeTime;
} __packed WsmHiMibDot11MaxReceiveLifetime_t;

typedef struct WsmHiMibWepDefaultKeyId_s {
	uint8_t    WepDefaultKeyId;
	uint8_t    Reserved[3];
} __packed WsmHiMibWepDefaultKeyId_t;

typedef struct WsmHiMibDot11RtsThreshold_s {
	uint32_t   Threshold;
} __packed WsmHiMibDot11RtsThreshold_t;

typedef struct WsmHiMibSlotTime_s {
	uint32_t   SlotTime;
} __packed WsmHiMibSlotTime_t;

typedef struct WsmHiMibCurrentTxPowerLevel_s {
	int32_t   PowerLevel;
} __packed WsmHiMibCurrentTxPowerLevel_t;

typedef struct WsmHiMibNonErpProtection_s {
	uint8_t   useCtsToSelf:1;
	uint8_t   Reserved1:7;
	uint8_t   Reserved2[3];
} __packed WsmHiMibNonErpProtection_t;

typedef enum WsmTxMode_e {
	WSM_TX_MODE_MIXED                        = 0x0,
	WSM_TX_MODE_GREENFIELD                   = 0x1
} WsmTxMode;

typedef enum WsmTmplt_e {
	WSM_TMPLT_PRBREQ                           = 0x0,
	WSM_TMPLT_BCN                              = 0x1,
	WSM_TMPLT_NULL                             = 0x2,
	WSM_TMPLT_QOSNUL                           = 0x3,
	WSM_TMPLT_PSPOLL                           = 0x4,
	WSM_TMPLT_PRBRES                           = 0x5,
	WSM_TMPLT_ARP                              = 0x6,
	WSM_TMPLT_NA                               = 0x7
} WsmTmplt;

#define WSM_API_MAX_TEMPLATE_FRAME_SIZE                              700

typedef struct WsmHiMibTemplateFrame_s {
	uint8_t    FrameType;
	uint8_t    InitRate:7;
	uint8_t    Mode:1;
	uint16_t   FrameLength;
	uint8_t    Frame[WSM_API_MAX_TEMPLATE_FRAME_SIZE];
} __packed WsmHiMibTemplateFrame_t;

typedef struct WsmHiMibBeaconWakeUpPeriod_s {
	uint8_t    WakeupPeriodMin;
	uint8_t    ReceiveDTIM:1;
	uint8_t    Reserved1:7;
	uint8_t    WakeupPeriodMax;
	uint8_t    Reserved2;
} __packed WsmHiMibBeaconWakeUpPeriod_t;

typedef struct WsmHiMibRcpiRssiThreshold_s {
	uint8_t    Detection:1;
	uint8_t    RcpiRssi:1;
	uint8_t    Upperthresh:1;
	uint8_t    Lowerthresh:1;
	uint8_t    Reserved:4;
	uint8_t    LowerThreshold;
	uint8_t    UpperThreshold;
	uint8_t    RollingAverageCount;
} __packed WsmHiMibRcpiRssiThreshold_t;

#define DEFAULT_BA_MAX_RX_BUFFER_SIZE 16

typedef struct WsmHiMibBlockAckPolicy_s {
	uint8_t    BlockAckTxTidPolicy;
	uint8_t    Reserved1;
	uint8_t    BlockAckRxTidPolicy;
	uint8_t    BlockAckRxMaxBufferSize;
} __packed WsmHiMibBlockAckPolicy_t;

typedef struct WsmHiMibOverrideIntRate_s {
	uint8_t    InternalTxRate;
	uint8_t    NonErpInternalTxRate;
	uint8_t    Reserved[2];
} __packed WsmHiMibOverrideIntRate_t;

typedef enum WsmMpduStartSpacing_e {
	WSM_MPDU_START_SPACING_NO_RESTRIC          = 0x0,
	WSM_MPDU_START_SPACING_QUARTER             = 0x1,
	WSM_MPDU_START_SPACING_HALF                = 0x2,
	WSM_MPDU_START_SPACING_ONE                 = 0x3,
	WSM_MPDU_START_SPACING_TWO                 = 0x4,
	WSM_MPDU_START_SPACING_FOUR                = 0x5,
	WSM_MPDU_START_SPACING_EIGHT               = 0x6,
	WSM_MPDU_START_SPACING_SIXTEEN             = 0x7
} WsmMpduStartSpacing;

typedef struct WsmHiMibSetAssociationMode_s {
	uint8_t    PreambtypeUse:1;
	uint8_t    Mode:1;
	uint8_t    Rateset:1;
	uint8_t    Spacing:1;
	uint8_t    Reserved:4;
	uint8_t    PreambleType;
	uint8_t    MixedOrGreenfieldType;
	uint8_t    MpduStartSpacing;
	uint32_t   BasicRateSet;
} __packed WsmHiMibSetAssociationMode_t;

typedef struct WsmHiMibSetUapsdInformation_s {
	uint8_t    TrigBckgrnd:1;
	uint8_t    TrigBe:1;
	uint8_t    TrigVideo:1;
	uint8_t    TrigVoice:1;
	uint8_t    Reserved1:4;
	uint8_t    DelivBckgrnd:1;
	uint8_t    DelivBe:1;
	uint8_t    DelivVideo:1;
	uint8_t    DelivVoice:1;
	uint8_t    Reserved2:4;
	uint16_t   MinAutoTriggerInterval;
	uint16_t   MaxAutoTriggerInterval;
	uint16_t   AutoTriggerStep;
} __packed WsmHiMibSetUapsdInformation_t;

typedef struct WsmHiMibTxRateRetryPolicy_s {
	uint8_t    PolicyIndex;
	uint8_t    ShortRetryCount;
	uint8_t    LongRetryCount;
	uint8_t    FirstRateSel:2;
	uint8_t    Terminate:1;
	uint8_t    CountInit:1;
	uint8_t    Reserved1:4;
	uint8_t    RateRecoveryCount;
	uint8_t    Reserved2[3];
	uint32_t   RateCountIndices0700;
	uint32_t   RateCountIndices1508;
	uint32_t   RateCountIndices2316;
} __packed WsmHiMibTxRateRetryPolicy_t;

#define WSM_MIB_NUM_TX_RATE_RETRY_POLICIES    16

typedef struct WsmHiMibSetTxRateRetryPolicy_s {
	uint8_t    NumTxRatePolicies;
	uint8_t    Reserved[3];
	WsmHiMibTxRateRetryPolicy_t TxRateRetryPolicy[];
} __packed WsmHiMibSetTxRateRetryPolicy_t;

typedef struct WsmHiMibProtectedMgmtPolicy_s {
	uint8_t   PmfEnable:1;
	uint8_t   UnpmfAllowed:1;
	uint8_t   HostEncAuthFrames:1;
	uint8_t   Reserved1:5;
	uint8_t   Reserved2[3];
} __packed WsmHiMibProtectedMgmtPolicy_t;

typedef struct WsmHiMibSetHtProtection_s {
	uint8_t   DualCtsProt:1;
	uint8_t   Reserved1:7;
	uint8_t   Reserved2[3];
} __packed WsmHiMibSetHtProtection_t;

typedef struct WsmHiMibKeepAlivePeriod_s {
	uint16_t   KeepAlivePeriod;
	uint8_t    Reserved[2];
} __packed WsmHiMibKeepAlivePeriod_t;

typedef struct WsmHiMibArpKeepAlivePeriod_s {
	uint16_t   ArpKeepAlivePeriod;
	uint8_t    EncrType;
	uint8_t    Reserved;
	uint8_t    SenderIpv4Address[WSM_API_IPV4_ADDRESS_SIZE];
	uint8_t    TargetIpv4Address[WSM_API_IPV4_ADDRESS_SIZE];
} __packed WsmHiMibArpKeepAlivePeriod_t;

typedef struct WsmHiMibInactivityTimer_s {
	uint8_t    MinActiveTime;
	uint8_t    MaxActiveTime;
	uint16_t   Reserved;
} __packed WsmHiMibInactivityTimer_t;

typedef struct WsmHiMibInterfaceProtection_s {
	uint8_t   useCtsProt:1;
	uint8_t   Reserved1:7;
	uint8_t   Reserved2[3];
} __packed WsmHiMibInterfaceProtection_t;


#endif
