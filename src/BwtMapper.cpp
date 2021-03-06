#include "BwtMapper.h"
#include "../VerifyBamID/Random.h"
#include "../libbwa/bwape.h"
#include "../libbwa/bwase.h"
#include "../libbwa/bwtgap.h"
#include "../libbwa/khash.h"
#include "Error.h"
#include <algorithm>
#include <iostream>
#include <stdio.h>
#include <time.h>

#include "Version.h"

#include "ctpl_stl.h"
//#include <gperftools/profiler.h>

using namespace std;

KHASH_MAP_INIT_INT64(64, poslist_t)

kh_64_t *g_hash;

std::mutex myMutex;

//#define DEBUG 0
#ifdef DEBUG
#define DBG(CODE) CODE
#else
#define DBG(CODE)
#endif

#define N_OCC 3

#ifdef HAVE_PTHREAD
#define THREAD_BLOCK_SIZE 1024

#include <pthread.h>

// static pthread_mutex_t g_seq_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

extern ubyte_t *bwa_refine_gapped(const bntseq_t *bns, int n_seqs,
                                  bwa_seq_t *seqs, ubyte_t *_pacseq,
                                  bntseq_t *ntbns);

extern void bwa_print_sam1(const bntseq_t *bns, bwa_seq_t *p,
                           const bwa_seq_t *mate, int mode, int max_top2);

extern void bwa_aln2seq_core(int n_aln, const bwt_aln1_t *aln, bwa_seq_t *s,
                             int set_main, int n_multi);

extern int bwa_approx_mapQ(const bwa_seq_t *p, int mm);

extern int bwt_cal_width(const bwt_t *rbwt, int len, const ubyte_t *str,
                         bwt_width_t *width);

extern bwa_seq_t *bwa_read_bam(bwa_seqio_t *bs, int n_needed, int *n,
                               int is_comp, int trim_qual);

extern int bwa_trim_read(int trim_qual, bwa_seq_t *p);

static void bwa_cal_sa_reg_gap(int tid, bwt_t *const bwt[2], int n_seqs,
                               bwa_seq_t *seqs, const gap_opt_t *opt,
                               const BwtIndexer *Indexer) {
  int i, max_l = 0, max_len;
  gap_stack_t *stack;
  bwt_width_t *w[2], *seed_w[2];
  const ubyte_t *seq[2];
  gap_opt_t local_opt = *opt;

  // initiate priority stack
  for (i = max_len = 0; i != n_seqs; ++i)
    if (seqs[i].len > max_len)
      max_len = seqs[i].len;
  if (opt->fnr > 0.0)
    local_opt.max_diff = bwa_cal_maxdiff(max_len, BWA_AVG_ERR, opt->fnr);
  if (local_opt.max_diff < local_opt.max_gapo)
    local_opt.max_gapo = local_opt.max_diff;
  stack = gap_init_stack(local_opt.max_diff, local_opt.max_gapo,
                         local_opt.max_gape, &local_opt);

  seed_w[0] = (bwt_width_t *)calloc(opt->seed_len + 1, sizeof(bwt_width_t));
  seed_w[1] = (bwt_width_t *)calloc(opt->seed_len + 1, sizeof(bwt_width_t));
  w[0] = w[1] = 0;
  // uint32_t unmapped_num = 0;
  for (i = 0; i != n_seqs; ++i) {
    bwa_seq_t *p = seqs + i;
    /*decoupling multithread*/
    /*
#ifdef HAVE_PTHREAD
if (opt->n_threads > 1)
{
pthread_mutex_lock(&g_seq_lock);
if (p->tid < 0)
{ // unassigned
int j;
for (j = i; j < n_seqs && j < i + THREAD_BLOCK_SIZE; ++j)
seqs[j].tid = tid;
}
else if (p->tid != tid)
{
pthread_mutex_unlock(&g_seq_lock);
continue;
}
pthread_mutex_unlock(&g_seq_lock);
}
#endif*/

    p->sa = 0;
    p->type = BWA_TYPE_NO_MATCH;
    p->c1 = p->c2 = 0;
    p->n_aln = 0;
    p->aln = 0;
    if (/* drand48() >opt->frac ||*/ p->filtered) {
      // unmapped_num++;
      continue;
    }
    seq[0] = p->seq;
    seq[1] = p->rseq;
    if (max_l < p->len) {
      max_l = p->len;
      w[0] = (bwt_width_t *)realloc(w[0], (max_l + 1) * sizeof(bwt_width_t));
      w[1] = (bwt_width_t *)realloc(w[1], (max_l + 1) * sizeof(bwt_width_t));
      memset(w[0], 0, (max_l + 1) * sizeof(bwt_width_t));
      memset(w[1], 0, (max_l + 1) * sizeof(bwt_width_t));
    }
    bwt_cal_width(bwt[0], p->len, seq[0], w[0]);
    bwt_cal_width(bwt[1], p->len, seq[1], w[1]);
    if (opt->fnr > 0.0)
      local_opt.max_diff = bwa_cal_maxdiff(p->len, BWA_AVG_ERR, opt->fnr);
    local_opt.seed_len = opt->seed_len < p->len ? opt->seed_len : 0x7fffffff;
    if (p->len > opt->seed_len) {
      bwt_cal_width(bwt[0], opt->seed_len, seq[0] + (p->len - opt->seed_len),
                    seed_w[0]);
      bwt_cal_width(bwt[1], opt->seed_len, seq[1] + (p->len - opt->seed_len),
                    seed_w[1]);
    }
    // core function

    p->aln =
        bwt_match_gap(bwt, p->len, seq, w, p->len <= opt->seed_len ? 0 : seed_w,
                      &local_opt, &p->n_aln, stack);
    //        if(strcmp(p->name,"SRR024362.3502826") ==0)
    //         {
    //         fwrite(w[0], sizeof(bwt_width_t), max_l, stdout);
    //         fwrite(w[1], sizeof(bwt_width_t), max_l, stdout);
    //         fwrite(seed_w[0], sizeof(bwt_width_t),opt->seed_len, stdout);
    //         fwrite(seed_w[1], sizeof(bwt_width_t),opt->seed_len, stdout);
    //         //fwrite(&local_opt, sizeof(gap_opt_t), 1, stdout);
    //         //fwrite(stack, sizeof(gap_stack_t), 1, stdout);
    //         fprintf(stderr,"n_stacks:%d\tbest:%d\tn_entries:%d\tstacks_n_entries:%d\tstacks_last_diff_pos:%d\n",stack->n_stacks,stack->best,stack->n_entries,stack->stacks->n_entries,stack->stacks->stack->last_diff_pos);
    //         for(int iter=0;iter<p->n_aln;++iter)
    //         fprintf(stderr,"l:%d\tk:%d\t strand:%d\t
    //         score:%d\n",(p->aln+iter)->l,(p->aln+iter)->k,(p->aln+iter)->a,
    //         (p->aln+iter)->score);
    //         }
    // store the alignment
    // free(p->name); free(p->seq); free(p->rseq); free(p->qual);
    // p->name = 0; p->seq = p->rseq = p->qual = 0;
  }
  // notice("RollingHash filtered %d reads...", unmapped_num);
  free(seed_w[0]);
  free(seed_w[1]);
  free(w[0]);
  free(w[1]);
  gap_destroy_stack(stack);
}

static int bwa_cal_pac_pos_pe(bwt_t *const _bwt[2], const int n_seqs,
                              bwa_seq_t *seqs[2], isize_info_t *ii,
                              const pe_opt_t *opt, const gap_opt_t *gopt,
                              const isize_info_t *last_ii, kh_64_t *hash);

BwtMapper::BwtMapper() : bwa_rg_line(nullptr), bwa_rg_id(nullptr) {}

BwtMapper::BwtMapper(BwtIndexer &BwtIndex, const string &FQList,
                     const string &Fastq_1, const string &Fastq_2,
                     const string &Prefix, const string &RefPath,
                     const pe_opt_t *popt, gap_opt_t *opt,
                     const std::string &targetRegionPath)
    : bwa_rg_line(nullptr), bwa_rg_id(nullptr) {
  if (bwa_set_rg(opt->RG) == -1)
    warning("Setting @RG tag failed!\n");
  if (opt->in_bam != 0) {
    notice("Input alignments from Bam file...");
    error("Input alignments from Bam file is disabled.");
    /*
    SamFileHeader SFH;
    SamFile SFIO;*/
    notice("Restore Variant Site Info...");
    collector.RestoreVcfSites(RefPath, opt);
    collector.SetGenomeSize(BwtIndex.ref_genome_size, BwtIndex.ref_N_size);
    if (targetRegionPath != "Empty")
      collector.SetTargetRegion(targetRegionPath);
    ofstream fout(Prefix + ".InsertSizeTable");
    int total_add = 0;
    collector.ReadAlignmentFromBam(opt, /*SFH, SFIO,*/ opt->in_bam, fout,
                                   total_add);
    notice("%d sequences are retained for QC.", total_add);
    fout.close();
    // BamFile->ifclose();
    notice("Calculate distributions...");
    collector.ProcessCore(Prefix, opt);
  } else {
    SamFileHeader SFH;
    BamInterface BamIO;
    IFILE BamFile = new InputFile((Prefix + ".bam").c_str(), "w",
                                  InputFile::ifileCompression::BGZF);
    StatGenStatus StatusTracker;
    StatusTracker.setStatus(StatGenStatus::Status::SUCCESS,
                            "Initialization when start.\n");
    if (!opt->out_bam) {
      bwa_print_sam_SQ(BwtIndex.bns);
      bwa_print_sam_PG();
    } else {
      if (!BamFile->isOpen()) {
        warning("Open Bam file for writing failed, abort!\n");
        exit(EXIT_FAILURE);
      }
      SetSamFileHeader(SFH, BwtIndex);
      BamIO.writeHeader(BamFile, SFH, StatusTracker);
    }
    double t_tmp = realtime();
    collector.RestoreVcfSites(RefPath, opt);
    collector.SetGenomeSize(BwtIndex.ref_genome_size, BwtIndex.ref_N_size);
    if (targetRegionPath != "Empty")
      collector.SetTargetRegion(targetRegionPath);
    notice("Restore Variant Site Info...%f sec", realtime() - t_tmp);
    ofstream fout(Prefix + ".InsertSizeTable");

    if (FQList != "Empty") {
      notice("Open Fastq List ...");
      ifstream fin(FQList);
      if (!fin.is_open())
        error("Open file %s failed", FQList.c_str());
      std::string line;
      int i(0);
      while (getline(fin, line)) {
        if (line[0] == '#')
          continue;
        ++i;
        std::string tmp_Fastq_1(""), tmp_Fastq_2("");
        stringstream ss(line);
        ss >> tmp_Fastq_1 >> tmp_Fastq_2;
        if (tmp_Fastq_2 != "") {
          notice("Processing Pair End mapping\t%d\t%s\t%s", i,
                 tmp_Fastq_1.c_str(), tmp_Fastq_2.c_str());
          t_tmp = realtime();
          FileStatCollector FSC(tmp_Fastq_1.c_str(), tmp_Fastq_2.c_str());
          PairEndMapper(BwtIndex, popt, opt, SFH, BamIO, BamFile, StatusTracker,
                        fout, FSC);
          notice("Processed Pair End mapping in %f sec", realtime() - t_tmp);
          collector.AddFSC(FSC);
        } else {
          notice("Processing Single End mapping\t%d\t%s\n", i,
                 tmp_Fastq_1.c_str());
          FileStatCollector FSC(tmp_Fastq_1.c_str());
          SingleEndMapper(BwtIndex, opt, SFH, BamIO, BamFile, StatusTracker,
                          fout, FSC);
          collector.AddFSC(FSC);
        }
      }
    } else if (Fastq_2 != "Empty") {
      notice("Processing Pair End mapping\t%s\t%s", Fastq_1.c_str(),
             Fastq_2.c_str());
      t_tmp = realtime();
      FileStatCollector FSC(Fastq_1.c_str(), Fastq_2.c_str());
      PairEndMapper(BwtIndex, popt, opt, SFH, BamIO, BamFile, StatusTracker,
                    fout, FSC);
      notice("Processed Pair End mapping in %f sec", realtime() - t_tmp);
      collector.AddFSC(FSC);
    } else {
      notice("Processing Single End mapping\t%s\n", Fastq_1.c_str());
      t_tmp = realtime();
      FileStatCollector FSC(Fastq_1.c_str());
      SingleEndMapper(BwtIndex, opt, SFH, BamIO, BamFile, StatusTracker, fout,
                      FSC);
      notice("Processed Single End mapping in %f sec", realtime() - t_tmp);
      collector.AddFSC(FSC);
    }

    fout.close();
    BamFile->ifclose();
    delete BamFile;
    // destroy
    t_tmp = realtime();
    collector.ProcessCore(Prefix, opt);
    notice("Calculate distributions... %f sec", realtime() - t_tmp);
  }
}

