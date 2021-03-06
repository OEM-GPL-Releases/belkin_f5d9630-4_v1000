/*****************************************************************************/
/*
 *          mxser.c  -- MOXA Smartio family multiport serial driver.
 *
 *      Copyright (C) 1999-2000  Moxa Technologies (support@moxa.com.tw).
 *
 *      This code is loosely based on the Linux serial driver, written by
 *      Linus Torvalds, Theodore T'so and others.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 *    MOXA Smartio Family Serial Driver
 *
 *      Copyright (C) 1999,2000  Moxa Technologies Co., LTD.
 *
 *      for             : LINUX 2.0.X, 2.2.X, 2.4.X
 *      date            : 2001/05/01
 *      version         : 1.2 
 *      
 *    Fixes for C104H/PCI by Tim Hockin <thockin@sun.com>
 *    Added support for: C102, CI-132, CI-134, CP-132, CP-114, CT-114 cards
 *                        by Damian Wrobel <dwrobel@ertel.com.pl>
 *
 *    Added support for serial card CP104
 *			  by James Nelson Provident Solutions <linux-info@provident-solutions.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

#define		MXSER_VERSION			"1.2.1"

#define		MXSERMAJOR	 	174
#define		MXSERCUMAJOR		175


#define	MXSER_EVENT_TXLOW	 1
#define	MXSER_EVENT_HANGUP	 2


#define 	SERIAL_DO_RESTART

#define 	MXSER_BOARDS		4	/* Max. boards */
#define 	MXSER_PORTS		32	/* Max. ports */
#define 	MXSER_PORTS_PER_BOARD	8	/* Max. ports per board */
#define 	MXSER_ISR_PASS_LIMIT	256

#define		MXSER_ERR_IOADDR	-1
#define		MXSER_ERR_IRQ		-2
#define		MXSER_ERR_IRQ_CONFLIT	-3
#define		MXSER_ERR_VECTOR	-4

#define 	SERIAL_TYPE_NORMAL	1

#define 	WAKEUP_CHARS		256

#define 	UART_MCR_AFE		0x20
#define 	UART_LSR_SPECIAL	0x1E

#define PORTNO(x)		((x)->index)

#define RELEVANT_IFLAG(iflag)	(iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

#define IRQ_T(info) ((info->flags & ASYNC_SHARE_IRQ) ? SA_SHIRQ : SA_INTERRUPT)

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

/*
 *    Define the Moxa PCI vendor and device IDs.
 */

#ifndef	PCI_VENDOR_ID_MOXA
#define	PCI_VENDOR_ID_MOXA	0x1393
#endif
#ifndef PCI_DEVICE_ID_C168
#define PCI_DEVICE_ID_C168	0x1680
#endif
#ifndef PCI_DEVICE_ID_C104
#define PCI_DEVICE_ID_C104	0x1040
#endif
#ifndef PCI_DEVICE_ID_CP104
#define PCI_DEVICE_ID_CP104	0x1041
#endif
#ifndef PCI_DEVICE_ID_CP132
#define PCI_DEVICE_ID_CP132	0x1320
#endif
#ifndef PCI_DEVICE_ID_CP114
#define PCI_DEVICE_ID_CP114	0x1141
#endif
#ifndef PCI_DEVICE_ID_CT114
#define PCI_DEVICE_ID_CT114	0x1140
#endif

#define C168_ASIC_ID    1
#define C104_ASIC_ID    2
#define CI134_ASIC_ID   3
#define CI132_ASIC_ID   4
#define CI104J_ASIC_ID  5
#define C102_ASIC_ID	0xB

enum {
	MXSER_BOARD_C168_ISA = 0,
	MXSER_BOARD_C104_ISA,
	MXSER_BOARD_CI104J,
	MXSER_BOARD_C168_PCI,
	MXSER_BOARD_C104_PCI,
	MXSER_BOARD_CP104_PCI,
	MXSER_BOARD_C102_ISA,
	MXSER_BOARD_CI132,
	MXSER_BOARD_CI134,
	MXSER_BOARD_CP132_PCI,
	MXSER_BOARD_CP114_PCI,
	MXSER_BOARD_CT114_PCI
};

static char *mxser_brdname[] =
{
	"C168 series",
	"C104 series",
	"CI-104J series",
	"C168H/PCI series",
	"C104H/PCI series",
	"CP104/PCI series",
	"C102 series",
	"CI-132 series",
	"CI-134 series",
	"CP-132 series",
	"CP-114 series",
	"CT-114 series"
};

static int mxser_numports[] =
{
	8,
	4,
	4,
	8,
	4,
	4,
	2,
	2,
	4,
	2,
	4,
	4
};

/*
 *    MOXA ioctls
 */
#define 	MOXA		0x400
#define 	MOXA_GETDATACOUNT     (MOXA + 23)
#define		MOXA_GET_CONF         (MOXA + 35)
#define 	MOXA_DIAGNOSE         (MOXA + 50)
#define 	MOXA_CHKPORTENABLE    (MOXA + 60)
#define 	MOXA_HighSpeedOn      (MOXA + 61)
#define         MOXA_GET_MAJOR        (MOXA + 63)
#define         MOXA_GET_CUMAJOR      (MOXA + 64)
#define         MOXA_GETMSTATUS       (MOXA + 65)

#ifdef CONFIG_PCI
static struct pci_device_id mxser_pcibrds[] = {
	{ PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_C168, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
	  MXSER_BOARD_C168_PCI },
	{ PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_C104, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
	  MXSER_BOARD_C104_PCI },
	{ PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_CP104, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 
	  MXSER_BOARD_CP104_PCI },
	{ PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_CP132, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  MXSER_BOARD_CP132_PCI },
	{ PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_CP114, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  MXSER_BOARD_CP114_PCI },
	{ PCI_VENDOR_ID_MOXA, PCI_DEVICE_ID_CT114, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	  MXSER_BOARD_CT114_PCI },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, mxser_pcibrds);
#endif /* CONFIG_PCI */

static int ioaddr[MXSER_BOARDS];
static int ttymajor = MXSERMAJOR;
static int verbose;

/* Variables for insmod */

MODULE_AUTHOR("William Chen");
MODULE_DESCRIPTION("MOXA Smartio Family Multiport Board Device Driver");
MODULE_LICENSE("GPL");
MODULE_PARM(ioaddr, "1-4i");
MODULE_PARM(ttymajor, "i");
MODULE_PARM(verbose, "i");

struct mxser_hwconf {
	int board_type;
	int ports;
	int irq;
	int vector;
	int vector_mask;
	int uart_type;
	int ioaddr[MXSER_PORTS_PER_BOARD];
	int baud_base[MXSER_PORTS_PER_BOARD];
	struct pci_dev *pdev;
};

struct mxser_struct {
	int port;
	int base;		/* port base address */
	int irq;		/* port using irq no. */
	int vector;		/* port irq vector */
	int vectormask;		/* port vector mask */
	int rx_trigger;		/* Rx fifo trigger level */
	int baud_base;		/* max. speed */
	int flags;		/* defined in tty.h */
	int type;		/* UART type */
	struct ailsnBenu

                                                 	256

#ss optiCE_ID_CP104#devectnsmodFAEy(8) manpage f           fpage#define rs_dprintk(f, str...)0ion56

#ss optiCE_ID_CP1RIBM P(f, str.dprintk(f, 2ntk(f,Ioae;		/*=tiCE_ID_CP1RIBM P(f, str.dp Ftape-  Define theE vecto4)
		/* Rx Tnecy PTrheE veect5    o_EEEVICE_ B optiCE_Ie-  Define theE number 8mort base adewne thUecy PTrheE veect5    o_EEEVICE_ B h>
TR-3") com
T-114AIpends BM P(f,VICE_ yrivercan r8	0x168  To comprDxGenerC      endi) CI_DEx:fig RTC
12*			   <linuxentation oEST	0x0#tit2dnamMD/VI_ yrivercan S
	int "Maximum nuuuuuuuuuuArcan S
	int "MaximuYuuuhACI_56

#ss optiCE_ID_CP1it -iivercan S
	  beYuuuhACI_56

#ss optiCE_ID_CP1it -iivercan S
	  beYuuuhACI_565D_CP1it -iCEystem aI         ) C    o_ENfem aI         ) Cas a modmuOR_ID  beYuBmf_ "HPGmask;		/_PE.I-ivercad can  RAW t vectI-iverc endi) CI_DEx:fig+ 64D  beYuBmf_ "HPGmask;		/_PE4 PCI_or_mask;
	in(8	0         MOXA_GET_MAJOR    _CP1   iT_MAJOR cdF{
	MFheE numSABLE(p  MIN
#dMOXA		0x400
#defiChen")t uartPCI_VEN     boarde 	SERI,l: -iE(p  MIn      kernel/linux/drivers/char/seri                                0     _IDserlsptiCE_ID_CPswi    ) MD/         R cdoevect2be ung. I/O 
(E_ B ate "TA B ;0x400
#defiChets b9cdoeve/_PE4 PCI_or_mavect    d6"
	help
S            aspI_or_ma!ABLE(CE_ B Orannel ux/ly Se. Waerfaces ux/S0020
#d"
	helwPE4 P 0
#def aI         ) C    o_ENfem aI  ove, you VpI_or_ma!ABLE(CEdefine Rx     	helwPE4 P 0
#def aI         ) C    o
	  This no a prof>
#i   	helwP); B h>
TR-3") com
T-114AIpendbs tober ofdriver.

rhis numdooned suppor the R       abr serial card CP PCyB h>
TR-3") elID_CNn    / + 23)
#deevem            elID_CNn    / + 23)
#deevem    l
	help
S         di    nicatuppor t2ppor G*/
#define d it is sFn   for wi    )     kp*wi    )     rsCI_ANY_di     di    n        eABLE    (MOXA + 60)
#define 	MOXA_HighSpeedOn      (MOID_CPswi    ) MD/    n   i  n     _or_mav.

rhYuuuhACI_565D_CP1it ID   3sa.2r set2ppor G*/
#(CE_ B(iE(p  MIn      kernel/linux/drivesll@realityd3sa.2r set2ppor G*/
#(CE_     optiompilAdefine Rx     ( whataction_dprGH1/05ilAdefine Rx     _Grq;		/* ( whataction_dprGH1/05ilAdefse of CMOle thiaccess _hACI_56e called sNDO    o7(ction5 riveis no wi    )8uO1addh>
E1MWAV<lin1O1ad#def 66_CP1RIBM P(f, str.dp Ftape-  Define theE0fine theE0fine;		/nel ux/ly Se. Wanse verr^;but WITHOU. Wanse vou wan20R7OU. Wa1VI_ yrive XHOU. WanA60ne g },
	{ PCI_VENDOR_ID_MOXA, PCI_2Ole thi your cle thcfg}  n, PC, PCI_Q0aHOU. Wa(A baud_base[MR, PCWa(A bauge_ord s
	int typ, DOR_ID_MOXA, PCI_2OltPCDevice Driver");
MODULE_E.PCI>.

	  The ftapa1VoBhe ftapa1VoXA, PCI_2OltPeDrive 0lt values for t {
	}UBhe(value*e1ftaXA, PCI_2OlDxGenerC      endi)Rcied Fhn_dO;0d buttMOXA, PCI max. speed */
	int flags;		/* defined in tty.h */
	int tyem aI1/eedOI_2OltPeDrive 0lt valu  n, idOI_2OltPerallel link cable is taXA, PCI_2OlDxGenerCfI_Q0aHOU. Wa(eOR_ID_MOXA, PCISER_BOARD-II, a Tech_inCfI_Q0aHOU. Wa(eOR_ID_MOXA, PCISER_BOA, PCI_2OlDxGener TechHOU. Wa(eOR_ID_MOXA, PCISER_BOA, PCI_2OlD ( OXA_GETDATACOUNT     computbe c., PCI_ANY_ID, 0, 0, 
	  MY_ItPe., Pfined in _h   erayrive XHeY_ID, endi) C(ALPHA ||   _ rs_dprintk(TX3912_UART_DEBamm s) an_ANY   er1ve XH.tPerallFC_ID   sl/linuEs)      och40ne g_TUD   sl/linu!/1ID_.*elwP); B h   er1ve XH.tPe "i");

struct mxser_hwconuA, PCI_iuuuhACI_5SER_BsnoA, PCI_2_ItPI_2OlDxGenerC ad.or_NDOR_ID_MOXA, PCI_2Ole thi  aI     XA, PCIlI,tr_NDO0         haracter specialewess _hw devices.

config0x00000200
#definer canE_ID_CPspeci. WaerfasU. Wa(A baud_base[5_8B016101 e Rx     _Grq;		/* ( whataction_dprGH1/05ilAdefse of CMOle thiaccess _hACI_5 devices.

config0x00000200
#defit_UART_DEBamm  MIn      . filcess _hACI_5 devacter specialewess _hw devices.

config0x00000200
#definer canE_ID   endi)Rcinbe        I_2OlDxian Wrig0x000lwPE4 P 0
#def aI         ) C    o
	  This n2EVICE_ine"#def       I_2OlDxian Wrig0x000lwPE o
	  T    ) C__user.0
#defit_UART_DEBamm    <http://www.applicom-int.com/>, or by email from David Wo s for tr)lp be use1O1ad#def 66_us car70
#def aSPECIAL	0x1E

#define PORTNO(x)		((x)->index)

#define RELEVANT_IFLAG(iflag)	(iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

#define IRQ_2config0x00o#define INPAR|PARMRK|INPCRMRK|sNPCK))

#define IRQ_2config0x00o#defiany controller~des kerttRcinbe        I_2OB Orannel_s"
};

stI_2, PCISER_BOA, PCI_    ER_BOA, PCI_2OlD ( OXA_GETDATACOUNT     computbe ( OXA_GETDATACOUNLaTDATACOOVr tr)lp be use1Oeneral Pbe c., PCI_ANY_ID,.2el/linux/drive TX3912_UAfw)
MAJOR ci.C!=y n,  ) C    o_lET_Ess */
	)
0	nput IR"Un recR ci.C!,|
	int,  ) (R ci|=_s"
}unT	0x0#tiS      (    aspI_or_m))lET_Ess */
	C 256. InunT	0x0#tifree software; you ca OXA_GETDATAC|
	int,rCfIlp
