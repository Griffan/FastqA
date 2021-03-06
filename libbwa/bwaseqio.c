#include <zlib.h>
#include <ctype.h>
#include "bwtaln.h"
#include "utils.h"
#include "bamlite.h"

#include "kseq.h"
KSEQ_INIT(gzFile, gzread)

extern unsigned char nst_nt4_table[256];
static char bam_nt16_nt4_table[] = { 4, 0, 1, 4, 2, 4, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4 };

struct __bwa_seqio_t {
	// for BAM input
	int is_bam, which; // 1st bit: read1, 2nd bit: read2, 3rd: SE
	bamFile fp;
	// for fastq input
	kseq_t *ks;
};

bwa_seqio_t *bwa_bam_open(const char *fn, int which)
{
	bwa_seqio_t *bs;
	bam_header_t *h;
	bs = (bwa_seqio_t*)calloc(1, sizeof(bwa_seqio_t));
	bs->is_bam = 1;
	bs->which = which;
	bs->fp = bam_open(fn, "r");
	h = bam_header_read(bs->fp);
	bam_header_destroy(h);
	return bs;
}

bwa_seqio_t *bwa_seq_open(const char *fn)
{
	gzFile fp;
	bwa_seqio_t *bs;
	bs = (bwa_seqio_t*)calloc(1, sizeof(bwa_seqio_t));
	fp = xzopen(fn, "r");
	bs->ks = kseq_init(fp);
	return bs;
}

void bwa_seq_close(bwa_seqio_t *bs)
{
	if (bs == 0) return;
	if (bs->is_bam) bam_close(bs->fp);
	else {
		gzclose(bs->ks->f->f);
		kseq_destroy(bs->ks);
	}
	free(bs);
}

void seq_reverse(int len, ubyte_t *seq, int is_comp)
{
	int i;
	if (is_comp) {
		for (i = 0; i < len >> 1; ++i) {
			char tmp = seq[len - 1 - i];
			if (tmp < 4) tmp = 3 - tmp;
			seq[len - 1 - i] = (seq[i] >= 4) ? seq[i] : 3 - seq[i];
			seq[i] = tmp;
		}
		if (len & 1) seq[i] = (seq[i] >= 4) ? seq[i] : 3 - seq[i];
	}
	else {
		for (i = 0; i < len >> 1; ++i) {
			char tmp = seq[len - 1 - i];
			seq[len - 1 - i] = seq[i]; seq[i] = tmp;
		}
	}
}

int bwa_trim_read(int trim_qual, bwa_seq_t *p)
{
	int s = 0, l, max = 0, max_l = p->len - 1;
	if (trim_qual < 1 || p->qual == 0) return 0;
	for (l = p->len - 1; l >= BWA_MIN_RDLEN - 1; --l) {
		s += trim_qual - (p->qual[l] - 33);
		if (s < 0) break;
		if (s > max) {
			max = s; max_l = l;
		}
	}
	p->clip_len = p->len = max_l + 1;
	return p->full_len - p->len;
}

bwa_seq_t *bwa_read_bam(bwa_seqio_t *bs, int n_needed, int *n, int is_comp, int trim_qual)
{
	bwa_seq_t *seqs, *p;
	int n_seqs, l, i;
	long n_trimmed = 0, n_tot = 0;
	bam1_t *b;

	b = bam_init1();
	n_seqs = 0;
	seqs = (bwa_seq_t*)calloc(n_needed, sizeof(bwa_seq_t));
	while (bam_read2(bs->fp, b) >= 0) {
		uint8_t *s, *q;
		int go = 0;
		if ((bs->which & 1) && (b->core.flag & BAM_FREAD1)) go = 1;
		if ((bs->which & 2) && (b->core.flag & BAM_FREAD2)) go = 1;
		if ((bs->which & 4) && !(b->core.flag& BAM_FREAD1) && !(b->core.flag& BAM_FREAD2))go = 1;
		if (go == 0) continue;
		l = b->core.l_qseq;
		p = &seqs[n_seqs++];
		p->tid = -1; // no assigned to a thread
		p->qual = 0;
		p->full_len = p->clip_len = p->len = l;
		n_tot += p->full_len;
		s = bam1_seq(b); q = bam1_qual(b);
		p->seq = (ubyte_t*)calloc(p->len + 1, 1);
		p->qual = (ubyte_t*)calloc(p->len + 1, 1);
		for (i = 0; i != p->full_len; ++i) {
			p->seq[i] = bam_nt16_nt4_table[(int)bam1_seqi(s, i)];
			p->qual[i] = q[i] + 33 < 126 ? q[i] + 33 : 126;
		}
		if (bam1_strand(b)) { // then reverse
			seq_reverse(p->len, p->seq, 1);
			seq_reverse(p->len, p->qual, 0);
		}
		if (trim_qual >= 1) n_trimmed += bwa_trim_read(trim_qual, p);
		p->rseq = (ubyte_t*)calloc(p->full_len, 1);
		memcpy(p->rseq, p->seq, p->len);
		seq_reverse(p->len, p->seq, 0); // *IMPORTANT*: will be reversed back in bwa_refine_gapped()
		seq_reverse(p->len, p->rseq, is_comp);
		p->name = strdup((const char*)bam1_qname(b));
		if (n_seqs == n_needed) break;
	}
	*n = n_seqs;
	if (n_seqs && trim_qual >= 1)
		fprintf(stderr, "[bwa_read_seq] %.1f%% bases are trimmed.\n", 100.0f * n_trimmed / n_tot);
	if (n_seqs == 0) {
		free(seqs);
		bam_destroy1(b);
		return 0;
	}
	bam_destroy1(b);
	return seqs;
}

