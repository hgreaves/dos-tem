/*
 * Vegetation_Bgc.cpp
 *
 * Purpose: Calculating (IN)GPP, (IN)NPP, RM, RG, N UPTAKE and resulted C & N changes
 *
 * History:
 *     June 28, 2011, by F.-M. Yuan:
 *          (1) Recoding based on DOS-TEM's code;
 *          (2) Multiple vegetation C&N pools added for DVM
 *
 * Important:
 *     (1) Parameters are read from 'CohortLookup.cpp', and set to 'bgcpar' (struct:: vegpar_bgc)
 *     (2) Calibrated Parameters are also read from 'CohortLookup.cpp' initially, and set to 'calpar' (strut:: vegpar_cal)
 *
 *     (3) The calculation is for ONE PFT only, so when calling it must be set the important index:
 *          pfttype
 *     (4) FIVE (5) data pointers must be initialized by calling corresponding 'set...' methods
 *          chtlu, ed, bd, sb, fd
 *     (5) all calculations are NOT 'fpc' adjusted - so, the unit is PER projected ground
 *
 */

#include "Vegetation_Bgc.h"

Vegetation_Bgc::Vegetation_Bgc(){
	bgcpar.dc2n = 0.000519;
	//increase of c:n = 0.176 if the nitrogen of veg is decreased by 15% per doubling of co2
	//proportional change in c:n per change co2
	//0.15 increase in C:N with double co2
	
};

Vegetation_Bgc::~Vegetation_Bgc(){
	
};

//Yuan: the parameterization and initialization is done for one PFT
//set the bgc parameters from inputs
void Vegetation_Bgc::initializeParameter(){
	
  	bgcpar.kc      = chtlu->kc[ipft];
  	bgcpar.ki      = chtlu->ki[ipft];
	bgcpar.tmin    = chtlu->tmin[ipft];
	bgcpar.toptmin = chtlu->toptmin[ipft];
	bgcpar.toptmax = chtlu->toptmax[ipft];
	bgcpar.tmax    = chtlu->tmax[ipft];

  	bgcpar.raq10a0 = chtlu->raq10a0[ipft];
  	bgcpar.raq10a1 = chtlu->raq10a1[ipft];
  	bgcpar.raq10a2 = chtlu->raq10a2[ipft];
  	bgcpar.raq10a3 = chtlu->raq10a3[ipft];
  
  	bgcpar.knuptake = chtlu->knuptake[ipft];
  
  	for (int i=0; i<NUM_PFT_PART; i++) {
  		bgcpar.cpart[i]   = chtlu->cpart[i][ipft];
  		bgcpar.npart[i]   = chtlu->npart[i][ipft];
  	}

  	bgcpar.c2na = chtlu->c2na[ipft];
  	for (int i=0; i<NUM_PFT_PART; i++) {
  		bgcpar.c2nb[i]   = chtlu->c2nb[i][ipft];
  		bgcpar.c2neven[i] = chtlu->initc2neven[i][ipft];
  	  	bgcpar.c2nmin[i] = chtlu->c2nmin[i][ipft];
  	}
  	bgcpar.labncon = chtlu->labncon[ipft];

  	// Calibrated parameters for vegetation BGC
  	calpar.cmax = chtlu->cmax[ipft];
  	calpar.nmax = chtlu->nmax[ipft];
  	for (int i=0; i<NUM_PFT_PART; i++) {
  		calpar.cfall[i] = chtlu->cfall[i][ipft];
  		calpar.nfall[i] = chtlu->nfall[i][ipft];
  	}
  	calpar.kra = chtlu->kra[ipft];
  	for (int i=0; i<NUM_PFT_PART; i++) {
  		calpar.krb[i] = chtlu->krb[i][ipft];
  	}
  	calpar.frg = chtlu->frg[ipft];
	
};

// set the initial states from inputs
void Vegetation_Bgc::initializeState(){
	
	double totvegn = 0.;
	for (int i=0; i<NUM_PFT_PART; i++){
		bd->m_vegs.c[i]    = chtlu->initvegc[i][ipft];
		bd->m_vegs.strn[i] = chtlu->initvegn[i][ipft]*0.95;

		totvegn += chtlu->initvegn[i][ipft];
	}
	bd->m_vegs.labn  = totvegn*0.05;

	bd->m_vegs.deadc = chtlu->initdeadc[ipft];
	bd->m_vegs.deadn = chtlu->initdeadn[ipft];

};

