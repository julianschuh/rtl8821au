#include "hw.h"

static void StopTxBeacon(struct rtl_priv *padapter)
{
	 struct rtw_hal *pHalData = GET_HAL_DATA(padapter);

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) & (~BIT6));
	pHalData->RegFwHwTxQCtrl &= (~BIT6);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0x64);
	pHalData->RegReg542 &= ~(BIT0);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);

	 /* todo: CheckFwRsvdPageContent(Adapter);  // 2010.06.23. Added by tynli. */
}

static void ResumeTxBeacon(struct rtl_priv *padapter)
{
	 struct rtw_hal *pHalData = GET_HAL_DATA(padapter);

	/*
	 * 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value
	 * which should be read from register to a global variable.
	 */

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) | BIT6);
	pHalData->RegFwHwTxQCtrl |= BIT6;
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0xff);
	pHalData->RegReg542 |= BIT0;
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);
}

static VOID _BeaconFunctionEnable(struct rtl_priv *Adapter, BOOLEAN Enable,
	BOOLEAN	Linked)
{
	rtw_write8(Adapter, REG_BCN_CTRL, (BIT4 | BIT3 | BIT1));
	/*
	 * SetBcnCtrlReg(Adapter, (BIT4 | BIT3 | BIT1), 0x00);
	 * RT_TRACE(COMP_BEACON, DBG_LOUD, ("_BeaconFunctionEnable 0x550 0x%x\n", PlatformEFIORead1Byte(Adapter, 0x550)));
	 */

	rtw_write8(Adapter, REG_RD_CTRL+1, 0x6F);
}

void SetBeaconRelatedRegisters8812A(struct rtl_priv *padapter)
{
	uint32_t	value32;
	 struct rtw_hal	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	uint32_t bcn_ctrl_reg 			= REG_BCN_CTRL;
	/* reset TSF, enable update TSF, correcting TSF On Beacon */

	/*
	 * REG_BCN_INTERVAL
	 * REG_BCNDMATIM
	 * REG_ATIMWND
	 * REG_TBTT_PROHIBIT
	 * REG_DRVERLYINT
	 * REG_BCN_MAX_ERR
	 * REG_BCNTCFG //(0x510)
	 * REG_DUAL_TSF_RST
	 * REG_BCN_CTRL //(0x550)
	 */

	/* BCN interval */
	rtw_write16(padapter, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);
	rtw_write8(padapter, REG_ATIMWND, 0x02);	/* 2ms */

	_InitBeaconParameters_8812A(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	value32 = rtw_read32(padapter, REG_TCR);
	value32 &= ~TSFRST;
	rtw_write32(padapter,  REG_TCR, value32);

	value32 |= TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	/* NOTE: Fix test chip's bug (about contention windows's randomness) */
	rtw_write8(padapter,  REG_RXTSF_OFFSET_CCK, 0x50);
	rtw_write8(padapter, REG_RXTSF_OFFSET_OFDM, 0x50);

	_BeaconFunctionEnable(padapter, _TRUE, _TRUE);

	ResumeTxBeacon(padapter);

	/* rtw_write8(padapter, 0x422, rtw_read8(padapter, 0x422)|BIT(6)); */

	/* rtw_write8(padapter, 0x541, 0xff); */

	/* rtw_write8(padapter, 0x542, rtw_read8(padapter, 0x541)|BIT(0)); */

	rtw_write8(padapter, bcn_ctrl_reg, rtw_read8(padapter, bcn_ctrl_reg)|BIT(1));

}

static void hw_var_set_opmode(struct rtl_priv *Adapter, uint8_t variable, uint8_t *val)
{
	uint8_t	val8;
	uint8_t	mode = *((uint8_t *)val);

	{
		/* disable Port0 TSF update */
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|DIS_TSF_UDT);

		/*  set net_type */
		val8 = rtw_read8(Adapter, MSR)&0x0c;
		val8 |= mode;
		rtw_write8(Adapter, MSR, val8);

		DBG_871X("%s()-%d mode = %d\n", __FUNCTION__, __LINE__, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
			{
				StopTxBeacon(Adapter);
			}

			rtw_write8(Adapter, REG_BCN_CTRL, 0x19);		/* disable atim wnd */
			/* rtw_write8(Adapter,REG_BCN_CTRL, 0x18); */
		} else if ((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/ ) {
			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter, REG_BCN_CTRL, 0x1a);
		} else if (mode == _HW_STATE_AP_) {
			ResumeTxBeacon(Adapter);

			rtw_write8(Adapter, REG_BCN_CTRL, 0x12);

			/* Set RCR */
			rtw_write32(Adapter, REG_RCR, 0x7000208e);	/* CBSSID_DATA must set to 0,reject ICV_ERR packet */
			/* enable to rx data frame */
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
			/* enable to rx ps-poll */
			rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

			/* Beacon Control related register for first time */
			rtw_write8(Adapter, REG_BCNDMATIM, 0x02); /* 2ms */

			/* rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF); */
			rtw_write8(Adapter, REG_ATIMWND, 0x0a); 	/* 10ms */
			rtw_write16(Adapter, REG_BCNTCFG, 0x00);
			rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);	/* +32767 (~32ms) */

			/* reset TSF */
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

			/*
			 * enable BCN0 Function for if1
			 * don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received)
			 */
			rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT|EN_BCN_FUNCTION | EN_TXBCN_RPT|DIS_BCNQ_SUB));

			if (IS_HARDWARE_TYPE_8821(Adapter)) {
				/*  select BCN on port 0 */
				rtw_write8(Adapter, REG_CCK_CHECK_8812,	rtw_read8(Adapter, REG_CCK_CHECK_8812)&(~BIT(5)));
			}


			/* dis BCN1 ATIM  WND if if2 is station */
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|DIS_ATIM);
		}
	}

}

