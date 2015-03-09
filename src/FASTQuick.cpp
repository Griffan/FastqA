//============================================================================
// Name        : FastqA.cpp
// Author      : FanZhang
// Version     :
// Copyright   : Copyright Reserved.
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include "../libbwa/utils.h"
#include "BwtIndexer.h"
#include "BwtMapper.h"
#include "PopulationIdentifier.cpp"
#include "../misc/params.h"
#include "../libmpu/mpuTool.h"
#include "../libmpu/Error.h"
//#include <gperftools/profiler.h>
using namespace std;

//#define USE_BWT 1
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "0.0.1"
#endif
#ifndef MPU_PATH
#define MPU_PATH "mpuTools"
#endif
extern void notice(const char*,...);
extern void warning(const char*,...);
extern void error(const char*,...);

int runOneStop(int argc, char ** argv)
{
	/*
	double t_real;
	t_real = realtime();
	int  opte = -1;
	gap_opt_t *opt;
	pe_opt_t *popt;
	popt = bwa_init_pe_opt();
	opt = gap_init_opt();

	std::string RefPath("Empty"), VcfPath("Empty"), MaskPath("Empty"), Fastq_1("Empty"), Fastq_2(
			"Empty"),  BamIn("Empty"),ReadGroup("default"),DepthDist, SitePileup,FaList("Empty"),DBsnpPath("Empty");

	bool loggap(0), nonstop(0), IL13(0),BamOut(0);
	paramList pl;

	BEGIN_LONG_PARAMS(longParameters) LONG_PARAM_GROUP("Input/Output Files","Input/Output files for the program[Complete Path Recommended]")
	LONG_STRING_PARAM("vcf",&VcfPath,"[String] Input Hapmap or Selected Sites VCF file[Required]")
	LONG_STRING_PARAM("dbsnp",&DBsnpPath,"[String] dbSNP VCF file[Required]")
	LONG_STRING_PARAM("ref",&RefPath,"[String] Reference FASTA file[Required]")
	LONG_STRING_PARAM("mask",&MaskPath,"[String] Repeat Mask FASTA file[Leave empty if using Selected Sites VCF]")
	LONG_STRING_PARAM("fastq_1",&Fastq_1,"[String] Pair end 1 fastq file[Leave empty if using fq_list or bam_in]")
	LONG_STRING_PARAM("fastq_2",&Fastq_2,"[String] Pair end 2 fastq file.[Leave empty if using single end]")
	LONG_STRING_PARAM("fq_list",&FaList,"[String] Path of input fastq files, tab-delimited, one pair-end files per line(one file per line for single end)[Leave empty if using bam_in or fastq_1]")
	LONG_STRING_PARAM("bam_in",&BamIn,"[String] Input bam file path[Leave empty if using fq_list or fastq_1]")
	LONG_PARAM("bam_out",&BamOut,"[Bool] If output bam file[Leave empty if using bam_in]")
	LONG_STRING_PARAM("prefix",&Prefix,"[String] Prefix of all the output files[Required]")


	//LONG_STRING_PARAM("out",&outf,"Output file prefix")
	LONG_PARAM_GROUP("Parameters for Reference Sequence ", "Parameters being used to extract reference sequences.[All Required]")
	// LONG_INT_PARAM("K",&Indexer.RollParam.kmer_size,"kmer size for Rolling Hash filtering")
	// LONG_INT_PARAM("T",&Indexer.RollParam.thresh,"threshold for filtering specific read")
	// LONG_INT_PARAM("S",&Indexer.RollParam.read_step_size,"step size for extracting kmer")
	LONG_INT_PARAM("var_long",&opt->num_variant_long,"[INT] number of variants with long flanking region")
	LONG_INT_PARAM("var_short",&opt->num_variant_short,"[INT] number of variants with short flanking region")
	LONG_INT_PARAM("flank_len",&opt->flank_len,"[INT] flanking region length around each marker")
	LONG_INT_PARAM("flank_long_len",&opt->flank_long_len,"[INT] long flanking region length around each marker")

	LONG_PARAM_GROUP("Parameters for Alignment ", "Parameters the are universal for both single end and pair end alignment.[Optional]")
	LONG_DOUBLE_PARAM("n",&opt->fnr,"[INT or Float] Max #diff (int) or missing prob under 0.02 error rate")
	LONG_INT_PARAM("o",&opt->max_gapo,"[INT] maximum number or fraction of gap opens")
	LONG_INT_PARAM("e",&opte,"[INT] maximum number of gap extensions, -1 for disabling long gaps [-1]")
	LONG_INT_PARAM("i",&opt->indel_end_skip,"[INT] do not put an indel within INT bp towards the ends")
	LONG_INT_PARAM("d",&opt->max_del_occ,"[INT] maximum occurrences for extending a long deletion")
	LONG_INT_PARAM("l",&opt->seed_len,"[INT] seed length")
	LONG_INT_PARAM("k",&opt->max_seed_diff,"[INT] maximal seed difference")
	LONG_INT_PARAM("m",&opt->max_entries,"[INT] maximal stack entries")
	LONG_INT_PARAM("t",&opt->n_threads,"[INT] number of threads")
	// LONG_INT_PARAM("L",&opt->seed_len,"seed length")
	LONG_INT_PARAM("R",&opt->max_top2,"[INT] stop searching when there are >INT equally best hits")
	LONG_INT_PARAM("q",&opt->trim_qual,"[INT] quality threshold for read trimming down to 35dbp")
	LONG_STRING_PARAM("RG",&ReadGroup,"[String] set ReadGroup name")
	//Roll Hash

	// LONG_PARAM("c",&compread,"seed length")
	LONG_PARAM("N",&nonstop,"[Bool] non-iterative mode: search for all n-difference hits (slooow)")
	LONG_PARAM("I",&IL13,"[Bool] the input is in the Illumina 1.3+ FASTQ-like format")
	LONG_PARAM("L",&loggap,"[Bool] log-scaled gap penalty for long deletions")

	LONG_PARAM_GROUP("Parameters for PairEnd ", "Parameters specified for Pair end mapping.[Optional]")
	LONG_INT_PARAM("max_isize",&popt->max_isize,"[INT] maximum insert size")
	LONG_INT_PARAM("max_occ",&popt->max_occ,"[INT] maximum occurrences for one end ")
	LONG_PARAM("is_sw",&popt->is_sw,"[Bool] disable Smith-Waterman for the unmapped mate")
	LONG_INT_PARAM("n_multi",&popt->n_multi,"[INT] maximum hits to output for paired reads")
	LONG_INT_PARAM("N_multi",&popt->N_multi,"[INT] maximum hits to output for discordant pairs")
	LONG_DOUBLE_PARAM("ap_prior",&popt->ap_prior,"[Double] prior of chimeric rate (lower bound) ")
	LONG_PARAM("force_isize",&popt->force_isize," [Bool] disable insert size estimate")

	LONG_PARAM_GROUP("Parameters for Statistics ", "Parameters specified for statistics and summary.[Optional]")
	LONG_PARAM("cal_dup",&opt->cal_dup,"[Bool] enable the calculation of duplicated reads in depth calculation ")
	LONG_PARAM("frac_samp",&opt->frac,"[Double] specify the downsampling fraction ")
	//LONG_STRING_PARAM("depth_dist",&DepthDist,"Output file for depth distribution ")
	//LONG_STRING_PARAM("site_pileup",&SitePileup,"Output file for Pileup information on specified sites ")

	END_LONG_PARAMS();

	pl.Add(new longParams("Available Options", longParameters));
	pl.Read(argc, argv);
	pl.Status();
	if(RefPath=="Empty")
	{
		error("--ref is required");
		exit(EXIT_FAILURE);
	}
	if(VcfPath=="Empty")
	{
		error("--vcf is required");
		exit(EXIT_FAILURE);
	}
	if(DBsnpPath=="Empty")
	{
		error("--dbsnp is required");
		exit(EXIT_FAILURE);
	}
	if(BamOut)
	{
	opt->out_bam=1;
	}
	if(BamIn!="Empty")
	{
	opt->in_bam=strdup(BamIn.c_str());
	}
	opt->RG=strdup(ReadGroup.c_str());
	if (opte > 0)
	{
		opt->max_gape = opte;
		opt->mode &= ~BWA_MODE_GAPE;
	}
	if (nonstop)
	{
		opt->mode |= BWA_MODE_NONSTOP;
		opt->max_top2 = 0x7fffffff;
	}
	if (IL13)
	{
		notice( "Using Illumina 1.3 version quality system..." );
		opt->mode |= BWA_MODE_IL13;
	}
	if (loggap)
	{
		opt->mode |= BWA_MODE_LOGGAP;
	}
	//Extract ref seqs from VCF and Ref

	struct stat sb;
//
//	if (USE_BWT) // using bwt methods
//	{
		//build ref index
		BwtIndexer Indexer;
		std::string BwtPath = RefPath + ".bwt";
		//string BwtPathR = RefPath + ".rbwt";
		if (stat(BwtPath.c_str(), &sb) != 0) //|| stat(BwtPathR.c_str(), &sb)!=0)
		{
			notice("Index file doesn't exist, building...\n");
			RefBuilder ArtiRef(VcfPath, RefPath, DBsnpPath, MaskPath, Prefix,opt);
					//Indexer.longRefTable);
			Indexer.BuildIndex(ArtiRef, RefPath, opt);
			if(FaList!="Empty")
			{
				BwtMapper Mapper(Indexer, FaList, VcfPath, popt, opt);
			}
			else
			{
				BwtMapper Mapper(Indexer, Fastq_1, Fastq_2, VcfPath, popt, opt);
			}
		}
		else //load ref index
		{
			notice("Index file exists, loading...\n");
			Indexer.LoadIndex(RefPath);
			if(FaList!="Empty")
			{
				BwtMapper Mapper(Indexer, FaList, VcfPath, popt, opt);
			}
			else
			{
				BwtMapper Mapper(Indexer, Fastq_1, Fastq_2, VcfPath, popt, opt);
			}
		}
//
//	}
//	else // using hash method
//	{
//		//build ref index
//		//main loop for mapping
//
//		//output alignment file
//	}

	//call mputools
		// ../src/mpuTool verify --vcf hapmap_3.3.b37.vcf.shuffled.all.vcf.SelectedSite.vcf.gz --out test --mpu hapmap_3.3.b37.vcf.shuffled.all.vcf.Pileup.gz --smID HAAAAA
		char cmdline[1024];
		char** params=new char* [9];
		sprintf(cmdline,MPU_PATH " verify --vcf %s.SelectedSite.vcf.gz --mpu %s.Pileup.gz --smID %s --out %s.ctm", Prefix.c_str(), Prefix.c_str(), ReadGroup.c_str(), Prefix.c_str());
		stringstream ss(cmdline);
		string para;
		ss>>para;
		for(int i=0;i!=9;++i)
		{
			ss>>para;
			 params[i]= new char[128];
			strcpy(params[i],para.c_str());
		}
		runVerify(9,(char**)(params));
//		if(system(cmdline)!=0)
//		{
//			std::cerr<<"Call command line:\n"<<cmdline<<"\nfailed!"<<std::endl;
//		}
		//tabix -pvcf hapmap_3.3.b37.vcf.shuffled.all.vcf.SelectedSite.vcf
		// tabix -s 1 -b 2 -e 2 hapmap_3.3.b37.vcf.shuffled.all.vcf.Pileup.gz
	notice("Version: %s\n", PACKAGE_VERSION);
	notice("Real time: %.3f sec; CPU: %.3f sec\n",
			realtime() - t_real, cputime());
	gap_free_opt(opt);
	free(popt);*/
	return 0;
}