//set the initial states from restart inputs
void Vegetation_Bgc::initializeState5restart(RestartData *resin){

	for (int i=0; i<NUM_PFT_PART; i++){
	  bd->m_vegs.c[i]    = resin->vegc[i][ipft];
	  bd->m_vegs.strn[i] = resin->strn[i][ipft];
	}
	bd->m_vegs.labn = resin->labn[ipft];

	bd->m_vegs.deadc = resin->deadc[ipft];
	bd->m_vegs.deadn = resin->deadn[ipft];
 	
};

//Yuan: the calculation is done for PFTs one by one
void Vegetation_Bgc::prepareIntegration(const bool &nfeedback){

	//option of N module
	nfeed = nfeedback;

	//canopy conductance for GPP
	double pet = ed->m_l2a.pet;    //
   	if (pet<=0.0) pet = 0.0001;
    double eet = ed->m_l2a.eet;    //
	bd->m_vegd.gv = getGV(eet,  pet);

	// previous 'topt' is the average of previous 10 years
	double prvtopt = 0.;
	deque<double> toptdeque = cd->toptque[ipft];
	int recnum=toptdeque.size();
	if (recnum>0) {
		for(int i=0; i<recnum; i++){
			prvtopt += toptdeque[i]/recnum;
		}
	} else {  //no previous year 'topt'
		prvtopt = (bgcpar.tmin+bgcpar.tmax)/2.;
	}

	// temperature factor for GPP
	bd->m_vegd.ftemp = getTempFactor4GPP(ed->m_atms.ta, prvtopt);

	// temperature factor for plant respiration
	bd->m_vegd.raq10 = getRaq10(ed->m_atms.ta);

	// litter-falling seasonal adjustment
	double prvttime = 0.;   //previous 10 year mean of growing season degree-day, using for normalizing current growing period needed for litterfalling
	deque<double> ttimedeque = cd->prvgrowingttimeque[ipft];
	recnum=ttimedeque.size();
	if (recnum>0) {
		for(int i=0; i<recnum; i++){
			prvttime += ttimedeque[i]/recnum;
		}
	} else {  //no previous year 'ttime'
		prvttime = (bgcpar.tmin+bgcpar.tmax)/2.;
	}

	for (int i=0; i<NUM_PFT_PART; i++){
		// assuming 'calpar.cfall' is the max. monthly fraction, we need to modify them for specific PFT types
		if (cd->m_veg.ifdeciwoody[ipft]) {
			calpar.cfall[I_leaf] = 1.0;     // for deciduous woody species, leaf max. falling fraction is 1.0; stem/root will take the calibrated.
			calpar.nfall[I_leaf] = 1.0;
		}
		if (!cd->m_veg.ifperenial[ipft]) {
			calpar.cfall[i] = 1.0;     // for annual species, leaf/stem/root max. falling/dying fraction is 1.0, no matter what calibrated
			calpar.nfall[i] = 1.0;
		}

		// assuming 'calpar.cfall' is the max. monthly fraction, and allowing the following seasonal variation
		fltrfall = 1.;              // non-growing season, max. litterfall assumed
		if (ed->m_soid.tsdegday>0. && prvttime>0.) {
			fltrfall = min(1., ed->m_soid.tsdegday/prvttime);
		}

 	}

	// sum of 'ipft' fine root fraction
	sumfrtfrac = 0.;
	for (int il=0; il<cd->m_soil.numsl; il++) {
		sumfrtfrac+=cd->m_soil.frootfrac[il][ipft];
	}

	//assign states to temporary state variable
	tmp_vegs.call    = 0.;
	tmp_vegs.strnall = 0.;
	for (int i=0; i<NUM_PFT_PART; i++){
		tmp_vegs.c[i]    = bd->m_vegs.c[i];
		tmp_vegs.call   += bd->m_vegs.c[i];

		if (nfeed) {
			tmp_vegs.strn[i] = bd->m_vegs.strn[i];
			tmp_vegs.strnall+= bd->m_vegs.strn[i];
		}
	}
	if (nfeed) {
		tmp_vegs.labn = bd->m_vegs.labn;
		tmp_vegs.nall = bd->m_vegs.labn+bd->m_vegs.strnall;
	}

};

