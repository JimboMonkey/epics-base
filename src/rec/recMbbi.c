/* recMbbi.c */
/* share/src/rec $Id$ */

/* recMbbi.c - Record Support Routines for multi bit binary Input records */
/*
 *      Original Author: Bob Dalesio
 *      Current Author:  Marty Kraimer
 *      Date:            5-9-88
 *
 *      Experimental Physics and Industrial Control System (EPICS)
 *
 *      Copyright 1991, the Regents of the University of California,
 *      and the University of Chicago Board of Governors.
 *
 *      This software was produced under  U.S. Government contracts:
 *      (W-7405-ENG-36) at the Los Alamos National Laboratory,
 *      and (W-31-109-ENG-38) at Argonne National Laboratory.
 *
 *      Initial development by:
 *              The Controls and Automation Group (AT-8)
 *              Ground Test Accelerator
 *              Accelerator Technology Division
 *              Los Alamos National Laboratory
 *
 *      Co-developed with
 *              The Controls and Computing Group
 *              Accelerator Systems Division
 *              Advanced Photon Source
 *              Argonne National Laboratory
 *
 * Modification Log:
 * -----------------
 * .01  12-12-88        lrd     lock the record while processing
 * .02  12-15-88        lrd     Process the forward scan link
 * .03  12-23-88        lrd     Alarm on locked MAX_LOCKED times
 * .04  01-13-89        lrd     delete db_read_mbbi
 * .05  03-29-89        lrd     make hardware errors MAJOR
 *                              remove hw severity spec from database
 * .06  04-07-89        lrd     add monitor detection
 * .07  05-03-89        lrd     removed process mask from arg list
 * .08  05-29-89        lrd     support 16 states
 * .09  05-30-89        lrd     fixed masks for allen-bradley IO
 * .10  06-06-89        lrd     fixed AB mbbi conversion - signal wrong
 *                              added ability to enter raw numbers if no
 *                              states are defined - like the mbbo
 * .11  12-06-89        lrd     add database fetch support
 * .12  02-08-90        lrd     add Allen-Bradley PLC support
 * .13  10-31-90	mrk	changes for new record and device support
 */

#include	<vxWorks.h>
#include	<types.h>
#include	<stdioLib.h>
#include	<lstLib.h>
#include	<strLib.h>

#include	<alarm.h>
#include	<dbAccess.h>
#include	<dbDefs.h>
#include	<dbFldTypes.h>
#include	<devSup.h>
#include	<errMdef.h>
#include	<link.h>
#include	<recSup.h>
#include	<special.h>
#include	<mbbiRecord.h>
/* Create RSET - Record Support Entry Table*/
#define report NULL
#define initialize NULL
long init_record();
long process();
long  special();
long get_value();
#define cvt_dbaddr NULL
#define get_array_info NULL
#define put_array_info NULL
#define get_units NULL
#define get_precision NULL
long get_enum_str();
long get_enum_strs();
long put_enum_str();
#define get_graphic_double NULL
#define get_control_double NULL
#define get_alarm_double NULL

struct rset mbbiRSET={
	RSETNUMBER,
	report,
	initialize,
	init_record,
	process,
	special,
	get_value,
	cvt_dbaddr,
	get_array_info,
	put_array_info,
	get_units,
	get_precision,
	get_enum_str,
	get_enum_strs,
	put_enum_str,
	get_graphic_double,
	get_control_double,
	get_alarm_double };

struct mbbidset { /* multi bit binary input dset */
	long		number;
	DEVSUPFUN	dev_report;
	DEVSUPFUN	init;
	DEVSUPFUN	init_record; /*returns: (-1,0)=>(failure,success)*/
	DEVSUPFUN	get_ioint_info;
	DEVSUPFUN	read_mbbi;/*(0,1,2)=>(success, asyn, success no convert)*/
};
void alarm();
void monitor();

static void init_common(pmbbi)
    struct mbbiRecord	*pmbbi;
{
        unsigned long 	*pstate_values;
        short  		i;

        /* determine if any states are defined */
        pstate_values = &(pmbbi->zrvl);
        pmbbi->sdef = FALSE;
        for (i=0; i<16; i++) {
                if (*(pstate_values+i) != 0) {
			pmbbi->sdef = TRUE;
			return;
		}
	}
	return;
}