int runIndex(int argc, char ** argv)
{

	double t_real;
	t_real = realtime();
	int /*c,*/ opte = -1;
	gap_opt_t *opt;
	opt = gap_init_opt();

	/*
	* Parameters
	*
	*/
	std::string RefPath("Empty"), VcfPath("Empty"), MaskPath("Empty"), DBsnpPath("Empty"),Prefix("Empty");
	bool reselect(false);
	//std::string Prefix("Empty");
	paramList pl;

	BEGIN_LONG_PARAMS(longParameters) LONG_PARAM_GROUP("Input/Output Files", "Input/Output files for the program[Complete Path Recommended]")
		LONG_STRING_PARAM("siteVCF", &VcfPath, "[String] Input Selected Sites VCF file,e.g. hapmap vcf[Required]")
		LONG_STRING_PARAM("dbsnpVCF", &DBsnpPath, "[String] dbSNP VCF file[Required]")
		LONG_STRING_PARAM("ref", &RefPath, "[String] Reference FASTA file[Required]")
		LONG_STRING_PARAM("out_idx_prefix", &Prefix, "[String] Prefix of all the output index files[Required]")
		LONG_STRING_PARAM("mask", &MaskPath, "[String] Repeat Mask FASTA file[Leave empty if using Selected Sites VCF]")
		LONG_PARAM_GROUP("Parameters for Reference Sequence ", "Parameters being used to extract reference sequences.[All Required]")
		LONG_INT_PARAM("var_long", &opt->num_variant_long, "[INT] number of variants with long flanking region")
		LONG_INT_PARAM("var_short", &opt->num_variant_short, "[INT] number of variants with short flanking region")
		LONG_INT_PARAM("flank_len", &opt->flank_len, "[INT] flanking region length around each marker")
		LONG_INT_PARAM("flank_long_len", &opt->flank_long_len, "[INT] long flanking region length around each marker")
		//LONG_PARAM("reselect", &reselect, "[Bool] If you want to reselect subset of snp sites for generating reference")
	END_LONG_PARAMS();

	pl.Add(new longParams("Available Options", longParameters));
	pl.Read(argc, argv);
	pl.Status();
	if (Prefix == "Empty")
	{
		error("--out_idx_prefix is required");
		exit(EXIT_FAILURE);
	}
	if (RefPath == "Empty")
	{
		error("--ref is required");
		exit(EXIT_FAILURE);
	}
	if (VcfPath == "Empty")
	{
		error("--siteVCF is required");
		exit(EXIT_FAILURE);
	}
	if (DBsnpPath == "Empty")
	{
		error("--dbsnpVCF is required");
		exit(EXIT_FAILURE);
	}

	struct stat sb;
	//
	//build ref index
	BwtIndexer Indexer;
	std::string NewRef = Prefix + ".FASTQuick.fa";
	std::string BwtPath = NewRef + ".bwt";
	//string BwtPathR = RefPath + ".rbwt";
	if (stat(BwtPath.c_str(), &sb) != 0) //|| stat(BwtPathR.c_str(), &sb)!=0)
	{
		notice("Index file doesn't exist, building...\n");
		RefBuilder ArtiRef(VcfPath, RefPath, NewRef, DBsnpPath, MaskPath, opt, reselect);
		//Indexer.longRefTable);
		Indexer.BuildIndex(ArtiRef, RefPath, NewRef, opt);
	}
	else //load ref index
	{
		notice("Index file exists, exit...\n");
		return 0;
	}

	ofstream ParamOut(NewRef + ".param");
	if (!ParamOut.is_open())
	{
		cerr << "Open file :" << NewRef + ".param" << "failed!" << endl;
	}
	ParamOut << "reference_path:\t" << RefPath << endl;
	ParamOut << "var_long:\t" << opt->num_variant_long << endl;
	ParamOut << "var_short:\t" << opt->num_variant_short << endl;
	ParamOut << "flank_len:\t" << opt->flank_len << endl;
	ParamOut << "flank_long_len:\t" << opt->flank_long_len << endl;
	ParamOut.close();

	notice("Version: %s\n", PACKAGE_VERSION);
	notice("Real time: %.3f sec; CPU: %.3f sec\n",realtime() - t_real, cputime());
	return 0;
}
int runAlign(int argc, char ** argv)
{
	//ProfilerStart("FastPopCon.prof");
	double t_real,t_tmp(0);
	t_real = realtime();
	int /*c,*/ opte = -1;
	gap_opt_t *opt;
	pe_opt_t *popt;
	popt = bwa_init_pe_opt();
	opt = gap_init_opt();

	std::string /*RefPath("Empty"), VcfPath("Empty"), MaskPath("Empty"),*/ Fastq_1("Empty"), Fastq_2(
		"Empty"), BamIn("Empty"), ReadGroup("@RG\tID:foo\tSM:bar"), DepthDist, SitePileup, FaList("Empty")/*, DBsnpPath("Empty")*/;
	std::string Prefix("Empty"), IndexPrefix("Empty");
	bool loggap(0), /*compread(0),*/ nonstop(0), NonIL13(0), NonBamOut(0);
	paramList pl;

	BEGIN_LONG_PARAMS(longParameters) LONG_PARAM_GROUP("Input/Output Files", "Input/Output files for the program[Complete Path Recommended]")
		//LONG_STRING_PARAM("vcf", &VcfPath, "[String] Input Hapmap or Selected Sites VCF file[Required]")
		//LONG_STRING_PARAM("dbsnp", &DBsnpPath, "[String] dbSNP VCF file[Required]")
		//LONG_STRING_PARAM("ref", &RefPath, "[String] Reference FASTA file[Required]")
		//LONG_STRING_PARAM("mask", &MaskPath, "[String] Repeat Mask FASTA file[Leave empty if using Selected Sites VCF]")
		LONG_STRING_PARAM("fastq_1", &Fastq_1, "[String] Pair end 1 fastq file[Leave empty if using fq_list or bam_in]")
		LONG_STRING_PARAM("fastq_2", &Fastq_2, "[String] Pair end 2 fastq file.[Leave empty if using single end]")
		LONG_STRING_PARAM("fq_list", &FaList, "[String] Path of input fastq files, tab-delimited, one pair-end files per line(one file per line for single end)[Leave empty if using bam_in or fastq_1]")
		LONG_STRING_PARAM("bam_in", &BamIn, "[String] Input bam file path[Leave empty if using fq_list or fastq_1]")
		EXCLUSIVE_PARAM("sam_out", &NonBamOut, "[Bool] If output bam file[Leave empty if using bam_in]")
		LONG_STRING_PARAM("out_prefix", &Prefix, "[String] Prefix of all the output files[Required]")
		LONG_STRING_PARAM("in_idx_prefix", &IndexPrefix, "[String] Input prefix of all the index files[Required]")


		//LONG_STRING_PARAM("out",&outf,"Output file prefix")
		//LONG_PARAM_GROUP("Parameters for Reference Sequence ", "Parameters being used to extract reference sequences.[All Required]")
		//// LONG_INT_PARAM("K",&Indexer.RollParam.kmer_size,"kmer size for Rolling Hash filtering")
		//// LONG_INT_PARAM("T",&Indexer.RollParam.thresh,"threshold for filtering specific read")
		//// LONG_INT_PARAM("S",&Indexer.RollParam.read_step_size,"step size for extracting kmer")

		//LONG_INT_PARAM("var_long", &opt->num_variant_long, "[INT] number of variants with long flanking region")
		//LONG_INT_PARAM("var_short", &opt->num_variant_short, "[INT] number of variants with short flanking region")
		//LONG_INT_PARAM("flank_len", &opt->flank_len, "[INT] flanking region length around each marker")
		//LONG_INT_PARAM("flank_long_len", &opt->flank_long_len, "[INT] long flanking region length around each marker")

		LONG_PARAM_GROUP("Parameters for Alignment ", "Parameters the are universal for both single end and pair end alignment.[Optional]")
		LONG_DOUBLE_PARAM("n", &opt->fnr, "[INT or Float] Max #diff (int) or missing prob under 0.02 error rate")
		LONG_INT_PARAM("o", &opt->max_gapo, "[INT] maximum number or fraction of gap opens")
		LONG_INT_PARAM("e", &opte, "[INT] maximum number of gap extensions, -1 for disabling long gaps [-1]")
		LONG_INT_PARAM("i", &opt->indel_end_skip, "[INT] do not put an indel within INT bp towards the ends")
		LONG_INT_PARAM("d", &opt->max_del_occ, "[INT] maximum occurrences for extending a long deletion")
		LONG_INT_PARAM("l", &opt->seed_len, "[INT] seed length")
		LONG_INT_PARAM("k", &opt->max_seed_diff, "[INT] maximal seed difference")
		LONG_INT_PARAM("m", &opt->max_entries, "[INT] maximal stack entries")
		LONG_INT_PARAM("t", &opt->n_threads, "[INT] number of threads")
		// LONG_INT_PARAM("L",&opt->seed_len,"seed length")
		LONG_INT_PARAM("R", &opt->max_top2, "[INT] stop searching when there are >INT equally best hits")
		LONG_INT_PARAM("q", &opt->trim_qual, "[INT] quality threshold for read trimming down to 35dbp")
		LONG_STRING_PARAM("RG", &ReadGroup, "[String] set ReadGroup name")
		//Roll Hash

		// LONG_PARAM("c",&compread,"seed length")
		LONG_PARAM("N", &nonstop, "[Bool] non-iterative mode: search for all n-difference hits (slooow)")
		EXCLUSIVE_PARAM("NonI", &NonIL13, "[Bool] the input is not in the Illumina 1.3+ FASTQ-like format")
		LONG_PARAM("L", &loggap, "[Bool] log-scaled gap penalty for long deletions")

		LONG_PARAM_GROUP("Parameters for PairEnd ", "Parameters specified for Pair end mapping.[Optional]")
		LONG_INT_PARAM("max_isize", &popt->max_isize, "[INT] maximum insert size")
		LONG_INT_PARAM("max_occ", &popt->max_occ, "[INT] maximum occurrences for one end ")
		LONG_PARAM("is_sw", &popt->is_sw, "[Bool] disable Smith-Waterman for the unmapped mate")
		LONG_INT_PARAM("n_multi", &popt->n_multi, "[INT] maximum hits to output for paired reads")
		LONG_INT_PARAM("N_multi", &popt->N_multi, "[INT] maximum hits to output for discordant pairs")
		LONG_DOUBLE_PARAM("ap_prior", &popt->ap_prior, "[Double] prior of chimeric rate (lower bound) ")
		LONG_PARAM("force_isize", &popt->force_isize, " [Bool] disable insert size estimate")

		LONG_PARAM_GROUP("Parameters for Statistics ", "Parameters specified for statistics and summary.[Optional]")
		LONG_PARAM("cal_dup", &opt->cal_dup, "[Bool] enable the calculation of duplicated reads in depth calculation ")
		LONG_DOUBLE_PARAM("frac_samp", &opt->frac, "[Double] specify the downsampling fraction ")
		//LONG_STRING_PARAM("depth_dist",&DepthDist,"Output file for depth distribution ")
		//LONG_STRING_PARAM("site_pileup",&SitePileup,"Output file for Pileup information on specified sites ")

		END_LONG_PARAMS();

	pl.Add(new longParams("Available Options", longParameters));
	pl.Read(argc, argv);
	pl.Status();
	if (Prefix == "Empty")
	{
		error("--out_prefix is required");
		exit(EXIT_FAILURE);
	}
	if (IndexPrefix == "Empty")
	{
		error("--in_idx_prefix is required");
		exit(EXIT_FAILURE);
	}
	//if (VcfPath == "Empty")
	//{
	//	error("--vcf is required");
	//	exit(EXIT_FAILURE);
	//}
	//if (DBsnpPath == "Empty")
	//{
	//	error("--dbsnp is required");
	//	exit(EXIT_FAILURE);
	//}
	if (NonBamOut)
	{
		opt->out_bam = 0;
	}
	if (BamIn != "Empty")
	{
		opt->in_bam = strdup(BamIn.c_str());
	}
	opt->RG = strdup(ReadGroup.c_str());
	if (opte > 0)
	{
		opt->max_gape = opte;
		opt->mode &= ~BWA_MODE_GAPE;
	}
	if (nonstop)
	{
		opt->mode |= BWA_MODE_NONSTOP;
		opt->max_top2 = 0x7fffffff;
	}
	if (NonIL13)
	{
		notice("using Sanger quality system...");
		//opt->mode |= BWA_MODE_IL13;
	}
	else
	{
		notice("using Illumina 1.3 version quality system...");
		opt->mode |= BWA_MODE_IL13;
	}
	if (loggap)
	{
		opt->mode |= BWA_MODE_LOGGAP;
	}
	//Extract ref seqs from VCF and Ref

	struct stat sb;
	BwtIndexer Indexer;
	std::string NewRef = IndexPrefix + ".FASTQuick.fa";

	ifstream ParamIn(NewRef + ".param");
	if (!ParamIn.is_open())
	{
		cerr << "Open file :" << NewRef + ".param" << "failed!" << endl;
	}
	std::string ParaStr,TmpStr;
	std::getline(ParamIn, ParaStr);//RefPath
	std::getline(ParamIn, ParaStr);//variant long
	stringstream ss(ParaStr);
	ss >> TmpStr;
	if (TmpStr == "var_long:") ss >> opt->num_variant_long;
	else { std::cerr << NewRef + ".param" << " corrupted!" << endl; exit(EXIT_FAILURE); }
	std::getline(ParamIn, ParaStr);//variant short
	ss.str("");
	ss.clear();
	ss<<ParaStr;
	ss >> TmpStr;
	if (TmpStr == "var_short:") ss >> opt->num_variant_short;
	else { std::cerr << NewRef + ".param" << " corrupted!" << endl; exit(EXIT_FAILURE); }
	std::getline(ParamIn, ParaStr);//flank_len
	ss.str("");
	ss.clear();
	ss << ParaStr;
	ss >> TmpStr;
	if (TmpStr == "flank_len:") ss >> opt->flank_len;
	else { std::cerr << NewRef + ".param" << " corrupted!" << endl; exit(EXIT_FAILURE); }
	std::getline(ParamIn, ParaStr);//flank_long_len
	ss.str("");
	ss.clear();
	ss << ParaStr;
	ss >> TmpStr;
	if (TmpStr == "flank_long_len:") ss >> opt->flank_long_len;
	else { std::cerr << NewRef + ".param" << " corrupted!" << endl; exit(EXIT_FAILURE); }
	ParamIn.close();



	std::string BwtPath = NewRef + ".bwt";
	//string BwtPathR = RefPath + ".rbwt";
	if (stat(BwtPath.c_str(), &sb) != 0) //|| stat(BwtPathR.c_str(), &sb)!=0)
	{
		notice("Index file doesn't exist, please build index file first...\n");
		return 0;
	}
	else //load ref index
	{
		t_tmp = realtime();
		Indexer.LoadIndex(NewRef);
		notice("[main]Index file exists, loading...%f sec\n", realtime() - t_tmp);
		t_tmp = realtime();
		if (FaList != "Empty")
		{
			BwtMapper Mapper(Indexer, FaList, Prefix, NewRef, popt, opt);
		}
		else
		{
			BwtMapper Mapper(Indexer, Fastq_1, Fastq_2, Prefix, NewRef, popt, opt);
		}
		notice("[main]Mapping...%f sec\n", realtime() - t_tmp);
	}
	notice("Version: %s\n", PACKAGE_VERSION);
	notice("[main]Real time: %.3f sec; CPU: %.3f sec\n",realtime() - t_real, cputime());
	gap_free_opt(opt);
	free(popt);
	//ProfilerStop();
	return 0;
}
int runPop(int argc, char ** argv)
{

	double t_real;
	t_real = realtime();
	std::string UDPath("Empty"), PCPath("Empty"), muPath("Empty"), glPath("Empty"), bedPath("Empty"), output("Empty");

	paramList pl;

	BEGIN_LONG_PARAMS(longParameters) LONG_PARAM_GROUP("Input/Output Files", "Input/Output files for the program[Complete Path Recommended]")
		LONG_STRING_PARAM("UD", &UDPath, "[String] Input UD matrix file in resource directory[Required]")
		LONG_STRING_PARAM("PC", &PCPath, "[String] Input PC matrix file in resource directory[Required]")
		LONG_STRING_PARAM("mu", &muPath, "[String] Input mu matrix file in resource directory[Required]")
		LONG_STRING_PARAM("gl", &glPath, "[String] Input genotype likelihood file generated from align[Required]")
		LONG_STRING_PARAM("bed", &bedPath, "[String] Input choose.bed.post.bed.allele.bed file in resource directory[Required]")
		LONG_STRING_PARAM("out", &output, "[String] Specify output file[Required]")
	END_LONG_PARAMS();

	pl.Add(new longParams("Available Options", longParameters));
	pl.Read(argc, argv);
	pl.Status();
	if (UDPath == "Empty")
	{
		error("--UD is required");
		exit(EXIT_FAILURE);
	}
	if (PCPath == "Empty")
	{
		error("--PC is required");
		exit(EXIT_FAILURE);
	}
	if (muPath == "Empty")
	{
		error("--mu is required");
		exit(EXIT_FAILURE);
	}
	if (glPath == "Empty")
	{
		error("--gl is required");
		exit(EXIT_FAILURE);
	}
	if (bedPath == "Empty")
	{
		error("--bed is required");
		exit(EXIT_FAILURE);
	}
	PopulationIdentifier pop(UDPath, PCPath, muPath, glPath, bedPath);
	pop.OptimizeLLK();

	notice("Version: %s\n", PACKAGE_VERSION);
	notice("Real time: %.3f sec; CPU: %.3f sec\n",realtime() - t_real, cputime());
	return 0;
}
int runCon(int argc, char ** argv)
{

	double t_real;
	t_real = realtime();
	/*
	* Parameters
	*
	*/

	std::string  ReadGroup("default"),Prefix("Empty"),idx_Prefix("Empty");

	paramList pl;

	BEGIN_LONG_PARAMS(longParameters) LONG_PARAM_GROUP("Input/Output Files", "Input/Output files for the program[Complete Path Recommended]")

		LONG_STRING_PARAM("in_idx_prefix", &idx_Prefix, "[String] Specify the prefix used by index files.[Required]")
		LONG_STRING_PARAM("in_prefix", &Prefix, "[String] Specify the prefix used as output prefix in step of align.[Required]")
		LONG_STRING_PARAM("RG", &ReadGroup, "[String] set ReadGroup name")


		END_LONG_PARAMS();

	pl.Add(new longParams("Available Options", longParameters));
	pl.Read(argc, argv);
	pl.Status();
	if (Prefix == "Empty")
	{
		error("--in_prefix is required");
		exit(EXIT_FAILURE);
	}
	if (idx_Prefix == "Empty")
	{
		error("--in_idx_prefix is required");
		exit(EXIT_FAILURE);
	}

	char cmdline[1024];
	char** params = new char*[9];
	sprintf(cmdline, MPU_PATH " verify --vcf %s.FASTQuick.fa.SelectedSite.vcf.gz --mpu %s.Pileup.gz --smID %s --out %s.ctm", idx_Prefix.c_str(), Prefix.c_str(), ReadGroup.c_str(), Prefix.c_str());//TODO:need to change SelectedSite.vcf.gz into vcf print by PCA mapping
	stringstream ss(cmdline);
	string para;
	ss >> para;
	for (int i = 0; i != 9; ++i)
	{
		ss >> para;
		params[i] = new char[128];
		strcpy(params[i], para.c_str());
	}
	//runVerify(argc,argv);
	runVerify(9, params);
	notice("Version: %s\n", PACKAGE_VERSION);
	notice("Real time: %.3f sec; CPU: %.3f sec\n",	realtime() - t_real, cputime());
	return 0;
}