// C and N fluxes without N limitation
void Vegetation_Bgc::delta(){

	/// some environment variables
	double co2 = ed->m_atms.co2;
	double par = ed->m_a2l.par;

	//leaf phenology
	// 1) current EET and previous max. EET controlled
  	double fleaf = cd->m_veg.fleaf[ipft];
  
  	// 2) plant size (biomass C) or age controlled
 	double ffoliage = cd->m_veg.ffoliage[ipft];

  	//GPP without N limitation
  	double ingppall = getGPP(co2, par, fleaf, ffoliage,
  			        bd->m_vegd.ftemp, bd->m_vegd.gv);
	for (int i=0; i<NUM_PFT_PART; i++){
		if (bgcpar.cpart[i]>0.) {
			del_a2v.ingpp[i] = ingppall*bgcpar.cpart[i];
		} else {
			del_a2v.ingpp[i] = 0.;
		}
	}

  	// litter-falling
	for (int i=0; i<NUM_PFT_PART; i++){
		if (calpar.cfall[i]>0.) {
			del_v2soi.ltrfalc[i] = max(0., fltrfall*calpar.cfall[i] * tmp_vegs.c[i]);

		} else {
			del_v2soi.ltrfalc[i] = 0.;
		}
 	}

  	// maintainence respiration and NPP first estimation
	for (int i=0; i<NUM_PFT_PART; i++){
		if (tmp_vegs.c[i]>0.) {

			bd->m_vegd.kr[i] = getKr(tmp_vegs.c[i], i);
			del_v2a.rm[i]    = getRm(tmp_vegs.c[i], bd->m_vegd.raq10, bd->m_vegd.kr[i]);

			del_a2v.innpp[i] = del_a2v.ingpp[i] - del_v2a.rm[i];
		} else {
			del_v2a.rm[i]    = 0.;
			del_a2v.innpp[i] = 0.;
		}

	}

  	// growth respiration and NPP adjusting
	for (int i=0; i<NUM_PFT_PART; i++){
		del_v2a.rg[i] = 0.;
		if ( del_a2v.innpp[i] > 0.0 ){
			del_v2a.rg[i]     = calpar.frg * del_a2v.innpp[i];
			del_a2v.innpp[i] *= (1.-calpar.frg);
		}
	}
 
};