static void hw_var_set_macaddr(struct rtl_priv *Adapter, uint8_t variable, uint8_t *val)
{
	uint8_t idx = 0;
	uint32_t reg_macid;

	{
		reg_macid = REG_MACID;
	}

	for (idx = 0 ; idx < 6; idx++) {
		rtw_write8(Adapter, (reg_macid+idx), val[idx]);
	}

}

static void hw_var_set_bssid(struct rtl_priv *Adapter, uint8_t variable, uint8_t *val)
{
	uint8_t	idx = 0;
	uint32_t reg_bssid;

	{
		reg_bssid = REG_BSSID;
	}

	for (idx = 0 ; idx < 6; idx++) {
		rtw_write8(Adapter, (reg_bssid+idx), val[idx]);
	}

}

static void hw_var_set_bcn_func(struct rtl_priv *Adapter, uint8_t variable, uint8_t *val)
{
	uint32_t bcn_ctrl_reg;

	{
		bcn_ctrl_reg = REG_BCN_CTRL;
	}

	if (*((uint8_t *) val)) {
		rtw_write8(Adapter, bcn_ctrl_reg, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
	} else {
		rtw_write8(Adapter, bcn_ctrl_reg, rtw_read8(Adapter, bcn_ctrl_reg)&(~(EN_BCN_FUNCTION | EN_TXBCN_RPT)));
	}


}

static void hw_var_set_mlme_sitesurvey(struct rtl_priv *Adapter, uint8_t variable, uint8_t *val)
{
	uint32_t value_rcr, rcr_clear_bit, reg_bcn_ctl;
	u16 value_rxfltmap2;
	 struct rtw_hal *pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv *pmlmepriv = &(Adapter->mlmepriv);

		reg_bcn_ctl = REG_BCN_CTRL;

	rcr_clear_bit = RCR_CBSSID_BCN;

	/* config RCR to receive different BSSID & not to receive data frame */
	value_rxfltmap2 = 0;

	if ((check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)) {
		rcr_clear_bit = RCR_CBSSID_BCN;
	}

	value_rcr = rtw_read32(Adapter, REG_RCR);

	if (*((uint8_t *) val)) {
		/* under sitesurvey */

		value_rcr &= ~(rcr_clear_bit);
		rtw_write32(Adapter, REG_RCR, value_rcr);

		rtw_write16(Adapter, REG_RXFLTMAP2, value_rxfltmap2);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
			/* disable update TSF */
			rtw_write8(Adapter, reg_bcn_ctl, rtw_read8(Adapter, reg_bcn_ctl)|DIS_TSF_UDT);
		}

		/* Save orignal RRSR setting. */
		pHalData->RegRRSR = rtw_read16(Adapter, REG_RRSR);

	} else {
		/* sitesurvey done */

		if (check_fwstate(pmlmepriv, (_FW_LINKED | WIFI_AP_STATE))) {
			/* enable to rx data frame */
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
		}

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
			/* enable update TSF */
			rtw_write8(Adapter, reg_bcn_ctl, rtw_read8(Adapter, reg_bcn_ctl)&(~(DIS_TSF_UDT)));
		}

		value_rcr |= rcr_clear_bit;
		rtw_write32(Adapter, REG_RCR, value_rcr);

		/* Restore orignal RRSR setting. */
		rtw_write16(Adapter, REG_RRSR, pHalData->RegRRSR);

	}
}