S      (    aspI_or_m)=y 	PCI_(ii.C!= i <rtio Family S= i++)de 	,  ) dh>
E1MWAi].irq no. */ == -1lET_	eded fornt,		icode 	,	stri_d.o dh>
E1MWAi].d.o, &. Waerfaces iTAC/
	int baud_base;		/* int,	}
	}
n,  ) C    o_lET_Ess */
	Dwi ,|
	int
}
nID_MOXA, PCOVrbrdine;		/nel ux/ly Se. Wanse verr^;but WIT_UAf0x00o#define INPAR|PAT_IF;namMD/VI_ yriver         PTrhetva S
	  beAJOn=y 	COVr_MUTEX(& set2ppor G*/
#(Cint,
	ni.C0x1E

#C/
	int baud_base;		/*S
	  foi.C&. Waerfaces n MINPCI_(ii.C!= i <rbut WI->ector; i++JOn++JO  fo++)de 	,  ) C    o_lde 	,	Ess */
	0, 0, 
	lp
M%d/cum%nfige/_%04x "JOnJOnJObut WI->/
	int i int,	,  ) but WI->er level *i] == hACI_5lET_		Ess */
	0f, ster l 
	  bove, yhACI_5 bps,|
	int,,		icoET_		Ess */
	0f, ster l 
	  bove, yo7(cti bps,|
	int,,} 	, e PCImanpa=On=y	, e PCIuct a=Obut WI->/
	int i =y	, e PCIopti=Obut WI->/rt ir, e PCIge f   =Obut WI->ctor */
	, e PCIge f       =Obut WI->ctor *-  Defin, e PCI#define rs =O14=y	, e PCIuc level  =Obut WI->er level *i]=y	, e PCI_DEVIC=_ID_C168	0x1680
=y	, e PCI. */ =Obut WI->rt vector ma,  ) ( e PCI. */ ==dor a_16450)    ( e PCI. */ ==dor a_es.
)lET_, e PCIeE number 8mor =O1nt,		icoET_, e PCIeE number 8mor =O16=y	, e PCIadewne thUecy  =Obut WI->er level *i]
#C16=y	, e PCIa14AIpends  =O5_masZ / 10=y	, e PCIa14A r8	0x16 =O30_masZ=y	,xfff_WORK(& e PCI.rcad ("GPL");A, PCI_2OlJO  fo)=y	, e PCIae	PCI= BAdef | CS8 | Cis s | HUPCL | CLOCAL=y	, e nu0x16rcad can  (& e PCICas a mod)=y	, e nu0x16rcad can  (& e PCIGmask;		/_)=y	, e nu0x16rcad can  (& e PCI t vectI-iverc)=y	}
n,TDA	_mahe uc  modulegistefmber ofaow.

ne thaw d_DEVI(_DEVI)=y 	ni.C0x1E

#C/
	int baud_base;		/*S
	  foi.C&. Waerfaces n MI
	cli()=y	hetva i.Cist
	st_d.o but WI->/rt,D_MOXA, PCI_2Ole,E_ID_C168
#d,
nux/tttt"_MOXA"JO  fo)=y	  ) hetva lde 	,r	st opt_DEVI(_DEVI)=y		Ess */
	nt bau%d: %s"JO	/nel u,
	4,
	4,
	8,
but WI->erq no. */])=y		Ess */
	  Rst
	st optifail,gist(%d)e a misct WIlblic FDC  hvcs.k as a m	stru e PCIopt)=y		r_NDOR) hetva l=y	}
,r	st opt_DEVI(_DEVI)=y
	r_NDOR)0=y}
-  Define theE0fine theE0fine;		/nel ux/ly Se. Wanse verr^;but WIT_UAfdh>
E1MWA	/nel, 0,;but WI=y}
-NDOR_ID_MOXA, PCI_DEVICE_)

#define RELESCRa1VI_. speed */
	int flagdefineerq no. */ ux/ly Se. Wanse verr^;but WIT_UAf  beA;namMD/VI_ ysk */
	int of=y
	but WI->erq no. */i.C0x1E
ector mabut WI->ectori.C (MOXA + 60)
#derq no. */]=y	 
	int ofi.C */
t os
	  000002 (flagde2)MINPCI_(ii.C!= i <rbut WI->ector; i++lET_but WI->/
	int i i.C 
	int ofi+ 8912_=y 	C
	int ofi.C */
t os
	  000002 (flagde3) mabut WI->ge f   =O/
	int of=y
	but WI->opti=Oflag->/rt i
	but WI->rt vector =dor a_16550A mabut WI->ge f  _     =O0MINPCI_(ii.C!= i <rbut WI->ector; i++lde 	,but WI->ge f  _     |=_(1 << i)=y		but WI->er level *i] =yo7(cti=y	}
,r	NDOR) 0)=y}
RM(ioaddr, "1-4i");
MODULE_PARM(rannel_s"
}upport fos}  n, PC,s 0, 
	.. It
=}  n, PC,en,
n.Gmask
=}  n, PGmask,
n. PCyB =}  n, P PCyB,
n.rCfI_Q0a =}  n, PrCfI_Q0a,
n. idOI_2OltP =}  n, P idOI_2OltP,
n. PCyBXA, P =}  n, P PCyBXA, P,
n.Gn tty.h */
	in
=}  n, PGn tty.h */
	in,
n. idOI_*/
	in
=}  n, P idOI_*/
	in,
n.CISER
=}  n, PCISER,
n.HeY_ID, 
=}  n, PHeY_ID, ,
n. XHeY_ID, 
=}  n, P XHeY_ID, ,
n.RT_DEBamm s
=}  n, PRT_DEBamm s,
n.Rtop
=}  n, PRtop,
n.Rtanpa=Ofig0x000002,
n.5SER_Ba=Ofig0x05SER_B,
n.Hfine IR
=}  n, PHfine IR,
n.HfinesIR
=}  n, PHfinesIR,
HKPORTENABLE   _PCOVr,.2el/linux/drCOVrTX3912_UAfw)
MAJOm,rhetva JO	;Af0x00o#define e verr^but WI=yAfdh  aspI_or_m
=}ET
	 Ilp
S      (/
	int baudi+ 1)=y	  ) !    aspI_or_m)y		r_NDOR)-ENOMEM=yAfEss */
	ree software; you caI_or_m
 *			  b;
	struR_BOARD	8	/* )=y
	GenerVr tr)lmodulelp
S       F charactMODULfdh  aspI_or_mCICwn_m
=}THIS_ase;		;Lfdh  aspI_or_mCI
	8,
=}"lp
M";Lfdh  aspI_or_mCIlagfs_
	8,
=}"lps/M";Lfdh  aspI_or_mCI);

struint port;Lfdh  aspI_or_mCI)e
	 _Rtanpa=O0;Lfdh  aspI_or_mCIctor =dTTYule det : SA__SHIRQ;Lfdh  aspI_or_mCIsubctor =d_SHIRQ : SA_INTERR;Lfdh  aspI_or_mCI e nuEBamm s
=}s"
};
PE o
	  T;Lfdh  aspI_or_mCI e nuEBamm s.c_ae	PCI= BAdef | CS8 | Cis s | HUPCL | CLOCAL=y	dh  aspI_or_mCI_DEVIC=_TTYule det is Lstat=y	s"
};T_Dupport fos(    aspI_or_m, &. n, PC,sint,rss */
	Tty a certairtc. If you e=u%d	struint port)=yAfdh  asyou VpI_a=O0;Lfde2OB O. Waerfaces B at/
	int baudi* 8morofg0x00o#define INPAR|));Lfde2OB O&    ) C   B at8morofg0x00o#define C  ));LyAfda=O0;LfGenStanpafifrequeer_bERIAL_Tthat ne tPCI_(bi.C!= b <rtio Family Stheim <rtio Family S= b++)de 	,     ap ma,  ) !( ap
=}  n,      R cdb])lET_	eded fornty		r_Nva i.Cu wan20R7OU. Wa1VI_ ap, &but WITHOma,  ) r_Nva i!.C!) 	,	Ess */
	     free s%sC0x1E

(R c=/_%x)	str
nux/tttt u,
	4,
	4,
	8,
but WI.irq no. */]r
nux/tttt u/
	int b]THOma,  ) r_Nva i<.C!)de 	,	  ) r_Nva i=ruct mx

#definlET_		Ess */
	Inva idnhanced Reaf you ,0x1E

er sand create|
	int,,		ico   ) r_Nva i=ruct mx

#define IRQ_T(lET_		Ess */
	Inva idnhanced Reaf you ,0x1E

er sand create|
	int,,		ico   ) r_Nva i=ruct mx

#deSYNC_SlET_		Ess */
	Inva idnhanced Reage f  ,0x1E

er sand create|
	int,,		ico   ) r_Nva i=ruct mx

#deflag)	lET_		Ess */
	Inva idnag.

int of,0x1E

er sand create|
	intET_	eded fornt,	}y		but WI.flagi.CNULLHOma,  ) OXA, PCOVrbrdim, &but WIT <r0lET_	eded fornty		0fine theE0fim, &but WITHOma,m++=y	}
n,TDnStanpafifrequeer_bERIAL_Tamed R"Un rearg ne tPCI_(bi.C!= b <rtio Family Stheim <rtio Family S= b++)de 	,     ap ma,  ) !( ap
=}/
	int b]TlET_	eded fornty		r_Nva i.Cu wan20R7OU. Wa1VI_ ap, &but WITHOma,  ) r_Nva i!.C!) 	,	Ess */
	     free s%sC0x1E

(R c=/_%x)	str
nux/tttt u,
	4,
	4,
	8,
but WI.irq no. */]r
nux/tttt u/
	int b]THOma,  ) r_Nva i<.C!)de 	,	  ) r_Nva i=ruct mx

#definlET_		Ess */
	Inva idnhanced Reaf you ,0x1E

er sand create|
	int,,		ico   ) r_Nva i=ruct mx

#define IRQ_T(lET_		Ess */
	Inva idnhanced Reaf you ,0x1E

er sand create|
	int,,		ico   ) r_Nva i=ruct mx

#deSYNC_SlET_		Ess */
	Inva idnhanced Reage f  ,0x1E

er sand create|
	int,,		ico   ) r_Nva i=ruct mx

#deflag)	lET_		Ess */
	Inva idnag.

int of,0x1E

er sand create|
	intET_	eded fornt,	}y		but WI.flagi.CNULLHOma,  ) OXA, PCOVrbrdim, &but WIT <r0lET_	eded fornty		0fine theE0fim, &but WITHOma,m++=y	}
n,TDnstanpafifreque@provx1E

that ne -NDOR_ID_MOXA, PCI_	e 	,. speed */
	int flagi.CNULLHO	,    t
=}(8morofgGPL");
MODULE_ / 8morofgGPL");
MODULE[0]Tl -O1nt,	PCI_(bi.C!= b <rn= b++)de 	,	dbs to((flagi.C */
fifrPCI_ANYgGPL");
MODULE[b].E_ID_C("GPL");
MODULE[b].CI_ANY,Oflag))lET_	e 	,a,  )  */
 modulPCI_ANYgflag)) 	,a,	eded fornt,			but WI.flagi.Cflags;		,	Ess */
	     free s%sC0x1E
(BusNo=%d,DevNo=%d)	str
nux		0fine 	4,
	8,
GPL");
MODULE[b].C_or_m_ere ]r
nux	flag->busCI
 you ,DESCRSLOT(flag->lagfn)int,,	,  ) O >=rtio Family S)de 	,	,	Ess */
	Toog FTAPoftware; you caERIAL_Ta    f(
	  out %d),0x1E