#define BARCODE_LOW_QUAL 13

bwa_seq_t *bwa_read_seq(bwa_seqio_t *bs, int n_needed, int *n, int mode, int trim_qual, double frac)
{
	bwa_seq_t *seqs, *p;
	kseq_t *seq = bs->ks;
	int n_seqs, l, i, is_comp = mode&BWA_MODE_COMPREAD, is_64 = mode&BWA_MODE_IL13, l_bc = mode >> 24;
	long n_trimmed = 0, n_tot = 0;

	if (l_bc > 15) {
		fprintf(stderr, "[%s] the maximum barcode length is 15.\n", __func__);
		return 0;
	}
	if (bs->is_bam) return bwa_read_bam(bs, n_needed, n, is_comp, trim_qual); // l_bc has no effect for BAM input
	n_seqs = 0;
	seqs = (bwa_seq_t*)calloc(n_needed, sizeof(bwa_seq_t));
	while ((l = kseq_read(seq)) >= 0) {

		if (seq->seq.l <= l_bc) continue; // sequence length equals or smaller than the barcode length
		p = &seqs[n_seqs++];

		if (l_bc) { // then trim barcode
			for (i = 0; i < l_bc; ++i)
				p->bc[i] = (seq->qual.l && seq->qual.s[i] - 33 < BARCODE_LOW_QUAL) ? tolower(seq->seq.s[i]) : toupper(seq->seq.s[i]);
			p->bc[i] = 0;
			for (; i < seq->seq.l; ++i)
				seq->seq.s[i - l_bc] = seq->seq.s[i];
			seq->seq.l -= l_bc; seq->seq.s[seq->seq.l] = 0;
			if (seq->qual.l) {
				for (i = l_bc; i < seq->qual.l; ++i)
					seq->qual.s[i - l_bc] = seq->qual.s[i];
				seq->qual.l -= l_bc; seq->qual.s[seq->qual.l] = 0;
			}
			l = seq->seq.l;
		}
		else p->bc[0] = 0;
		p->tid = -1; // no assigned to a thread
		p->qual = 0;
		//p->count=0;
		p->full_len = p->clip_len = p->len = l;
		n_tot += p->full_len;
		//if (drand48() >frac)
		//{
		//	p->filtered |= 1;
		//	if (n_seqs == n_needed) break;
		//	else
		//		continue;// downsampling
		//}
		p->seq = (ubyte_t*)calloc(p->len, 1);
		/*
		p->original_seq =(char*)calloc(p->len, 1);
		strcpy(p->original_seq,seq->seq.s);
		*/
		for (i = 0; i != p->full_len; ++i)
			p->seq[i] = nst_nt4_table[(int)seq->seq.s[i]];
		if (seq->qual.l) { // copy quality
			if (is_64 && seq->qual.l)
				for (i = 0; i < seq->qual.l; ++i) seq->qual.s[i] -= 31;
			p->qual = (ubyte_t*)strdup((char*)seq->qual.s);
			if (trim_qual >= 1) n_trimmed += bwa_trim_read(trim_qual, p);
		}
		p->rseq = (ubyte_t*)calloc(p->full_len, 1);
		memcpy(p->rseq, p->seq, p->len);
		//fprintf(stderr, "I have been here: %d times!\n",i);
		seq_reverse(p->len, p->seq, 0); // *IMPORTANT*: will be reversed back in bwa_refine_gapped()
		seq_reverse(p->len, p->rseq, is_comp);
		p->name = strdup((const char*)seq->name.s);
		{ // trim /[12]$
			int t = strlen(p->name);
			if (t > 2 && p->name[t - 2] == '/' && (p->name[t - 1] == '1' || p->name[t - 1] == '2')) p->name[t - 2] = '\0';
		}
		if (n_seqs == n_needed) break;
	}
	*n = n_seqs;
	if (n_seqs && trim_qual >= 1)
		fprintf(stderr, "[bwa_read_seq] %.1f%% bases are trimmed.\n", 100.0f * n_trimmed / n_tot);
	if (n_seqs == 0) {
		free(seqs);
		return 0;
	}
	return seqs;
}
void bwa_init_read_seq(int n_seqs, bwa_seq_t * seqs, const gap_opt_t * opt)
{
	bwa_seq_t* p;
	int i = 0;
	for (i = 0; i != n_seqs; ++i)
	{
		p = &seqs[i];
		p->name =(char*) calloc(opt->read_len * 2,1);
		p->seq = (ubyte_t*)calloc(opt->read_len, 1);
		p->rseq = (ubyte_t*)calloc(opt->read_len, 1);
		p->qual = (ubyte_t*)calloc(opt->read_len, 1);
		p->len = 0;
		p->strand = 0;
		p->type = 0;
		p->filtered = 0;
		p->extra_flag = 0;
		p->n_mm = 0;
		p->n_gape = 0;
		p->n_gapo = 0;
		p->mapQ = 0;
		p->score = 0;
		p->clip_len = 0;
		p->n_aln = 0;
		p->aln = 0;
		p->n_multi = 0;
		p->multi = 0;
		p->sa = 0;
		p->pos = 0;
		p->c1 = 0;
		p->c2 = 0;
		p->seQ = 0;
		p->n_cigar = 0;
		p->cigar = 0;
		p->tid = -1;
		p->full_len = 0;
		p->nm = 0;
		p->md = 0;
	}
}