int BwtMapper::bwa_cal_pac_pos(BwtIndexer &BwtIndex, int n_seqs,
                               bwa_seq_t *seqs, int max_mm, float fnr) {
  int i, j;
  // char str[1024];
  bwt_t *bwt;
  // load forward SA
  // strcpy(str, prefix); strcat(str, ".bwt");
  bwt = BwtIndex.bwt_d; // bwt_restore_bwt(str);
  // strcpy(str, prefix); strcat(str, ".sa");
  // bwt_restore_sa(str, bwt);
  for (i = 0; i != n_seqs; ++i) {
    if (seqs[i].strand)
      bwa_cal_pac_pos_core(bwt, 0, &seqs[i], max_mm, fnr);
    for (j = 0; j < seqs[i].n_multi; ++j) {
      bwt_multi1_t *p = seqs[i].multi + j;
      if (p->strand)
        p->pos = bwt_sa(bwt, p->pos); // transform pos into actual position
    }
  }
  // bwt_destroy(bwt);
  // load reverse BWT and SA
  // strcpy(str, prefix); strcat(str, ".rbwt");
  bwt = BwtIndex.rbwt_d; // bwt_restore_bwt(str);
  // strcpy(str, prefix); strcat(str, ".rsa");
  // bwt_restore_sa(str, bwt);
  for (i = 0; i != n_seqs; ++i) {
    if (!seqs[i].strand)
      bwa_cal_pac_pos_core(0, bwt, &seqs[i], max_mm, fnr);
    for (j = 0; j < seqs[i].n_multi; ++j) {
      bwt_multi1_t *p = seqs[i].multi + j;
      if (!p->strand)
        p->pos = bwt->seq_len - (bwt_sa(bwt, p->pos) + seqs[i].len);
    }
  }
  // bwt_destroy(bwt);
  return 1;
}

typedef struct {
  kvec_t(bwt_aln1_t) aln;
} aln_buf_t;
// specific wrapper for bwa_read_seq[begin]
#include "../libbwa/bamlite.h"
#include "../libbwa/kseq.h"
#include "zlib.h"

KSEQ_INIT_FPC(gzFile, gzread)

struct __bwa_seqio_t {
  // for BAM input
  int is_bam, which; // 1st bit: read1, 2nd bit: read2, 3rd: SE
  bamFile fp;
  // for fastq input
  kseq_t *ks;
};
#define BARCODE_LOW_QUAL 13

static bwa_seq_t *bwa_read_seq_with_hash(BwtIndexer *BwtIndex, bwa_seqio_t *bs,
                                         int n_needed, int *n, int mode,
                                         int trim_qual, double frac,
                                         uint32_t seed) {

  // struct drand48_data randBuffer;
  // srand48_r(seed, &randBuffer);
  Random randGen(seed);
  bwa_seq_t *seqs, *p;
  kseq_t *seq = bs->ks;
  int n_seqs, l, i, is_comp = mode & BWA_MODE_COMPREAD,
                    is_64 = mode & BWA_MODE_IL13, l_bc = mode >> 24;
  long n_trimmed = 0, n_tot = 0;

  if (l_bc > 15) {
    fprintf(stderr, "[%s] the maximum barcode length is 15.\n", __func__);
    return 0;
  }
  if (bs->is_bam)
    return bwa_read_bam(bs, n_needed, n, is_comp,
                        trim_qual); // l_bc has no effect for BAM input
  n_seqs = 0;
  seqs = (bwa_seq_t *)calloc(n_needed, sizeof(bwa_seq_t));
  // seqs = new bwa_seq_t[n_needed];
  // ProfilerStart("FastPopCon.prof");
  /*while ((l = kseq_read(seq)) >= 0) {*/

  /*if (Skip(BwtIndex, mode, seq->seq.s, seq->qual.s, 0, 0, seq->seq.l, frac))
   * continue;*/
  double rand_num = 0;
  while (1) {
    rand_num = randGen.Next();
    // drand48_r(&randBuffer,&rand_num);

    if (rand_num > frac) {
      // notice("before length:%d", seq->seq.l);
      if ((l = kseq_read4_fpc(seq)) < 0)
        break;
      // notice("after length:%d", seq->seq.l);
      continue;
    }
    if ((l = kseq_read3_fpc(seq)) < 0)
      break;

    if (int(seq->seq.l) <= l_bc)
      continue; // sequence length equals or smaller than the barcode length
    p = &seqs[n_seqs++];

    if (l_bc) { // then trim barcode
      for (i = 0; i < l_bc; ++i)
        p->bc[i] = (seq->qual.l && seq->qual.s[i] - 33 < BARCODE_LOW_QUAL)
                       ? tolower(seq->seq.s[i])
                       : toupper(seq->seq.s[i]);
      p->bc[i] = 0;
      for (; i < int(seq->seq.l); ++i)
        seq->seq.s[i - l_bc] = seq->seq.s[i];
      seq->seq.l -= l_bc;
      seq->seq.s[seq->seq.l] = 0;
      if (seq->qual.l) {
        for (i = l_bc; i < int(seq->qual.l); ++i)
          seq->qual.s[i - l_bc] = seq->qual.s[i];
        seq->qual.l -= l_bc;
        seq->qual.s[seq->qual.l] = 0;
      }
      l = seq->seq.l;
    } else
      p->bc[0] = 0;
    p->tid = -1; // no assigned to a thread
    p->qual = 0;
    // p->count=0;
    p->full_len = p->clip_len = p->len = l;
    n_tot += p->full_len;
    p->seq = (ubyte_t *)calloc(p->len < 96 ? 96 : p->len, 1);
    for (i = 0; i != p->full_len; ++i)
      p->seq[i] = nst_nt4_table[(int)seq->seq.s[i]];
    if (seq->qual.l) { // copy quality
      if (is_64)
        for (i = 0; i < int(seq->qual.l); ++i)
          seq->qual.s[i] -= 31;
      p->qual = (ubyte_t *)strdup((char *)seq->qual.s);
      if (trim_qual >= 1)
        n_trimmed += bwa_trim_read(trim_qual, p);
    }

    if (BwtIndex->RollParam.thresh != 0 &&
        BwtIndex->IsReadFiltered(p->seq, p->qual, p->len)) {
      p->filtered |= 1;
      if (n_seqs == n_needed)
        break;
      continue;
    }

    p->rseq = (ubyte_t *)calloc(p->full_len, 1);
    memcpy(p->rseq, p->seq, p->len);
    // fprintf(stderr, "I have been here: %d times!\n",i);
    seq_reverse(
        p->len, p->seq,
        0); // *IMPORTANT*: will be reversed back in
            // bwa_refine_gapped()//reversing here might affect hash filtering
            // result comparing to old version that put hash after this
    seq_reverse(p->len, p->rseq, is_comp);

    p->name = strdup((const char *)seq->name.s);
    { // trim /[12]$
      int t = seq->name.l;
      if (t > 2 && p->name[t - 2] == '/' &&
          (p->name[t - 1] == '1' || p->name[t - 1] == '2'))
        p->name[t - 2] = '\0';
    }

    if (n_seqs == n_needed)
      break;
  }

  *n = n_seqs;
  //  if (n_seqs && trim_qual >= 1)
  //    notice("%.1f%% bases are trimmed.", 100.0f * n_trimmed / n_tot);
  if (n_seqs == 0) {
    free(seqs);
    // delete[] seqs;
    return 0;
  }
  // ProfilerStop();
  return seqs;
}