er sand create|
	,rtio Family S)nt,,	,} 	icode 	,			r_Nva i.Cu wan20R7OESCRa1VI_flagdeGPL");
MODULE[b].C_or_m_ere , &but WITHO	,,	,  ) r_Nva i<C!)de 	,		,	  ) r_Nva i=ruct mx

#definlET_		_		Ess */
	Inva idnhanced Reaf you ,0x1E

er sand create|
	int,,	,,		ico   ) r_Nva i=ruct mx

#define IRQ_T(lET_		_		Ess */
	Inva idnhanced Reaf you ,0x1E

er sand create|
	in	t,,	,,		ico   ) r_Nva i=ruct mx

#deSYNC_SlET_		_		Ess */
	Inva idnhanced Reage f  ,0x1E

er sand create|
	int,,	,,		ico   ) r_Nva i=ruct mx

#deflag)	lET_		_		Ess */
	Inva idnag.

int of,0x1E

er sand create|
	int		,a,	eded fornt,				}y			a,  ) OXA, PCOVrbrdim, &but WIT <r0lET_	a,	eded fornt,				0fine theE0fim, &but WITHO,				0++=y				}y			}t,	}
	}
_C104_PCNPCI_(ii.Cm= i <rtio Family S= i++)de 	,dh>
E1MWAi].irq no. */ = -1=y	}
ny	  ) !s"
}T	0x0#tiS      (    aspI_or_m))y		r_NDOR)0=yAfECfIlp
S      (    aspI_or_m)=y_Ess */
	C 256. Innfig)   ree software; you caI_or_m
!|
	intETPCI_(ii.C!= i <rtio Family S= i++)de 	,  ) dh>
E1MWAi].irq no. */ == -1lET_	eded fornt,	stri_d.o dh>
E1MWAi].d.o, &. Waerfaces iTAC/
	int baud_base;		/* int,}
,r	NDOR)-1=y}-  Define theE0fine A, PCI_2Ole thi yEssvate_T_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  Essvate_; theE vecto4)
		/* Rx Tnecy	s"

=}/e PCI.Tnecy f Mwav)de 	,  ) .

s_afrPcledefiit(RR_IOADDR	-1
#def, &/e PCIcan S))de 	,	  ) MwavCI_DEVICE_(1 << TTYulO_WRITE_ MIN
#))d&&
nux/tttwavCIl
	  . PCyBXwakeuplET_	a(wavCIl
	  . PCyBXwakeupl Mwav)=y			wake_up, PCI_2Olet, s(&wavCI PCyBXwa/_)=y	,} 	,  ) .

s_afrPcledefiit(RR_IOADDR	-1define, &/e PCIcan S))de 	,	to4)5SER_Bswav)=,TDnFIXME: R"Un redevica irfig Mhat - AKPM ne t	}
	}
}_ B(iE(p  MInroutatac  thntaini
#deet wil;		/* XON/XOFhttp:deed     iE(p module (8	0       PCI_l;		/* XON/XO,e: tkfdriverly oDEx:f F charactM(8	oa proulegistcha         RTC
	herage s   rsCI_ANY-		x_char3912_OVr tr)lp be uPCI_or_ms"

F charact.
MODULE_PARM(ttym  n, PC, PCI_Q0aHOU. Wa(A baudU. , PCISER_BOA, PBOApT_UAf0x00o#define INPAR|PAT_IF;na PTrhetva ,e: te;namMD/VI_ yriver
rhiecy	hatac=dor and wav)=y	  ) hatac==C/
	int baud)y		r_NDOR) 0)=y	  ) Mhatac<C!)d   (hatac>C/
	int baud))y		r_NDOR) -ENODEV)S
	  foi.C. Waerfaces +e: te;na  ) ! e PCIuct )y		r_NDOR) -ENODEV)S

, e PCIa		/_++=y	wavCIC_or_m_ere 
=}/e P;
, e PCIs"

=} Tnecy	  ) !    aspor G*/)de 	,
	  T=de a_sioned_
	  (GFP_KERNEL) ma,  ) !
	  lET_	r_NDOR) -ENOMEM) ma,  )     aspor G*/)ET_	stri_
	  (
	  lnt,		icoET_,LE(CEdefine Rx=}(PeDrive 0lt val)r
rhiec	}
	TDA	_maStanpa/cha	/* XON/XO.

ne tr_Nva i.Cu wan20000200
  fo)=y	  ) hetva ly		r_NDOR) hetva l=y
,r	NDOR)000200
#defit_UART_DEBU. , BOApJO  fo)=y}_ B(iE(p  MInroutatac  thntaini
#de   rsCI_ANY_di  de asPGmaskd   F    , w    )0x16 PCI_or_mla
sourmaximum ere 
 (eg /sn Sth then, w e supnkhoosenumdEx:f F charactMers/char/hanced Reacha  tefmber ofaownologyweistria prouategistefmbo
	  Dor G*eftthe GNU cha   tape-  Define theE0fine p, DOR_ID_MOXA, PCI_2OltPU. , PCISER_BOA, PBOApT_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;namMD/VI_ yriver        mMD/VI_ yriveror G*/
ecy	  ) or and wav)c==C/
	int baud)y		r_NDOR;na  ) ! e P)y		r_NDOR;n thaw d_DEVI(_DEVI)=y	cli()=ycy f Mwav_hung_up,p(BOApTlde 	,r	st opt_DEVI(_DEVI)=y		r_NDOR;na}
	  ) MwavCI+ 64D == h)their e PCIa		/_i!.C1Tlde 	,TDA		_maUh, oh.0, 0, 
	lp
CIa		/_ir G1,al
	  Smeansrs/char/pclp
A		_maF charactMc Licensstrid    e PCIa		/_i0  MoxacumentA		_mabt"
	dehe GNUem al104nclude you it's gs
	  ues fnA		_ma
	d, w 've got for Rprocesm   di    n  meansrs/eA		_maFI_ANY_di  dwo. Incr lDxian W.A		_m/y		Ess */
	0fine p, DO: badaFI_ANY_di  dbeYuBm	lp
CIa		/_ir G1,a"
ux/tttt u" e PCIa		/_ir G%d	stru e PCIa		/_)=y	, e PCIa 64D = 1;na}
	  ) -- e PCIa 64D <C!)de 	,Ess */
	0fine p, DO: badaFI_ANY_di  dbeYuB PCI_otys%d: %d	str
nu/tttt u/e PCImanpru e PCIa		/_)=y	, e PCIa 64D = 0;na}
	  )  e PCIa		/_)de 	,r	st opt_DEVI(_DEVI)=y		r_NDOR;na}
	 e PCI_DEVIC|=_ID_C163912ING;
, e PCIae	PCI= lp
CIEBamm sCIa_iE(p  MITDA	_maNowywei0x16 PCI_or_mWa(A bau)     kp (eclede;ologyweibo
    	 proule: the
	  modulep (e     T_DEBUG00000 00000167622 1s..

ne tlp
CIa14A r8 = 1;na  )  e PCIa14A r8	0x16 !=_ID_C163912ING_ MIT_NONE)y		wav_I-ive webl IRQtBU. ,  e PCIa14A r8	0x16) MITDA	_maAt2.4.X
 o PTrwne oopawN. ptfdriveECf.mpiledo2.4.X, w  	_ma the HPE86 ||s _hw e: the      0(8	0      nology M hconfig */hanced ReaI_or_m
Fn  oopa/05ilplease  ere 
RT_DEodem he GNUig */: the      0T	0x0#ti..

ne t e PCIIER &= ~
 *  Iet iLSI;LfGenby ector_mSmar  e PCIheE veect5    o_ &= ~
 *    DeDR;.

ne t  )  e PCI_DEVICE_ID_C16xfffIALIZED)de 	,*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y		TDA		_maBefa.2rwftapopaDTR, ne d  Sonase  ape-  a(A bautiver	_ma     thelete caI_a*=ti;2.4.X
.X
eBOA, PCl
A		_mai 60)
lledi.
 *
at the fWa(A bau)FIFO!A		_m/y		or G*/
 = jiff abr+asZ=y	,dbs to(!( eb( e PCIvel  + 
 *    D)th 
 *    D_TEMT))de 	,	;T_DcurrRQtveecte(TASKI_DEVICE_IIBLE)=y			ude <x/dror G*/
(5int,	,  ) or G_afead_jiff ab,ror G*/
)) 	,a,confi; t	}
	}
	  I_2OlDxian Wr  fo)=y	  ) wavCIC_or_mCI_DdOI_*/
	in)y		wavCIC_or_mCI_DdOI_*/
	in wav)=y	  ) wavCIl
	  ._DdOI_*/
	in)y		wavCIl
	  ._DdOI_*/
	in wav)=y	lp
CIa14A r8 = 0;
, e PCIcan S = 0;
, e PCIs"

=}NULLHO	  )  e PCImuYuuuhACI_5)de 	,  )  e PCIa14AIpends )de 	,	;T_DcurrRQtveecte(TASKI_DEVICE_IIBLE)=y			ude <x/dror G*/
( e PCIa14AIpends )=y	,} 	,wake_up, PCI_2Olet, s(& e PCICas a mod)=y	}
	 e PCI_DEVIC&= ~(ID_C16INTERR_ACTIVE |_ID_C163912ING)=y	wake_up, PCI_2Olet, s(& e PCIGmask;		/_)=y	r	st opt_DEVI(_DEVI)=y
}ULE_PARM(ttym  n, P  The ftapa1VoBhe ftapa1VU. ,  etMers/PAR|Pr
nu/tttt uI_2OltPeDrive 0lt val*/
,  etMa		/_)_UAfw)
Mc,roota i.C0;Af0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;namMD/VI_ yriver       y	  ) !s"
d   ! e PCIeE nue Rx   !    aspor G*/)ET_r_NDOR) 0)=y thaw d_DEVI(_DEVI)=y	  ) ers/PAR|P)de 	,an Wr& set2ppor G*/
#(Cint,,dbs to(1)de 	,	ctrucIN(a		/_,ucIN(_SHIRQ XMIT_SIZE -  e PCIeE nuc)
M-G1,O,				t u_SHIRQ XMIT_SIZE -  e PCIeE nuan  )int,	,  ) ci<.C!) 	,a,confi;  	,	ct-=o., L_ers/PAR|P     aspor G*/, G*/, cint,	,  ) !c)de 	,		  ) !sota lET_	a,oota i.C-EFAULT; 	,a,confi; t		}
n,		cli()=y	,	ctrucIN(a,ucIN(_SHIRQ XMIT_SIZE -  e PCIeE nuc)
M-G1,O,			/tttt u_SHIRQ XMIT_SIZE -  e PCIeE nuan  )int,	,TR-3")( e PCIeE nue Rx+  e PCIeE nuan  ,     aspor G*/, cint,	, e PCIeE nuan  
=}( e PCIeE nuan  
+ ciCE_(_SHIRQ XMIT_SIZE - 1int,	, e PCIeE nuc)
M+=o.;ET_	r_st opt_DEVI(_DEVI)=y
	a,c Rx+=o.;ET_	a 64D -=o.;ET_	oota i+=o.;ET_} 	,00
& set2ppor G*/
#(Cint,} 	icode 	,dbs to(1)de 	,	cli()=y	,	ctrucIN(a		/_,ucIN(_SHIRQ XMIT_SIZE -  e PCIeE nuc)
M-G1,O,				t u_SHIRQ XMIT_SIZE -  e PCIeE nuan  )int,	,  ) ci<.C!)de 	,		r	st opt_DEVI(_DEVI)=y		a,confi; t		}
n,		TR-3")( e PCIeE nue Rx+  e PCIeE nuan  , G*/, cint,	, e PCIeE nuan  
=}( e PCIeE nuan  
+ ciCE_(_SHIRQ XMIT_SIZE - 1int,	, e PCIeE nuc)
M+=o.;ET_	r_st opt_DEVI(_DEVI)=y
	a,c Rx+=o.;ET_	a 64D -=o.;ET_	oota i+=o.;ET_} 	}I
	cli()=y	  )  e PCIeE nuc)
Mhei!wavCI oopp_ yhei!wavCIhw_ oopp_ yheSmar  !( e PCIIERth 
 *  Iet THRI))de 	,   PCIIERt|=_
 *  Iet THRI; 	,*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y	}
,r	st opt_DEVI(_DEVI)=y_r_NDOR) sota l=y}-  Define theE0fine rCfI_Q0aHOU. Wa(eOR_ID_MOXA,U. , PeDrive 0lt vachT_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;namMD/VI_ yriver       y	  ) !s"
d   ! e PCIeE nue R)y		r_NDOR;n thaw d_DEVI(_DEVI)=y	cli()=y	  )  e PCIeE nuc)
M>=u_SHIRQ XMIT_SIZE - 1)de 	,r	st opt_DEVI(_DEVI)=y		r_NDOR;na}
	 e PCIeE nue R[ e PCIeE nuan  ++] =ych;