static void Hal_PatchwithJaguar_8812(struct rtl_priv *Adapter, RT_MEDIA_STATUS	MediaStatus)
{
	 struct rtw_hal	*pHalData = GET_HAL_DATA(Adapter);
	struct mlme_ext_priv	*pmlmeext = &(Adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if ((MediaStatus == RT_MEDIA_CONNECT)
	  && (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK_JAGUAR_BCUTAP)) {
		rtw_write8(Adapter, rVhtlen_Use_Lsig_Jaguar, 0x1);
		rtw_write8(Adapter, REG_TCR+3, BIT2);
	} else {
		rtw_write8(Adapter, rVhtlen_Use_Lsig_Jaguar, 0x3F);
		rtw_write8(Adapter, REG_TCR+3, BIT0|BIT1|BIT2);
	}


	if ((MediaStatus == RT_MEDIA_CONNECT)
	   && ((pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK_JAGUAR_BCUTAP)
	      || (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP))) {
		pHalData->Reg837 |= BIT2;
		rtw_write8(Adapter, rBWIndication_Jaguar+3, pHalData->Reg837);
	} else {
		pHalData->Reg837 &= (~BIT2);
		rtw_write8(Adapter, rBWIndication_Jaguar+3, pHalData->Reg837);
	}
}

static void SetHwReg8812A(struct rtl_priv *padapter, uint8_t variable, uint8_t *pval)
{
	struct rtw_hal *pHalData;
	struct dm_priv *pdmpriv;
	struct rtl_dm *podmpriv;
	uint8_t val8;
	u16 val16;
	uint32_t val32;

	pHalData = GET_HAL_DATA(padapter);
	pdmpriv = &pHalData->dmpriv;
	podmpriv = &pHalData->odmpriv;

	switch (variable) {
	case HW_VAR_MEDIA_STATUS:
		val8 = rtw_read8(padapter, MSR) & 0x0c;
		val8 |= *pval;
		rtw_write8(padapter, MSR, val8);
		break;

	case HW_VAR_MEDIA_STATUS1:
		val8 = rtw_read8(padapter, MSR) & 0x03;
		val8 |= *pval << 2;
		rtw_write8(padapter, MSR, val8);
		break;

	case HW_VAR_SET_OPMODE:
		hw_var_set_opmode(padapter, variable, pval);
		break;

	case HW_VAR_MAC_ADDR:
		hw_var_set_macaddr(padapter, variable, pval);
		break;

	case HW_VAR_BSSID:
		hw_var_set_bssid(padapter, variable, pval);
		break;

	case HW_VAR_BASIC_RATE:
		{
			u16 BrateCfg = 0;
			uint8_t RateIndex = 0;

			/*
			 * 2007.01.16, by Emily
			 * Select RRSR (in Legacy-OFDM and CCK)
			 * For 8190, we select only 24M, 12M, 6M, 11M, 5.5M, 2M, and 1M from the Basic rate.
			 * We do not use other rates.
			 */
			HalSetBrateCfg(padapter, pval, &BrateCfg);

			if (pHalData->CurrentBandType == BAND_ON_2_4G) {
				/*
				 * CCK 2M ACK should be disabled for some BCM and Atheros AP IOT
				 * because CCK 2M has poor TXEVM
				 * CCK 5.5M & 11M ACK should be enabled for better performance
				 */
				pHalData->BasicRateSet = BrateCfg = (BrateCfg | 0xd) & 0x15d;
				BrateCfg |= 0x01; /* default enable 1M ACK rate */
			} else { /* 5G */
				pHalData->BasicRateSet &= 0xFF0;
				BrateCfg |= 0x10; /* default enable 6M ACK rate */
			}
			/*
			 * DBG_8192C("HW_VAR_BASIC_RATE: BrateCfg(%#x)\n", BrateCfg);
			 */

			/* Set RRSR rate table. */
			rtw_write8(padapter, REG_RRSR, BrateCfg&0xff);
			rtw_write8(padapter, REG_RRSR+1, (BrateCfg>>8)&0xff);
			rtw_write8(padapter, REG_RRSR+2, rtw_read8(padapter, REG_RRSR+2)&0xf0);
		}
		break;

	case HW_VAR_TXPAUSE:
		rtw_write8(padapter, REG_TXPAUSE, *pval);
		break;

	case HW_VAR_BCN_FUNC:
		hw_var_set_bcn_func(padapter, variable, pval);
		break;

	case HW_VAR_CORRECT_TSF:
		{
			u64	tsf;
			struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
			struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

			/*
			 * tsf = pmlmeext->TSFValue - ((u32)pmlmeext->TSFValue % (pmlmeinfo->bcn_interval*1024)) -1024; //us
			 */
			tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024)) - 1024; /* us */

			if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
			   || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)) {
				/*
				 * pHalData->RegTxPause |= STOP_BCNQ;BIT(6)
				 * rtw_write8(padapter, REG_TXPAUSE, (rtw_read8(padapter, REG_TXPAUSE)|BIT(6)));
				 */
				StopTxBeacon(padapter);
			}

			/* disable related TSF function */
			rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)&(~BIT(3)));

			rtw_write32(padapter, REG_TSFTR, tsf);
			rtw_write32(padapter, REG_TSFTR+4, tsf>>32);

			/* enable related TSF function */
			rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|BIT(3));


			if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
			   || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)) {
				/*
				 * pHalData->RegTxPause  &= (~STOP_BCNQ);
				 * rtw_write8(padapter, REG_TXPAUSE, (rtw_read8(padapter, REG_TXPAUSE)&(~BIT(6))));
				 */
				ResumeTxBeacon(padapter);
			}
		}
		break;

	case HW_VAR_CHECK_BSSID:
		val32 = rtw_read32(padapter, REG_RCR);
		if (*pval)
			val32 |= RCR_CBSSID_DATA|RCR_CBSSID_BCN;
		else
			val32 &= ~(RCR_CBSSID_DATA|RCR_CBSSID_BCN);
		rtw_write32(padapter, REG_RCR, val32);
		break;

	case HW_VAR_MLME_DISCONNECT:
		{
			/* Set RCR to not to receive data frame when NO LINK state
			 * val32 = rtw_read32(padapter, REG_RCR);
			 * val32 &= ~RCR_ADF;
			 * rtw_write32(padapter, REG_RCR, val32);
			 */

			 /* reject all data frames */
			rtw_write16(padapter, REG_RXFLTMAP2, 0x00);

			/* reset TSF */
			val8 = BIT(0) | BIT(1);
			rtw_write8(padapter, REG_DUAL_TSF_RST, val8);

			/* disable update TSF */
			val8 = rtw_read8(padapter, REG_BCN_CTRL);
			val8 |= BIT(4);
			rtw_write8(padapter, REG_BCN_CTRL, val8);
		}
		break;

	case HW_VAR_MLME_SITESURVEY:
		hw_var_set_mlme_sitesurvey(padapter, variable,  pval);

		break;

	case HW_VAR_MLME_JOIN:
		{
			uint8_t RetryLimit = 0x30;
			uint8_t type = *(uint8_t *)pval;
			struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
			EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

			if (type == 0) { 	/* prepare to join  */
				/*
				 * enable to rx data frame.Accept all data frame
				 * rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);
				 */
				rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);

				val32 = rtw_read32(padapter, REG_RCR);
				if (padapter->in_cta_test)
					val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);/* | RCR_ADF */
				else
					val32 |= RCR_CBSSID_DATA|RCR_CBSSID_BCN;
				rtw_write32(padapter, REG_RCR, val32);

				if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE) {
					RetryLimit = (pEEPROM->CustomerID == RT_CID_CCX) ? 7 : 48;
				} else { /* Ad-hoc Mode */
					RetryLimit = 0x7;
				}

				pHalData->bNeedIQK = _TRUE;
			} else if (type == 1) { /* joinbss_event call back when join res < 0  */

				rtw_write16(padapter, REG_RXFLTMAP2, 0x00);
			} else if (type == 2) { /* sta add event call back */
				/* enable update TSF */
				val8 = rtw_read8(padapter, REG_BCN_CTRL);
				val8 &= ~BIT(4);
				rtw_write8(padapter, REG_BCN_CTRL, val8);

				if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE)) {
					RetryLimit = 0x7;
				}
			}

			val16 = RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT;
			rtw_write16(padapter, REG_RL, val16);
		}

		break;

	case HW_VAR_ON_RCR_AM:
		val32 = rtw_read32(padapter, REG_RCR);
		val32 |= RCR_AM;
		rtw_write32(padapter, REG_RCR, val32);
		DBG_8192C("%s, %d, RCR= %x\n", __FUNCTION__, __LINE__, rtw_read32(padapter, REG_RCR));
		break;

	case HW_VAR_OFF_RCR_AM:
		val32 = rtw_read32(padapter, REG_RCR);
		val32 &= ~RCR_AM;
		rtw_write32(padapter, REG_RCR, val32);
		DBG_8192C("%s, %d, RCR= %x\n", __FUNCTION__, __LINE__, rtw_read32(padapter, REG_RCR));
		break;

	case HW_VAR_BEACON_INTERVAL:
		rtw_write16(padapter, REG_BCN_INTERVAL, *(u16 *)pval);
		break;

	case HW_VAR_SLOT_TIME:
		rtw_write8(padapter, REG_SLOT, *pval);
		break;

	case HW_VAR_RESP_SIFS:
		/*
		 * SIFS_Timer = 0x0a0a0808;
		 * RESP_SIFS for CCK
		 */
		rtw_write8(padapter, REG_RESP_SIFS_CCK, pval[0]); 	/* SIFS_T2T_CCK (0x08) */
		rtw_write8(padapter, REG_RESP_SIFS_CCK+1, pval[1]); 	/* SIFS_R2T_CCK(0x08) */
		/*  RESP_SIFS for OFDM */
		rtw_write8(padapter, REG_RESP_SIFS_OFDM, pval[2]); 	/* SIFS_T2T_OFDM (0x0a) */
		rtw_write8(padapter, REG_RESP_SIFS_OFDM+1, pval[3]); 	/* SIFS_R2T_OFDM(0x0a) */
		break;

	case HW_VAR_ACK_PREAMBLE:
		{
			uint8_t bShortPreamble = *pval;

			/*  Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily) */
			val8 = (pHalData->nCur40MhzPrimeSC) << 5;
			if (bShortPreamble)
				val8 |= 0x80;
			rtw_write8(padapter, REG_RRSR+2, val8);
		}
		break;

	case HW_VAR_SEC_CFG:
		val8 = *pval;
		rtw_write8(padapter, REG_SECCFG, val8);
		break;

	case HW_VAR_DM_FLAG:
		podmpriv->SupportAbility = *(u32 *) pval;
		break;

	case HW_VAR_DM_FUNC_OP:
		if (*pval) 	/* save dm flag */
			podmpriv->BK_SupportAbility = podmpriv->SupportAbility;
		else 		/* restore dm flag */
			podmpriv->SupportAbility = podmpriv->BK_SupportAbility;
		break;

	case HW_VAR_DM_FUNC_SET:
		val32 = *(u32 *) pval;
		if (val32 == DYNAMIC_ALL_FUNC_ENABLE) {
			pdmpriv->DMFlag = pdmpriv->InitDMFlag;
			podmpriv->SupportAbility = pdmpriv->InitODMFlag;
		} else {
			podmpriv->SupportAbility |= val32;
		}
		break;

	case HW_VAR_DM_FUNC_CLR:
		val32 = *(u32 *) pval;
		/*
		 *  input is already a mask to clear function
		 *  don't invert it again! George,Lucas@20130513
		 */
		podmpriv->SupportAbility &= val32;
		break;

	case HW_VAR_CAM_EMPTY_ENTRY:
		{
			uint8_t ucIndex = *pval;
			uint8_t i;
			uint32_t	ulCommand = 0;
			uint32_t	ulContent = 0;
			uint32_t	ulEncAlgo = CAM_AES;

			for (i = 0; i < CAM_CONTENT_COUNT; i++) {
				/* filled id in CAM config 2 byte */
				if (i == 0) {
					ulContent |= (ucIndex & 0x03) | ((u16)(ulEncAlgo)<<2);
					/* ulContent |= CAM_VALID; */
				} else 	{
					ulContent = 0;
				}
				/*  polling bit, and No Write enable, and address */
				ulCommand = CAM_CONTENT_COUNT*ucIndex+i;
				ulCommand = ulCommand | CAM_POLLINIG | CAM_WRITE;
				/* write content 0 is equall to mark invalid */
				rtw_write32(padapter, WCAMI, ulContent);  /* delay_ms(40); */
				rtw_write32(padapter, RWCAM, ulCommand);  /* delay_ms(40); */
			}
		}
		break;

	case HW_VAR_CAM_INVALID_ALL:
		val32 = BIT(31) | BIT(30);
		rtw_write32(padapter, RWCAM, val32);
		break;

	case HW_VAR_CAM_WRITE:
		{
			uint32_t cmd;
			uint32_t *cam_val = (u32 *)pval;

			rtw_write32(padapter, WCAMI, cam_val[0]);

			cmd = CAM_POLLINIG | CAM_WRITE | cam_val[1];
			rtw_write32(padapter, RWCAM, cmd);
		}
		break;

	case HW_VAR_CAM_READ:
		break;

	case HW_VAR_AC_PARAM_VO:
		rtw_write32(padapter, REG_EDCA_VO_PARAM, *(u32 *)pval);
		break;

	case HW_VAR_AC_PARAM_VI:
		rtw_write32(padapter, REG_EDCA_VI_PARAM, *(u32 *)pval);
		break;

	case HW_VAR_AC_PARAM_BE:
		pHalData->AcParam_BE = *(u32 *)pval;
		rtw_write32(padapter, REG_EDCA_BE_PARAM, *(u32 *)pval);
		break;

	case HW_VAR_AC_PARAM_BK:
		rtw_write32(padapter, REG_EDCA_BK_PARAM, *(u32 *)pval);
		break;

	case HW_VAR_ACM_CTRL:
		{
			uint8_t acm_ctrl;
			uint8_t AcmCtrl;

			acm_ctrl = *(uint8_t *)pval;
			AcmCtrl = rtw_read8(padapter, REG_ACMHWCTRL);

			if (acm_ctrl > 1)
				AcmCtrl = AcmCtrl | 0x1;

			if (acm_ctrl & BIT(3))
				AcmCtrl |= AcmHw_VoqEn;
			else
				AcmCtrl &= (~AcmHw_VoqEn);

			if (acm_ctrl & BIT(2))
				AcmCtrl |= AcmHw_ViqEn;
			else
				AcmCtrl &= (~AcmHw_ViqEn);

			if (acm_ctrl & BIT(1))
				AcmCtrl |= AcmHw_BeqEn;
			else
				AcmCtrl &= (~AcmHw_BeqEn);

			DBG_8192C("[HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl);
			rtw_write8(padapter, REG_ACMHWCTRL, AcmCtrl);
		}
		break;

	case HW_VAR_AMPDU_MIN_SPACE:
		pHalData->AMPDUDensity = *(uint8_t *)pval;
		break;

	case HW_VAR_AMPDU_FACTOR:
		{
			uint32_t	AMPDULen = *(uint8_t *)pval;

			if (IS_HARDWARE_TYPE_8812(padapter)) {
				if (AMPDULen < VHT_AGG_SIZE_128K)
					AMPDULen = (0x2000 << *(uint8_t *)pval) - 1;
				else
					AMPDULen = 0x1ffff;
			} else if (IS_HARDWARE_TYPE_8821(padapter)) {
				if (AMPDULen < HT_AGG_SIZE_64K)
					AMPDULen = (0x2000 << *(uint8_t *)pval) - 1;
				else
					AMPDULen = 0xffff;
			}
			AMPDULen |= BIT(31);
			rtw_write32(padapter, REG_AMPDU_MAX_LENGTH_8812, AMPDULen);
		}
		break;
	case HW_VAR_H2C_FW_PWRMODE:
		{
			uint8_t psmode = *pval;

			/*
			 * Forece leave RF low power mode for 1T1R to prevent conficting setting in Fw power
			 * saving sequence. 2010.06.07. Added by tynli. Suggested by SD3 yschang.
			 */
			rtl8812_set_FwPwrMode_cmd(padapter, psmode);
		}
		break;

	case HW_VAR_H2C_FW_JOINBSSRPT:
		rtl8812_set_FwJoinBssReport_cmd(padapter, *pval);
		break;

	case HW_VAR_INITIAL_GAIN:
		{
			pDIG_T pDigTable = &podmpriv->DM_DigTable;
			uint32_t rx_gain = *(u32 *)pval;

			if (rx_gain == 0xff) {		/* restore rx gain */
				ODM_Write_DIG(podmpriv, pDigTable->BackupIGValue);
			} else {
				pDigTable->BackupIGValue = pDigTable->CurIGValue;
				ODM_Write_DIG(podmpriv, rx_gain);
			}
		}
		break;


#if (RATE_ADAPTIVE_SUPPORT == 1)
	case HW_VAR_RPT_TIMER_SETTING:
		{
			val16 = *(u16 *)pval;
			ODM_RA_Set_TxRPT_Time(podmpriv, val16);
		}
		break;
#endif

#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	case HW_VAR_ANTENNA_DIVERSITY_LINK:
		/* SwAntDivRestAfterLink8192C(padapter); */
		ODM_SwAntDivRestAfterLink(podmpriv);
		break;

	case HW_VAR_ANTENNA_DIVERSITY_SELECT:
		{
			uint8_t Optimum_antenna = *pval;
			uint8_t 	Ant;

			/*
			 * switch antenna to Optimum_antenna
			 * DBG_8192C("==> HW_VAR_ANTENNA_DIVERSITY_SELECT , Ant_(%s)\n",(Optimum_antenna==2)?"A":"B");
			 */
			if (pHalData->CurAntenna != Optimum_antenna) {
				Ant = (Optimum_antenna == 2) ? MAIN_ANT : AUX_ANT;
				ODM_UpdateRxIdleAnt_88E(podmpriv, Ant);

				pHalData->CurAntenna = Optimum_antenna;
				/*
				 * DBG_8192C("==> HW_VAR_ANTENNA_DIVERSITY_SELECT , Ant_(%s)\n",(Optimum_antenna==2)?"A":"B");
				 */
			}
		}
		break;
#endif

	case HW_VAR_EFUSE_USAGE:
		pHalData->EfuseUsedPercentage = *pval;
		break;

	case HW_VAR_EFUSE_BYTES:
		pHalData->EfuseUsedBytes = *(u16 *)pval;
		break;
	case HW_VAR_FIFO_CLEARN_UP:
		{
			struct pwrctrl_priv *pwrpriv;
			uint8_t trycnt = 100;

			pwrpriv = &padapter->pwrctrlpriv;

			/* pause tx */
			rtw_write8(padapter, REG_TXPAUSE, 0xff);

			/* keep sn */
			padapter->xmitpriv.nqos_ssn = rtw_read16(padapter, REG_NQOS_SEQ);

			if (pwrpriv->bkeepfwalive != _TRUE) {
				/* RX DMA stop */
				val32 = rtw_read32(padapter, REG_RXPKT_NUM);
				val32 |= RW_RELEASE_EN;
				rtw_write32(padapter, REG_RXPKT_NUM, val32);
				do {
					val32 = rtw_read32(padapter, REG_RXPKT_NUM);
					val32 &= RXDMA_IDLE;
					if (!val32)
						break;
				} while (trycnt--);
				if (trycnt == 0) {
					DBG_8192C("[HW_VAR_FIFO_CLEARN_UP] Stop RX DMA failed......\n");
				}

				/* RQPN Load 0 */
				rtw_write16(padapter, REG_RQPN_NPQ, 0x0);
				rtw_write32(padapter, REG_RQPN, 0x80000000);
				mdelay(10);
			}
		}
		break;


#if (RATE_ADAPTIVE_SUPPORT == 1)
	case HW_VAR_TX_RPT_MAX_MACID:
		{
			uint8_t maxMacid = *pval;
			DBG_8192C("### MacID(%d),Set Max Tx RPT MID(%d)\n", maxMacid, maxMacid+1);
			rtw_write8(padapter, REG_TX_RPT_CTRL+1, maxMacid+1);
		}
		break;
#endif

	case HW_VAR_H2C_MEDIA_STATUS_RPT:
		{
			struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
			RT_MEDIA_STATUS	mstatus = *(u16 *)pval & 0xFF;

			rtl8812_set_FwMediaStatus_cmd(padapter, *(u16 *)pval);

			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
				Hal_PatchwithJaguar_8812(padapter, mstatus);
		}
		break;

	case HW_VAR_APFM_ON_MAC:
		pHalData->bMacPwrCtrlOn = *pval;
		DBG_8192C("%s: bMacPwrCtrlOn=%d\n", __FUNCTION__, pHalData->bMacPwrCtrlOn);
		break;

	case HW_VAR_NAV_UPPER:
		{
			uint32_t usNavUpper = *((u32 *)pval);

			if (usNavUpper > HAL_NAV_UPPER_UNIT * 0xFF) {
				DBG_8192C("%s: [HW_VAR_NAV_UPPER] set value(0x%08X us) is larger than (%d * 0xFF)!\n",
					__FUNCTION__, usNavUpper, HAL_NAV_UPPER_UNIT);
				break;
			}

			/*
			 *  The value of ((usNavUpper + HAL_NAV_UPPER_UNIT - 1) / HAL_NAV_UPPER_UNIT)
			 * is getting the upper integer.
			 */
			usNavUpper = (usNavUpper + HAL_NAV_UPPER_UNIT - 1) / HAL_NAV_UPPER_UNIT;
			rtw_write8(padapter, REG_NAV_UPPER, (uint8_t)usNavUpper);
		}
		break;

	case HW_VAR_BCN_VALID:
		{
			/*
			 * BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2, write 1 to clear, Clear by sw
			 */
			val8 = rtw_read8(padapter, REG_TDECTRL+2);
			val8 |= BIT(0);
			rtw_write8(padapter, REG_TDECTRL+2, val8);
		}
		break;

	case HW_VAR_DL_BCN_SEL:
		{
			/* SW_BCN_SEL - Port0 */
			val8 = rtw_read8(padapter, REG_TDECTRL1_8812+2);
			val8 &= ~BIT(4);
			rtw_write8(padapter, REG_TDECTRL1_8812+2, val8);
		}
		break;

	case HW_VAR_WIRELESS_MODE:
		{
			uint8_t	R2T_SIFS = 0, SIFS_Timer = 0;
			uint8_t	wireless_mode = *pval;

			if ((wireless_mode == WIRELESS_11BG) || (wireless_mode == WIRELESS_11G))
				SIFS_Timer = 0xa;
			else
				SIFS_Timer = 0xe;

			/* SIFS for OFDM Data ACK */
			rtw_write8(padapter, REG_SIFS_CTX+1, SIFS_Timer);
			/* SIFS for OFDM consecutive tx like CTS data! */
			rtw_write8(padapter, REG_SIFS_TRX+1, SIFS_Timer);

			rtw_write8(padapter, REG_SPEC_SIFS+1, SIFS_Timer);
			rtw_write8(padapter, REG_MAC_SPEC_SIFS+1, SIFS_Timer);

			/* 20100719 Joseph: Revise SIFS setting due to Hardware register definition change. */
			rtw_write8(padapter, REG_RESP_SIFS_OFDM+1, SIFS_Timer);
			rtw_write8(padapter, REG_RESP_SIFS_OFDM, SIFS_Timer);

			/*
			 * Adjust R2T SIFS for IOT issue. Add by hpfan 2013.01.25
			 * Set R2T SIFS to 0x0a for Atheros IOT. Add by hpfan 2013.02.22
			 *
			 * Mac has 10 us delay so use 0xa value is enough.
			 */
			R2T_SIFS = 0xa;

			rtw_write8(padapter, REG_RESP_SIFS_OFDM+1, R2T_SIFS);
		}
		break;

	default:
		DBG_8192C("%s: [WARNNING] variable(%d) not defined!\n", __FUNCTION__, variable);
		break;
	}
}