static int bwa_read_seq_with_hash_dev(BwtIndexer *BwtIndex, bwa_seqio_t *bs,
                                      int n_needed, int *n, int mode,
                                      int trim_qual, double frac, uint32_t seed,
                                      bwa_seq_t *seqs, int &read_len) {

  // struct drand48_data randBuffer;
  // srand48_r(seed, &randBuffer);
  Random randGen(seed);
  bwa_seq_t /**seqs,*/ *p;
  kseq_t *seq = bs->ks;
  int n_seqs, l, i, is_comp = mode & BWA_MODE_COMPREAD,
                    is_64 = mode & BWA_MODE_IL13, l_bc = mode >> 24;
  long n_trimmed = 0, n_tot = 0;

  if (l_bc > 15) {
    fprintf(stderr, "[%s] the maximum barcode length is 15.\n", __func__);
    return 0;
  }
  // if (bs->is_bam) return bwa_read_bam(bs, n_needed, n, is_comp, trim_qual);
  // // l_bc has no effect for BAM input
  n_seqs = 0;
  // ProfilerStart("FastPopCon.prof");
  double rand_num = 0;
  while (1) {
    rand_num = randGen.Next();
    // drand48_r(&randBuffer, &rand_num);
    if (rand_num > frac) {
      if ((l = kseq_read4_fpc(seq)) < 0) {
        break;
      }
      continue;
    }
    if ((l = kseq_read3_fpc(seq)) < 0) {
      break;
    }
    if (int(seq->seq.l) <= l_bc)
      continue; // sequence length equals or smaller than the barcode length
    p = &seqs[n_seqs++];
    if (l_bc) { // then trim barcode
      for (i = 0; i < l_bc; ++i)
        p->bc[i] = (seq->qual.l && seq->qual.s[i] - 33 < BARCODE_LOW_QUAL)
                       ? tolower(seq->seq.s[i])
                       : toupper(seq->seq.s[i]);
      p->bc[i] = 0;
      for (; i < int(seq->seq.l); ++i)
        seq->seq.s[i - l_bc] = seq->seq.s[i];
      seq->seq.l -= l_bc;
      seq->seq.s[seq->seq.l] = 0;
      if (seq->qual.l) {
        for (i = l_bc; i < int(seq->qual.l); ++i)
          seq->qual.s[i - l_bc] = seq->qual.s[i];
        seq->qual.l -= l_bc;
        seq->qual.s[seq->qual.l] = 0;
      }
      l = seq->seq.l;
    } else
      p->bc[0] = 0;
    p->full_len = p->clip_len = p->len = l;
    n_tot += p->full_len;
    p->filtered = 0;
    if (p->len > read_len) {
      // fprintf(stderr, "the length is
      // weird:%d\np->seq:%x\n%s\n",p->len,p->seq,(char*)p->seq);
      free(p->seq);
      free(p->rseq);
      free(p->qual);
      p->seq = (ubyte_t *)calloc(p->len, 1);
      p->qual = (ubyte_t *)calloc(p->len, 1);
      p->rseq = (ubyte_t *)calloc(p->len, 1);
      read_len = p->len; // update opt->read_len
    }
    for (i = 0; i != p->full_len; ++i)
      p->seq[i] = nst_nt4_table[(int)seq->seq.s[i]];
    if (seq->qual.l) { // copy quality
      if (is_64)
        for (i = 0; i < int(seq->qual.l); ++i) {
          seq->qual.s[i] -= 31;
          p->qual[i] = seq->qual.s[i];
        }
      else {
        for (i = 0; i < int(seq->qual.l); ++i) {
          p->qual[i] = seq->qual.s[i];
        }
      }
      if (trim_qual >= 1)
        n_trimmed += bwa_trim_read(trim_qual, p);
    }
    // for debug begin
    strncpy(p->name, seq->name.s, seq->name.l);
    { // trim /[12]$
      int t = seq->name.l;
      if (t > 2 && p->name[t - 2] == '/' &&
          (p->name[t - 1] == '1' || p->name[t - 1] == '2'))
        p->name[t - 2] = '\0';
    }
    // for debug end
    //        strncpy(p->name, seq->name.s, seq->name.l);
    if (BwtIndex->RollParam.thresh != 0 &&
        BwtIndex->IsReadFiltered(p->seq, p->qual, p->len)) {
      p->filtered |= 1;
      if (n_seqs == n_needed)
        break;
      continue;
    }
    // p->rseq = (ubyte_t*)calloc(p->full_len, 1);
    memcpy(p->rseq, p->seq, p->len);
    seq_reverse(
        p->len, p->seq,
        0); // *IMPORTANT*: will be reversed back in
            // bwa_refine_gapped()//reversing here might affect hash filtering
            // result comparing to old version that put hash after this
    seq_reverse(p->len, p->rseq, is_comp);
    // p->name = strdup((const char*)seq->name.s);
    // strncpy(p->name, seq->name.s, seq->name.l);
    //		{ // trim /[12]$
    //			int t = strlen(p->name);
    //			if (t > 2 && p->name[t - 2] == '/' && (p->name[t - 1] ==
    //'1'
    //|| p->name[t - 1] == '2')) p->name[t - 2] = '\0';
    //		}

    if (n_seqs == n_needed)
      break;
  }
  *n = n_seqs;
  //  if (n_seqs && trim_qual >= 1)
  //    fprintf(stderr, "%.1f%% bases are trimmed.\n", 100.0f * n_trimmed /
  //    n_tot);
  if (n_seqs == 0) {
    // free(seqs);
    // delete[] seqs;
    return 0;
  }
  // ProfilerStop();
  // return seqs;
  // ProfilerStop();
  return n_seqs; // return success
}

#ifdef HAVE_PTHREAD

static void bwa_cal_sa_reg_gap(int tid, bwt_t *const bwt[2], int n_seqs,
                               bwa_seq_t *seqs, const gap_opt_t *opt,
                               const BwtIndexer *Indexer);

typedef struct {
  int tid;
  bwt_t *bwt[2];
  int n_seqs;
  bwa_seq_t *seqs;
  const gap_opt_t *opt;
  const BwtIndexer *Indexer_Ptr;
} thread_aux_t;

static void *worker(void *data) {
  thread_aux_t *d = (thread_aux_t *)data;
  bwa_cal_sa_reg_gap(d->tid, d->bwt, d->n_seqs, d->seqs, d->opt,
                     d->Indexer_Ptr);
  return 0;
}

void workerAlt(int id, void *data) {
  //  fprintf(stderr,"echo from %d worker thread...\n",id);
  thread_aux_t *d = (thread_aux_t *)data;
  bwa_cal_sa_reg_gap(d->tid, d->bwt, d->n_seqs, d->seqs, d->opt,
                     d->Indexer_Ptr);
}

typedef struct {
  thread_aux_t *aux1;
  thread_aux_t *aux2;
  const pe_opt_t *popt;
  isize_info_t last_ii;
  ubyte_t *pacseq;
  bntseq_t *ntbns;
  kh_64_t *l_hash;
} thread_PE_t;

void PEworker(int id, void *data) {
  thread_PE_t *d = (thread_PE_t *)data;

  bwa_seq_t *seqs[2];
  seqs[0] = d->aux1->seqs;
  seqs[1] = d->aux2->seqs;

  isize_info_t ii;

  bwa_cal_pac_pos_pe(d->aux1->bwt, d->aux1->n_seqs, seqs, &ii, d->popt,
                     d->aux1->opt, &d->last_ii, d->l_hash);

  if (d->pacseq == 0) // indexing path
  {
    /*pacseq = */
    d->pacseq =
        bwa_paired_sw(d->aux1->Indexer_Ptr->bns, d->aux1->Indexer_Ptr->pac_buf,
                      d->aux1->n_seqs, seqs, d->popt, &ii, d->aux1->opt->mode);
  } else {
    /*pacseq = */
    d->pacseq =
        bwa_paired_sw(d->aux1->Indexer_Ptr->bns, d->pacseq, d->aux1->n_seqs,
                      seqs, d->popt, &ii, d->aux1->opt->mode);
  }

  for (int j = 0; j < 2; ++j)
    bwa_refine_gapped(d->aux1->Indexer_Ptr->bns, d->aux1->n_seqs, seqs[j],
                      d->pacseq, d->ntbns);

  d->last_ii = ii;
}

typedef struct {
  BwtIndexer *BwtIndex;
  bwa_seq_t *seqAddress;
  bwa_seqio_t *ksAddress;
  int *n_seqs;
  int mode;
  int trim_qual;
  double frac;
  uint32_t round;
  int *ret;
  int read_len;
} thread_IO_t;

static void *IOworker(void *data) {
  thread_IO_t *d = (thread_IO_t *)data;
  // d->seqAddress = bwa_read_seq_with_hash(d->BwtIndex, d->ksAddress,
  // READ_BUFFER_SIZE, d->n_seqs, d->mode, d->trim_qual, d->frac, d->round);
  *(d->ret) = bwa_read_seq_with_hash_dev(
      d->BwtIndex, d->ksAddress, READ_BUFFER_SIZE, d->n_seqs, d->mode,
      d->trim_qual, d->frac, d->round, d->seqAddress, d->read_len);
  return 0;
}

void IOworkerAlt(int id, void *data) {
  thread_IO_t *d = (thread_IO_t *)data;
  // d->seqAddress = bwa_read_seq_with_hash(d->BwtIndex, d->ksAddress,
  // READ_BUFFER_SIZE, d->n_seqs, d->mode, d->trim_qual, d->frac, d->round);
  //  fprintf(stderr,"echo from %d IO thread...\n",id);
  *(d->ret) = bwa_read_seq_with_hash_dev(
      d->BwtIndex, d->ksAddress, READ_BUFFER_SIZE, d->n_seqs, d->mode,
      d->trim_qual, d->frac, d->round, d->seqAddress, d->read_len);
}

#endif

