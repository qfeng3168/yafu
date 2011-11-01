/*----------------------------------------------------------------------
This source distribution is placed in the public domain by its author,
Ben Buhrow. You may use it for any purpose, free of charge,
without having to notify anyone. I disclaim any responsibility for any
errors.

Optionally, please be nice and tell me if you find this source to be
useful. Again optionally, if you add to the functionality present here
please consider making those additions public too, so that others may 
benefit from your work.	

       				   --bbuhrow@gmail.com 7/1/10
----------------------------------------------------------------------*/

#include "soe.h"

void getRoots(soe_staticdata_t *sdata)
{
	int prime, prodN;
	uint64 startprime;
	uint64 i;
		
	//timing
	double t;
	struct timeval tstart, tstop;
	TIME_DIFF *	difference;

	prodN = (int)sdata->prodN;
	startprime = sdata->startprime;

	gettimeofday(&tstart, NULL);
	
	for (i=startprime;i<sdata->bucket_start_id;i++)
	{
		uint32 inv;
		prime = sdata->sieve_p[i];

		//sieving requires that we find the offset of each sieve prime in each block 
		//that we sieve.  We are more restricted in choice of offset because we
		//sieve residue classes.  A good way to find the offset is the extended 
		//euclidean algorithm, which reads ax + by = gcd(a,b),
		//where a = prime, b = prodN, and therefore gcd = 1.  
		//since a and b are coprime, y is the multiplicative inverse of prodN modulo prime.
		//This value is a constant, so compute it here in order to facilitate 
		//finding offsets later.

		//solve prodN ^ -1 % p 
		inv = modinv_1(prodN,prime);
		sdata->root[i] = prime - inv;
	}

	gettimeofday(&tstop, NULL);

	difference = my_difftime(&tstart, &tstop);
	t = ((double)difference->secs + (double)difference->usecs / 1000000);
	free(difference);

	if (VFLAG > 2)
		printf("time to compute linear sieve roots = %1.2f\n", t);

	gettimeofday(&tstart, NULL);

	// bucket sieved primes need more data
	if (sdata->sieve_range == 0)
	{
		for (; i < sdata->inplace_start_id;i++)
		{
			uint32 inv;
			prime = sdata->sieve_p[i];

			//sieving requires that we find the offset of each sieve prime in each block 
			//that we sieve.  We are more restricted in choice of offset because we
			//sieve residue classes.  A good way to find the offset is the extended 
			//euclidean algorithm, which reads ax + by = gcd(a,b),
			//where a = prime, b = prodN, and therefore gcd = 1.  
			//since a and b are coprime, y is the multiplicative inverse of prodN modulo prime.
			//This value is a constant, so compute it here in order to facilitate 
			//finding offsets later.

			//solve prodN ^ -1 % p 
			inv = modinv_1(prodN,prime);
			sdata->root[i] = prime - inv;

			//we can also speed things up by computing and storing the residue
			//mod p of the first sieve location in the first residue class.  This provides
			//a speedup by pulling this constant (involving a division) out of a critical loop
			//when finding offsets of bucket sieved primes.
			//these are only used by bucket sieved primes.
			sdata->lower_mod_prime[i-sdata->bucket_start_id] = (sdata->lowlimit + 1) % prime;
		}
	}
	else
	{
		mpz_t tmpz;
		mpz_init(tmpz);

		mpz_add_ui(tmpz, sdata->offset, sdata->lowlimit + 1);
		for (; i < sdata->inplace_start_id;i++)
		{
			uint32 inv;
			prime = sdata->sieve_p[i];

			//sieving requires that we find the offset of each sieve prime in each block 
			//that we sieve.  We are more restricted in choice of offset because we
			//sieve residue classes.  A good way to find the offset is the extended 
			//euclidean algorithm, which reads ax + by = gcd(a,b),
			//where a = prime, b = prodN, and therefore gcd = 1.  
			//since a and b are coprime, y is the multiplicative inverse of prodN modulo prime.
			//This value is a constant, so compute it here in order to facilitate 
			//finding offsets later.

			//solve prodN ^ -1 % p 
			inv = modinv_1(prodN,prime);
			sdata->root[i] = prime - inv;

			//we can also speed things up by computing and storing the residue
			//mod p of the first sieve location in the first residue class.  This provides
			//a speedup by pulling this constant (involving a division) out of a critical loop
			//when finding offsets of bucket sieved primes.
			//these are only used by bucket sieved primes.			
			sdata->lower_mod_prime[i-sdata->bucket_start_id] = mpz_tdiv_ui(tmpz, prime);
		}
	}

	gettimeofday(&tstop, NULL);

	difference = my_difftime(&tstart, &tstop);
	t = ((double)difference->secs + (double)difference->usecs / 1000000);
	free(difference);

	if (VFLAG > 2)
		printf("time to compute bucket sieve roots = %1.2f\n", t);

#ifdef INPLACE_BUCKET
	gettimeofday(&tstart, NULL);

	// inplace primes have special requirements because they operate on
	// the normal number line, and not in residue space
	for (; i < sdata->pboundi;i++)
	{
		uint64 starthit;
		uint32 startclass;
		uint64 startbit;
		uint32 rclass, bnum, rclassid;
		uint32 index = i - sdata->inplace_start_id;
		int a;

		// copy the prime into the special data structure
		//sdata->inplace_data[index].prime = sdata->sieve_p[i];

		// pull some computations involving a division out of the inner loop.
		// we need to know what prime/prodN and prime%prodN are.
		sdata->inplace_data[index].p_div = 
			sdata->sieve_p[i] / prodN;
		rclass = sdata->sieve_p[i] % prodN;
		rclassid = resID_mod30[rclass];
		sdata->inplace_data[index].p_mod = rclass;

		// now compute the starting hit in our sieve interval...
		starthit = (sdata->lowlimit / sdata->sieve_p[i] + 1) * sdata->sieve_p[i];

		// ... that is in one of our residue classes
		startclass = starthit % prodN;

		// using a lookup table
		startclass = next_mod30[rclassid][startclass];

		starthit += ((uint64)sdata->sieve_p[i] * (uint64)(startclass >> 8));
		startclass = startclass & 0xff;

		// the starting accumulated error is equal to the starting class
		sdata->inplace_data[index].eacc = startclass;

		// now compute the starting bit and block location for this starting hit
		startbit = (starthit - sdata->lowlimit - (uint64)startclass) / (uint64)prodN;

		// sanity check
		if (((starthit - sdata->lowlimit - (uint64)startclass) % (uint64)prodN) != 0)
			printf("starting bit is invalid!\n");

		sdata->inplace_data[index].bitloc = startbit & FLAGSIZEm1;
		bnum = startbit >> FLAGBITS;

		// finally, add the prime to a linked list
		// if the next hit is within our interval
		if (bnum < sdata->blocks)
		{
			//then reassign this prime to its next hit
			if (sdata->inplace_ptrs[bnum][resID_mod30[startclass]] == -1)
			{
				// this is the first hit in this block and rclass, so set the pointer
				// to this prime, and set next_pid = 0 so that we know to stop here
				// when we sieve
				sdata->inplace_ptrs[bnum][resID_mod30[startclass]] = index;
				sdata->inplace_data[index].next_pid = 0;
			}
			else
			{
				// add this prime to a listed list within the inplace sieve array.
				// this is done by first setting the next id to the current prime
				// at the end of the list
				sdata->inplace_data[index].next_pid = sdata->inplace_ptrs[bnum][resID_mod30[startclass]];

				// and then setting the end of the list to this prime
				sdata->inplace_ptrs[bnum][resID_mod30[startclass]] = index;						
			}
		}					

	}

	gettimeofday(&tstop, NULL);

	difference = my_difftime(&tstart, &tstop);
	t = ((double)difference->secs + (double)difference->usecs / 1000000);
	free(difference);

	if (VFLAG > 2)
		printf("time to compute inplace sieve roots = %1.2f\n", t);

#endif

	return;
}