, e PCIeE nuan  
&=u_SHIRQ XMIT_SIZE - 1;
, e PCIeE nuc/_++=y	ux serial driver, written by
 *      Linus Torv why ???  Linus Torvy	  ) i!wavCI oopp_ yhei!wavCIhw_ oopp_ yheSmar   !( e PCIIERth 
 *  Iet THRI) )de 	tt u/e PCIIERt|=_
 *  Iet THRI; 	tt u*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y	}
,ver, written by
 *      Linus Torvalds, Theodore T'so and others.
 ,r	st opt_DEVI(_DEVI)=y}-  Define theE0fine  idOI_2OltPerallel link cable iwav)_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;namMD/VI_ yriver       y	  )  e PCIeE nuc)
M<.C!d   wavCI oopp_ y   wavCIhw_ oopp_ y||Smar  ! e PCIeE nue R)y		r_NDOR;n thaw d_DEVI(_DEVI)=y	cli()=y	 e PCIIERt|=_
 *  Iet THRI; 	*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y	r	st opt_DEVI(_DEVI)=y}-  DefineBhe(value*e1ftaXA, PCI_2OlDxGenerC      wav)_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;na PTrhet=y
,r	N =u_SHIRQ XMIT_SIZE -  e PCIeE nuc)
M-G1=y	  ) het <r0lET_r	N =u0=y_r_NDOR) r_N)=y}-  DefineBhe(value*in tty.h */
	int tyem aI1/eedOI_2Olwav)_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;ny_r_NDOR)  e PCIeE nuc)
)=y}-  Define theE0fine  idOI_*/
	int tyem aI1/eedOI_2Olwav)_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;namMD/VI_ yriver       y	haw d_DEVI(_DEVI)=y	cli()=y	 e PCIeE nuc)
M=  e PCIeE nuan  M=  e PCIeE nucan  =u0=y_r_st opt_DEVI(_DEVI)=y	wake_up, PCI_2Olet, s(&wavCI PCyBXwa/_)=y	  ) MwavCI_DEVICE_(1 << TTYulO_WRITE_ MIN
#))d&&
n/tttwavCIl
	  . PCyBXwakeuplET_(wavCIl
	  . PCyBXwakeupl Mwav)=y}-  DefineBhe(value*OU. Wa(eOR_ID_MOXA, PCISEU. , PCISER_BOA, PBOAer
nu/tttt umMD/VI_ ysk *cmdGETDATACOUNT   earg)_UAfmMD/VI_ yriver        0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;na PTrhetva S
	P 0
#deDEx:fig+ 64D cpragdecnowmf_ "HPGmask;		/_PE4 P 0sine theE vecse1O1ad#;		/_PEine PORTNO(x)		(p_c(x)	   mMD/VI_ yriveroP 0 S
	 PCI_AN(x)		(argp
=}( PCI_AN(x)		()argecy	  ) or and wav)c==C/
	int baud)y		r_NDOR) OXA, PCISER_BOA, PCIcmdGEarg))=y	  ) Mcmd !=_TIOCG_SHIRQ)theircmd !=_TIOCMI MIT)d&&
n/tttMcmd !=_TIOCGI     ))de 	,  ) .avCI_DEVICE_(1 << TTYuIO

#dOR)lET_	r_NDOR) -EIO)=y	}
,switchtMcmd)de 	P); BTCSBRK:m nuuSVID
 *			  :s forsionearg -->s f confi_m/y		r_Nva i.C_MOX/05ilA
	  ThMwav)=y		  ) hetva ly			r_NDOR) hetva l=y		wav_I-ive webl IRQtBU. , 0) ma,  ) !arg)_			0fine IRQ_2configT_IF,asZ / 4)um nuu1/ ( Oal10_m/y		r_NDOR) 0)=y	P); BTCSBRKP:m nuu<linux/interPOSIX tcIRQ_config)_m/y		r_Nva i.C_MOX/05ilA
	  ThMwav)=y		  ) hetva ly			r_NDOR) hetva l=y		wav_I-ive webl IRQtBU. , 0) ma,0fine IRQ_2configT_IF,aarg ?earg n (sZ / 10
#ifsZ / 4)uy		r_NDOR) 0)=y	P); BTIOCG_OFTCAR:y		r_NDOR)rCfIAR|P 16391CAL wav)c? 1#if0,}(PeDrive 0riverAN(x)		()argp)=y	P); BTIOCS_OFTCAR:y		if(e a_AR|P oP 0 ,}(PeDrive 0riverAN(x)		()Earg))y			r_NDOR)-EFAULT; 	,arg =roP 0 S
		lp
CIEBamm sCIa_iE(p 
=}((lp
CIEBamm sCIa_iE(p 
& ~391CAL) |O,				t(arg ?e391CAL#if0))uy		r_NDOR) 0)=y	P); BTIOCG_SHIRQ:y		r_NDOR)0
#defit_UART_DEBamm  T_IF,aargp)=y	P); BTIOCS_SHIRQ:y		r_NDOR)0
#defbe use1O1ad#def T_IF,aargp)=y	P); BTIOCSERGETLSR: nuuG	N : the      0T	0x0#ti_m/y		r_NDOR)define RELEVANT_IFLT_IF,aargp)=y		TDA		_maWx16 PCI_FTAPD.
 *
 4 defseiveECfs (DCD,RI,DSR,Cud)p (ec	  ThA		_ma-      passtiCE_Iarg PCI_: thSpeed PCI__stA		_mattM(x) |'tiCTIOCM_RNG/DSR/CD/Cud
 *
 *   ing)_		_maCntairi0  Moxau; BTIOCGI     
Fn  eeal
	  S
	deh    sA		_m/y	P); BTIOCMI MIT:y		haw d_DEVI(_DEVI)=y		cli()=y	,cpragM=  e PCI beYuBmf_ "no module;		/_PE4 ontrucry_m/y		r_st opt_DEVI(_DEVI)=y		dbs to(1)de 	,	 PCI_2Olet, s_sleep_on(& e PCI t vectI-iverc)=y	m nuu<eo   )a ude <l didnht_m/y			  ) ude <l_    ing(currRQt)) 	,a,r_NDOR) -E3
#defiSYS)nt,,	haw d_DEVI(_DEVI)=y			cli()=y	,	cnowM=  e PCI beYuBmf_ "atom[] =, LTm/y			r	st opt_DEVI(_DEVI)=y		a  ) cnow.rr8 == cprag.rr8 heicnow.dsr == cprag.dsr &&
nux/tcnow.dcd == cprag.dcd heicnow.    == cprag.   ) 	,a,r_NDOR) -EIO)=f_ "noec	  Th =>OR cD_CP104,	  ) M(arg &CTIOCM_RNG)theircnow.rr8 != cprag.rr8))y||Smvicearg &CTIOCM_D D)theircnow.dsr != cprag.dsr))y||Smvi cearg &CTIOCM_CD)theircnow.dcd != cprag.dcd))y||Smvicearg &CTIOCM_Cud)pheircnow.    != cprag.   )))de 	,		r	NDOR) 0)=y			}y			cpragM= cnowmET_} 	,_ "NOTREACHEDCP104,TDA		_maG	N ;		/_PE4eed P
	inFI_ANY_: the(8	0       (DCD,RI,DSR,Cud)A		_maR	NDOR:P PCyB ;		/_PE4  : 2001(x)		passtiC;		/_PE4ne PORA		_maNB: both 1->0ology0->1fWa(A 4ncludD_CPs;		/_Pd ex. pt
 *
A		_matt  RIi
#dor wilyy0->1f  the	/_Pd.A		_m/y	P); BTIOCGI     :y		haw d_DEVI(_DEVI)=y		cli()=y	,cnowM=  e PCI beYuBmy		r_st opt_DEVI(_DEVI)=y		p_c(x)	
=}Ergp=y		  (rCfIAR|P cnow.   , &p_c(x)	CIa  ))y			r_NDOR)-EFAULT; 	,  (rCfIAR|P cnow.dsr, &p_c(x)	CIdsr))y			r_NDOR)-EFAULT; 	,  (rCfIAR|P cnow.rr8, &p_c(x)	CIrr8))y			r_NDOR)-EFAULT; 	,r_NDOR)rCfIAR|P cnow.dcd, &p_c(x)	CIdcd)=y	P); B_DEVICE_ID_C168,:y		r_NDOR)rCfIAR|P  e PCIuc level  != hACI_5c? 1#if0,}(T|IGNPAR|PARMargp)=y	default:y		r_NDOR) -ENOIOCTLCMDint,}
,r	NDOR) 0)=y}
 Wa(eOR_ID_MOXA, PCISER_BOA, PCI_2OlD ( OXA_*cmdGETDATACOUNT   earg)_UAfw)
MAJOr_sult,e      S
	 PCI_AN(x)		(argp
=}( PCI_AN(x)		()argecy	switchtMcmd)de 	P); B  MOXA_GETMST:y		if(., L_toIAR|P argp,ddh>
E1MWr
nux/tttt8morofg0x00o#define but WIT * 4))
nux/tttt	r_NDOR)-EFAULT; 	,r_NDOR)0; 	P); B  MOXA_GEser_h:y		if(., L_toIAR|P argp,d&int port;
8morofg OXA))y			r_NDOR)-EFAULT; 	,r_NDOR)0=yAfP); B  MOXA_GETUser_h:y		r_sult =u0=y_	if(., L_toIAR|P argp,d&r_sult,e morofg OXA))y			r_NDOR)-EFAULT; 	,r_NDOR)0=yAfP); B  MOXibrds[] = {
	:y		r_sult =u0=y_	PCI_(ii.C!= i <rtio Fa baud= i++)de 	,,  )     aspaces i].uct )y				r_sult |=_(1 << i)=y		}y		r_NDOR)rCfIAR|P r_sult,e(PeDrive 0riverAN(x)		()Eargp); 	P); B  MOXA_GJOR      :y		  ) ., L_toIAR|P argp,d&    ) C   B8morofgGP  ) C  A))y			r_NDOR)-EFAULT; 	,r_NDOR) 0)=y	P); B  MXSER_BOARD_C:y_	PCI_(ii.C!= i <rtio Fa baud= i++)de 	,,     _IDsi].ri =u0=y_		  ) !    aspaces i].uct )de 	,		     _IDsi].dcd =u0=y_			     _IDsi].dsr =u0=y_			     _IDsi].    =u0=y_			eded fornt,		}y_		  ) !    aspaces i].s"
d   !    aspaces i].s"
CIEBamm s)y_			     _IDsi]. E(p 
=}    aspaces i].iE(p  MI,		icoET_		     _IDsi]. E(p 
=}    aspaces i].lp
CIEBamm sCIa_iE(p  Mt,,	h     0=  eb     aspaces i].uct  + 
 *  MSR)=y		a  ) h     0& 0x80 /*
 *  MSR_DCD_m/ )y_			     _IDsi].dcd =u1 MI,		icoET_		     _IDsi].dcd =u0=yy		a  ) h     0& 0x20 /*
 *  MSR_DSR_m/ )y_			     _IDsi].dsr =u1 MI,		icoET_		     _IDsi].dsci.C!=y n,	a  ) h     0& 0x10 /*
 *  MSR_Cud
m/ )y_			     _IDsi].    =u1 MI,		icoET_		     _IDsi].    =u0=y_	}y_	if(., L_toIAR|P argp,d     _IDr
nux/tttt8morofg0x00o#define        )TAC/
	int baud))y			r_NDOR)-EFAULT; 	,r_NDOR)0=y	default:y		r_NDOR) -ENOIOCTLCMDint,}
,r	NDOR) 0)=y}
 B(iE(p  MInroutatac  thntainiby 2001(pp)	ClayeI_oty layeI_oo ude <l s/ch3912_Ocom[ver0167622 1si0  MoxabmoduY_ID, d  ) MD/    n  c., PCI_ANY_ID, 0, 0, 
	  MY_ItPe., Pfwav)_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;namMD/VI_ yriver       y	  ) I_I 000 wav))de 	,   PCIxI_Q0a =}STO#definMwav)=y		haw d_DEVI(_DEVI)=y		cli()=y	,*/
b( e PCIIER, 0)=y	, e PCIIERt|=_
 *  Iet THRI; 	,*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=	,_ "PCIc BTx/hanced Ream/y		r_st opt_DEVI(_DEVI)=y	}
	  )  e PCIlp
CIEBamm sCIa_iE(p 
& CaudCud)pe 	,   PCIMCR &= ~
 *  ? (aaud=y		haw d_DEVI(_DEVI)=y		cli()=y	,*/
b( e PCI? (,  e PCIvel  + 
 *  ? ()my		r_st opt_DEVI(_DEVI)=y	}y}-  Define theE0fine  XHeY_ID, endi) C(ALPHA ||   _wav)_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;namMD/VI_ yriver       y	  ) I_I 000 wav))de 	,  )  e PCIeI_Q0a)
		,   PCIxI_Q0a =}0=y_		icode 	,	   PCIxI_Q0a =}ST *  efinMwav)=y			haw d_DEVI(_DEVI)=y			cli()=y	,	*/