// C and N fluxes regulated by N uptakes
void Vegetation_Bgc::deltanfeed(){

	if(nfeed){

		// max. N uptake determined by plant f(foliage), air temperature, and soil conditions
		if (cd->m_veg.nonvascular[ipft]==0) {
			del_soi2v.innuptake = getNuptake(cd->m_veg.ffoliage[ipft], bd->m_vegd.raq10,
	  			                             bgcpar.knuptake, calpar.nmax);
		} else {
			del_soi2v.innuptake = calpar.nmax * cd->m_veg.ffoliage[ipft];    //need more mechanism algorithm for non-vascular plants:
			                                                       // they absorb N mainly from wet-deposition, and could from substrate (soil) through co-existed plants, or from biofixation
		}
	  	if (del_soi2v.innuptake < 0.0) del_soi2v.innuptake = 0.0;
	  	double avln = bd->m_soid.avlnsum;   // NOTE: 'avln' already updated in 'Soil_bgc' with N i/o and mineralization (? - need careful checking here???)
	  	if (del_soi2v.innuptake > 0.95*avln) del_soi2v.innuptake = 0.95*avln;

		// N litterfall and accompanying resorbtion
		for (int i=0; i<NUM_PFT_PART; i++){
			if (calpar.nfall[i]>0.) {
				// assuming 'calpar.nfall' is the max. monthly fraction, and allowing the following seasonal variation
				del_v2soi.ltrfaln[i] = max(0., fltrfall*calpar.nfall[i] * tmp_vegs.strn[i]);

			} else {
				del_v2soi.ltrfaln[i] = 0.;
			}


		}

		double nresorball = 0.;
		for (int i=0; i<NUM_PFT_PART; i++){
/*			if (bgcpar.c2neven[i]>0.) {
				if(del_v2soi.ltrfaln[i] <= del_v2soi.ltrfalc[i]/bgcpar.c2neven[i]){
					del_v2v.nresorb[i] = del_v2soi.ltrfalc[i]/bgcpar.c2neven[i]
						             - del_v2soi.ltrfaln[i];
				}else{
					del_v2soi.ltrfaln[i] = del_v2soi.ltrfalc[i]/bgcpar.c2neven[i];
					del_v2v.nresorb[i]   = 0.;
				}

				nresorball +=del_v2v.nresorb[i];
			}
*/
			double c2n = tmp_vegs.c[i]/tmp_vegs.strn[i];
			if (c2n>0.) {
				if(del_v2soi.ltrfaln[i] <= del_v2soi.ltrfalc[i]/c2n){
					del_v2v.nresorb[i] = del_v2soi.ltrfalc[i]/c2n
						             - del_v2soi.ltrfaln[i];
				}else{
					del_v2soi.ltrfaln[i] = del_v2soi.ltrfalc[i]/c2n;
					del_v2v.nresorb[i]   = 0.;
				}

				nresorball +=del_v2v.nresorb[i];
			}
		}

		//N requirements if fully no N limitation
		double nrequireall = 0.0;
		double nrequire[NUM_PFT_PART];
		for (int i=0; i<NUM_PFT_PART; i++){
			if (del_a2v.innpp[i]>0.){
				nrequire[i] = del_a2v.innpp[i]/bgcpar.c2neven[i];
				nrequireall+= nrequire[i];
			} else {
				nrequire[i] = 0.;
			}
		}

		// all N supply
		double tempnuptake = del_soi2v.innuptake;
		double templabn = nresorball+tmp_vegs.labn;   //resorbed N is portion of available 'labile N'
		double nsupply = tempnuptake+templabn;

		//C fluxes regulated by N uptake and labile N and N requirements
		double reduction = 1.0;
		if (nrequireall>0.) reduction = max(0., nsupply/nrequireall);

		if (reduction < 1.0) {

			for (int i=0; i<NUM_PFT_PART; i++){
				// npp/gpp reduction due to N limitation
    			if (bgcpar.cpart[i]>0.) {
    				del_a2v.npp[i] = del_a2v.innpp[i]*reduction;
    				if (del_a2v.npp[i] > 0.0) {
    					del_v2a.rg[i] = calpar.frg * del_a2v.npp[i];
    				} else {
    					del_v2a.rg[i] = 0.0;
    				}

    				del_a2v.gpp[i] = del_a2v.npp[i] + del_v2a.rg[i] + del_v2a.rm[i];
    				if (del_a2v.gpp[i] < 0.0) {
    					del_a2v.gpp[i] = 0.0;
    				}

    			} else {
    				del_a2v.npp[i] = 0.;
    				del_v2a.rg[i]  = 0.;
    				del_a2v.gpp[i] = 0.;
    			}

    			// N allocation
    			if (bgcpar.cpart[i]>0.) {

    				//empties the labile N pools to each nmobil pools, which later on added to each N pool
    				del_v2v.nmobil[i]  = min(1., max(0., nrequire[i]/max(1.e-8, nrequireall)))
						            *templabn;

    				//allocates uptaken N to each structural N pool
    				del_soi2v.snuptake[i] = min(1., max(0., nrequire[i]/max(1.e-8, nrequireall)))
				                       *tempnuptake;
    			} else {
    				del_v2v.nmobil[i] = 0.;
    				del_soi2v.snuptake[i] = 0.;

    			}
			}

		// N requirement less than max.Nuptake+labN (total N potential supply), which may need down-regulated in turn
		} else {
			// no N limitation on GPP/NPP
			double nppall = 0;
			for (int i=0; i<NUM_PFT_PART; i++){
				del_a2v.gpp[i] = del_a2v.ingpp[i];
				del_a2v.npp[i] = del_a2v.innpp[i];

				nppall += del_a2v.npp[i];
			}

			double inprodcn = nppall / nsupply;

		  	tempnuptake  *= (inprodcn * (inprodcn - 2*nppall/max(1.e-8, nrequireall)));  //Yuan: from E.E. 2009 paper, seems not correct
		  	if (tempnuptake< 0.0 ) {tempnuptake = 0.0; }

		  	// if N require even less than labile N + resorbed N
		  	if (templabn >= nrequireall) {
		  		for (int i=0; i<NUM_PFT_PART; i++){
        			if (bgcpar.cpart[i]>0.) {
        				del_v2v.nmobil[i] = nrequire[i];
        				del_soi2v.snuptake[i] = 0.;
        			} else {
        				del_v2v.nmobil[i] = 0.;
        				del_soi2v.snuptake[i] = 0.;
        			}
		  		}

		  	// N requirement greater than labile N + resorbed N, but less than total potential supply - implying must uptake from soil
		  	} else {

        		for (int i=0; i<NUM_PFT_PART; i++){
        			if (bgcpar.cpart[i]>0.) {
        				del_v2v.nmobil[i]     = templabn*nrequire[i]/max(1.e-8, nrequireall); //empty all available labile N first
        				del_soi2v.snuptake[i] = (del_a2v.npp[i]/bgcpar.c2neven[i]) - del_v2v.nmobil[i];
				
        				if ( del_soi2v.snuptake[i] < 0.0 ) del_soi2v.snuptake[i] = 0.0; // it's possible, because npp may be negative
        			} else {
        				del_v2v.nmobil[i] = 0.;
        				del_soi2v.snuptake[i] = 0.;
        			}

		  		}

      	    }

		} // end of N requirement < N supply

		//N uptake for labile N, if possible, after N budget estimation

		nsupply = tempnuptake+templabn;   // note: 'tempnuptake' may be changed
		double snuptakeall = 0.;
		double nmobilall = 0.;
		double nfall = 0.;
		for (int i=0; i<NUM_PFT_PART; i++) {
			snuptakeall+=del_soi2v.snuptake[i];
			nmobilall  +=del_v2v.nmobil[i];
			nfall      +=del_v2soi.ltrfaln[i];
		}

		double nsurplus = nsupply - snuptakeall - nmobilall;    //vegetation N labile N after changes ('nmobile' already includes contribution from 'resorbed N')
		double nstruc = tmp_vegs.strnall + snuptakeall + nmobilall - nfall - nresorball;  //vegetation structural N after changes

		if (nsurplus <= bgcpar.labncon*nstruc) { //
			del_soi2v.lnuptake = tempnuptake - snuptakeall;

		} else {  //reducing actual 'lnuptake'
        	del_soi2v.lnuptake = bgcpar.labncon * nstruc
        			            -(tmp_vegs.labn + nresorball - nmobilall);
		}
		if (del_soi2v.lnuptake < 1.0e-10 ) del_soi2v.lnuptake = 0.0;

        // end of nfeed
	} else {

		for (int i=0; i<NUM_PFT_PART; i++){
			del_a2v.gpp[i] = del_a2v.ingpp[i];
			del_a2v.npp[i] = del_a2v.innpp[i];
		}

	}
};

