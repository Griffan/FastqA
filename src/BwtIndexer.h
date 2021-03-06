/*The MIT License (MIT)
Copyright (c) 2017 Fan Zhang, Hyun Min Kang
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */
/* Contact: Fan Zhang <fanzhang@umich.edu> */


#ifndef BWTINDEXER_H_
#define BWTINDEXER_H_
#include "../libbwa/bntseq.h"
#include "../libbwa/bwt.h"
#include <cstdio>
#include <cstdlib>
#include "RefBuilder.h"
#include <sys/stat.h>
#include <assert.h>
#include <cmath>
#include <time.h>
#include <iostream>
#include <map>
//Bloom filter parameters
#define VCF_SITES 10000
#define WINDOW_SIZE 1000
#define KMER_SIZE  32
//#define BLOOM_FPP 0.1
#define PROCESS_RATIO 0.5
#define READ_STEP_SIZE 1
//static bool debug_flag=false;
struct _RollParam
{
	int kmer_size;
	int read_step_size;
	int thresh;
};
//extern struct _RollParam RollParam;


class BwtIndexer
{
public:

	std::vector<std::pair<std::string,int> > contigSize;
    std::string RefPath;
	long ref_genome_size;
	long ref_N_size;
	ubyte_t * pac_buf;
	ubyte_t * rpac_buf;
	unsigned char* bwt_buf;
	unsigned char* rbwt_buf;
	bntseq_t *bns;
    uint64_t l_buf;
    uint64_t m_pac_buf, m_rpac_buf, m_bwt_buf, m_rbwt_buf;

	bwt_t *bwt_d;
	bwt_t *rbwt_d;

	BwtIndexer();

	BwtIndexer(int thresh);

	BwtIndexer(std::string & NewRef);

	BwtIndexer(RefBuilder & ArtiRef, std::string & NewRef);

	int LoadContigSize();

	bool LoadIndex(std::string & NewRef);

	bool BuildIndex(RefBuilder & ArtiRef, std::string & OldRef, std::string & NewRef,
			const gap_opt_t * opt);

	bool Fa2Pac(RefBuilder & ArtiRef, const char *prefix, const gap_opt_t* op);

	bool Fa2Pac(const char *prefix);
/**************begin of function for whole genome index******/
    bool LoadIndexFromWholeGenome(std::string & NewRef);

    bool BuildIndexFromWholeGenome(RefBuilder & ArtiRef, std::string & OldRef, std::string & NewRef,
                                               const gap_opt_t * opt);

    bool Fa2PacFromWholeGenome();//write .pac file free(pac_buf)

    bool FillHashTable(const char *prefix);
/**************end of function for whole genome index******/
	bool Fa2RevPac(const char * prefix);

	bwt_t* Pac2Bwt(unsigned char * pac);

	void bwt_bwtupdate_core(bwt_t *bwt);

	void bwt_cal_sa(bwt_t *bwt, int intv);

	bool IsReadHighQ(const ubyte_t *Q, int len)const;

	bool IsKmerInHash(uint64_t kmer)const;

	int CountKmerHitInHash(uint64_t kmer)const;

	bool IsReadInHash(ubyte_t * S, int len)const;

	bool IsReadInHash(const ubyte_t * S, int len, bool n_chunck)const;

	bool IsReadInHashByCount(ubyte_t * S, int len)const;

	//typedef uint32_t v4si __attribute__ ((vector_size (16)));
	bool IsReadFiltered(ubyte_t * S, const ubyte_t * Q, int len)const;

	bool IsReadInHashByCountMoreChunck(const ubyte_t *S, int len)const;

#ifdef BLOOM_FPP
#include "bloom_filter.hpp"
	bloom_parameters parameters;
	bloom_filter filter;
	ubyte_t BloomReadBuff[KMER_SIZE+1];
	inline void AddSeq2BloomFliter(const std::string & Seq)
	{
		//std::string tmp;
		for(int i=0;i!=Seq.length()-KMER_SIZE/*last one considered*/;++i)
		{
			//tmp=Seq.substr(i,KMER_SIZE);
			//for(int j=0;j!=KMER_SIZE;++j)
			//tmp[i]=nst_nt4_table[(int)tmp[i]];
			//printf("/*******now insert: %s**************/\n",Seq.substr(i,KMER_SIZE).c_str());
			filter.insert(Seq.substr(i,KMER_SIZE));

		}
	}

	inline bool IsReadInHash( const char * S, int len)
	{
		//std::string Seq(S);
		int processed(0);
		//int total= len-KMER_SIZE+1;

		for(int i=0;i<len-KMER_SIZE/*last one considered*/;i+=READ_STEP_SIZE,++processed)
		{
			// if(filter.contains(Seq.substr(i,KMER_SIZE)))
			//assert(BloomReadBuff!=0);
			//printf("/*********************/\n");
			//for(int j=0;j!=KMER_SIZE;++j)
			//printf("number %d : %d\n",i,S[i]);
			//printf("/*********************/\n");
			//memcpy(BloomReadBuff,&S[i],KMER_SIZE*sizeof(char));
			//BloomReadBuff[KMER_SIZE]='\0';
			//printf("/*******now test: %s**************/\n",std::string(BloomReadBuff).c_str());
			if(filter.contains(std::string(S).substr(i,KMER_SIZE)))
			{
				return true;
			}
			//else if (total*READ_STEP_SIZE>PROCESS_RATIO*total)
			//return false;
		}
//		if(double(hit)/total > HIT_RATIO)// is in hash
//		{

//			return true;
//		}
//		else
		{
			//printf("the total is: %d\n the hit is : %d\n the ratio is :%f\n",total,hit, double(hit)/total);
			return false;
		}
	}

#else
	_RollParam RollParam;
	//Fast Rolling Hash parameters
#define OVERFLOWED_KMER_SIZE 32
#define LSIZE(k)     (1L << (2*(k)))    /* language size, 4096, 16384          */
#define LOMEGA(k)    (LSIZE(k)-1)      /* last (all-one) word, 4095, 16383,   */
#define MERMASK(k)   ((1 << (k))-1)    /* 6->63 and 7->127 (K-bits-all-one)   */

#define min_only(X,Y) ((X) < (Y) ? (X) : (Y))