static long init_record(pmbbi)
    struct mbbiRecord	*pmbbi;
{
    struct mbbidset *pdset;
    long status;
    int i;

    if(!(pdset = (struct mbbidset *)(pmbbi->dset))) {
	recGblRecordError(S_dev_noDSET,pmbbi,"mbbi: init_record");
	return(S_dev_noDSET);
    }
    /* must have read_mbbi function defined */
    if( (pdset->number < 5) || (pdset->read_mbbi == NULL) ) {
	recGblRecordError(S_dev_missingSup,pmbbi,"mbbi: init_record");
	return(S_dev_missingSup);
    }
    /* initialize mask*/
    pmbbi->mask = 0;
    for (i=0; i<pmbbi->nobt; i++) {
	pmbbi->mask <<= 1; /* shift left 1 bit*/
	pmbbi->mask |= 1;  /* set low order bit*/
    }
    if( pdset->init_record ) {
	if((status=(*pdset->init_record)(pmbbi,process))) return(status);
    }
    init_common(pmbbi);
    return(0);
}

static long process(paddr)
    struct dbAddr	*paddr;
{
    struct mbbiRecord	*pmbbi=(struct mbbiRecord *)(paddr->precord);
	struct mbbidset	*pdset = (struct mbbidset *)(pmbbi->dset);
	long		status;

	if( (pdset==NULL) || (pdset->read_mbbi==NULL) ) {
		pmbbi->pact=TRUE;
		recGblRecordError(S_dev_missingSup,pmbbi,"read_mbbi");
		return(S_dev_missingSup);
	}

	status=(*pdset->read_mbbi)(pmbbi); /* read the new value */
	pmbbi->pact = TRUE;
	if (status==1) return(0);
	tsLocalTime(&pmbbi->time);
	if(status==0) { /* convert the value */
        	unsigned long 	*pstate_values;
        	short  		i;
		unsigned long rval = pmbbi->rval;

		if(pmbbi->shft>0) rval >>= pmbbi->shft;
		if (pmbbi->sdef){
			pstate_values = &(pmbbi->zrvl);
			pmbbi->val = 65535;         /* initalize to unknown state*/
			pmbbi->udf = TRUE;
			for (i = 0; i < 16; i++){
				if (*pstate_values == rval){
                               		pmbbi->val = i;
                               		pmbbi->udf = FALSE;
                               		break;
			    	}
			    	pstate_values++;
			}
		}else{
			/* the raw value is the desired value */
			pmbbi->val =  (unsigned short)rval;
			pmbbi->udf =  FALSE;
		}
	}
	else if(status == 2) status = 0;

	/* check for alarms */
	alarm(pmbbi);


	/* check event list */
	monitor(pmbbi);

	/* process the forward scan link record */
	if (pmbbi->flnk.type==DB_LINK) dbScanPassive(pmbbi->flnk.value.db_link.pdbAddr);

	pmbbi->pact=FALSE;
	return(status);
}


static long special(paddr,after)
    struct dbAddr *paddr;
    int           after;
{
    struct mbbiRecord     *pmbbi = (struct mbbiRecord *)(paddr->precord);
    int                 special_type = paddr->special;

    if(!after) return(0);
    switch(special_type) {
    case(SPC_MOD):
	init_common(pmbbi);
	return(0);
    default:
        recGblDbaddrError(S_db_badChoice,paddr,"mbbi: special");
        return(S_db_badChoice);
    }
}

static long get_value(pmbbi,pvdes)
    struct mbbiRecord		*pmbbi;
    struct valueDes	*pvdes;
{
    pvdes->field_type = DBF_ENUM;
    pvdes->no_elements=1;
    (unsigned short *)(pvdes->pvalue) = &pmbbi->val;
    return(0);
}

static long get_enum_str(paddr,pstring)
    struct dbAddr *paddr;
    char	  *pstring;
{
    struct mbbiRecord	*pmbbi=(struct mbbiRecord *)paddr->precord;
    char		*psource;
    unsigned short	val=pmbbi->val;

    if(val<= 15) {
	psource = (pmbbi->zrst);
	psource += (val * sizeof(pmbbi->zrst));
	strncpy(pstring,psource,sizeof(pmbbi->zrst));
    } else {
	strcpy(pstring,"Illegal Value");
    }
    return(0);
}