// summarize C and N state variable changes
void Vegetation_Bgc::deltastate(){

	for (int i=0; i<NUM_PFT_PART; i++) {
  		del_vegs.c[i] = del_a2v.npp[i] - del_v2soi.ltrfalc[i];
  	}

  	if(nfeed){
  		del_vegs.labn = del_soi2v.lnuptake;
  		for (int i=0; i<NUM_PFT_PART; i++) {
  			del_vegs.strn[i] = del_soi2v.snuptake[i] + del_v2v.nmobil[i] - del_v2soi.ltrfaln[i] - del_v2v.nresorb[i];
  	  		del_vegs.labn += del_v2v.nresorb[i] - del_v2v.nmobil[i];  // 'nresorb' - N transfer from dead/falling tissues to 'labn', 'nmobil' in reverse direction
  		}

  	}
};

// summarize some variables not done in 'integrator'
void Vegetation_Bgc::afterIntegration(){
	// states
	bd->m_vegs.call    = 0.;
	bd->m_vegs.strnall = 0.;
	for (int i=0; i<NUM_PFT_PART; i++) {
		bd->m_vegs.call += bd->m_vegs.c[i];
		if(nfeed) {
			bd->m_vegs.strnall += bd->m_vegs.strn[i];
		}
	}
	if (nfeed) bd->m_vegs.nall = bd->m_vegs.strnall + bd->m_vegs.labn;

	// fluxes
	bd->m_a2v.ingppall = 0.;
	bd->m_a2v.innppall = 0.;
	bd->m_a2v.gppall = 0.;
	bd->m_a2v.nppall = 0.;
	bd->m_v2a.rgall  = 0.;
	bd->m_v2a.rmall  = 0.;

	bd->m_v2soi.ltrfalcall = 0.;
	bd->m_v2soi.ltrfalnall = 0.;
	bd->m_v2soi.mossdeathc = 0.;
	bd->m_v2soi.mossdeathn = 0.;

	bd->m_v2v.nmobilall  = 0.;
	bd->m_v2v.nresorball = 0.;

 	bd->m_soi2v.snuptakeall= 0.;

	for (int i=0; i<NUM_PFT_PART; i++) {
		bd->m_a2v.ingppall += bd->m_a2v.ingpp[i];
		bd->m_a2v.innppall += bd->m_a2v.innpp[i];
		bd->m_a2v.gppall   += bd->m_a2v.gpp[i];
		bd->m_a2v.nppall   += bd->m_a2v.npp[i];
		bd->m_v2a.rgall    += bd->m_v2a.rg[i];
		bd->m_v2a.rmall    += bd->m_v2a.rm[i];

		if (cd->m_veg.nonvascular[ipft]==0) {
			bd->m_v2soi.ltrfalcall += bd->m_v2soi.ltrfalc[i];
		} else if (cd->m_veg.nonvascular[ipft]>0) {
			bd->m_v2soi.mossdeathc += bd->m_v2soi.ltrfalc[i];
		}

		if(nfeed) {
			if (cd->m_veg.nonvascular[ipft]==0) {
				bd->m_v2soi.ltrfalnall += bd->m_v2soi.ltrfaln[i];
			} else if (cd->m_veg.nonvascular[ipft]>0) {
				bd->m_v2soi.mossdeathn += bd->m_v2soi.ltrfaln[i];
			}

			bd->m_v2v.nmobilall  += bd->m_v2v.nmobil[i];
			bd->m_v2v.nresorball += bd->m_v2v.nresorb[i];

		 	bd->m_soi2v.snuptakeall+= bd->m_soi2v.snuptake[i];
		}
	}

	if (nfeed) {
   		// total actual N uptake for root N extraction from different soil layers
		double tempnuptake = bd->m_soi2v.snuptakeall+bd->m_soi2v.lnuptake;
	 	for (int il=0; il<cd->m_soil.numsl; il++) {
	 		if (sumfrtfrac>0.) {
	 			bd->m_soi2v.nextract[il] = tempnuptake * cd->m_soil.frootfrac[il][ipft]/sumfrtfrac;   //
	 		} else {
	 			bd->m_soi2v.nextract[il] = 0.;
	 		}
	 	}

	}

};