b( e PCIIER, 0)=y	,, e PCIIERt|=_
 *  Iet THRI;,_ "PCIc BTx/hanced Ream/y		,*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y			r_st opt_DEVI(_DEVI)=y		}y	}
	  )  e PCIlp
CIEBamm sCIa_iE(p 
& CaudCud)pe 	,   PCIMCR |=_
 *  ? (aaud=y		haw d_DEVI(_DEVI)=y		cli()=y	,*/
b( e PCI? (,  e PCIvel  + 
 *  ? ()my		r_st opt_DEVI(_DEVI)=y	}y}-  Define theE0fine RT_DEBamm s) an_ANY   er1ve XH.U. ,
nux/ttttOlDxian Wrig0x000lwPE o
	  T _UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;ny_ "8-2-99nby ector_mSt u/ ) i(lp
CIEBamm sCIa_iE(p 
== lwPE o
	  TCIa_iE(p )d&&
attMCI_VENDOR_ID_MOlp
CIEBamm sCIa_XA
#de
==
attCI_VENDOR_ID_MOlwPE o
	  TCIa_XA
#de) )yattr_NDOR;n t u,
	4,

	  This n2EVT_IF,alwPE o
	  T   St u/ ) i(lwPE o
	  TCIa_iE(p 
& CaudCud)p&&
att!(lp
CIEBamm sCIa_iE(p 
& CaudCud)p)pe tttwavCIhw_ oopp_ y=}0=yt u,
	4,
tPe "iwav)=yt u}

ne t  ) (lp
CIEBamm sCIa_iE(p 
!= lwPE o
	  TCIa_iE(p )d||Smar  MCI_VENDOR_ID_MOlp
CIEBamm sCIa_XA
#de
!=Smar   CI_VENDOR_ID_MOlwPE o
	  TCIa_XA
#de))pe 
a,0fine 
	  This n2EVT_IF,alwPE o
	  T   S,	  ) MlwPE o
	  TCIa_iE(p 
& CaudCud)p&&
	mar  !(lp
CIEBamm sCIa_iE(p 
& CaudCud))de 	,	to4CIhw_ oopp_ y=}0=y			0fine IPe "iwav)=y		}y	}
_ "Hand tobe  oopp_ yne t  ) (lwPE o
	  TCIa_XA
#d
& I 0N)yheSmar  !(lp
CIEBamm sCIa_XA
#d
& I 0N))de 	,wavCI oopp_ y=}0=y		0fine IPe "iwav)=y	}
}_ B(iE(p  n, PRtop()ology0fine IPe "i)ya(iE(p  MInroutatadD_CPs;ntainibefa.2rRT_tfdriCI_r_sT_tfdriwavCI oopp_ .ATACOUNyp moduliCI_ the HPE8a(A bautiv0(8	0      nolsmber ofaow  ) MD/    n  c., PCI_ANg_TUD   sl/linu!/1ID_.*ewav)_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;namMD/VI_ yriver       y	haw d_DEVI(_DEVI)=y	cli()=y	  )  e PCIIERth 
 *  Iet THRI) e 	,   PCIIER &= ~
 *  Iet THRI; 	,*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y	}
,r	st opt_DEVI(_DEVI)=y}-  Define theE0fine RPe "i");

struct mxser_hwav)_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;namMD/VI_ yriver       y	haw d_DEVI(_DEVI)=y	cli()=y	  )  e PCIeE nuc)
Mhei e PCIeE nue RxheSmar  !( e PCIIERth 
 *  Iet THRI))de 	,   PCIIERt|=_
 *  Iet THRI; 	,*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y	}
,r	st opt_DEVI(_DEVI)=y}
 B(iE(p  MInroutatac  thntainiby 2o4)5SER_Bs)i
#de a 5SER_Ba  tude <l d  ) MDI_iuuuhACI_5SER_BsnoA, PCI_2_ItPI_2Olwav)_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;ny_0fine  idOI_*/
	intwav)=y	  I_2OlDxian Wr  fo)=y	 e PCIcan S = 0;
, e PCIa 64D = 0;na e PCI_DEVIC&= ~ID_C16INTERR_ACTIVE;na e PCIs"

=}NULLHO	wake_up, PCI_2Olet, s(& e PCICas a mod)=y}_ B(iE(p  MIn     rsCI_ANY_C_or_m's genI_Ac/hanced Rearoutata(CE_     opti.or_NDOR_ID_MOXA, PCI_2Ole thi d.o,  thi ydev_id XA, PCIlI,tr_NDO0r_ND)_UAfw)
M      ,eA;na0x00o#define INPAR|PAT_IF;na0x00o#define INPAR|PAype;		/* UAmax, d.obi  nobi  nominux/drivpass_;		/_PE4= 0;na et 5SEdled =u0=yy	manpa=ONULLHO	PCI_(ii.C!= i <rtio Family S= i++)de 	,  ) dev_id
== &(. Waerfaces iTAC/
	int baud_base;		/* ilde 	,	Eanpa=Odev_id=y			confi; t	}
	}
y	  )  c==C/
	intmily S)y		r_NDOR)fineNONE=y	  ) Eanpa==r0lET_r	NDOR)fineNONE=y	max .C (MOXA + 60)
#ddh>
E1MWAi].irq no. */ MI
	dbs to(1)de 	,d.obi  0=  eb 60)
->ge f  iCE_60)
->ge f    Defin,  )  .obi  0==_60)
->ge f    De)y			confi; t	5SEdled =u1nt,	PCI_(ii.C!nobi   =u1n i <rmax; i++JO .obi  0|=_bi  nobi   <<= 1)de 	,,  )  .obi  0==_60)
->ge f    De)y			,confi; t		  ) bi  0&O .obi  )y_			eded fornt,		T_IF
=}Eanpa+eA;na	a  ) ! e PCIs"
d  
nux/t( eb( e PCIvel  + 
 *  IID)th 
 *  IIDeNOI_DE))y_			eded fornt,		h     0=  eb  e PCIvel  + 
 *    D)th  e PCIheE veect5    o_=y		a  ) h     0& 
 *    DeDR)y_			 (MOXAss _hw devicesT_IF,a&      )=y			0sci.C eb  e PCIvel  + 
 *  MSR)=y		a  ) 0sci& 
 *  MSRRIPTIDELTA)y_			 (MOXA/05ilAdefse of CMOlT_IF,a0sc)=y		a  ) h     0& 
 *    D_THRE)de _ "8-2-99nby ector_mSt u/ ) i   PCIxI_Q0a    ( e PCIeE nuc)
M>C!)d)
am/y		,	 (MOXAWa(A baud_base[  fo)=y			}t,	}
		  ) Eass_;		/_PE++c>C/
	intI D_PASS_LIMIT)d{-NDO 0 	,	Ess */
	ree software/Indusware; you caI_or_m
hanced Realoop confi|
	int_C104_P		,confi;,_ "Prcan S   f_OVrealoops ne t	}
	}
_r	NDOR)fineRETVAL(5SEdled)=y}
 Wa(eOR_IDacter specialewess _hw devices.

config0x00000200
#T_IF,
,				tfiner      )_UAf0x00o#dI_2_ItPI_2Olwav
=}/e PCI.TnecyPeDrive 0lt vach;na et _ B opd4= 0;na et c4D = 0;n
	dode 	,chi.C eb  e PCIvel  + 
 *  RX)=y		  ) *h     0&  e PCI  B optiCE_Ie-  De)de 	,,  ) ++_ B opd4> 100)y			,confi; t	} 	icode 	,	  ) .avCI_Dip.a 64D >=_TTYuFLIPBUF_SIZE)y			,confi; t		.avCI_Dip.a 64D++=y			  ) *h     0& 
 *    D_SPECIRQ)te 	,		  ) *h     0& 
 *    D_BI)de 	,		,*.avCI_Dip.RI,l:G*/
ptE++c=_TTYuBREAK=y_			t  )  e PCI_DEVICE_ID_C16SAKlET_	a,	do6SAKMwav)=y				} 	icod  ) *h     0& 
 *    D_PE)de 	,		,*.avCI_Dip.RI,l:G*/
ptE++c=_TTYuPARITY=y				} 	icod  ) *h     0& 
 *    D_FE)de 	,		,*.avCI_Dip.RI,l:G*/
ptE++c=_TTYuFRAME=y				} 	icod  ) *h     0& 
 *    D_OE)de 	,		,*.avCI_Dip.RI,l:G*/
ptE++c=_TTYuOVVICEN=y				} 	ico 	,		,*.avCI_Dip.RI,l:G*/
ptE++c=_0=y			} 	ico 	,		*.avCI_Dip.RI,l:G*/
ptE++c=_0=y			*.avCI_Dip.lt v:G*/
ptE++c=_ch;na	,cnD++=y		}
		*h     0=  eb  e PCIvel  + 
 *    D)th  e PCIheE veect5    o_=y	}ndbs to *h     0& 
 *    D_DR)=y	    ) C  .MFheE  e PCIEanp]i+=o.CI_565de <x/drends ed_t -i(&wavCI_Dip.t -i, 1inty}
 Wa(eOR_IDacter specialeweWa(A baud_base[5_8B016101 e Rx     _  fo)_UAfw)
Mc		/_,u.CI_5
,  )  e PCIeI_Q0a)de 	,*/
b( e PCIeI_Q0a,  e PCIvel  + 
 *  TX)=y	, e PCIxI_Q0a =}0=y_	    ) C  .tFheE  e PCIEanp]++=y		r_NDOR;na}
	  ) M e PCIeE nuc)
M<.C!)d    e PCIlp
CI oopp_ y||Smar   e PCIlp
CIhw_ oopp_ ) e 	,   PCIIER &= ~
 *  Iet THRI; 	,*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y		r_NDOR;na}
	c)
M=  e PCIeE nu.CI_56a 64D =  e PCIeE number 8mort bdode 	,*/
b( e PCIeE nue R[ e PCIeE nucan ++],  e PCIvel  + 
 *  TX)=y	, e PCIxE nucan  =u e PCIxE nucan  E_(_SHIRQ XMIT_SIZE - 1int,	  ) -- e PCIeE nuc)
M<.C!)y			confi; t}ndbs to --a 64D > 0)=y	    ) C  .tFheE  e PCIEanp]i+=o(c)
M-G e PCIeE nuc)
)=yy	  )  e PCIeE nuc)
M<  MIN
# efind)pe 	,RT_Diit(RR_IOADDR	-1
#def, &/e PCIcan S)=y		hde <x/drt -i(& e PCIlrcad )=y	}
	  )  e PCIeE nuc)
M<.C!)de 	,   PCIIER &= ~
 *  Iet THRI; 	,*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y	}
}
 Wa(eOR_IDacter specialewe/05ilAdefse of CMOle thiaccess _hACI_5 dT_IF,
,				tttt u/e
M      )_UALfGenupd  mo P
	inacter;		/_PE4 ne t  ) h     0& 
 *  M D_TERI) 	,   PCI beYuB.rr8++=y	  ) h     0& 
 *  M D_DD D) 	,   PCI beYuB.dsr++=y	  ) h     0& 
 *  M D_DDCD) 	,   PCI beYuB.dcd++=y	  ) h     0& 
 *  M D_DCud)A		   PCI beYuB.cts++=y	wake_up, PCI_2Olet, s(& e PCI t vectI-iverc)=y
	  ) M e PCI_DEVICE_ID_C16CHECK_CD)theirh     0& 
 *  M D_DDCD))de 	,  ) h     0& 
 *  M D_DCD) 	,,wake_up, PCI_2Olet, s(& e PCICas a mod)=y			icoET_	RT_Diit(RR_IOADDR	-1define, &/e PCIcan S)=y		hde <x/drt -i(& e PCIlrcad )=y	}
	  )  e PCI_DEVICE_ID_C16Cud_Fdef)de 	,  )  e PCIlp
CIhw_ oopp_ ) e 	,,  ) h     0& 
 *  M D_Cud)pe 	,		 e PCIlp
CIhw_ oopp_  =u0=y_			   PCIIERt|=_
 *  Iet THRI; 	,	,*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y 	,	,RT_Diit(RR_IOADDR	-1
#def, &/e PCIcan S)=y				hde <x/drt -i(& e PCIlrcad )=y			}t,	} 	icode 	,	  ) ! h     0& 
 *  M D_Cud))pe 	,		 e PCIlp