void rtl8821au_set_hw_reg(struct rtl_priv *Adapter, u8 variable,u8 *val)
{
	 struct rtw_hal	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct rtl_dm *podmpriv = &pHalData->odmpriv;

	switch (variable) {
	case HW_VAR_RXDMA_AGG_PG_TH:
#ifdef CONFIG_USB_RX_AGGREGATION
		{
			/*uint8_t	threshold = *((uint8_t *)val);
			if ( threshold == 0)
			{
				threshold = pHalData->UsbRxAggPageCount;
			}
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, threshold);*/
		}
#endif
		break;
	case HW_VAR_SET_RPWM:
		break;
	case HW_VAR_USB_MODE:
		if (*val == 1)
			rtw_write8(Adapter, REG_OPT_CTRL_8812, 0x4);
		else if (*val == 2)
			rtw_write8(Adapter, REG_OPT_CTRL_8812, 0x8);

		rtw_write8(Adapter, REG_SDIO_CTRL_8812, 0x2);
		rtw_write8(Adapter, REG_ACLK_MON, 0x1);
		/*
		 * 2013/01/29 MH Test with chunchu/cheng, in Alpha AMD platform. when
		 * U2/U3 switch 8812AU will be recognized as another card and then
		 * OS will treat it as a new card and assign a new GUID. Then SWUSB
		 * service can not work well. We need to delay the card switch time to U3
		 * Then OS can unload the previous U2 port card and load new U3 port card later.
		 * The strange sympton can disappear.
		 */
		rtw_write8(Adapter, REG_CAL_TIMER+1, 0x40);
		/* rtw_write8(Adapter, REG_CAL_TIMER+1, 0x3); */
		rtw_write8(Adapter, REG_APS_FSMCO+1, 0x80);
		break;
	default:
		SetHwReg8812A(Adapter, variable, val);
		break;
	}
}