static int bwa_cal_pac_pos_pe(bwt_t *const _bwt[2], const int n_seqs,
                              bwa_seq_t *seqs[2], isize_info_t *ii,
                              const pe_opt_t *opt, const gap_opt_t *gopt,
                              const isize_info_t *last_ii, kh_64_t *hash) {
  int i, j, cnt_chg = 0;
  bwt_t *bwt[2];
  pe_data_t *d;
  aln_buf_t *buf[2];

  d = (pe_data_t *)calloc(1, sizeof(pe_data_t));
  buf[0] = (aln_buf_t *)calloc(n_seqs, sizeof(aln_buf_t));
  buf[1] = (aln_buf_t *)calloc(n_seqs, sizeof(aln_buf_t));
  /*
   if (_bwt[0] == 0) { // load forward SA
   strcpy(str, prefix); strcat(str, ".bwt");  bwt[0] = bwt_restore_bwt(str);
   strcpy(str, prefix); strcat(str, ".sa"); bwt_restore_sa(str, bwt[0]);
   strcpy(str, prefix); strcat(str, ".rbwt"); bwt[1] = bwt_restore_bwt(str);
   strcpy(str, prefix); strcat(str, ".rsa"); bwt_restore_sa(str, bwt[1]);
   } else
   */
  bwt[0] = _bwt[0], bwt[1] = _bwt[1];
  DBG(cerr << "Arrived check point 1....\n";)
  // SE
  for (i = 0; i != n_seqs; ++i) { // for each seqs
    bwa_seq_t *p[2];
    for (j = 0; j < 2; ++j) { // for each end
      uint32_t n_aln;
      p[j] = seqs[j] + i;
      p[j]->n_multi = 0;
      p[j]->extra_flag |= SAM_FPD | (j == 0 ? SAM_FR1 : SAM_FR2);
      // if ((seqs[0] + i)->filtered && (seqs[1] + i)->filtered) continue;
      if (p[j]->filtered) {
        continue;
      }
      // read(&n_aln, 4, 1, fp_sa[j]);// read in total number of aln
      n_aln = p[j]->n_aln;
      if (n_aln > kv_max(d->aln[j]))
        kv_resize(bwt_aln1_t, d->aln[j], n_aln);
      d->aln[j].n = n_aln; // update total number
      // fread(d->aln[j].a, sizeof(bwt_aln1_t), n_aln, fp_sa[j]);// read in aln
      // of one end d->aln[j].a=p[j]->aln;
      memcpy(d->aln[j].a, p[j]->aln, n_aln * sizeof(bwt_aln1_t));
      kv_copy(bwt_aln1_t, buf[j][i].aln, d->aln[j]); // backup d->aln[j]
      // generate SE alignment and mapping quality
      bwa_aln2seq(n_aln, d->aln[j].a, p[j]);
      if (p[j]->type == BWA_TYPE_UNIQUE || p[j]->type == BWA_TYPE_REPEAT) {
        int max_diff = gopt->fnr > 0.0
                           ? bwa_cal_maxdiff(p[j]->len, BWA_AVG_ERR, gopt->fnr)
                           : gopt->max_diff;
        p[j]->pos = p[j]->strand ? bwt_sa(bwt[0], p[j]->sa)
                                 : bwt[1]->seq_len -
                                       (bwt_sa(bwt[1], p[j]->sa) + p[j]->len);
        p[j]->seQ = p[j]->mapQ = bwa_approx_mapQ(p[j], max_diff);
      }
    }
  }
  DBG(cerr << "Arrived check point 2....\n";)
  // infer isize
  infer_isize(n_seqs, seqs, ii, opt->ap_prior, bwt[0]->seq_len);
  if (ii->avg < 0.0 && last_ii->avg > 0.0)
    *ii = *last_ii;
  if (opt->force_isize) {
    notice("discard insert size estimate as user's request.\n");
    ii->low = ii->high = 0;
    ii->avg = ii->std = -1.0;
  }

  // PE
  for (i = 0; i != n_seqs; ++i) {
    bwa_seq_t *p[2];
    for (j = 0; j < 2; ++j) {
      p[j] = seqs[j] + i;
      if ((seqs[0] + i)->filtered && (seqs[1] + i)->filtered)
        continue;
      kv_copy(bwt_aln1_t, d->aln[j], buf[j][i].aln); // copy aln back to d
    }
    if ((p[0]->type == BWA_TYPE_UNIQUE || p[0]->type == BWA_TYPE_REPEAT) &&
        (p[1]->type == BWA_TYPE_UNIQUE ||
         p[1]->type == BWA_TYPE_REPEAT)) { // only when both ends mapped
      uint64_t x;
      uint32_t j, k, n_occ[2];
      for (j = 0; j < 2; ++j) {
        n_occ[j] = 0;
        for (k = 0; k < d->aln[j].n; ++k) // for each aln
          n_occ[j] += d->aln[j].a[k].l - d->aln[j].a[k].k + 1;
      }
      if (n_occ[0] > opt->max_occ || n_occ[1] > opt->max_occ)
        continue; // if any end of the pair exceeded max occ  then process next
                  // pair of sequence
      d->arr.n = 0;
      for (j = 0; j < 2; ++j) {
        for (k = 0; k < d->aln[j].n; ++k) { // for each alignment
          bwt_aln1_t *r = d->aln[j].a + k;
          bwtint_t l;
          if (r->l - r->k + 1 >= MIN_HASH_WIDTH) { // then check hash table
            uint64_t key = (uint64_t)r->k << 32 |
                           r->l; // key is formed by lower and upper bound
            int ret;
            khint_t iter = kh_put(64, hash, key, &ret);
            if (ret) { // if this key is not in the hash table; ret must equal 1
                       // as we never remove elements
              poslist_t *z = &kh_val(
                  hash, iter); // return the bwtint_ts pointed by this iter
              z->n = r->l - r->k + 1;
              z->a = (bwtint_t *)malloc(sizeof(bwtint_t) * z->n);
              for (l = r->k; l <= r->l; ++l)
                z->a[l - r->k] =
                    r->a ? bwt_sa(bwt[0], l)
                         : bwt[1]->seq_len -
                               (bwt_sa(bwt[1], l) +
                                p[j]->len); // call forward / reverse bwt
                                            // respectively
            }
            for (l = 0; (int)l < kh_val(hash,
                                        iter)
                                     .n;
                 ++l) { // ret will surelly show this key in hash, just get its
                        // value
              x = kh_val(hash, iter).a[l];
              x = x << 32 | k << 1 |
                  j; // packed by  bwtint, lower bound k and pair end j
              kv_push(uint64_t, d->arr, x);
            }
          } else { // then calculate on the fly
            for (l = r->k; l <= r->l; ++l) {
              x = r->a ? bwt_sa(bwt[0], l)
                       : bwt[1]->seq_len - (bwt_sa(bwt[1], l) + p[j]->len);
              x = x << 32 | k << 1 | j;
              kv_push(uint64_t, d->arr, x);
            }
          }
        }
      }
      cnt_chg += pairing(p, d, opt, gopt->s_mm, ii);
    }
    DBG(cerr << "Arrived check point 3....\n";)
    if (opt->N_multi || opt->n_multi) {
      DBG(cerr << "Arrived check point 3-a....\n";)
      for (j = 0; j < 2; ++j) {
        DBG(cerr << "Arrived check point 3-0....\n";)
        if (p[j]->type != BWA_TYPE_NO_MATCH) {
          int k;
          if (!(p[j]->extra_flag & SAM_FPP) &&
              p[1 - j]->type != BWA_TYPE_NO_MATCH) {
            DBG(cerr << "Arrived check point 3-1....\n";)
            bwa_aln2seq_core(d->aln[j].n, d->aln[j].a, p[j], 0,
                             p[j]->c1 + p[j]->c2 - 1 > opt->N_multi
                                 ? opt->n_multi
                                 : opt->N_multi);
            DBG(cerr << "Arrived check point 3-2....\n";)
          } else {
            DBG(cerr << "Arrived check point 3-3....\n";)
            bwa_aln2seq_core(d->aln[j].n, d->aln[j].a, p[j], 0, opt->n_multi);
            DBG(cerr << "Arrived check point 3-4....\n";)
          }

          for (k = 0; k < p[j]->n_multi; ++k) {
            bwt_multi1_t *q = p[j]->multi + k;
            q->pos = q->strand ? bwt_sa(bwt[0], q->pos)
                               : bwt[1]->seq_len -
                                     (bwt_sa(bwt[1], q->pos) + p[j]->len);
          }
        }
      }
    }
  }
  DBG(cerr << "Arrived check point 4....\n";)
  // free

  for (i = 0; i < n_seqs; ++i) {
    kv_destroy(buf[0][i].aln);
    kv_destroy(buf[1][i].aln);
  }
  free(buf[0]);
  free(buf[1]);
  /*
  if (_bwt[0] == 0) {
  bwt_destroy(bwt[0]); bwt_destroy(bwt[1]);
  }*/
  kv_destroy(d->arr);
  kv_destroy(d->pos[0]);
  kv_destroy(d->pos[1]);
  kv_destroy(d->aln[0]);
  kv_destroy(d->aln[1]);
  free(d);
  return cnt_chg;
}

static int64_t pos_5(const bwa_seq_t *p) {
  if (p->type != BWA_TYPE_NO_MATCH)
    return p->strand ? pos_end(p) : p->pos;
  return -1;
}

extern char *bwa_escape(char *s);

int BwtMapper::bwa_set_rg(const char *s) {
  char *p, *q, *r;
  if (strstr(s, "@RG") != s) {
    bwa_rg_line = 0;
    bwa_rg_id = 0;
    return -1;
  }
  if (bwa_rg_line)
    free(bwa_rg_line);
  if (bwa_rg_id)
    free(bwa_rg_id);
  bwa_rg_line = strdup(s);
  bwa_rg_id = 0;
  bwa_escape(bwa_rg_line);
  p = strstr(bwa_rg_line, "\tID:");
  if (p == 0)
    return -1;
  p += 4;
  for (q = p; *q && *q != '\t' && *q != '\n'; ++q)
    ;
  bwa_rg_id = static_cast<char *>(calloc(q - p + 1, 1));
  for (q = p, r = bwa_rg_id; *q && *q != '\t' && *q != '\n'; ++q)
    *r++ = *q;
  return 0;
}

extern int64_t pos_end_multi(const bwt_multi1_t *p,
                             int len); // analogy to pos_end()

#define BAM_DEBUG 1
bool BwtMapper::SetSamFileHeader(SamFileHeader &SFH,
                                 const BwtIndexer &BwtIndex) {

  if (!SFH.setPGTag("VN", PACKAGE_VERSION, "FASTQuick"))
    std::cerr << "WARNING:SetPGTag failed" << endl;

  if (bwa_rg_line && strstr(bwa_rg_line, "@RG") == bwa_rg_line) {
    stringstream RGline(bwa_rg_line);
    string token, value;
    char tag[3];
    while (RGline >> token) {
      tag[0] = token[0];
      tag[1] = token[1];
      tag[2] = 0;
      value = token.substr(3, token.size() - 3);
      if (!SFH.setRGTag(tag, value.c_str(), bwa_rg_id))
        std::cerr << "WARNING:SetRGTag failed" << endl;
    }
  } else {
    std::cerr << "WARNING:@RG is empty" << endl;
  }

  for (size_t i = 0; i < BwtIndex.contigSize.size(); ++i) {
    std::string chrom = BwtIndex.contigSize[i].first;
    int len = BwtIndex.contigSize[i].second;
    SFH.setSQTag("LN", std::to_string(len).c_str(), chrom.c_str());
  }
  return 0;
}