CIhw_ oopp_  =u1=y_			   PCIIERt&= ~
 *  Iet THRI; 	,	,*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y			}t,	}
	}
}
 Wa(eOR_ID_MOXA, P
#defit_UART_DEBamm  MIn      . filU. , PCISER_BOA, PBOAp,O,			/5_8B016101 e Rx     _  fo)_UAfDECL0x16 MITQUEUE( mod,u.urrRQt);namMD/VI_ yriver         PTrhetva S
	  bedo6c uc l = 0;n
	TDA	_maI.
 *
 CI_ANYn   he GNU midd toof be[ver0maskd, GNUn114AIpA	_ma webl it's d
	d, r set2pe Gry aga   t

ne t  ) tav_hung_up,p(BOApT    ( e PCI_DEVICE_ID_C16C912ING))de 	,  )  e PCI_DEVICE_ID_C16C912ING)t,		T_CI_2Olet, s_sleep_on(& e PCIGmask;		/_)=yNDOR_ID_SHIRQ DO_3
#defi 	,  )  e PCI_DEVICE_ID_C16H
# NOTIFYlET_	r_NDOR) -EAGAIN)=y			icoET_	r_NDOR) -E3
#defiSYS)nt#	icoET_r_NDOR) -EAGAIN)=y_C104_P	}
	TDA	_maI.
 for14AIput IR"UYn   sIR, CI_or_mN/XOFhtter s moduld, 	 prouln ne d GNU ch5ila/chfro ber set2pe ive  t

ne t  ) (BOApCI___DEVICE_OeNONBLOCK)d||Smar  M.avCI_DEVICE_(1 << TTYuIO

#dOR)l)de 	,   PCI_DEVIC|=_ID_C16INTERR_ACTIVE;na,r_NDOR) 0)=y	}y	  ) wavCIEBamm sCIa_iE(p 
& C91CAL)
,	do6c uc l = 1;n
	TDA	_maB4AIpi0x16ut IPCI_or_mcarris.k atecber set2p dulep (ebecomUig */stri)  .e.,ter she u; Bby 2001hnta*/
).  Wbs toweD_CPsin 	 prour G*oopru e PCIa		/_Fhttapoppiniby 
	d, so s/ch3	E(p  n, Pp, DOR) knowsi
#de  o/stri)
	  Dde yW ||sst opnht_upon 	 prive , eivcs.kn    l CI_abn    l t

ne tr_Nva i.C0=y_add_I-ivercad (& e PCICas a mod, & mod)=y	haw d_DEVI(_DEVI)=y	cli()=y	  ) !tav_hung_up,p(BOApT)A		   PCIa		/_--;y_r_st opt_DEVI(_DEVI)=y	 e PCIvuYuuuhACI_5++=y	wbs to(1)de 	,haw d_DEVI(_DEVI)=y		cli()=y	,*/
b( eb  e PCIvel  + 
 *  MCD)t|_
 *  ? (aDTRt|_
 *  ? (afiSr
nu/tttt e PCIvel  + 
 *  ? ()my		r_st opt_DEVI(_DEVI)=y		;T_DcurrRQtveecte(TASKI_DEVICE_IIBLE)=y		  ) tav_hung_up,p(BOApT    !  e PCI_DEVICE_ID_C16xfffIALIZED))d{-NDOR_ID_SHIRQ DO_3
#defi 	,,  )  e PCI_DEVICE_ID_C16H
# NOTIFYlET_	tr_Nva i.C-EAGAIN=y				ico 	,		r_Nva i.C-E3
#defiSYSnt#	icoET_tr_Nva i.C-EAGAIN=y_C104_P		,confi;t,	}
		  ) !  e PCI_DEVICE_ID_C16C912ING)p&&
	mar  (do6c uc l    ( eb  e PCIvel  + 
 *  MSR)0& 
 *  M D_DCD)))y			confi; t	  ) ude <l_    ing(currRQt))pe 	,	r_Nva i.C-E3
#defiSYSnt		,confi;t,	}
		hde <x/d()=y	}
,sT_DcurrRQtveecte(TASKIRUNNING)=y	device_I-ivercad (& e PCICas a mod, & mod)=y	  ) !tav_hung_up,p(BOApT)A		   PCIa		/_++=y	 e PCIvuYuuuhACI_5--;y_  ) hetva ly		r_NDOR) hetva l=y,   PCI_DEVIC|=_ID_C16INTERR_ACTIVE;nar	NDOR) 0)=y}
 Wa(eOR_ID_MOXA, P0000200
#definer canE_ID   en  fo)_UAfmMD/VI_ yriver        mMD/VI_ yriver
rhiecy	
	  T=de a_sioned_
	  (GFP_KERNEL) ma  ) !
	  lET_r_NDOR) -ENOMEM) m thaw d_DEVI(_DEVI)=y	cli()=ycy f M e PCI_DEVICE_ID_C16xfffIALIZED)pe 	,stri_
	  (
	  lnt,	r	st opt_DEVI(_DEVI)=y		r_NDOR) 0)=y	}y	  ) ! e PCIuct d   ! e PCI. */)de 	,  )  e PCIlp
)ET_	RT_Diit(TTYuIO

#dOR, &/e PCI.avCI_DEVI)=y		stri_
	  (
	  lnt,	r	st opt_DEVI(_DEVI)=y		r_NDOR) 0)=y	}y	  )  e PCIeE nue R)y		stri_
	  (
	  lnt,	icoET_ e PCIeE nue Rx=}(PeDrive 0lt val)r
rhiec
	TDA	_maClede 2001FIFO)     kser se the HPE86 mA	_ma(tUNypc Licenstrimoduldshe 0fine 
	  This n2EVT)A	
ne t  )  e PCIeE number 8mor ==O16)y	,*/
b((
 *  F (aCLEA(afCVRt|_
 *  F (aCLEA(aXMIT)r
nu/tttt e PCIvel  + 
 *  FCt)=y 	TDA	_maAt2.4.X
 o PTr *
at'tteri0xy 2001LSRr;		ld/5_ Licens0xFF;ig */hu it .X, GNUn11an  */
,ebecau; B *
at'ttlike caeriUefi 	_ma ect.
	
ne t  )  eb  e PCIvel  + 
 *    D)t==r0xff)de 	,r	st opt_DEVI(_DEVI)=y		  ) .apodul(R c_SYS_ADMIN))de 	,	  )  e PCIlp
)ET_		RT_Diit(TTYuIO

#dOR, &/e PCI.avCI_DEVI)=y			r	NDOR) 0)=y		} 	ico 	,	r_NDOR) -ENODEV)S
	}
	TDA	_maClede 2001hanced Rear	0x0#tis..

ne tTX3912  eb  e PCIvel  + 
 *    D); tTX3912  eb  e PCIvel  + 
 *  RX)=y	TX3912  eb  e PCIvel  + 
 *  IID)=y	TX3912  eb  e PCIvel  + 
 *  MSR)=yMITDA	_maNow,2_OVr tr)lnase  ape-.

ne t*/
b(
 *    (aWLEN8,  e PCIvel  + 
 *  LCt)=fGenr_sT_ DLAB
ne t e PCIMCR =_
 *  ? (aDTRt|_
 *  ? (afiS;
,*/
b( e PCI? (,  e PCIvel  + 
 *  ? ()myMITDA	_maFinPCl
,p moduli(8	0      .

ne t e PCIIER =_
 *  Iet MSIt|_
 *  Iet iLSIt|_
 *  Iet iDI; 	*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=	,_ " moduli(8	0      MODULfTDA	_maA seclede 2001hanced Rear	0x0#tis aga   PCI_:uck..

ne tTX3912  eb  e PCIvel  + 
 *    D); tTX3912  eb  e PCIvel  + 
 *  RX)=y	TX3912  eb  e PCIvel  + 
 *  IID)=y	TX3912  eb  e PCIvel  + 
 *  MSR)=yMI  )  e PCIlp
)ET_.

s_afrPcledefiit(TTYuIO

#dOR, &/e PCI.avCI_DEVI)=yy	 e PCIeE nuc)
M=  e PCIeE nuan  M=  e PCIeE nucan  =u0=yLfTDA	_mar sesT_   rsC n2EPD.
 *
 a	/* XON/XO.

ne t0fine 
	  This n2EVT_IF,aNULL)=yy	 e PCI_DEVIC|=_ID_C16xfffIALIZED;y_r_st opt_DEVI(_DEVI)=y	r	NDOR) 0)=y}
 B(iE(p  MInroutatacc LiclDxian W_l;		/* XON/XO;i(8	0      Mmaybee the HPd, r siE(pDTRthttapoppinii.
 *
 5SER_Baonr0mask Wrig0xr    Fhttpn  ) MD/    n  c., PCI_ANgDxian Wr#definer canE_ID   en  fo)_UAfmMD/VI_ yriver       
	  ) !  e PCI_DEVICE_ID_C16xfffIALIZED))y		r_NDOR;n thaw d_DEVI(_DEVI)=y	cli()=		,_ "Dthe HPE(8	0      MODULfTDA	_maclede  t vectI-iverc rcad   o/a c., Pem ledks:oweD a mstri)
	eO .o 	_ma ect so s/e rcad  might eet wibei0xkUn1up.

ne twake_up, PCI_2Olet, s(& e PCI t vectI-iverc)=y
	TDA	_maFtri)
	eOIRQ,tefmber ofaow.

ne t  )  e PCIeE nue R)pe 	,stri_
	  ((PeDrive 0rive)i e PCIeE nue R)=y	, e PCIxE nue Rx=}NULLHO	} t e PCIIER =_0; 	*/
b(0x00,  e PCIvel  + 
 *  Iet)=	/ma the HPE)    PCr MODULf  ) ! e PCIs"
d  )  e PCIlp
CIEBamm sCIa_iE(p 
& HUPCLT)A		   PCIMCR &= ~(
 *  ? (aDTRt|_
 *  ? (afiS);
,*/
b( e PCI? (,  e PCIvel  + 
 *  ? ()myMITDaclede Rx/Tx1FIFO's
ne t*/
b((
 *  F (aCLEA(afCVRt|_
 *  F (aCLEA(aXMIT)rt e PCIvel  + 
 *  FCt)=yfGenr_  Mere 
N/XOF o/r_sT_ 
	  Dd
ne tTX3912  eb  e PCIvel  + 
 *  RX)=yMI  )  e PCIlp
)ET_RT_Diit(TTYuIO

#dOR, &/e PCI.avCI_DEVI)=yna e PCI_DEVIC&= ~ID_C16xfffIALIZED;y_r_st opt_DEVI(_DEVI)=y}
 B(iE(p  MInroutatac  thntainiFn  etase  ape- divisCI_r_0x0#tis tog Ftcha proule		x_chainibaud r  moPCI_l;		/* XON/XO  ) MD/    n Bhe(value*in  This n2EVe thiaccess _hACI_5 dT_IF,
,		/ttttOlDxian Wrig0x000lwPE o
	  T _UAfBhe(quoD = 0;naPeDrive 0l_DEV, cva ,efcnux/drivi    PTrhet = 0;naPeDrive 0river       
	  ) ! e PCIs"
d  )! e PCIlp
CIEBamm slET_r_NDOR)het=y	 E(p 
=} e PCIlp
CIEBamm sCIa_iE(p ;
	  ) !  e PCIuct )lET_r_NDOR)het=y-NDOnR_IDBo7(cti
#R_IatacBo7(cti (B4608ti +1)y_C104_P	switchtMcE(p 
& (CBAUs | CBAUsEX))de 	P); BBo7(cti:y		 
=}20=y_	confi;t,P); BB4608ti:y		 
=}19=y_	confi;t,P); BB2304ti:y		 
=}18=y_	confi;t,P); BBhACI_5:y		 
=}17=y_	confi;t,P); BB57cti:y		 
=}16=y_	confi;t,P); BB384ti:y		 
=}15=y_	confi;t,P); BBh9I_5:y		 
=}14=y_	confi;t,P); BB9cti:y		 
=}13=y_	confi;t,P); BB48ti:y		 
=}12=y_	confi;t,P); BB24ti:y		 
=}11=y_	confi;t,P); BBh8ti:y		 
=}10=y_	confi;t,P); BB1I_5:y		 
=}9=y_	confi;t,P); BBcti:y		 
=}8=y_	confi;t,P); BB3ti:y		 
=}7=y_	confi;t,P); BBI_5:y		 
=}6=y_	confi;t,P); BB155:y		 
=}5=y_	confi;t,P); BBh34:y		 
=}4=y_	confi;t,P); BB115:y		 
=}3=y_	confi;t,P); BB75:y		 
=}2=y_	confi;t,P); BB5i:y		 
=}1=y_	confi;t,default:y		i =u0=y_	confi; t}
y	  )  c==C15)de 	,  )   e PCI_DEVICE_ID_C16SPD_MASK)t==rID_C16SPD_HI)t,		T
=}16=fGen57cti bps ne t	  )   e PCI_DEVICE_ID_C16SPD_MASK)t==rID_C16SPD_VHI)t,		T
=}17=fGenhACI_5cbps ne -NDOR_IDID_C16SPD_SHI t	  )   e PCI_DEVICE_ID_C16SPD_MASK)t==rID_C16SPD_SHI)t,		T
=}18=y_C104_P-NDOR_IDID_C16SPD_WARP t	  )   e PCI_DEVICE_ID_C16SPD_MASK)t==rID_C16SPD_WARP)t,		T
=}19=y_C104_P	}
	  )     asuc lepaces i]c==C134)de 	,quoD = (2912_O PCIuc level  / 269)S
	} 	icod  )     asuc lepaces i])de 	,quoD = _O PCIuc level  /     asuc lepaces i]=y		  ) !quoD &&alwPE o
	  T de 	,	Genr_-hntcul  moP104,	 e PCIlp