	unsigned char * roll_hash_table[6]; //11110000

	/*debug hash*/

	std::map<int,int> debug_hash;
	int AddSeq2DebugHash(int shrinked);
	inline int DebugHashPrint()
	{
		std::cerr<<"Enter DebugHashPrint, debug hash size:"<<debug_hash.size()<<"\n"<<std::endl;
		for(std::map<int,int>::iterator iter=debug_hash.begin();iter!=debug_hash.end();iter++)
			std::cout<<iter->first<<"\t"<<iter->second<<std::endl;
		return 0;
	}
	/*debug hash end*/

	long long int hash_table_size;

	void InitializeRollHashTable(int thresh);

	void ReadRollHashTable(const std::string& prefix);

	void DumpRollHashTable(const std::string& prefix);

	void DestroyRollHashTable();

	std::string ReverseComplement(const std::string & a);

	uint32_t KmerShrinkage(uint64_t a, unsigned int mask)const;

	void AddSeq2HashCore(const std::string & Seq, int iter, const std::vector<char>& alleles);

	void AddSeq2Hash(const std::string & Seq, const std::vector<char>& alleles);

#endif
	virtual ~BwtIndexer();

	void ConvertSeq2Pac(int32_t &m_seqs, int32_t &m_holes, bntamb1_t *q, uint64_t& size_pac_buf,
                        const std::string &CurrentSeqName, const std::string &CurrentSeqNameAnno,
                        const std::string &CurrentSeq);
};
static double QualQuickTable[46] =
{ 1, 0.7943, 0.6310, 0.5012, 0.3981, 0.3162, 0.2512, 0.1995, 0.1585, 0.1259,
		0.1000, 0.0794, 0.0631, 0.0501, 0.0398, 0.0316, 0.0251, 0.0200, 0.0158,
		0.0126, 0.0100, 0.0079, 0.0063, 0.0050, 0.0040, 0.0032, 0.0025, 0.0020,
		0.0016, 0.0013, 0.0010, 0.0008, 0.0006, 0.0005, 0.0004, 0.0003, 0.0003,
		0.0002, 0.0002, 0.0001, 0.0001, 0.0001, 0.0001, 0.0001, 0.0001, 0.0001 };
static std::unordered_map<char, char> match_table =
	{
	{ 'a', 'T' },
	{ 'A', 'T' },
	{ 'c', 'G' },
	{ 'C', 'G' },
	{ 'G', 'C' },
	{ 'g', 'C' },
	{ 't', 'A' },
	{ 'T', 'A' } };

inline bool BwtIndexer::IsReadHighQ(const ubyte_t *Q, int len)const//check if first 25 bases are of high quality
{
	double tmp(1);
	for (int i = 0; i != 25; ++i)
	{
		//std::cerr << int(Q[i]) << "\t";
		tmp *= (1 - QualQuickTable[Q[i]]);
	}
	//std::cerr << std::endl;
	if (1 - tmp < 0.1)
		return true;
	else
		return false;
}

inline uint32_t BwtIndexer::KmerShrinkage(uint64_t kmer, unsigned int iter)const
{
	//uint32_t tmp(0), i(1);
	////printf("the incoming kmer:%016llx\t",a);
	//while (mask) //32 circle
	//{
	//	if (mask & 1)
	//	{
	//		tmp |= ((a & 3) << (2 * i - 2));
	//		++i;
	//	}

	//	mask >>= 1;
	//	a >>= 2;
	//}
	////printf("the resulted i is :%d\n",i);
	//return tmp;

	uint32_t shrinked(0);
	switch (iter)
	{
	case 0:
	/*0xffffffff00000000*/
	shrinked |= (kmer & 0xffffffff00000000) >> 32;
	return shrinked;
	case 1:
	/*0xffffffff*/
	shrinked |= kmer & 0xffffffff;
	return shrinked;
	case 2:
	/*0xffff00000000ffff*/
	shrinked |= (kmer & 0xffff000000000000) >> 32;
	shrinked |= (kmer & 0xffff);
	return shrinked;
	case 3:
	/*0xffffffff0000*/
	shrinked |= (kmer & 0xffffffff0000) >> 16;
	return shrinked;
	case 4:
	/*0xffff0000ffff0000*/
	shrinked |= (kmer & 0xffff000000000000) >> 32;
	shrinked |= (kmer & 0xffff0000) >> 16;
	return shrinked;
	case 5:
	/*0xffff0000ffff*/
	shrinked |= (kmer & 0xffff00000000) >> 16;
	shrinked |= (kmer & 0xffff);
	return shrinked;
	default:
		fprintf(stderr,"Fatal error: unknown mask.\n");
		exit(EXIT_FAILURE);
	}

}

inline void BwtIndexer::AddSeq2Hash(const std::string & Seq, const std::vector<char>& alleles)
{
	for (int i = 0; i != 6; ++i)
	{
		AddSeq2HashCore(Seq, i, alleles);
	}
}


#endif /* BWTINDEXER_H_ */