/////////////////////////////////////////
/// private functions
/////////////////////////////////////////

//Note that - fpc not adjusted here
double  Vegetation_Bgc::getGPP(const double &co2, const double & par,
                   const double &leaf, const double & foliage,
                   const double &ftemp, const double & gv) {

  	double ci  = co2 * gv;
  	double thawpcnt = ed->m_soid.growpct;
  	double fpar = par/(bgcpar.ki +par);   // par : photosynthetically active radiation in J/(m2s)
  	double gpp = calpar.cmax * foliage * ci / (bgcpar.kc + ci);
  	gpp *= leaf * fpar;
  	gpp *= ftemp;
  	gpp *= thawpcnt;
  	if(gpp<0)gpp=0.;
  	return gpp;

};

double  Vegetation_Bgc::getRm(const double & vegc, const double & raq10,
   const double &kr) {

  	double respq10 = raq10;
  	double rm = 0.;

  	rm = kr * vegc;
  	rm *= respq10;

  	if (rm< 0.0 ) rm= 0.0;
	if (rm>0.10*vegc) rm= 0.10*vegc;  //maintenance resp. cannot be over 10% of veg.C

 	return rm;

};

double Vegetation_Bgc::getGV(const double & eet,const double & pet ){
  	double gv;
  
    if ( eet/pet <= 0.1 ){
      	gv = (-10.0 * pow( (eet/pet),2.0 ))
           	+ (2.9 * (eet/pet));
      	if ( gv < 0.0 ) { gv = 0.0; }
    } else {
      	gv = 0.1 + (0.9 * eet / pet);
    }
  
  	if(gv>1) gv =1;

  	return gv;	
};

double Vegetation_Bgc::getTempFactor4GPP(const double & tair, const double &tgppopt){
  	double ftemp = 0.;
  	double tmin = bgcpar.tmin;
  	double toptmax = bgcpar.toptmax;
  	double tmax = bgcpar.tmax;

  	if ( tair <=tmin|| tair >=tmax ){
    	ftemp = 0.0;
  	} else {
    	if ( tair >= tgppopt && tair <= toptmax ){
      		ftemp = 1.0;
    	} else {
      		if (tair >tmin && tair< tgppopt ) {
				ftemp = (tair-tmin)*(tair-tmax)
               			/((tair-tmin)*(tair-tmax)
               				- pow( (tair-tgppopt),2.0 ));
      		} else {
				ftemp = (tair-tmin)*(tair-tmax)
               			/((tair-tmin)*(tair-tmax)
              				- pow( (tair-toptmax),2.0 ));
      		}
    	}
  	}

  	return ftemp;
};