CIEBamm sCIa_iE(p 
&= ~CBAUs;04,	 e PCIlp
CIEBamm sCIa_iE(p 
|=_(lwPE o
	  TCIa_iE(p 
& CBAUs)=y			hwitchtM e PCIlp
CIEBamm sCIa_iE(p 
& (CBAUs | CBAUsEX))de 			P); BBo7(cti:y				 
=}20=y_		,confi; t		P); BB4608ti:y				 
=}19=y_		,confi; t		P); BB2304ti:y				 
=}18=y_		,confi; t		P); BBhACI_5:y				 
=}17=y_		,confi; t		P); BB57cti:y				 
=}16=y_		,confi; t		P); BB384ti:y				 
=}15=y_		,confi; t		P); BBh9I_5:y				 
=}14=y_		,confi; t		P); BB9cti:y				 
=}13=y_		,confi; t		P); BB48ti:y				 
=}12=y_		,confi; t		P); BB24ti:y				 
=}11=y_		,confi; t		P); BBh8ti:y				 
=}10=y_		,confi; t		P); BB1I_5:y				 
=}9=y_		,confi; t		P); BBcti:y				 
=}8=y_		,confi; t		P); BB3ti:y				 
=}7=y_		,confi; t		P); BBI_5:y				 
=}6=y_		,confi; t		P); BB155:y				 
=}5=y_		,confi; t		P); BB134:y				 
=}4=y_		,confi; t		P); BBhA5:y				 
=}3=y_		,confi; t		P); BB75:y				 
=}2=y_		,confi; t		P); BB5i:y				 
=}1=y_		,confi; t		default:y				i =u0=y_	a,confi; t		}
	,	  )  c==C15)de 	,	,  )   e PCI_DEVICE_ID_C16SPD_MASK)t==rID_C16SPD_HI)t,				T
=}16=ffGen57cti bps ne t	t	  )   e PCI_DEVICE_ID_C16SPD_MASK)t==rID_C16SPD_VHI)t,				T
=}17=ffGenhACI_5cbps ne NDOR_IDID_C16SPD_SHI t	t	  )   e PCI_DEVICE_ID_C16SPD_MASK)t==rID_C16SPD_SHI)t,				T
=}18=y_C104_PNDOR_IDID_C16SPD_WARP t	t	  )   e PCI_DEVICE_ID_C16SPD_MASK)t==rID_C16SPD_WARP)t,				T
=}19=y_C104_P			}
	,	  )     asuc lepaces i]c==C134)de 	,	,quoD = (2912_O PCIuc level  / 269)S
			} 	icod  )     asuc lepaces i])de 	,	,quoD = _O PCIuc level  /     asuc lepaces i]=y		,	  ) quoD =.C!) 	,a,,quoD = 1S
			} 	icode 	,	,quoD = 0; t		}
	,} 	icod  ) quoD =.C!) 	,aquoD = 1S
	} 	icode 	,quoD = 0; t}
y	  ) quoD)pe 	,   PCIMCR |=_
 *  ? (aDTR; t	haw d_DEVI(_DEVI)=y		cli()=y	,*/
b( e PCI? (,  e PCIvel  + 
 *  ? ()my		r_st opt_DEVI(_DEVI)=y	} 	icode 	,   PCIMCR &= ~
 *  ? (aDTR; t	haw d_DEVI(_DEVI)=y		cli()=y	,*/
b( e PCI? (,  e PCIvel  + 
 *  ? ()my		r_st opt_DEVI(_DEVI)=y	_r_NDOR)het=y	}LfGenby mo8mor r separitLTm/y	switchtMcE(p 
& CSIZE)de 	P); BCS5:y		cva i.C0x00=y_	confi;t,P); BCS6:y		cva i.C0x01=y_	confi;t,P); BCS7:y		cva i.C0x02=y_	confi;t,P); BCS8:y		cva i.C0x03=y_	confi;t,default:y		cva i.C0x00=y_	confi;ffGentoo keep GCCclDxi...Tm/y	}
	  ) cE(p 
& CSTOPB)y		cva i|.C0x04=y_  ) cE(p 
& PARENB)y		cva i|.C
 *    (aPARITY=y	  ) ! cE(p 
& PARODD))y		cva i|.C
 *    (aEPAR;
	  ) M e PCI. */ ==  bau_8250T    ( e PCI. */ ==  bau_16450T)pe 	,sca =}0=y_} 	icode 	,sca =}
 *  F (a = {
	_FIFO; t	hwitchtM e PCIrx_trigg|P)de 	,P); B1:y			sca |=}
 *  F (aTRIGGER_1S
			confi; t	P); B4:y			sca |=}
 *  F (aTRIGGER_4S
			confi; t	P); B8:y			sca |=}
 *  F (aTRIGGER_8S
			confi; t	default:y			sca |=}
 *  F (aTRIGGER_14S
		}
	}
y	GenCud
 lowuI_2trolr    Flogy0efsei      0(8	0      
ne t e PCIIER &= ~
 *  Iet MSI;Lf   PCIMCR &= ~
 *  ? (aAFE=y_  ) cE(p 
& CaudCud)pe 	,   PCI_DEVIC|=_ID_C16Cud_Fdef; 	,   PCIIERt|=_
 *  Iet MSI;LfI  )  e PCIl */ ==  bau_16550A)t,		T_ PCIMCR |=_
 *  ? (aAFE=y_} 	icode 	,   PCI_DEVIC&= ~ID_C16Cud_Fdef; 	}
,*/
b( e PCI? (,  e PCIvel  + 
 *  ? ()my_  ) cE(p 
& C91CAL)
,	   PCI_DEVIC&= ~ID_C16CHECK_CD;
		icode 	,   PCI_DEVIC|=_ID_C16CHECK_CD;
	,   PCIIERt|=_
 *  Iet MSI;Lf} 	*/
b( e PCIIER,  e PCIvel  + 
 *  Iet)=y
	TDA	_maSepa/chparitLTch5ila_DEV.

ne t e PCIheE veect5    o_ = 
 *    D_OEt|_
 *    D_THREt|_
 *    D_DR;
	  ) I_INPCK  e PCIlp
))
,	   PCIheE veect5    o_ |= 
 *    D_FEt|_
 *    D_PE;
	  ) I_BRKINT  e PCIlp
)    IaPARMRK  e PCIlp
))
,	   PCIheE veect5    o_ |= 
 *    D_BI=yna e PCI  B optiCE_Ie-  De =}0=yNDO 0 	/(p  MIn0  Moxabmosafe, G*6 PCI_some brokUn1bi  0of hardware...Tm/y	  ) I_IGNPAR  e PCIlp
))de 	,   PCI  B optiCE_Ie-  De |= 
 *    D_PEt|_
 *    D_FE=y_	   PCIheE veect5    o_ |= 
 *    D_PEt|_
 *    D_FE=y_}y_C104_P	  ) I_IGNBRK  e PCIlp
))de 	,   PCI  B optiCE_Ie-  De |= 
 *    D_BI=y_	   PCIheE veect5    o_ |= 
 *    D_BI=y_	TDA		_maIf w 'opnh B ophparitLTlogyconfi_i104cat o ,eA B opA		_maov0   nsrsoo.  (FCI_r_a irfwu<linux/).A		_m/y		  ) I_IGNPAR  e PCIlp
))de 	,,   PCI  B optiCE_Ie-  De |= 
 *    D_OEt|_
 *    D_PEt|_
 *    D_FE=y_		   PCIheE veect5    o_ |= 
 *    D_OEt|_
 *    D_PEt|_
 *    D_FE=y_	}
	}
_haw d_DEVI(_DEVI)=y	cli()=y	*/
b(cva i|C
 *    (aDLAB,  e PCIvel  + 
 *  LCt)=fGensT_ DLAB
ne t*/
b(quoD &r0xff,  e PCIvel  + 
 *  DLL)=fGenLS0of divisCI_ne t*/
b(quoD >> 8,  e PCIvel  + 
 *  DLM)=	,_ "MS0of divisCI_ne t*/
b(cva ,e e PCIvel  + 
 *  LCt)=fGenr_sT_ DLAB
ne t*/
b(scart e PCIvel  + 
 *  FCt)=fGensT_ sca .
 ,r	st opt_DEVI(_DEVI)=y
_r_NDOR)het=y}
 B(iE(p------------------------------------------------------------
 */stiC10 0of value*OU. Wa)iE(p------------------------------------------------------------
 *MD/    n Bhe(value*it_UART_DEBamm  e thiaccess _hACI_5 dT_IF,
,			OlDxian ART_DEBne PORTNO(x)		(het  fo)_UAflDxian ART_DEBne PORTtmp  
	  ) !het  fo)_,	r_NDOR) -EFAULT)=y	 emsT_(&tmp,C!no8morofgtmp))uy	tmp.l */ =} e PCIl */uy	tmp.dulep=} e PCIype;		/tmp.Eanpa=O e PCIvel 		/tmp.irqa=O e PCIirq		/tmp._DEVIC=O e PCI        tmp.uc level  = _O PCIuc level    tmp.a14AIpends  = _O PCIa14AIpends    tmp.a14A r8	0x16 = _O PCIa14A r8	0x16   tmp.aust m_divisCI_= _O PCIaust m_divisCI   tmp.hub6 =u0=y_r_NDOR)., L_toIAR|P het  fo,d&imp,C8morofg(het  fo))c? -EFAULT#if0=y}
 Wa(eOR_ID_MOXA, P0t_UART_DEBamm  e thiaccess _hACI_5 dT_IF,
,			OlDxian ART_DEBne PORTNO(x)		(new_  fo)_UAflDxian ART_DEBne PORTnew_ART_DE;naPeDrive 0 etMe        PTrhetva  =u0=yLf  ) !new_  fod  )! e PCIuct )y		r_NDOR) -EFAULT)=y	  ) ., L_ers/PAR|P &new_ART_DE, new_  fo,C8morofgnew_ART_DE))lET_r_NDOR)-EFAULT; 
	  ) Mnew_ART_DE.irqa!=O e PCIirq)d||Smar  Mnew_ART_DE.Eanpa!=O e PCIvel )d||Smar  Mnew_ART_DE.l */ !=} e PCIl */)d||Smar  Mnew_ART_DE.aust m_divisCI_!= _O PCIaust m_divisCI)d||Smar  Mnew_ART_DE.uc level  != _O PCIuc level ))y		r_NDOR) -EPERM) m t_DEVIC=O e PCI     CE_ID_C16SPD_MASK=yLf  ) !.apodul(R c_SYS_ADMIN))de 	,  ) Mnew_ART_DE.uc level  != _O PCIuc level )d  
nuar  Mnew_ART_DE.a14AIpends  != _O PCIa14AIpends )d  
nuar  MMnew_ART_DE.     CE_~ID_C16U D_MASK)t!=Sm	/tttt  e PCI_DEVICE_~ID_C16U D_MASK))lET_	r_NDOR) -EPERM) m	,   PCI_DEVIC=)   e PCI_DEVICE_~ID_C16U D_MASK)t 
nux/t ar  Mnew_ART_DE.     CE_ID_C16U D_MASK))=y_} 	icode 	,TDA		_maOK,vpast2.4.X
 o PT,E)   ouleR cD_C/05ilplea    beUn1d
	d.A		_maAt2.4.X
 o PT,rwne oarUAmak[ver016nges.....A		_m/y		   PCI_DEVIC=)   e PCI_DEVICE_~ID_C16ID_MS)t 