bool BwtMapper::SetSamRecord(const bntseq_t *bns, bwa_seq_t *p,
                             const bwa_seq_t *mate, SamFileHeader &SFH,
                             SamRecord &SR, const gap_opt_t *opt) {
  int mode = opt->mode;
  int max_top2 = opt->max_top2;
  int j;
  int is_64 = mode & BWA_MODE_IL13;
  int flag;
  if (p->type != BWA_TYPE_NO_MATCH ||
      (mate && mate->type != BWA_TYPE_NO_MATCH)) {
    int seqid, nn, am = 0;
    flag = p->extra_flag;
    char XT[2];

    if (p->type == BWA_TYPE_NO_MATCH) {
      p->pos = mate->pos;
      p->strand = mate->strand;
      flag |= SAM_FSU;
      j = 1;
    } else
      j = pos_end(p) -
          p->pos; // j is the length of the reference in the alignment

    // get seqid
    nn = bns_coor_pac2real(bns, p->pos, j, &seqid);
    if (p->type != BWA_TYPE_NO_MATCH &&
        p->pos + j - bns->anns[seqid].offset > bns->anns[seqid].len)
      flag |= SAM_FSU; // flag UNMAP as this alignment bridges two adjacent
                       // reference sequences
    // update flag and print it
    if (p->strand)
      flag |= SAM_FSR;
    if (mate) {
      if (mate->type != BWA_TYPE_NO_MATCH) {
        if (mate->strand)
          flag |= SAM_FMR;
      } else
        flag |= SAM_FMU;
    }

    SR.setReadName(p->name);
    SR.setFlag(flag);

#ifdef BAM_DEBUG
    int readRealStart = 0;
    if (p->type == BWA_TYPE_NO_MATCH) {
      SR.setReferenceName(SFH, "*");
      SR.set1BasedPosition(0);
    } else {
      std::string chrName = std::string(bns->anns[seqid].name);
      int pos = static_cast<int>(p->pos - bns->anns[seqid].offset + 1);
      size_t atPos = chrName.find('@');
      size_t colonPos = chrName.find(':');
      char *pEnd;
      std::string chrom = chrName.substr(0, colonPos);
      int refCoord = static_cast<int>(strtol(
          chrName.substr(colonPos + 1, atPos - colonPos + 1).c_str(), &pEnd,
          10)); // absolute coordinate of variant
      if (chrName.back() == 'L') {
        readRealStart = refCoord - opt->flank_long_len + pos -
                        1; // absolute coordinate of current reads on reference

      } else {
        readRealStart = refCoord - opt->flank_len + pos - 1;
      }
      SR.setReferenceName(SFH, chrom.c_str());
      SR.set1BasedPosition(readRealStart);
    }
#else
    if (p->type == BWA_TYPE_NO_MATCH)
      SR.setReferenceName(SFH, "*");
    else
      SR.setReferenceName(SFH, bns->anns[seqid].name);
    SR.set1BasedPosition((int)(p->pos - bns->anns[seqid].offset + 1));

#endif
    SR.setMapQuality(p->mapQ);
    // print CIGAR
    ostringstream ss;

    if (p->type == BWA_TYPE_NO_MATCH)
      ss << "*";
    else if (p->cigar) {
      for (j = 0; j != p->n_cigar; ++j)
        ss << __cigar_len(p->cigar[j]) << "MIDS"[__cigar_op(p->cigar[j])];
    } else
      ss << p->len << "M";
    SR.setCigar(ss.str().c_str());
    // print mate coordinate
    if (mate && mate->type != BWA_TYPE_NO_MATCH) {
      int m_seqid;
      long long isize;
      am = mate->seQ < p->seQ ? mate->seQ
                              : p->seQ; // smaller single-end mapping quality
      // redundant calculation here, but should not matter too much
      int m_is_N = bns_coor_pac2real(bns, mate->pos, mate->len, &m_seqid);

#ifdef BAM_DEBUG
      std::string chrName = std::string(bns->anns[m_seqid].name);
      int pos = static_cast<int>(mate->pos - bns->anns[m_seqid].offset + 1);
      size_t atPos = chrName.find('@');
      size_t colonPos = chrName.find(':');
      char *pEnd;
      std::string chrom = chrName.substr(0, colonPos);
      int refCoord = static_cast<int>(strtol(
          chrName.substr(colonPos + 1, atPos - colonPos + 1).c_str(), &pEnd,
          10)); // absolute coordinate of variant
      if (chrName.back() == 'L') {
        readRealStart = refCoord - opt->flank_long_len + pos -
                        1; // absolute coordinate of current reads on reference
      } else {
        readRealStart = refCoord - opt->flank_len + pos - 1;
      }
      (seqid == m_seqid) ? SR.setMateReferenceName(SFH, "=")
                         : SR.setMateReferenceName(SFH, chrom.c_str());
      isize = (seqid == m_seqid) ? pos_5(mate) - pos_5(p) : 0;
      if (p->type == BWA_TYPE_NO_MATCH)
        isize = 0;
      SR.set1BasedMatePosition(readRealStart);
      SR.setInsertSize(isize);
#else
      (seqid == m_seqid)
          ? SR.setMateReferenceName(SFH, "=")
          : SR.setMateReferenceName(SFH, bns->anns[m_seqid].name);
      isize = (seqid == m_seqid) ? pos_5(mate) - pos_5(p) : 0;
      if (p->type == BWA_TYPE_NO_MATCH)
        isize = 0;
      // printf("%d\t%lld\t", (int) (mate->pos - bns->anns[m_seqid].offset + 1),
      // isize); ss<<(int) (mate->pos - bns->anns[m_seqid].offset +
      // 1)<<"\t"<<isize;
      SR.set1BasedMatePosition(
          (int)(mate->pos - bns->anns[m_seqid].offset + 1));
      SR.setInsertSize(isize);
#endif
    } else if (mate) // mate is unmapped
    {
      SR.setMateReferenceName(SFH, "=");
#ifdef BAM_DEBUG
      SR.set1BasedMatePosition(readRealStart);
#else
      SR.set1BasedMatePosition((int)(p->pos - bns->anns[seqid].offset + 1));
#endif
      SR.setInsertSize(0);
    } else {
      SR.setMateReferenceName(SFH, "*");
      SR.set1BasedMatePosition(0);
      SR.setInsertSize(0);
    }
    ss.clear();
    ss.str("");
    // print sequence and quality
    if (p->strand == 0) {
      for (j = 0; j != p->full_len; ++j)
        ss << "ACGTN"[(int)p->seq[j]];
      SR.setSequence(ss.str().c_str());
    } else {
      for (j = 0; j != p->full_len; ++j)
        ss << "TGCAN"[p->seq[p->full_len - 1 - j]];
      SR.setSequence(ss.str().c_str());
    }
    ss.clear();
    ss.str("");
    if (p->qual) {
      if (is_64) {
        for (int i = 0; i < p->len; ++i)
          p->qual[i] += 31;
      }
      if (p->strand)
        seq_reverse(p->len, p->qual, 0); // reverse quality
      SR.setQuality((char *)p->qual);
    } else
      SR.setQuality("*");

    if (bwa_rg_id)
      SR.addTag("RG", 'Z', bwa_rg_id);
    if (p->bc[0])
      SR.addTag("BC", 'Z', p->bc);
    if (p->clip_len < p->full_len)
      SR.addTag(
          "XC", 'i',
          std::to_string(static_cast<long long int>(p->clip_len)).c_str());
    if (p->type != BWA_TYPE_NO_MATCH) {
      int i;
      // calculate XT tag
      XT[0] = "NURM"[p->type];
      if (nn > 10)
        XT[0] = 'N';
      XT[1] = '\0';
      SR.addTag("XT", 'A', XT);
      ss.clear();
      ss.str("");
      if (mode & BWA_MODE_COMPREAD) {
        ss << "NM";
      } else {
        ss << "CM";
      }
      SR.addTag(ss.str().c_str(), 'i',
                to_string(static_cast<long long int>(p->nm)).c_str());
      if (nn)
        SR.addTag("XN", 'i', to_string(static_cast<long long int>(nn)).c_str());
      if (mate) {
        SR.addTag("SM", 'i',
                  to_string(static_cast<long long int>(p->seQ)).c_str());
        SR.addTag("AM", 'i', to_string(static_cast<long long int>(am)).c_str());
      }
      if (p->type != BWA_TYPE_MATESW) { // X0 and X1 are not available for this
                                        // type of alignment
        SR.addTag("X0", 'i',
                  to_string(static_cast<long long int>(p->c1)).c_str());
        if (p->c1 <= max_top2)
          SR.addTag("X1", 'i',
                    to_string(static_cast<long long int>(p->c2)).c_str());
      }
      SR.addTag("XM", 'i',
                to_string(static_cast<long long int>(p->n_mm)).c_str());
      SR.addTag("XO", 'i',
                to_string(static_cast<long long int>(p->n_gapo)).c_str());
      SR.addTag(
          "XG", 'i',
          to_string(static_cast<long long int>(p->n_gapo + p->n_gape)).c_str());
      if (p->md)
        SR.addTag("MD", 'Z', p->md);
      // print multiple hits
      ss.clear();
      ss.str("");
      if (p->n_multi) {
        for (i = 0; i < p->n_multi; ++i) {
          bwt_multi1_t *q = p->multi + i;
          int k;
          j = pos_end_multi(q, p->len) - q->pos;
          nn = bns_coor_pac2real(bns, q->pos, j, &seqid);
          ss << bns->anns[seqid].name << ",";
          if (q->strand)
            ss << '-';
          else
            ss << '+';
          ss << (int)(q->pos - bns->anns[seqid].offset + 1) << ",";
          if (q->cigar) {
            for (k = 0; k < q->n_cigar; ++k)
              ss << __cigar_len(q->cigar[k]) << "MIDS"[__cigar_op(q->cigar[k])];
          } else
            ss << p->len << "M";
          ss << "," << q->gap + q->mm << ";";
        }
        SR.addTag("XA", 'Z', ss.str().c_str());
      }
    }
  } else { // this read has no match
    ostringstream ss;
    ubyte_t *s = p->strand ? p->rseq : p->seq;
    flag = p->extra_flag | SAM_FSU;
    if (mate && mate->type == BWA_TYPE_NO_MATCH)
      flag |= SAM_FMU;
    SR.setReadName(p->name);
    SR.setFlag(flag);
    SR.setReferenceName(SFH, "*");
    SR.set1BasedPosition(0);
    SR.setMapQuality(0);
    SR.setCigar("*");
    SR.setMateReferenceName(SFH, "*");
    SR.set1BasedMatePosition(0);
    SR.setInsertSize(0);
    ss.clear();
    ss.str("");
    for (j = 0; j != p->len; ++j)
      ss << "ACGTN"[(int)s[j]];
    SR.setSequence(ss.str().c_str());
    ss.clear();
    ss.str("");
    if (p->qual) {
      if (p->strand)
        seq_reverse(p->len, p->qual, 0); // reverse quality
      SR.setQuality((char *)p->qual);
    } else
      SR.setQuality("*");
    if (bwa_rg_id)
      SR.addTag("RG", 'Z', bwa_rg_id);
    if (p->bc[0])
      SR.addTag("BC", 'Z', p->bc);
    if (p->clip_len < p->full_len)
      SR.addTag("XC", 'i',
                to_string(static_cast<long long int>(p->clip_len)).c_str());
  }
  if ((flag & SAM_FSU) && (flag & SAM_FMU))
    return 1; // this read pair is unmapped
  else
    return 0;
}

bool BwtMapper::SingleEndMapper(BwtIndexer &BwtIndex, const gap_opt_t *opt,
                                SamFileHeader &SFH, BamInterface &BamIO,
                                IFILE BamFile, StatGenStatus &StatusTracker,
                                std::ofstream &fout, FileStatCollector &FSC) {
  int i, n_seqs, tot_seqs = 0;
  bwa_seq_t *seqs;
  bwa_seqio_t *ks;
  clock_t t;
  bwt_t *bwt[2];
  bntseq_t *ntbns = 0;
  // initialization
  bwase_initialize();

  srand48(BwtIndex.bns->seed);
  ks = bwa_seq_open(FSC.FileName1.c_str());

  bwt[0] = BwtIndex.bwt_d;
  bwt[1] = BwtIndex.rbwt_d;

  ubyte_t *pacseq = 0;
  t = clock();
  while ((seqs = bwa_read_seq_with_hash(&BwtIndex, ks, READ_BUFFER_SIZE,
                                        &n_seqs, opt->mode, opt->trim_qual,
                                        opt->frac, t)) != 0) {
    tot_seqs += n_seqs;
    FSC.NumRead += n_seqs;

#ifdef HAVE_PTHREAD

    if (opt->n_threads <= 1) { // no multi-threading at all
      bwa_cal_sa_reg_gap(0, bwt, n_seqs, seqs, opt, &BwtIndex);
    } else {
      int n_align_thread = opt->n_threads;
      pthread_t *tid;
      pthread_attr_t attr;
      thread_aux_t *data;
      int j;
      pthread_attr_init(&attr);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
      data = (thread_aux_t *)calloc(n_align_thread, sizeof(thread_aux_t));
      tid = (pthread_t *)calloc(n_align_thread, sizeof(pthread_t));

      size_t grain_size = n_seqs / n_align_thread;

      for (j = 0; j < n_align_thread; ++j) {
        data[j].tid = j;
        data[j].bwt[0] = bwt[0];
        data[j].bwt[1] = bwt[1];
        if (j == n_align_thread - 1)
          data[j].n_seqs = n_seqs - grain_size * (n_align_thread - 1);
        else
          data[j].n_seqs = grain_size;
        data[j].seqs = seqs + j * grain_size;
        data[j].opt = opt;
        data[j].Indexer_Ptr = &BwtIndex;
        pthread_create(&tid[j], &attr, worker, data + j);
      }
      for (j = 0; j < n_align_thread; ++j)
        pthread_join(tid[j], 0);
      free(data);
      free(tid);
    }
#else
    // DBG(fprintf(stderr,"Before come into cal sa reg
    // gap...%s\n%d\nlength:%d\n",seqs->name,seqs->seq[0],seqs->len);)
    bwa_cal_sa_reg_gap(0, bwt, n_seqs, seqs, opt, &BwtIndex);
    // DBG(fprintf(stderr,"After come into cal sa reg
    // gap...%s\n%d\nlength:%d\n",seqs->name,seqs->seq[0],seqs->len);)
#endif

    t = clock();

    for (i = 0; i < n_seqs; ++i) {
      bwa_seq_t *p = seqs + i;
      FSC.NumBase += p->full_len;
      if (p->filtered) {
        continue;
      }
      bwa_aln2seq_core(p->n_aln, p->aln, p, 1, N_OCC);
    }

    bwa_cal_pac_pos(BwtIndex, n_seqs, seqs, opt->max_diff,
                    opt->fnr); // forward bwt will be destroyed here

    if (pacseq == 0)
      pacseq = bwa_refine_gapped(BwtIndex.bns, n_seqs, seqs, BwtIndex.pac_buf,
                                 ntbns);
    else
      pacseq = bwa_refine_gapped(BwtIndex.bns, n_seqs, seqs, pacseq, ntbns);

    if (!opt->out_bam) {
      for (i = 0; i < n_seqs; ++i) {
        bwa_seq_t *p = seqs + i;
        if (p->filtered) {
          FSC.TotalFiltered++;
          continue;
        }
        if ((p == 0 || p->type == BWA_TYPE_NO_MATCH)) {
          FSC.BwaUnmapped++;
          continue;
        }

        FSC.TotalRetained += collector.AddAlignment(BwtIndex.bns, seqs + i, 0,
                                                    opt, fout, FSC.TotalMAPQ);
        bwa_print_sam1(BwtIndex.bns, seqs + i, 0, opt->mode, opt->max_top2);
      }
    } else {
      for (i = 0; i < n_seqs; ++i) {
        bwa_seq_t *p = seqs + i;
        if (p->filtered) {
          FSC.TotalFiltered++;
          continue;
        }
        if ((p == 0 || p->type == BWA_TYPE_NO_MATCH)) {
          FSC.BwaUnmapped++;
          continue;
        }
        FSC.TotalRetained += collector.AddAlignment(BwtIndex.bns, seqs + i, 0,
                                                    opt, fout, FSC.TotalMAPQ);
        SamRecord SR;
        SetSamRecord(BwtIndex.bns, seqs + i, 0, SFH, SR, opt);
        BamIO.writeRecord(BamFile, SFH, SR,
                          SamRecord::SequenceTranslation::NONE);
      }
    }

    fprintf(stderr, "%.2f sec\n", (float)(clock() - t) / CLOCKS_PER_SEC);
    t = clock();
    bwa_free_read_seq(n_seqs, seqs);
    notice("%d sequences are loaded.", FSC.NumRead);
    notice("%ld sequences are filtered.", FSC.TotalFiltered);
    notice("%d sequences are retained for QC.", FSC.TotalRetained);

  } // end while
  // bam_destroy1(b);
  if (pacseq)
    free(pacseq);
  // destroy
  // bwt_destroy(bwt[0]); bwt_destroy(bwt[1]);
  bwa_seq_close(ks);
  return 0;
}