void rtl8821au_get_hw_reg(struct rtl_priv *padapter, u8 variable,u8 *pval)
{
	struct rtw_hal *pHalData;
	struct rtl_dm *podmpriv;
	uint8_t val8;
	u16 val16;
	uint32_t val32;

	pHalData = GET_HAL_DATA(padapter);
	podmpriv = &pHalData->odmpriv;

	switch (variable) {
	case HW_VAR_BASIC_RATE:
		*(u16 *)pval = pHalData->BasicRateSet;
		break;

	case HW_VAR_TXPAUSE:
		*pval = rtw_read8(padapter, REG_TXPAUSE);
		break;

	case HW_VAR_BCN_VALID:
		{
			/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2 */
			val8 = rtw_read8(padapter, REG_TDECTRL+2);
			*pval = (BIT(0) & val8) ? _TRUE:_FALSE;
		}
		break;

	case HW_VAR_DM_FLAG:
		*pval = podmpriv->SupportAbility;
		break;

	case HW_VAR_RF_TYPE:
		*pval = pHalData->rf_type;
		break;

	case HW_VAR_FWLPS_RF_ON:
		/* When we halt NIC, we should check if FW LPS is leave. */
		if (padapter->pwrctrlpriv.rf_pwrstate == rf_off) {
			/*
			 *  If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave,
			 *  because Fw is unload.
			 */
			*pval = _TRUE;
		} else {
			uint32_t valRCR;
			valRCR = rtw_read32(padapter, REG_RCR);
			valRCR &= 0x00070000;
			if (valRCR)
				*pval = _FALSE;
			else
				*pval = _TRUE;
		}

		break;

#ifdef CONFIG_ANTENNA_DIVERSITY
	case HW_VAR_CURRENT_ANTENNA:
		*pval = pHalData->CurAntenna;
		break;
#endif
	case HW_VAR_EFUSE_BYTES: /*  To get EFUE total used bytes, added by Roger, 2008.12.22. */
		*(u16 *)pval = pHalData->EfuseUsedBytes;
		break;

	case HW_VAR_APFM_ON_MAC:
		*pval = pHalData->bMacPwrCtrlOn;
		break;

	case HW_VAR_CHK_HI_QUEUE_EMPTY:
		val16 = rtw_read16(padapter, REG_TXPKT_EMPTY);
		*pval = (val16 & BIT(10)) ? _TRUE:_FALSE;
		break;

	default:
		DBG_8192C("%s: [WARNNING] variable(%d) not defined!\n", __FUNCTION__, variable);
		break;
	}
}