void bwa_clean_read_seq(int n_seqs, bwa_seq_t *seqs)
{
	int i, j;
	for (i = 0; i != n_seqs; ++i) {
		bwa_seq_t *p = seqs + i;
		for (j = 0; j < p->n_multi; ++j)
			if (p->multi[j].cigar) free(p->multi[j].cigar);

			//we don't delete space of original reads
//          free(p->name);
//          free(p->seq); free(p->qual);
//          free(p->rseq);

		if (p->aln)
		free(p->aln);

		//fprintf(stderr, "md:%x\n%s\ndelete 3\n",p->md,p->md);
		if (p->md)
		free(p->md);

		//fprintf(stderr, "delete 4\n");
		if (p->multi)
		free(p->multi);

		//fprintf(stderr, "delete 5\n");
		if (p->cigar)
		free(p->cigar);

		p->len = 0;
		p->strand = 0;
		p->type = 0;
		p->filtered = 0;
		p->extra_flag = 0;
		p->n_mm = 0;
		p->n_gape = 0;
		p->n_gapo = 0;
		p->mapQ = 0;
		p->score = 0;
		p->clip_len = 0;
		p->n_aln = 0;
		p->aln = 0;
		p->n_multi = 0;
		p->multi = 0;
		p->sa = 0;
		p->pos = 0;
		p->c1 = 0;
		p->c2 = 0;
		p->seQ = 0;
		p->n_cigar = 0;
		p->cigar = 0;
		p->tid = -1;
		p->full_len = 0;
		p->nm = 0;
		p->md = 0;
	}
	//free(seqs);
}
void bwa_free_read_seq(int n_seqs, bwa_seq_t *seqs)//retire soon, keep it because of potential usage of bwa_read_seq_with_hash
{
	int i, j;
	for (i = 0; i != n_seqs; ++i) {
		bwa_seq_t *p = seqs + i;
		for (j = 0; j < p->n_multi; ++j)
			if (p->multi[j].cigar) free(p->multi[j].cigar);
		free(p->name);
		free(p->seq); free(p->qual);
		//if (p->rseq&&p->filtered) { fprintf(stderr, "%s\n%s\n%x\n%s\nstrange thing happend!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n", (char*)p->seq,(char*)p->qual, p->rseq, p->rseq); }
		free(p->rseq);
		if (p->aln)
		free(p->aln);

		//fprintf(stderr, "md:%x\n%s\ndelete 3\n",p->md,p->md);
		if (p->md)
		free(p->md);

		//fprintf(stderr, "delete 4\n");
		if (p->multi)
		free(p->multi);

		//fprintf(stderr, "delete 5\n");
		if (p->cigar)
		free(p->cigar);
	}
	free(seqs);
}