bool BwtMapper::PairEndMapper_without_asyncIO(
    BwtIndexer &BwtIndex, const pe_opt_t *popt, gap_opt_t *opt,
    SamFileHeader &SFH, BamInterface &BamIO, IFILE BamFile,
    StatGenStatus &StatusTracker, std::ofstream &fout, FileStatCollector &FSC) {

  int i, j, n_seqs[2] = {0, 0}, n_seqs_buff[2] = {0, 0};

  bwa_seq_t *seqs[2];
  bwa_seq_t *seqs_buff[2];
  bwa_seqio_t *ks[2];
  clock_t t;
  bwt_t *bwt[2];
  bntseq_t *ntbns = 0;
  khint_t iter;
  isize_info_t last_ii; // this is for the last batch of reads
  // initialization
  bwase_initialize(); // initialize g_log_n[] in bwase.c

  srand48(BwtIndex.bns->seed);
  g_hash = kh_init(64);
  last_ii.avg = -1.0;
  ks[0] = bwa_seq_open(FSC.FileName1.c_str());
  ks[1] = bwa_seq_open(FSC.FileName2.c_str());

  bwt[0] = BwtIndex.bwt_d;
  bwt[1] = BwtIndex.rbwt_d;

  ubyte_t *pacseq = 0;

  //	opt->read_len = kseq_get_seq_len_fpc(ks[0]->ks);
  seqs[0] = (bwa_seq_t *)calloc(READ_BUFFER_SIZE, sizeof(bwa_seq_t));
  seqs[1] = (bwa_seq_t *)calloc(READ_BUFFER_SIZE, sizeof(bwa_seq_t));
  seqs_buff[0] = (bwa_seq_t *)calloc(READ_BUFFER_SIZE, sizeof(bwa_seq_t));
  seqs_buff[1] = (bwa_seq_t *)calloc(READ_BUFFER_SIZE, sizeof(bwa_seq_t));
  bwa_init_read_seq(READ_BUFFER_SIZE, seqs[0], opt);
  bwa_init_read_seq(READ_BUFFER_SIZE, seqs[1], opt);
  bwa_init_read_seq(READ_BUFFER_SIZE, seqs_buff[0], opt);
  bwa_init_read_seq(READ_BUFFER_SIZE, seqs_buff[1], opt);

  int ReadIsGood = 2;
  uint32_t round = 0;
  int ret(-1);
#ifdef __ctpl_stl_thread_pool_H__
  int n_align_thread = opt->n_threads % 2 == 0
                           ? opt->n_threads
                           : opt->n_threads + 1; // round thread number up
  //  notice("CTPL is initializing with %d threads...", n_align_thread+2);

  ctpl::thread_pool p(n_align_thread + 2 /* two threads in the pool */);
  std::vector<std::future<void>> results(n_align_thread + 2);

  int n_first_thread = n_align_thread / 2;
  int n_second_thread = n_align_thread - n_first_thread;
  thread_IO_t *IO_param[2];
  IO_param[0] = (thread_IO_t *)calloc(1, sizeof(thread_IO_t));
  IO_param[1] = (thread_IO_t *)calloc(1, sizeof(thread_IO_t));
  /*added for IO end*/
  thread_aux_t *data =
      (thread_aux_t *)calloc(n_align_thread, sizeof(thread_aux_t));
#endif
  while (ReadIsGood) { // opt should be different for two fa files theoretically
    if (ReadIsGood == 2) {
      if ((ret = bwa_read_seq_with_hash_dev(&BwtIndex, ks[0], READ_BUFFER_SIZE,
                                            &n_seqs[0], opt->mode,
                                            opt->trim_qual, opt->frac, round,
                                            seqs[0], opt->read_len)) != 0) {
        ReadIsGood = 1;
      } else
        ReadIsGood = 0;

      if ((ret = bwa_read_seq_with_hash_dev(&BwtIndex, ks[1], READ_BUFFER_SIZE,
                                            &n_seqs[1], opt->mode,
                                            opt->trim_qual, opt->frac, round,
                                            seqs[1], opt->read_len)) != 0) {
      } else
        ReadIsGood = 0;
    }
    int cnt_chg;
    isize_info_t ii;
    t = clock();
#ifdef __ctpl_stl_thread_pool_H__
    size_t grain_size = n_seqs[0] / n_first_thread;
    for (int j = 0; j < n_first_thread; ++j) {
      data[j].tid = j;
      data[j].bwt[0] = bwt[0];
      data[j].bwt[1] = bwt[1];
      if (j == n_first_thread - 1)
        data[j].n_seqs = n_seqs[0] - grain_size * (n_first_thread - 1);
      else
        data[j].n_seqs = grain_size;
      data[j].seqs = seqs[0] + j * grain_size;
      data[j].opt = opt;
      data[j].Indexer_Ptr = &BwtIndex;
      results[j] = p.push(workerAlt, data + j);
    }
    for (int j = n_first_thread; j < n_align_thread; ++j) {
      data[j].tid = j;
      data[j].bwt[0] = bwt[0];
      data[j].bwt[1] = bwt[1];
      if (j == n_align_thread - 1)
        data[j].n_seqs = n_seqs[1] - grain_size * (n_second_thread - 1);
      else
        data[j].n_seqs = grain_size;
      data[j].seqs = seqs[1] + (j - n_first_thread) * grain_size;
      data[j].opt = opt;
      data[j].Indexer_Ptr = &BwtIndex;
      results[j] = p.push(workerAlt, data + j);
    }
    int ret_tmp[2] = {-1, -1};
    for (int j = 0; j < 2; ++j) {
      IO_param[j]->BwtIndex = &BwtIndex;
      IO_param[j]->ksAddress = ks[j];
      IO_param[j]->n_seqs = &n_seqs_buff[j];
      IO_param[j]->mode = opt->mode;
      IO_param[j]->trim_qual = opt->trim_qual;
      IO_param[j]->frac = opt->frac;
      IO_param[j]->round = round;
      IO_param[j]->seqAddress = seqs_buff[j];
      IO_param[j]->ret = &ret_tmp[j];
      IO_param[j]->read_len = opt->read_len;
      results[n_align_thread + j] = p.push(IOworkerAlt, IO_param[j]);
    }
    for (j = 0; j < n_align_thread + 2; ++j)
      results[j].get();
    /*IO thread*/
    round++;
    if ((ret_tmp[0] | ret_tmp[1]) == 0)
      ReadIsGood = 0;

#elif defined HAVE_PTHREAD
    if (opt->n_threads <= 1) { // no multi-threading at all
      for (int pair_idx = 0; pair_idx < 2; ++pair_idx) {
        bwa_cal_sa_reg_gap(0, bwt, n_seqs[pair_idx], seqs[pair_idx], opt,
                           &BwtIndex);
      }
      round++;
      if ((ret = bwa_read_seq_with_hash_dev(
               &BwtIndex, ks[0], READ_BUFFER_SIZE, &n_seqs_buff[0], opt->mode,
               opt->trim_qual, opt->frac, round, seqs_buff[0],
               opt->read_len)) != 0) {
        // ReadIsGood = 1;
        // FSC.NumRead += n_seqs_buff[0];
      } else
        ReadIsGood = 0;
      if ((ret = bwa_read_seq_with_hash_dev(
               &BwtIndex, ks[1], READ_BUFFER_SIZE, &n_seqs_buff[1], opt->mode,
               opt->trim_qual, opt->frac, round, seqs_buff[1],
               opt->read_len)) != 0) {
        // ReadIsGood = 1;
        // FSC.NumRead += n_seqs_buff[1];
      } else
        ReadIsGood = 0;
    } else {
      pthread_t *tid;
      pthread_attr_t attr;
      thread_aux_t *data;
      /*added for IO begin*/
      int n_align_thread = opt->n_threads % 2 == 0
                               ? opt->n_threads
                               : opt->n_threads + 1; // round thread number up
      thread_IO_t *IO_param[2];
      IO_param[0] = (thread_IO_t *)calloc(1, sizeof(thread_IO_t));
      IO_param[1] = (thread_IO_t *)calloc(1, sizeof(thread_IO_t));
      /*added for IO end*/
      pthread_attr_init(&attr);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
      data = (thread_aux_t *)calloc(n_align_thread, sizeof(thread_aux_t));
      tid = (pthread_t *)calloc(n_align_thread + 2, sizeof(pthread_t));
      /*decopling of the multithread*/
      int n_first_thread = n_align_thread / 2;
      int n_second_thread = n_align_thread - n_first_thread;

      size_t grain_size = n_seqs[0] / n_first_thread;
      for (int j = 0; j < n_first_thread; ++j) {
        data[j].tid = j;
        data[j].bwt[0] = bwt[0];
        data[j].bwt[1] = bwt[1];
        if (j == n_first_thread - 1)
          data[j].n_seqs = n_seqs[0] - grain_size * (n_first_thread - 1);
        else
          data[j].n_seqs = grain_size;
        data[j].seqs = seqs[0] + j * grain_size;
        data[j].opt = opt;
        data[j].Indexer_Ptr = &BwtIndex;
        pthread_create(&tid[j], &attr, worker, data + j);
      }
      for (int j = n_first_thread; j < n_align_thread; ++j) {
        data[j].tid = j;
        data[j].bwt[0] = bwt[0];
        data[j].bwt[1] = bwt[1];
        if (j == n_align_thread - 1)
          data[j].n_seqs = n_seqs[1] - grain_size * (n_second_thread - 1);
        else
          data[j].n_seqs = grain_size;
        data[j].seqs = seqs[1] + (j - n_first_thread) * grain_size;
        data[j].opt = opt;
        data[j].Indexer_Ptr = &BwtIndex;
        pthread_create(&tid[j], &attr, worker, data + j);
      }
      int ret_tmp[2] = {-1, -1};
      for (int j = 0; j < 2; ++j) {
        IO_param[j]->BwtIndex = &BwtIndex;
        IO_param[j]->ksAddress = ks[j];
        IO_param[j]->n_seqs = &n_seqs_buff[j];
        IO_param[j]->mode = opt->mode;
        IO_param[j]->trim_qual = opt->trim_qual;
        IO_param[j]->frac = opt->frac;
        IO_param[j]->round = round;
        IO_param[j]->seqAddress = seqs_buff[j];
        IO_param[j]->ret = &ret_tmp[j];
        IO_param[j]->read_len = opt->read_len;
        pthread_create(&tid[n_align_thread + j], &attr, IOworker, IO_param[j]);
      }

      for (j = 0; j < n_align_thread + 2; ++j)
        pthread_join(tid[j], 0);

      /*IO thread*/
      round++;

      if ((ret_tmp[0] | ret_tmp[1]) == 0)
        ReadIsGood = 0;
      else
        free(IO_param[0]);
      free(IO_param[1]);
      free(data);
      free(tid);
    }
#else

    for (int pair_idx = 0; pair_idx < 2; ++pair_idx) {
      bwa_cal_sa_reg_gap(0, bwt, n_seqs[pair_idx], seqs[pair_idx], opt,
                         &BwtIndex);
    }
    round++;
    if ((ret = bwa_read_seq_with_hash_dev(&BwtIndex, ks[0], READ_BUFFER_SIZE,
                                          &n_seqs_buff[0], opt->mode,
                                          opt->trim_qual, opt->frac, round,
                                          seqs_buff[0], opt->read_len)) != 0) {
      // ReadIsGood = 1;
      // FSC.NumRead += n_seqs_buff[0];
    } else
      ReadIsGood = 0;
    if ((ret = bwa_read_seq_with_hash_dev(&BwtIndex, ks[1], READ_BUFFER_SIZE,
                                          &n_seqs_buff[1], opt->mode,
                                          opt->trim_qual, opt->frac, round,
                                          seqs_buff[1], opt->read_len)) != 0) {
      // ReadIsGood = 1;
      // FSC.NumRead += n_seqs_buff[1];
    } else
      ReadIsGood = 0;
#endif
    cnt_chg = bwa_cal_pac_pos_pe(bwt, n_seqs[0], seqs, &ii, popt, opt, &last_ii,
                                 g_hash);

    if (pacseq == 0) // indexing path
    {
      /*pacseq = */
      pacseq = bwa_paired_sw(BwtIndex.bns, BwtIndex.pac_buf, n_seqs[0], seqs,
                             popt, &ii, opt->mode);
    } else {
      /*pacseq = */
      pacseq = bwa_paired_sw(BwtIndex.bns, pacseq, n_seqs[0], seqs, popt, &ii,
                             opt->mode);
    }

    for (j = 0; j < 2; ++j)
      bwa_refine_gapped(BwtIndex.bns, n_seqs[0], seqs[j], pacseq, ntbns);

    // if (pacseq!= 0) free(pacseq);

    if (!opt->out_bam) {
      for (i = 0; i < n_seqs[0]; ++i) {
        bwa_seq_t *p[2];
        p[0] = seqs[0] + i;
        p[1] = seqs[1] + i;

        FSC.NumBase += p[0]->full_len;
        FSC.NumBase += p[1]->full_len;
        if (p[0]->filtered && p[1]->filtered) {
          ++FSC.TotalFiltered;
          continue;
        }
        if ((p[0] == 0 || p[0]->type == BWA_TYPE_NO_MATCH) &&
            (p[1] == 0 || p[1]->type == BWA_TYPE_NO_MATCH)) {
          FSC.BwaUnmapped++;
          continue;
        }
        if (p[0]->bc[0] || p[1]->bc[0]) {
          strcat(p[0]->bc, p[1]->bc);
          strcpy(p[1]->bc, p[0]->bc);
        }

        FSC.TotalRetained += collector.AddAlignment(
            BwtIndex.bns, seqs[0] + i, seqs[1] + i, opt, fout, FSC.TotalMAPQ);
        bwa_print_sam1(BwtIndex.bns, p[0], p[1], opt->mode, opt->max_top2);
        bwa_print_sam1(BwtIndex.bns, p[1], p[0], opt->mode, opt->max_top2);
      }
    } else {
      for (i = 0; i < n_seqs[0]; ++i) {
        bwa_seq_t *p[2];
        p[0] = seqs[0] + i;
        p[1] = seqs[1] + i;

        FSC.NumBase += p[0]->full_len;
        FSC.NumBase += p[1]->full_len;

        if (p[0]->filtered && p[1]->filtered) {
          ++FSC.TotalFiltered;
          continue;
        }
        if ((p[0] == 0 || p[0]->type == BWA_TYPE_NO_MATCH) &&
            (p[1] == 0 || p[1]->type == BWA_TYPE_NO_MATCH)) {
          ++FSC.BwaUnmapped;
          continue;
        }
        if (p[0]->bc[0] || p[1]->bc[0]) {
          strcat(p[0]->bc, p[1]->bc);
          strcpy(p[1]->bc, p[0]->bc);
        }
        FSC.TotalRetained += collector.AddAlignment(
            BwtIndex.bns, seqs[0] + i, seqs[1] + i, opt, fout, FSC.TotalMAPQ);
        SamRecord SR[2];
        SetSamRecord(BwtIndex.bns, seqs[0] + i, seqs[1] + i, SFH, SR[0], opt);
        SetSamRecord(BwtIndex.bns, seqs[1] + i, seqs[0] + i, SFH, SR[1], opt);

        BamIO.writeRecord(BamFile, SFH, SR[0],
                          SamRecord::SequenceTranslation::NONE);
        BamIO.writeRecord(BamFile, SFH, SR[1],
                          SamRecord::SequenceTranslation::NONE);
      }
    }

    FSC.NumRead += (n_seqs[0] + n_seqs[1]);
    for (j = 0; j < 2; ++j) {
      // bwa_free_read_seq(n_seqs, seqs[j]);
      // delete[] seqs[j];
      bwa_clean_read_seq(n_seqs[j], seqs[j]);
      n_seqs[j] = n_seqs_buff[j];
      bwa_seq_t *tmp = seqs[j];
      seqs[j] = seqs_buff[j];
      seqs_buff[j] = tmp;
      bwa_clean_read_seq(n_seqs_buff[j], seqs_buff[j]);
    }
    if (FSC.NumRead % 5000000 == 0)
      fprintf(stderr, "NOTICE - %lld sequences are processed.\n", FSC.NumRead);

    last_ii = ii;
  } // end while

#ifdef __ctpl_stl_thread_pool_H__
  free(IO_param[0]);
  free(IO_param[1]);
  free(data);
#endif
  notice("%lld sequences are loaded.", FSC.NumRead);
  // fprintf(stderr, "NOTICE - %ld sequences are filtered by hash.\n",
  // FSC.HashFiltered);
  notice("%ld sequences are filtered.", FSC.TotalFiltered * 2);
  notice("%ld sequences are unmapped.", FSC.BwaUnmapped * 2);
  notice("%ld sequences are discarded of low mapQ.", FSC.TotalMAPQ);
  notice("%ld sequences are retained for QC.", FSC.TotalRetained);

  for (j = 0; j < 2; ++j) {
    bwa_free_read_seq(READ_BUFFER_SIZE, seqs[j]);
    bwa_free_read_seq(READ_BUFFER_SIZE, seqs_buff[j]);
  }
  //	free(seqs[0]);
  //	free(seqs[1]);
  //	free(seqs_buff[0]);
  //	free(seqs_buff[1]);
  if (pacseq)
    free(pacseq);
  // bns_destroy(bns);
  if (ntbns)
    bns_destroy(ntbns);

  for (i = 0; i < 2; ++i) {
    bwa_seq_close(ks[i]);
  }
  for (iter = kh_begin(g_hash); iter != kh_end(g_hash); ++iter)
    if (kh_exist(g_hash, iter))
      free(kh_val(g_hash, iter).a);
  kh_destroy(64, g_hash);
  return 0;
}