// the maintainence respiration constant
double Vegetation_Bgc::getKr(const double & vegc, const int & ipart){
	// kr is for calculating maintainance respiration
  	double kr;
  	double kra = calpar.kra;
  	double krb = calpar.krb[ipart];
  
  	kr = exp((kra*vegc)+krb);	
  	return kr;
};

// the maintainence respiration temperature factor
double Vegetation_Bgc::getRaq10(const double & tair){
  	double raq10;
  	double raq10a0 = bgcpar.raq10a0;
  	double raq10a1 = bgcpar.raq10a1;
  	double raq10a2 = bgcpar.raq10a2;
  	double raq10a3 = bgcpar.raq10a3;
  
  	double q10 = raq10a0+ (raq10a1*tair) + (raq10a2*pow( tair,2.0 ))
                + (raq10a3*pow(tair,3.0 ));
  	raq10 = pow( q10,tair/10.0 );
 	// if(isnan(raq10)){
  	//	cout <<"raq10 is nan\n";
 	// 	exit(-1);
 	// }
  	return raq10;
};

// Plant new production C/N ratios (bgcpar.cneven) adjusting at end of a year
void Vegetation_Bgc::adapt(){

      // Determine vegetation C/N parameter as a function 
      // of vegetation type, annual PET, annual EET, 
      // CO2 concentration

      updateCNeven(ed->y_l2a.eet, ed->y_l2a.pet, cd->rd->initco2, ed->y_atms.co2);
}

//
void Vegetation_Bgc::updateCNeven(const double & yreet,const double & yrpet,
  const double & initco2,const double & currentco2 ) {
  
	for (int i=0; i<NUM_PFT_PART; i++) {
		if (yrpet > 0.0){
			bgcpar.c2neven[i] = bgcpar.c2nb[i] + bgcpar.c2na*(yreet/yrpet);
		} else {
			bgcpar.c2neven[i] = bgcpar.c2nb[i];
		}

		if (bgcpar.c2neven[i] < bgcpar.c2nmin[i]) {
			bgcpar.c2neven[i] = bgcpar.c2nmin[i];
		}
  
		double adjc2n = 1.0 + (bgcpar.dc2n * (currentco2 - initco2));
		bgcpar.c2neven[i] *= adjc2n;
	}

};

double Vegetation_Bgc::getNuptake(const double & foliage, const double & raq10,
                            const double & kn1, const double &nmax){

	double nuptake = 0.;

	//root zone's water/N contents for N uptake
	double meanrzksoil = 0.;  // root zone liq water volume adjusted by root fraction for N uptake
	double totrzliq = 0.;     // root zone liq water content for N uptake
	double totrzavln= 0.;    // root zone avaliable N concent for N uptake
    for(int il =0; il<cd->m_soil.numsl; il++){
		if (cd->m_soil.frootfrac[il][ipft]> 0.) {
			meanrzksoil += bd->m_soid.knmoist[il]*cd->m_soil.frootfrac[il][ipft];  //NOTE: 'bd->m_soid.knmoist' is updated in Soil_bgc.cpp

			totrzliq += ed->m_sois.liq[il];
			totrzavln+= bd->m_sois.avln[il];
		}
    }

 	if(totrzliq>0. && totrzavln>0. && meanrzksoil>0.){
  			nuptake  = (totrzavln * meanrzksoil)/ totrzliq;
  			nuptake /= (kn1 +nuptake);

  			nuptake *=(nmax * foliage);
  			nuptake *= raq10;
 	}else{
  			nuptake=0.;
 	}


  	return nuptake;
};

void Vegetation_Bgc::setCohortLookup(CohortLookup* chtlup){
  	 chtlu = chtlup;
};

void Vegetation_Bgc::setCohortData(CohortData* cdp){
  	 cd = cdp;
};

void Vegetation_Bgc::setEnvData(EnvData* edp){
  	 ed = edp;
};

void Vegetation_Bgc::setBgcData(BgcData* bdp){
  	 bd =bdp;
}