nux/t ar  Mnew_ART_DE.     CE_ID_C16ID_MS)) m	,   PCIa14AIpends  = new_ART_DE.a14AIpends  * sZ / 100 m	,   PCIa14A r8	0x16 = new_ART_DE.a14A r8	0x16 * sZ / 100 m	}
y	  )  e PCI_DEVICE_ID_C16xfffIALIZED)de 	,  ) _DEVIC!=)  e PCI_DEVICE_ID_C16SPD_MASK))de 	,,0fine 
	  This n2EVT_IF,aNULL)=y_	}
	} 	icoET_r_Nva  =uOXA, P0000200
  fo);
	r_NDOR) hetva l=y}_ B(iE(p  n, P RELEVANT_IF -de a : the      0T	0x0#ti_T_IFya(iE(pPurp4AI: Lepa/x)		c)    U. Wa) toge a T_IF 
#de  e  ape- physicPCl
A_matt  /tttt s emptied.  On1b  0l */ttlike RS485, GNUE8a(A bautiv0mustA_matt  /ttttrelea; B *
1b  0afeadE8a(A baut r8.p  MInmustabmod
	d 
#deA_matt  /ttttGNUE8a(A baun0 ift0T	0x0#ti_Ts empty,ter sbmod
	d 
#de GNUi_matt  /ttttGa(A baunhol ing0T	0x0#ti_Ts empty.    MInfuncnclualitLi_matt  /ttttnta*wser  RS485aI_or_m
Fn bei0rautinshe u; I_space  ) MD/    n Bhe(value* RELEVANT_IFLe thiaccess _hACI_5 dT_IF,umMD/VI_ ysk *NO(x)		(va ue)_UAfmMD/VI_ ylt va      S
	mMD/VI_ ysk *r_sult;namMD/VI_ yriver       y	haw d_DEVI(_DEVI)=y	cli()=y	h     0=  eb  e PCIvel  + 
 *    D);y_r_st opt_DEVI(_DEVI)=y	r	sult =u( h     0& 
 *    D_TEMT)c? TIOCSER_TEMT#if0);
	r_NDOR)rCfIAR|P r_sult,eva ue)=y}
 B(iE(p  MInroutatacsC10 0ayconfi_0167622 1 */

 *
 a	/* XON/XO  ) MD/    n  c., PCI_ANgRQ_2confige thiaccess _hACI_5 dT_IF,u  beduranclu)_UAfmMD/VI_ yriver          ) ! e PCIuct )y		r_NDOR;n	;T_DcurrRQtveecte(TASKI_DEVICE_IIBLE)=y	haw d_DEVI(_DEVI)=y	cli()=y	*/
b( eb  e PCIvel  + 
 *   CD)t|_
 *    (aSBC,e e PCIvel  + 
 *  LCt)=
	ude <x/dror G*/
(duranclu)=y	*/
b( eb  e PCIvel  + 
 *   CD)t& ~
 *    (aSBC,e e PCIvel  + 
 *  LCt)=
	r	st opt_DEVI(_DEVI)=y}-  DefineBhe(value*nclcm REa(eOR_ID_MOXA, PCISEU. , PCISER_BOA, PBOAe)_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;namMD/VI_ ylt vac_2trol,a      S
	mMD/VI_ yriver       
	  ) or and wav)c==C/
	int baud)y		r_NDOR) -ENOIOCTLCMDint,  ) .avCI_DEVICE_(1 << TTYuIO

#dOR)ly		r_NDOR) -EIO)=y
	I_2trolr= _O PCIMCR=y	haw d_DEVI(_DEVI)=y	cli()=y	h     0=  eb  e PCIvel  + 
 *  MSR)=y	  ) h     0& 
 *  M D_IPTIDELTA)y_	 (MOXA/05ilAdefse of CMOlT_IF,a      )=y	r_st opt_DEVI(_DEVI)=y	r	NDOR) (I_2trolr& 
 *  M (afiS)c? TIOCMafiS#if0) |Smar  M(I_2trolr& 
 *  M (aDTR)c? TIOCMaDTRtif0) |Smar  M(h     0& 
 *  M D_DCD)c? TIOCMaCARtif0) |Smar  M(h     0& 
 *  M D_RI) ?CTIOCM_RNGtif0) |Smar  M(h     0& 
 *  M D_DSR)c? TIOCMaDSRtif0) |Smar  M(h     0& 
 *  M D_CiS)c? TIOCMaCiS#if0)=y}-  DefineBhe(value*nclcmsREa(eOR_ID_MOXA, PCISEU. , PCISER_BOA, PBOAe,
,		/tmMD/VI_ ysk *sIR, _2OlD ( OXA_*clede)_UAf0x00o#define INPAR|PAT_IF
=}(8g0x000lwPE4 P 0
#def  wavCIC_or_m_ere ;namMD/VI_ yriver       y	  ) or and wav)c==C/
	int baud)y		r_NDOR) -ENOIOCTLCMDint,  ) .avCI_DEVICE_(1 << TTYuIO

#dOR)ly		r_NDOR) -EIO)=y
	haw d_DEVI(_DEVI)=y	cli()=y	  ) sT_ & TIOCMafiS)A		   PCIMCR |=_
 *  ? (aaud=y	  ) sT_ & TIOCMaDTR) 	,   PCIMCR |=_
 *  ? (aDTR; y	  ) .lede & TIOCMafiS)A		   PCIMCR &= ~
 *  ? (aaud=y	  ) .lede & TIOCMaDTR) 	,   PCIMCR &= ~
 *  ? (aDTR; 
,*/
b( e PCI? (,  e PCIvel  + 
 *  ? ()my_r_st opt_DEVI(_DEVI)=y	r	NDOR) 0)=y}
  DefineBhe(value*heE vT	0x0#ti( PT,rmMD/VI_ yshanpa*)=y DefineBhe(value*programAdefsg OXA;D/    n  c., PCI_ANn    lAdefsg OXA;DD/    n Bhe(value* RELISA_t WI thi .ap, PCISER_efine but WI *but WIT_UAfw)
MAd,MAJObi  ;namMD/VI_ yshanpar_ND[16]JO .o;namMD/VI_ ylt vascrFtch,ascrFtch2; y	 d =uOXA, PheE vT	0x0#ti(.ap, r_ND)=y	  ) id
== C168_ASI16xD) 	,but WICIvrq no. */ =C/
	intmily _C168_ISA;
		icod  ) id
== C104_ASI16xD) 	,but WICIvrq no. */ =C/
	intmily _C104_ISA;
		icod  ) id
== C102_ASI16xD) 	,but WICIvrq no. */ =C/
	intmily _C102_ISA;
		icod  ) id
== CI132_ASI16xD) 	,but WICIvrq no. */ =C/
	intmily _CI132;
		icod  ) id
== CI134_ASI16xD) 	,but WICIvrq no. */ =C/
	intmily _CI134;
		icod  ) id
== CI104J_ASI16xD) 	,but WICIvrq no. */ =C/
	intmily _CI104J;t,	icoET_r	NDOR) 0)=y	irqa=Or_ND[9] &r0x0F=y	irqa=Oirqa|)  .o << 4)=y	irqa=Oirqa|)  .o << 8)=y	  ) (irqa!=Or_ND[9]T    ( id
== 1)theirirqa!=Or_ND[10])l)de 	,r	NDOR) RR_IOADRR_fineTMSTLIT)=y	}y	  ) ! rq)de 	,r	NDOR) RR_IOADRR_fin)=y	}y	PCI_(ii.C!= i <r8= i++) 	,but WICIioaddr i]c=}(T|I)Or_ND[i + 1] &r0xFFF8S
	but WICIirqa=O(T|I)Orirqa&r0x0F)=y	  ) (r_ND[12]0& 0x80)t==r0)de 	,r	NDOR) RR_IOADRR_VECTOt)=y	}
,but WICIge f  c=}(T|I)Or_ND[11]=fGenhanced Reage f  cne t  )  d
== 1) 	,but WICIge f  -  De =}0x00FF;ig	icoET_but WICIge f  -  De =}0x000F=y	PCI_(ii.C7nobi   =u0x010!= i >.C!= i--nobi   <<= 1)de 	,  ) heND[12]0& bi  )y_		but WICIvc level  i]c=}o7(cti=y_		ico 	,	but WICIvc level  i]c=}hACI_5=y	}
,scrFtch20=  eb .ap + 
 *   CD)t& (~
 *    (aDLAB)=y	*/
b(scrFtch20|C
 *    (aDLAB, .ap + 
 *   CD)=y	*/
b(0, .ap + 
 *  EFt)=fGenEFtn     rsCameolsmFCR
ne t*/
b(scrFtch2, .ap + 
 *   CD)=y	*/
b(
 *  F (a = {
	_FIFO, .ap + 
 *  FCt)=
	udrFtch0=  eb .ap + 
 *  IID)=y	  ) sdrFtch0& 0xC0) 	,but WICIuarto. */ =C bau_16550A;ig	icoET_but WICIuarto. */ =C bau_16450; t  )  d
== 1) 	,but WICI60)
#
=}8=y_	icoET_but WICI60)
#
=}4=y	r	NDOR) but WICI60)
#)=y}
 #R_IatacCHIP_SK 	0x01 nuuS	/* XODre 
C4AIpishe Eprom ne NR_IatacCHIP_DO 	0x02 nuuS	/* XODre 
Out
	inhe Eprom ne NR_IatacCHIP_CS 	0x04 nuuS	/* XOChipuS	lecbehe Eprom ne NR_IatacCHIP_DI 	0x08 nuuS	/* XODre 
IP
	inehe Eprom ne NR_IatacEN_CCMD 	0x000	GenChip' themmlogyT	0x0#ti_____ne NR_IatacEN0_RSARLO	0x008 nuuRemo mo oarUAaddres 0T	0 0__ne NR_IatacEN0_RSARHI	0x009 nuuRemo mo oarUAaddres 0T	0 1__ne NR_IatacEN0_RCNTLO	0x00A nuuRemo moby moa		/_FT	0 WR____ne NR_IatacEN0_RCNTHI	0x00B nuuRemo moby moa		/_FT	0 WR____ne NR_IatacEN0_DCFG	0x00E,_ "Dre 
t WIigurancluFT	0 WR___ne NR_IatacEN0_ bau	0x010	GenRcv misstiCframeoR cD_C/		/_PE4RD_ne NR_IatacENC_PAGE0	0x000	GenS	lecbe
	  T00of chipur_0x0#tis  _ne NR_IatacENC_PAGE3	0x0C0	GenS	lecbe
	  T30of chipur_0x0#tis  _ne  DefineBhe(value*heE vT	0x0#ti( PTON/XO,rmMD/VI_ yshanpa*r_ND)_UAfw)
MAJOk,eva ueJO dS
	mMD/VI_ ysk *j; y	 d =uOXA, PprogramAdefsgnux/); t  )  d
<r0lET_r	NDOR)  d);y	PCI_(ii.C!= i <r14= i++)de 	,kc=}(T0& 0x3F)t|_0x180=y_	PCI_(j =u0x10!= j > 0= j >>= 1)de 	,,*/
b(CHIP_CS, nux/); t	,  ) k0& j)de 	,	,*/
b(CHIP_CS | CHIP_DO, nux/); t	,,*/
b(CHIP_CS | CHIP_DO | CHIP_SK, nux/);	GenA? bi 0of r_  Mne t	t} 	icode 	,	,*/
b(CHIP_CS, nux/); t	,,*/
b(CHIP_CS | CHIP_SK, nux/);	GenA? bi 0of r_  Mne t	t}y_	}
	tTX3912  eb nux/); t	va ue = 0; t	PCI_(e =}0, j =u0x800!= k <r16= k++JOj >>= 1)de 	,,*/
b(CHIP_CS, nux/); t	,*/
b(CHIP_CS | CHIP_SK, nux/); t	,  )  eb nux/)
& CHIP_DI) 	,a,va ue |=_j=y_	}
		r_ND[i]c=}va ue=y_	*/
b(0, nux/); t} t0fine n    lAdefsgnux/); tr	NDOR)  d);y}
  DefineBhe(value*programAdefsg OX nux/)_UAfw)
MAd,MAJOj, n;namMD/VI_ yriver       y	haw d_DEVI(_DEVI)=y	cli()=y	*/
b(0, nux/); t*/
b(0, nux/); t*/
b(0, nux/); tTX3912  eb nux/); tTX3912  eb nux/); t*/
b(0, nux/); tTX3912  eb nux/); tr_st opt_DEVI(_DEVI)=y	 d =  eb 60)
 + 1)0& 0x1F=y	  ) (id != C168_ASI16xD)theirid != C104_ASI16xD)theirid != CI104J_ASI16xD)p&&
	mrid != C102_ASI16xD)p&&	rid != CI132_ASI16xD)theirid != CI134_ASI16xD)ly		r_NDOR) -1);y	PCI_(ii.C!, j =u0= i <r4= i++)de 	,n =  eb 60)
 + 2)=y		  ) n
== 'M')de 	,,j = 1S
		} 	icod  ) (j == 1)theirn == 1))de 	,,j = 2=y_		confi; t	} 	ico 	,,j = 0=y	}y	  ) j != 2) 	, d = -2; tr	NDOR)  d);y}
  Define c., PCI_ANn    lAdefsg OX nux/)_UAfw)
MA, n;n 	*/
b(0xA5, 60)
 + 1); t*/
b(0x80, 60)
 + 3); t*/
b(12, 60)
 + 0);	Gen9cti bps ne t*/
b(0, nux/ + 1); t*/
b(0x03, 60)
 + 3);	Gen8