bool BwtMapper::PairEndMapper(BwtIndexer &BwtIndex, const pe_opt_t *popt,
                              gap_opt_t *opt, SamFileHeader &SFH,
                              BamInterface &BamIO, IFILE BamFile,
                              StatGenStatus &StatusTracker, std::ofstream &fout,
                              FileStatCollector &FSC) {

  int n_seqs[2] = {0, 0}, n_seqs_buff[2] = {0, 0};

  bwa_seq_t *seqs[2];
  bwa_seq_t *seqs_buff[2];
  bwa_seqio_t *ks[2];
  clock_t t;
  bwt_t *bwt[2];
  bntseq_t *ntbns = 0;
  khint_t iter;
  isize_info_t last_ii; // this is for the last batch of reads
  last_ii.avg = -1.0;

  // initialization
  bwase_initialize(); // initialize g_log_n[] in bwase.c

  srand48(BwtIndex.bns->seed);
  g_hash = kh_init(64);
  ks[0] = bwa_seq_open(FSC.FileName1.c_str());
  ks[1] = bwa_seq_open(FSC.FileName2.c_str());

  bwt[0] = BwtIndex.bwt_d;
  bwt[1] = BwtIndex.rbwt_d;

  ubyte_t *pacseq = 0;

  seqs[0] = (bwa_seq_t *)calloc(READ_BUFFER_SIZE, sizeof(bwa_seq_t));
  seqs[1] = (bwa_seq_t *)calloc(READ_BUFFER_SIZE, sizeof(bwa_seq_t));
  seqs_buff[0] = (bwa_seq_t *)calloc(READ_BUFFER_SIZE, sizeof(bwa_seq_t));
  seqs_buff[1] = (bwa_seq_t *)calloc(READ_BUFFER_SIZE, sizeof(bwa_seq_t));
  bwa_init_read_seq(READ_BUFFER_SIZE, seqs[0], opt);
  bwa_init_read_seq(READ_BUFFER_SIZE, seqs[1], opt);
  bwa_init_read_seq(READ_BUFFER_SIZE, seqs_buff[0], opt);
  bwa_init_read_seq(READ_BUFFER_SIZE, seqs_buff[1], opt);

  int ReadIsGood = 2;
  uint32_t round = 0;
  int ret(-1);
  // round thread number up
  int concurentThreadsSupported = std::thread::hardware_concurrency();
  int n_align_thread =
      (opt->n_threads <= concurentThreadsSupported ? opt->n_threads
                                                   : concurentThreadsSupported);
  if (n_align_thread < 4)
    n_align_thread = 4;

#ifdef HAVE_PTHREAD
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  thread_aux_t *data =
      (thread_aux_t *)calloc(n_align_thread, sizeof(thread_aux_t));
  for (int j = 0; j < n_align_thread; ++j) {
    data[j].tid = j;
    data[j].bwt[0] = bwt[0];
    data[j].bwt[1] = bwt[1];
    data[j].opt = opt;
    data[j].Indexer_Ptr = &BwtIndex;
  }

  int n_first_thread = n_align_thread / 2;
  int n_second_thread = n_align_thread - n_first_thread;

  thread_PE_t *pe_data =
      (thread_PE_t *)calloc(n_first_thread, sizeof(thread_PE_t));
  for (int k = 0; k < n_first_thread; ++k) {
    pe_data[k].ntbns = ntbns;
    pe_data[k].pacseq = pacseq;
    pe_data[k].popt = popt;
    pe_data[k].last_ii.avg = -1.0;
    pe_data[k].l_hash = kh_init(64);
  }

  thread_IO_t *IO_param[2];
  for (int j = 0; j < 2; ++j) {
    IO_param[j] = (thread_IO_t *)calloc(1, sizeof(thread_IO_t));
    IO_param[j]->BwtIndex = &BwtIndex;
    IO_param[j]->mode = opt->mode;
    IO_param[j]->trim_qual = opt->trim_qual;
    IO_param[j]->frac = opt->frac;
    IO_param[j]->read_len = opt->read_len;
  }

#ifdef __ctpl_stl_thread_pool_H__
  ctpl::thread_pool p(n_align_thread);
  std::vector<std::future<void>> results(n_align_thread);
#endif
#endif

  while (ReadIsGood) { // opt should be different for two fa files theoretically
    if (ReadIsGood == 2) {
      if ((ret = bwa_read_seq_with_hash_dev(&BwtIndex, ks[0], READ_BUFFER_SIZE,
                                            &n_seqs[0], opt->mode,
                                            opt->trim_qual, opt->frac, round,
                                            seqs[0], opt->read_len)) != 0) {
        ReadIsGood = 1;
      } else
        ReadIsGood = 0;

      if ((ret = bwa_read_seq_with_hash_dev(&BwtIndex, ks[1], READ_BUFFER_SIZE,
                                            &n_seqs[1], opt->mode,
                                            opt->trim_qual, opt->frac, round,
                                            seqs[1], opt->read_len)) != 0) {
      } else
        ReadIsGood = 0;
    }

    int cnt_chg;
    isize_info_t ii;

#ifdef HAVE_PTHREAD
    //    if (opt->n_threads <= 1) { // no multi-threading at all
    //      for (int pair_idx = 0; pair_idx < 2; ++pair_idx) {
    //        bwa_cal_sa_reg_gap(0, bwt, n_seqs[pair_idx], seqs[pair_idx], opt,
    //                           &BwtIndex);
    //      }
    //      round++;
    //      if ((ret = bwa_read_seq_with_hash_dev(
    //               &BwtIndex, ks[0], READ_BUFFER_SIZE, &n_seqs_buff[0],
    //               opt->mode, opt->trim_qual, opt->frac, round, seqs_buff[0],
    //               opt->read_len)) != 0) {
    //      } else
    //        ReadIsGood = 0;
    //      if ((ret = bwa_read_seq_with_hash_dev(
    //               &BwtIndex, ks[1], READ_BUFFER_SIZE, &n_seqs_buff[1],
    //               opt->mode, opt->trim_qual, opt->frac, round, seqs_buff[1],
    //               opt->read_len)) != 0) {
    //      } else
    //        ReadIsGood = 0;
    //    } else
    {
      size_t grain_size = n_seqs[0] / n_first_thread;
      for (int j = 0; j < n_first_thread; ++j) {
        if (j == n_first_thread - 1)
          data[j].n_seqs = n_seqs[0] - grain_size * (n_first_thread - 1);
        else
          data[j].n_seqs = grain_size;
        data[j].seqs = seqs[0] + j * grain_size;
        results[j] = p.push(workerAlt, data + j);
      }
      for (int j = n_first_thread; j < n_align_thread; ++j) {
        if (j == n_align_thread - 1)
          data[j].n_seqs = n_seqs[1] - grain_size * (n_second_thread - 1);
        else
          data[j].n_seqs = grain_size;
        data[j].seqs = seqs[1] + (j - n_first_thread) * grain_size;
        results[j] = p.push(workerAlt, data + j);
      }

      for (int j = 0; j < n_align_thread; ++j)
        results[j].get();

      p.clear_queue();

      //      int n_pe_threads = n_first_thread;
      //      for (int j = 0; j < n_pe_threads; ++j) {
      //        pe_data[j].aux1 = &(data[j]);
      //        pe_data[j].aux2 = &(data[n_first_thread+j]);
      //        results[j] = p.push(PEworker, pe_data + j);
      //      }

      int n_pe_threads = 1;
      for (int j = 0; j < n_pe_threads; ++j) {
        data[j].n_seqs = n_seqs[0];
        pe_data[j].aux1 = &(data[j]);
        data[n_first_thread + j].n_seqs = n_seqs[1];
        pe_data[j].aux2 = &(data[n_first_thread + j]);
        results[j] = p.push(PEworker, pe_data + j);
      }

      int ret_tmp[2] = {-1, -1};
      for (int j = 0; j < 2; ++j) {
        IO_param[j]->ksAddress = ks[j];
        IO_param[j]->n_seqs = &n_seqs_buff[j];
        IO_param[j]->round = round;
        IO_param[j]->seqAddress = seqs_buff[j];
        IO_param[j]->ret = &ret_tmp[j];
        results[n_pe_threads + j] = p.push(IOworkerAlt, IO_param[j]);
      }
      for (int j = 0; j < n_pe_threads + 2; ++j)
        results[j].get();

      /*IO thread*/
      round++;
      if ((ret_tmp[0] | ret_tmp[1]) == 0)
        ReadIsGood = 0;
    }
#else
    for (int pair_idx = 0; pair_idx < 2; ++pair_idx) {
      bwa_cal_sa_reg_gap(0, bwt, n_seqs[pair_idx], seqs[pair_idx], opt,
                         &BwtIndex);
    }
    round++;
    if ((ret = bwa_read_seq_with_hash_dev(&BwtIndex, ks[0], READ_BUFFER_SIZE,
                                          &n_seqs_buff[0], opt->mode,
                                          opt->trim_qual, opt->frac, round,
                                          seqs_buff[0], opt->read_len)) != 0) {
    } else
      ReadIsGood = 0;
    if ((ret = bwa_read_seq_with_hash_dev(&BwtIndex, ks[1], READ_BUFFER_SIZE,
                                          &n_seqs_buff[1], opt->mode,
                                          opt->trim_qual, opt->frac, round,
                                          seqs_buff[1], opt->read_len)) != 0) {
    } else
      ReadIsGood = 0;

    cnt_chg = bwa_cal_pac_pos_pe(bwt, n_seqs[0], seqs, &ii, popt, opt, &last_ii,
                                 g_hash);

    if (pacseq == 0) // indexing path
    {
      /*pacseq = */
      pacseq = bwa_paired_sw(BwtIndex.bns, BwtIndex.pac_buf, n_seqs[0], seqs,
                             popt, &ii, opt->mode);
    } else {
      /*pacseq = */
      pacseq = bwa_paired_sw(BwtIndex.bns, pacseq, n_seqs[0], seqs, popt, &ii,
                             opt->mode);
    }

    for (int j = 0; j < 2; ++j)
      bwa_refine_gapped(BwtIndex.bns, n_seqs[0], seqs[j], pacseq, ntbns);
#endif

    if (!opt->out_bam) {
      for (int i = 0; i < n_seqs[0]; ++i) {
        bwa_seq_t *p[2];
        p[0] = seqs[0] + i;
        p[1] = seqs[1] + i;

        FSC.NumBase += p[0]->full_len;
        FSC.NumBase += p[1]->full_len;
        if (p[0]->filtered && p[1]->filtered) {
          ++FSC.TotalFiltered;
          continue;
        }
        if ((p[0] == 0 || p[0]->type == BWA_TYPE_NO_MATCH) &&
            (p[1] == 0 || p[1]->type == BWA_TYPE_NO_MATCH)) {
          FSC.BwaUnmapped++;
          continue;
        }
        if (p[0]->bc[0] || p[1]->bc[0]) {
          strcat(p[0]->bc, p[1]->bc);
          strcpy(p[1]->bc, p[0]->bc);
        }

        FSC.TotalRetained += collector.AddAlignment(
            BwtIndex.bns, seqs[0] + i, seqs[1] + i, opt, fout, FSC.TotalMAPQ);
        bwa_print_sam1(BwtIndex.bns, p[0], p[1], opt->mode, opt->max_top2);
        bwa_print_sam1(BwtIndex.bns, p[1], p[0], opt->mode, opt->max_top2);
      }
    } else
      for (int i = 0; i < n_seqs[0]; ++i) {
        bwa_seq_t *p[2];
        p[0] = seqs[0] + i;
        p[1] = seqs[1] + i;

        FSC.NumBase += p[0]->full_len;
        FSC.NumBase += p[1]->full_len;

        if (p[0]->filtered && p[1]->filtered) {
          ++FSC.TotalFiltered;
          continue;
        }
        if ((p[0] == 0 || p[0]->type == BWA_TYPE_NO_MATCH) &&
            (p[1] == 0 || p[1]->type == BWA_TYPE_NO_MATCH)) {
          ++FSC.BwaUnmapped;
          continue;
        }
        if (p[0]->bc[0] || p[1]->bc[0]) {
          strcat(p[0]->bc, p[1]->bc);
          strcpy(p[1]->bc, p[0]->bc);
        }
        FSC.TotalRetained += collector.AddAlignment(
            BwtIndex.bns, seqs[0] + i, seqs[1] + i, opt, fout, FSC.TotalMAPQ);
        SamRecord SR[2];
        SetSamRecord(BwtIndex.bns, seqs[0] + i, seqs[1] + i, SFH, SR[0], opt);
        SetSamRecord(BwtIndex.bns, seqs[1] + i, seqs[0] + i, SFH, SR[1], opt);

        BamIO.writeRecord(BamFile, SFH, SR[0],
                          SamRecord::SequenceTranslation::NONE);
        BamIO.writeRecord(BamFile, SFH, SR[1],
                          SamRecord::SequenceTranslation::NONE);
      }

    FSC.NumRead += (n_seqs[0] + n_seqs[1]);
    if (FSC.NumRead % READ_BUFFER_SIZE == 0) {
      if (std::strncmp(seqs[0]->name, seqs[1]->name, opt->read_len) != 0)
        error("Abort, please make sure input pair of fastq files are in the "
              "same order!");
      fprintf(stderr, "NOTICE - %lld sequences are processed.\n", FSC.NumRead);
    }

    for (int j = 0; j < 2; ++j) {
      // bwa_free_read_seq(n_seqs, seqs[j]);
      // delete[] seqs[j];
      bwa_clean_read_seq(n_seqs[j], seqs[j]);
      n_seqs[j] = n_seqs_buff[j];
      bwa_seq_t *tmp = seqs[j];
      seqs[j] = seqs_buff[j];
      seqs_buff[j] = tmp;
      bwa_clean_read_seq(n_seqs_buff[j], seqs_buff[j]);
    }

    last_ii = ii;
  } // end while

#ifdef HAVE_PTHREAD
  free(IO_param[0]);
  free(IO_param[1]);
  free(data);
  free(pe_data);
#endif

  notice("%lld sequences are loaded.", FSC.NumRead);
  // fprintf(stderr, "NOTICE - %ld sequences are filtered by hash.\n",
  // FSC.HashFiltered);
  notice("%ld sequences are filtered.", FSC.TotalFiltered * 2);
  notice("%ld sequences are unmapped.", FSC.BwaUnmapped * 2);
  notice("%ld sequences are discarded of low mapQ.", FSC.TotalMAPQ);
  notice("%ld sequences are retained for QC.", FSC.TotalRetained);

  for (int j = 0; j < 2; ++j) {
    bwa_free_read_seq(READ_BUFFER_SIZE, seqs[j]);
    bwa_free_read_seq(READ_BUFFER_SIZE, seqs_buff[j]);
  }

  if (pacseq)
    free(pacseq);
  // bns_destroy(bns);
  if (ntbns)
    bns_destroy(ntbns);

  for (int i = 0; i < 2; ++i) {
    bwa_seq_close(ks[i]);
  }
  for (iter = kh_begin(g_hash); iter != kh_end(g_hash); ++iter)
    if (kh_exist(g_hash, iter))
      free(kh_val(g_hash, iter).a);
  kh_destroy(64, g_hash);
  return 0;
}

BwtMapper::~BwtMapper() {
  if (bwa_rg_line)
    free(bwa_rg_line);
  if (bwa_rg_id)
    free(bwa_rg_id);
}