static long get_enum_strs(paddr,pes)
    struct dbAddr *paddr;
    struct dbr_enumStrs *pes;
{
    struct mbbiRecord	*pmbbi=(struct mbbiRecord *)paddr->precord;
    char		*psource;
    int			i;
    short		no_str;

    no_str = 0;
    bzero(pes->strs,sizeof(pes->strs));
    for(i=0,psource=(pmbbi->zrst); i<16; i++, psource += sizeof(pmbbi->zrst) ) {
	strncpy(pes->strs[i],psource,sizeof(pmbbi->zrst));
	if(*psource!=0) no_str=i+1;
    }
    pes->no_str=no_str;
    return(0);
}
static long put_enum_str(paddr,pstring)
    struct dbAddr *paddr;
    char          *pstring;
{
    struct mbbiRecord     *pmbbi=(struct mbbiRecord *)paddr->precord;
        char              *pstate_name;
        short             i;

        if (pmbbi->sdef){
                pstate_name = pmbbi->zrst;
                for (i = 0; i < 16; i++){
                        if(strncmp(pstate_name,pstring,sizeof(pmbbi->zrst))==0){
        			pmbbi->val = i;
                                return(0);
                        }
                	pstate_name += sizeof(pmbbi->zrst);
                }
        }
	return(S_db_badChoice);
}

static void alarm(pmbbi)
    struct mbbiRecord	*pmbbi;
{
	unsigned short *severities;
	unsigned short	val=pmbbi->val;

        /* check for udf alarm */
        if(pmbbi->udf == TRUE ){
                if (pmbbi->nsev<VALID_ALARM){
                        pmbbi->nsta = UDF_ALARM;
                        pmbbi->nsev = VALID_ALARM;
                }
        }

        /* check for  state alarm */
        /* unknown state */
        if (val > 15){
                if (pmbbi->nsev<pmbbi->unsv){
                        pmbbi->nsta = STATE_ALARM;
                        pmbbi->nsev = pmbbi->unsv;
                }
        } else {
        	/* in a state which is an error */
        	severities = (unsigned short *)&(pmbbi->zrsv);
        	if (pmbbi->nsev<severities[pmbbi->val]){
                	pmbbi->nsta = STATE_ALARM;
                	pmbbi->nsev = severities[pmbbi->val];
        	}
	}

        /* check for cos alarm */
	if(val == pmbbi->lalm) return;
        if (pmbbi->nsev<pmbbi->cosv){
                pmbbi->nsta = COS_ALARM;
                pmbbi->nsev = pmbbi->cosv;
                return;
        }
	pmbbi->lalm = val;
	return;
}

static void monitor(pmbbi)
    struct mbbiRecord	*pmbbi;
{
	unsigned short	monitor_mask;
        short           stat,sevr,nsta,nsev;

        /* get previous stat and sevr  and new stat and sevr*/
        stat=pmbbi->stat;
        sevr=pmbbi->sevr;
        nsta=pmbbi->nsta;
        nsev=pmbbi->nsev;
        /*set current stat and sevr*/
        pmbbi->stat = nsta;
        pmbbi->sevr = nsev;
        pmbbi->nsta = 0;
        pmbbi->nsev = 0;

	monitor_mask = 0;

        /* alarm condition changed this scan */
        if (stat!=nsta || sevr!=nsev){
                /* post events for alarm condition change*/
                monitor_mask = DBE_ALARM;
                /* post stat and sevr fields */
                db_post_events(pmbbi,&pmbbi->stat,DBE_VALUE);
                db_post_events(pmbbi,&pmbbi->sevr,DBE_VALUE);
        }
        /* check for value change */
        if (pmbbi->mlst != pmbbi->val){
                /* post events for value change and archive change */
                monitor_mask |= (DBE_VALUE | DBE_LOG);
                /* update last value monitored */
                pmbbi->mlst = pmbbi->val;
        }
        /* send out monitors connected to the value field */
        if (monitor_mask){
                db_post_events(pmbbi,&pmbbi->val,monitor_mask);
	}
        if(pmbbi->oraw!=pmbbi->rval) {
                db_post_events(pmbbi,&pmbbi->rval,monitor_mask|DBE_VALUE);
                pmbbi->oraw = pmbbi->rval;
        }
        return;
}