static int usage()
{
	fprintf(stderr, "\n");
	fprintf(stderr, "Program: FastPopCon (Fast Population-identification and Contamination analysis tool for NGS data)\n");
	fprintf(stderr, "Version: %s\n", PACKAGE_VERSION);
	fprintf(stderr, "Contact: Fan Zhang <fanzhang@umich.edu>\n\n");
	fprintf(stderr, "Usage:   FastPopCon <command> [options]\n\n");
	fprintf(stderr, "Command: index         index sequences in the FASTA format\n");
	fprintf(stderr, "         align         summarize alignment based basic statisic\n");
	fprintf(stderr, "         pop         generate sample population identification\n");
	fprintf(stderr, "         con         estimate sample contamination level\n");
	fprintf(stderr, "\n");
	return 1;
}

int main(int argc, char *argv[])
{
	if (argc < 2) return usage();
	if (strcmp(argv[1], "oneStop") == 0) return runOneStop(argc - 1, argv + 1);
	else if (strcmp(argv[1], "index") == 0) return runIndex(argc - 1, argv + 1);
	else if (strcmp(argv[1], "align") == 0) return runAlign(argc - 1, argv + 1);
	else if (strcmp(argv[1], "pop") == 0) return runPop(argc - 1, argv + 1);
	else if (strcmp(argv[1], "con") == 0) return runCon(argc - 1, argv + 1);

	else {
		warning("unrecognized command '%s'\n", argv[1]);
		return 1;
	}
	return 0;
}
