// Cycle table for Falcon instructions
// All cycles are given for 4 cycles bus

// Head, Tail, I-Cache Case (r/p/w), No-Cache Case (r/p/w), Instruction


struct table_falcon_cycles_t {
	int head;
	int tail;
	int cache_cycles;
	int cache_cycles_r; 
	int cache_cycles_p; 
	int cache_cycles_w; 
	int noncache_cycles;
	int noncache_cycles_r; 
	int noncache_cycles_p; 
	int noncache_cycles_w; 
};

struct table_falcon_cycles_t table_falcon_cycles [] = {
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// ORI.B #<data>.W,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// ORI.B #<data>.W,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// ORI.B #<data>.W,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// ORI.B #<data>.W,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// ORI.B #<data>.W,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ORI.B #<data>.W,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// ORI.B #<data>.W,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// ORI.B #<data>.W,(xxx).L
	{4,	0,	12,0,0,0,	18,0,2,0},	// ORI.B #<data>.W, SR
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// ORI.W #<data>.W,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// ORI.W #<data>.W,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// ORI.W #<data>.W,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// ORI.W #<data>.W,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// ORI.W #<data>.W,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ORI.W #<data>.W,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// ORI.W #<data>.W,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// ORI.W #<data>.W,(xxx).L
	{4,	0,	12,0,0,0,	18,0,2,0},	// ORI.W #<data>.W, SR
	{4,	0,	 6,0,0,0,	10,0,2,0},	// ORI.L #<data>.L,Dn
	{1,	1,	11,1,0,1,	17,1,2,1},	// ORI.L #<data>.L,(An)
	{4,	2,	14,1,0,1,	19,1,2,1},	// ORI.L #<data>.L,(An)+
	{2,	1,	11,1,0,1,	17,1,2,1},	// ORI.L #<data>.L,-(An)
	{4,	1,	13,1,0,1,	22,1,3,1},	// ORI.L #<data>.L,(d16,An)
	{8,	3,	17,1,0,1,	24,1,3,1},	// ORI.L #<data>.L,(d8,An,Xn)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ORI.L #<data>.L,(xxx).W
	{5,	1,	15,1,0,1,	23,1,3,1},	// ORI.L #<data>.L,(xxx).L
	{},	// CHK2.B #<data>.W,(An)
	{},	// CHK2.B #<data>.W,(d16,An)
	{},	// CHK2.B #<data>.W,(d8,An,Xn)
	{},	// CHK2.B #<data>.W,(xxx).W
	{},	// CHK2.B #<data>.W,(xxx).L
	{},	// CHK2.B #<data>.W,(d16,PC)
	{},	// CHK2.B #<data>.W,(d8,PC,Xn)
	{},	// BTST.L Dn,Dn
	{},	// MVPMR.W (d16,An),Dn
	{},	// BTST.B Dn,(An)
	{},	// BTST.B Dn,(An)+
	{},	// BTST.B Dn,-(An)
	{},	// BTST.B Dn,(d16,An)
	{},	// BTST.B Dn,(d8,An,Xn)
	{},	// BTST.B Dn,(xxx).W
	{},	// BTST.B Dn,(xxx).L
	{},	// BTST.B Dn,(d16,PC)
	{},	// BTST.B Dn,(d8,PC,Xn)
	{},	// BTST.B Dn,#<data>.B
	{},	// BCHG.L Dn,Dn
	{},	// MVPMR.L (d16,An),Dn
	{},	// BCHG.B Dn,(An)
	{},	// BCHG.B Dn,(An)+
	{},	// BCHG.B Dn,-(An)
	{},	// BCHG.B Dn,(d16,An)
	{},	// BCHG.B Dn,(d8,An,Xn)
	{},	// BCHG.B Dn,(xxx).W
	{},	// BCHG.B Dn,(xxx).L
	{},	// BCHG.B Dn,(d16,PC)
	{},	// BCHG.B Dn,(d8,PC,Xn)
	{},	// BCLR.L Dn,Dn
	{},	// MVPRM.W Dn,(d16,An)
	{},	// BCLR.B Dn,(An)
	{},	// BCLR.B Dn,(An)+
	{},	// BCLR.B Dn,-(An)
	{},	// BCLR.B Dn,(d16,An)
	{},	// BCLR.B Dn,(d8,An,Xn)
	{},	// BCLR.B Dn,(xxx).W
	{},	// BCLR.B Dn,(xxx).L
	{},	// BCLR.B Dn,(d16,PC)
	{},	// BCLR.B Dn,(d8,PC,Xn)
	{},	// BSET.L Dn,Dn
	{},	// MVPRM.L Dn,(d16,An)
	{},	// BSET.B Dn,(An)
	{},	// BSET.B Dn,(An)+
	{},	// BSET.B Dn,-(An)
	{},	// BSET.B Dn,(d16,An)
	{},	// BSET.B Dn,(d8,An,Xn)
	{},	// BSET.B Dn,(xxx).W
	{},	// BSET.B Dn,(xxx).L
	{},	// BSET.B Dn,(d16,PC)
	{},	// BSET.B Dn,(d8,PC,Xn)
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// ANDI.B #<data>.W,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// ANDI.B #<data>.W,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// ANDI.B #<data>.W,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// ANDI.B #<data>.W,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// ANDI.B #<data>.W,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ANDI.B #<data>.W,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// ANDI.B #<data>.W,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// ANDI.B #<data>.W,(xxx).L
	{4,	0,	12,0,0,0,	18,0,2,0},	// ANDI.B #<data>.W, SR
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// ANDI.W #<data>.W,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// ANDI.W #<data>.W,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// ANDI.W #<data>.W,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// ANDI.W #<data>.W,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// ANDI.W #<data>.W,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ANDI.W #<data>.W,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// ANDI.W #<data>.W,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// ANDI.W #<data>.W,(xxx).L
	{4,	0,	12,0,0,0,	18,0,2,0},	// ANDI.W #<data>.W, SR
	{4,	0,	 6,0,0,0,	10,0,2,0},	// ANDI.L #<data>.L,Dn
	{1,	1,	11,1,0,1,	17,1,2,1},	// ANDI.L #<data>.L,(An)
	{4,	2,	14,1,0,1,	19,1,2,1},	// ANDI.L #<data>.L,(An)+
	{2,	1,	11,1,0,1,	17,1,2,1},	// ANDI.L #<data>.L,-(An)
	{4,	1,	13,1,0,1,	22,1,3,1},	// ANDI.L #<data>.L,(d16,An)
	{8,	3,	17,1,0,1,	24,1,3,1},	// ANDI.L #<data>.L,(d8,An,Xn)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ANDI.L #<data>.L,(xxx).W
	{5,	1,	15,1,0,1,	23,1,3,1},	// ANDI.L #<data>.L,(xxx).L
	{},	// CHK2.W #<data>.W,(An)
	{},	// CHK2.W #<data>.W,(d16,An)
	{},	// CHK2.W #<data>.W,(d8,An,Xn)
	{},	// CHK2.W #<data>.W,(xxx).W
	{},	// CHK2.W #<data>.W,(xxx).L
	{},	// CHK2.W #<data>.W,(d16,PC)
	{},	// CHK2.W #<data>.W,(d8,PC,Xn)
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// SUBI.B #<data>.W,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// SUBI.B #<data>.W,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// SUBI.B #<data>.W,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// SUBI.B #<data>.W,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// SUBI.B #<data>.W,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// SUBI.B #<data>.W,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// SUBI.B #<data>.W,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// SUBI.B #<data>.W,(xxx).L
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// SUBI.W #<data>.W,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// SUBI.W #<data>.W,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// SUBI.W #<data>.W,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// SUBI.W #<data>.W,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// SUBI.W #<data>.W,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// SUBI.W #<data>.W,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// SUBI.W #<data>.W,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// SUBI.W #<data>.W,(xxx).L
	{4,	0,	 6,0,0,0,	10,0,2,0},	// SUBI.L #<data>.L,Dn
	{1,	1,	11,1,0,1,	17,1,2,1},	// SUBI.L #<data>.L,(An)
	{4,	2,	14,1,0,1,	19,1,2,1},	// SUBI.L #<data>.L,(An)+
	{2,	1,	11,1,0,1,	17,1,2,1},	// SUBI.L #<data>.L,-(An)
	{4,	1,	13,1,0,1,	22,1,3,1},	// SUBI.L #<data>.L,(d16,An)
	{8,	3,	17,1,0,1,	24,1,3,1},	// SUBI.L #<data>.L,(d8,An,Xn)
	{6,	3,	15,1,0,1,	22,1,3,1},	// SUBI.L #<data>.L,(xxx).W
	{5,	1,	15,1,0,1,	23,1,3,1},	// SUBI.L #<data>.L,(xxx).L
	{},	// CHK2.L #<data>.W,(An)
	{},	// CHK2.L #<data>.W,(d16,An)
	{},	// CHK2.L #<data>.W,(d8,An,Xn)
	{},	// CHK2.L #<data>.W,(xxx).W
	{},	// CHK2.L #<data>.W,(xxx).L
	{},	// CHK2.L #<data>.W,(d16,PC)
	{},	// CHK2.L #<data>.W,(d8,PC,Xn)
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// ADDI.B #<data>.W,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// ADDI.B #<data>.W,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// ADDI.B #<data>.W,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// ADDI.B #<data>.W,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// ADDI.B #<data>.W,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ADDI.B #<data>.W,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// ADDI.B #<data>.W,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// ADDI.B #<data>.W,(xxx).L
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// ADDI.W #<data>.W,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// ADDI.W #<data>.W,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// ADDI.W #<data>.W,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// ADDI.W #<data>.W,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// ADDI.W #<data>.W,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ADDI.W #<data>.W,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// ADDI.W #<data>.W,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// ADDI.W #<data>.W,(xxx).L
	{4,	0,	 6,0,0,0,	10,0,2,0},	// ADDI.L #<data>.L,Dn
	{1,	1,	11,1,0,1,	17,1,2,1},	// ADDI.L #<data>.L,(An)
	{4,	2,	14,1,0,1,	19,1,2,1},	// ADDI.L #<data>.L,(An)+
	{2,	1,	11,1,0,1,	17,1,2,1},	// ADDI.L #<data>.L,-(An)
	{4,	1,	13,1,0,1,	22,1,3,1},	// ADDI.L #<data>.L,(d16,An)
	{8,	3,	17,1,0,1,	24,1,3,1},	// ADDI.L #<data>.L,(d8,An,Xn)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ADDI.L #<data>.L,(xxx).W
	{5,	1,	15,1,0,1,	23,1,3,1},	// ADDI.L #<data>.L,(xxx).L
	{},	// RTM.L Dn
	{},	// RTM.L An
	{},	// CALLM.L (An)
	{},	// CALLM.L (d16,An)
	{},	// CALLM.L (d8,An,Xn)
	{},	// CALLM.L (xxx).W
	{},	// CALLM.L (xxx).L
	{},	// CALLM.L (d16,PC)
	{},	// CALLM.L (d8,PC,Xn)
	{},	// BTST.L #<data>.W,Dn
	{},	// BTST.B #<data>.W,(An)
	{},	// BTST.B #<data>.W,(An)+
	{},	// BTST.B #<data>.W,-(An)
	{},	// BTST.B #<data>.W,(d16,An)
	{},	// BTST.B #<data>.W,(d8,An,Xn)
	{},	// BTST.B #<data>.W,(xxx).W
	{},	// BTST.B #<data>.W,(xxx).L
	{},	// BTST.B #<data>.W,(d16,PC)
	{},	// BTST.B #<data>.W,(d8,PC,Xn)
	{},	// BTST.B #<data>.W,#<data>.B
	{},	// BCHG.L #<data>.W,Dn
	{},	// BCHG.B #<data>.W,(An)
	{},	// BCHG.B #<data>.W,(An)+
	{},	// BCHG.B #<data>.W,-(An)
	{},	// BCHG.B #<data>.W,(d16,An)
	{},	// BCHG.B #<data>.W,(d8,An,Xn)
	{},	// BCHG.B #<data>.W,(xxx).W
	{},	// BCHG.B #<data>.W,(xxx).L
	{},	// BCHG.B #<data>.W,(d16,PC)
	{},	// BCHG.B #<data>.W,(d8,PC,Xn)
	{},	// BCLR.L #<data>.W,Dn
	{},	// BCLR.B #<data>.W,(An)
	{},	// BCLR.B #<data>.W,(An)+
	{},	// BCLR.B #<data>.W,-(An)
	{},	// BCLR.B #<data>.W,(d16,An)
	{},	// BCLR.B #<data>.W,(d8,An,Xn)
	{},	// BCLR.B #<data>.W,(xxx).W
	{},	// BCLR.B #<data>.W,(xxx).L
	{},	// BCLR.B #<data>.W,(d16,PC)
	{},	// BCLR.B #<data>.W,(d8,PC,Xn)
	{},	// BSET.L #<data>.W,Dn
	{},	// BSET.B #<data>.W,(An)
	{},	// BSET.B #<data>.W,(An)+
	{},	// BSET.B #<data>.W,-(An)
	{},	// BSET.B #<data>.W,(d16,An)
	{},	// BSET.B #<data>.W,(d8,An,Xn)
	{},	// BSET.B #<data>.W,(xxx).W
	{},	// BSET.B #<data>.W,(xxx).L
	{},	// BSET.B #<data>.W,(d16,PC)
	{},	// BSET.B #<data>.W,(d8,PC,Xn)
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// EORI.B #<data>.W,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// EORI.B #<data>.W,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// EORI.B #<data>.W,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// EORI.B #<data>.W,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// EORI.B #<data>.W,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// EORI.B #<data>.W,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// EORI.B #<data>.W,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// EORI.B #<data>.W,(xxx).L
	{4,	0,	12,0,0,0,	18,0,2,0},	// EORI.B #<data>.W, SR
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// EORI.W #<data>.W,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// EORI.W #<data>.W,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// EORI.W #<data>.W,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// EORI.W #<data>.W,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// EORI.W #<data>.W,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// EORI.W #<data>.W,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// EORI.W #<data>.W,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// EORI.W #<data>.W,(xxx).L
	{4,	0,	12,0,0,0,	18,0,2,0},	// EORI.W #<data>.W, SR
	{4,	0,	 6,0,0,0,	10,0,2,0},	// EORI.L #<data>.L,Dn
	{1,	1,	11,1,0,1,	17,1,2,1},	// EORI.L #<data>.L,(An)
	{4,	2,	14,1,0,1,	19,1,2,1},	// EORI.L #<data>.L,(An)+
	{2,	1,	11,1,0,1,	17,1,2,1},	// EORI.L #<data>.L,-(An)
	{4,	1,	13,1,0,1,	22,1,3,1},	// EORI.L #<data>.L,(d16,An)
	{8,	3,	17,1,0,1,	24,1,3,1},	// EORI.L #<data>.L,(d8,An,Xn)
	{6,	3,	15,1,0,1,	22,1,3,1},	// EORI.L #<data>.L,(xxx).W
	{5,	1,	15,1,0,1,	23,1,3,1},	// EORI.L #<data>.L,(xxx).L
	{},	// CAS.B #<data>.W,(An)
	{},	// CAS.B #<data>.W,(An)+
	{},	// CAS.B #<data>.W,-(An)
	{},	// CAS.B #<data>.W,(d16,An)
	{},	// CAS.B #<data>.W,(d8,An,Xn)
	{},	// CAS.B #<data>.W,(xxx).W
	{},	// CAS.B #<data>.W,(xxx).L
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// CMPI.B #<data>.B,Dn
	{1,	1,	 7,1,0,0,	12,1,2,0},	// CMPI.B #<data>.W,(An)
	{2,	1,	 9,1,0,0,	13,1,2,0},	// CMPI.B #<data>.W,(An)+
	{2,	2,	 8,1,0,0,	12,1,2,0},	// CMPI.B #<data>.W,-(An)
	{2,	0,	 8,1,0,0,	13,1,2,0},	// CMPI.B #<data>.W,(d16,An)
	{6,	2,	12,1,0,0,	18,1,3,0},	// CMPI.B #<data>.W,(d8,An,Xn)
	{4,	2,	10,1,0,0,	14,1,2,0},	// CMPI.B #<data>.W,(xxx).W
	{3,	0,	10,1,0,0,	17,1,3,0},	// CMPI.B #<data>.W,(xxx).L
	{2,	0,	 8,1,0,0,	13,1,2,0},	// CMPI.B #<data>.W,(d16,PC)
	{6,	2,	12,1,0,0,	18,1,3,0},	// CMPI.B #<data>.W,(d8,PC,Xn)
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// CMPI.W #<data>.W,Dn
	{1,	1,	 7,1,0,0,	12,1,2,0},	// CMPI.W #<data>.W,(An)
	{2,	1,	 9,1,0,0,	13,1,2,0},	// CMPI.W #<data>.W,(An)+
	{2,	2,	 8,1,0,0,	12,1,2,0},	// CMPI.W #<data>.W,-(An)
	{2,	0,	 8,1,0,0,	13,1,2,0},	// CMPI.W #<data>.W,(d16,An)
	{6,	2,	12,1,0,0,	18,1,3,0},	// CMPI.W #<data>.W,(d8,An,Xn)
	{4,	2,	10,1,0,0,	14,1,2,0},	// CMPI.W #<data>.W,(xxx).W
	{3,	0,	10,1,0,0,	17,1,3,0},	// CMPI.W #<data>.W,(xxx).L
	{2,	0,	 8,1,0,0,	13,1,2,0},	// CMPI.W #<data>.W,(d16,PC)
	{6,	2,	12,1,0,0,	18,1,3,0},	// CMPI.W #<data>.W,(d8,PC,Xn)
	{6,	0,	 6,0,0,0,	10,0,2,0},	// CMPI.L #<data>.L,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// CMPI.L #<data>.L,(An)
	{4,	1,	11,1,0,0,	15,1,2,0},	// CMPI.L #<data>.L,(An)+
	{2,	0,	 8,1,0,0,	13,1,2,0},	// CMPI.L #<data>.L,-(An)
	{4,	0,	10,1,0,0,	18,1,3,0},	// CMPI.L #<data>.L,(d16,An)
	{8,	2,	14,1,0,0,	20,1,3,0},	// CMPI.L #<data>.L,(d8,An,Xn)
	{6,	2,	12,1,0,0,	18,1,3,0},	// CMPI.L #<data>.L,(xxx).W
	{5,	0,	12,1,0,0,	19,1,3,0},	// CMPI.L #<data>.L,(xxx).L
	{4,	0,	10,1,0,0,	18,1,3,0},	// CMPI.L #<data>.L,(d16,PC)
	{8,	2,	14,1,0,0,	20,1,3,0},	// CMPI.L #<data>.L,(d8,PC,Xn)
	{},	// CAS.W #<data>.W,(An)
	{},	// CAS.W #<data>.W,(An)+
	{},	// CAS.W #<data>.W,-(An)
	{},	// CAS.W #<data>.W,(d16,An)
	{},	// CAS.W #<data>.W,(d8,An,Xn)
	{},	// CAS.W #<data>.W,(xxx).W
	{},	// CAS.W #<data>.W,(xxx).L
	{},	// CAS2.W #<data>.L
	{},	// MOVES.B #<data>.W,(An)
	{},	// MOVES.B #<data>.W,(An)+
	{},	// MOVES.B #<data>.W,-(An)
	{},	// MOVES.B #<data>.W,(d16,An)
	{},	// MOVES.B #<data>.W,(d8,An,Xn)
	{},	// MOVES.B #<data>.W,(xxx).W
	{},	// MOVES.B #<data>.W,(xxx).L
	{},	// MOVES.W #<data>.W,(An)
	{},	// MOVES.W #<data>.W,(An)+
	{},	// MOVES.W #<data>.W,-(An)
	{},	// MOVES.W #<data>.W,(d16,An)
	{},	// MOVES.W #<data>.W,(d8,An,Xn)
	{},	// MOVES.W #<data>.W,(xxx).W
	{},	// MOVES.W #<data>.W,(xxx).L
	{},	// MOVES.L #<data>.W,(An)
	{},	// MOVES.L #<data>.W,(An)+
	{},	// MOVES.L #<data>.W,-(An)
	{},	// MOVES.L #<data>.W,(d16,An)
	{},	// MOVES.L #<data>.W,(d8,An,Xn)
	{},	// MOVES.L #<data>.W,(xxx).W
	{},	// MOVES.L #<data>.W,(xxx).L
	{},	// CAS.L #<data>.W,(An)
	{},	// CAS.L #<data>.W,(An)+
	{},	// CAS.L #<data>.W,-(An)
	{},	// CAS.L #<data>.W,(d16,An)
	{},	// CAS.L #<data>.W,(d8,An,Xn)
	{},	// CAS.L #<data>.W,(xxx).W
	{},	// CAS.L #<data>.W,(xxx).L
	{},	// CAS2.L #<data>.L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// MOVE.B Dn,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// MOVE.B (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// MOVE.B (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// MOVE.B -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// MOVE.B (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// MOVE.B (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVE.B (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// MOVE.B (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVE.B (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// MOVE.B (d8,PC,Xn),Dn
	{2,	0,	 4,0,0,0,	 8,0,2,0},	// MOVE.B #<data>.B,Dn
	{0,	1,	 5,0,0,1,	 8,0,1,1},	// MOVE.B Dn,(An)
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.B (An),(An)
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.B (An)+,(An)
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.B -(An),(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (d16,An),(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d8,An,Xn),(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (xxx).W,(An)
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.B (xxx).L,(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (d16,PC),(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d8,PC,Xn),(An)
	{4,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.B #<data>.B,(An)
	{0,	1,	 5,0,0,1,	 8,0,1,1},	// MOVE.B Dn,(An)+
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.B (An),(An)+
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.B (An)+,(An)+
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.B -(An),(An)+
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (d16,An),(An)+
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d8,An,Xn),(An)+
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (xxx).W,(An)+
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.B (xxx).L,(An)+
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (d16,PC),(An)+
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d8,PC,Xn),(An)+
	{4,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.B #<data>.B,(An)+
	{0,	2,	 6,0,0,1,	 8,0,1,1},	// MOVE.B Dn,-(An)
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.B (An),-(An)
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.B (An)+,-(An)
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.B -(An),-(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (d16,An),-(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d8,An,Xn),-(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (xxx).W,-(An)
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.B (xxx).L,-(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (d16,PC),-(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d8,PC,Xn),-(An)
	{4,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.B #<data>.B,-(An)
	{2,	0,	 6,0,0,1,	 9,0,1,1},	// MOVE.B Dn,(d16,An)
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.B (An),(d16,An)
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.B (An)+,(d16,An)
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.B -(An),(d16,An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (d16,An),(d16,An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d8,An,Xn),(d16,An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (xxx).W,(d16,An)
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.B (xxx).L,(d16,An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.B (d16,PC),(d16,An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d8,PC,Xn),(d16,An)
	{4,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.B #<data>.B,(d16,An)
	{4,	0,	 8,0,0,1,	11,0,1,1},	// MOVE.B Dn,(d8,An,Xn)
	{5,	1,	13,1,0,1,	16,1,1,1},	// MOVE.B (An),(d8,An,Xn)
	{4,	1,	13,1,0,1,	16,1,1,1},	// MOVE.B (An)+,(d8,An,Xn)
	{6,	2,	14,1,0,1,	17,1,1,1},	// MOVE.B -(An),(d8,An,Xn)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d16,An),(d8,An,Xn)
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.B (d8,An,Xn),(d8,An,Xn)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (xxx).W,(d8,An,Xn)
	{5,	0,	14,1,0,1,	20,1,2,1},	// MOVE.B (xxx).L,(d8,An,Xn)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d16,PC),(d8,An,Xn)
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.B (d8,PC,Xn),(d8,An,Xn)
	{6,	0,	10,0,0,1,	15,0,2,1},	// MOVE.B #<data>.B,(d8,An,Xn)
	{4,	0,	 8,0,0,1,	11,0,1,1},	// MOVE.B Dn,(xxx).W
	{5,	1,	13,1,0,1,	16,1,1,1},	// MOVE.B (An),(xxx).W
	{4,	1,	13,1,0,1,	16,1,1,1},	// MOVE.B (An)+,(xxx).W
	{6,	2,	14,1,0,1,	17,1,1,1},	// MOVE.B -(An),(xxx).W
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d16,An),(xxx).W
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.B (d8,An,Xn),(xxx).W
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (xxx).W,(xxx).W
	{5,	0,	14,1,0,1,	20,1,2,1},	// MOVE.B (xxx).L,(xxx).W
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B (d16,PC),(xxx).W
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.B (d8,PC,Xn),(xxx).W
	{6,	0,	10,0,0,1,	15,0,2,1},	// MOVE.B #<data>.B,(xxx).W
	{0,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.B Dn,(xxx).L
	{1,	1,	13,1,0,1,	18,1,2,1},	// MOVE.B (An),(xxx).L
	{0,	1,	13,1,0,1,	18,1,2,1},	// MOVE.B (An)+,(xxx).L
	{2,	2,	14,1,0,1,	19,1,2,1},	// MOVE.B -(An),(xxx).L
	{2,	2,	14,1,0,1,	21,1,3,1},	// MOVE.B (d16,An),(xxx).L
	{4,	2,	16,1,0,1,	23,1,3,1},	// MOVE.B (d8,An,Xn),(xxx).L
	{2,	2,	14,1,0,1,	21,1,3,1},	// MOVE.B (xxx).W,(xxx).L
	{1,	0,	14,1,0,1,	22,1,3,1},	// MOVE.B (xxx).L,(xxx).L
	{2,	2,	14,1,0,1,	21,1,3,1},	// MOVE.B (d16,PC),(xxx).L
	{4,	2,	16,1,0,1,	23,1,3,1},	// MOVE.B (d8,PC,Xn),(xxx).L
	{2,	0,	10,0,0,1,	17,0,3,1},	// MOVE.B #<data>.B,(xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// MOVE.L Dn,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// MOVE.L An,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// MOVE.L (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// MOVE.L (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// MOVE.L -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// MOVE.L (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// MOVE.L (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVE.L (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// MOVE.L (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVE.L (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// MOVE.L (d8,PC,Xn),Dn
	{4,	0,	 6,0,0,0,	10,0,2,0},	// MOVE.L #<data>.L,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// MOVEA.L Dn,An
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// MOVEA.L An,An
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// MOVEA.L (An),An
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// MOVEA.L (An)+,An
	{2,	2,	 8,1,0,0,	10,1,1,0},	// MOVEA.L -(An),An
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVEA.L (d16,An),An
	{4,	2,	10,1,0,0,	14,1,2,0},	// MOVEA.L (d8,An,Xn),An
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVEA.L (xxx).W,An
	{1,	0,	 8,1,0,0,	13,1,2,0},	// MOVEA.L (xxx).L,An
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVEA.L (d16,PC),An
	{4,	2,	10,1,0,0,	14,1,2,0},	// MOVEA.L (d8,PC,Xn),An
	{4,	0,	 6,0,0,0,	10,0,2,0},	// MOVEA.L #<data>.L,An
	{0,	1,	 5,0,0,1,	 8,0,1,1},	// MOVE.L Dn,(An)
	{0,	1,	 5,0,0,1,	 8,0,1,1},	// MOVE.L An,(An)
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.L (An),(An)
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.L (An)+,(An)
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.L -(An),(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (d16,An),(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d8,An,Xn),(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (xxx).W,(An)
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.L (xxx).L,(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (d16,PC),(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d8,PC,Xn),(An)
	{6,	0,	10,0,0,1,	15,0,2,1},	// MOVE.L #<data>.L,(An)
	{0,	1,	 5,0,0,1,	 8,0,1,1},	// MOVE.L Dn,(An)+
	{0,	1,	 5,0,0,1,	 8,0,1,1},	// MOVE.L An,(An)+
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.L (An),(An)+
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.L (An)+,(An)+
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.L -(An),(An)+
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (d16,An),(An)+
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d8,An,Xn),(An)+
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (xxx).W,(An)+
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.L (xxx).L,(An)+
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (d16,PC),(An)+
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d8,PC,Xn),(An)+
	{6,	0,	10,1,0,1,	11,1,2,1},	// MOVE.L #<data>.L,(An)+
	{0,	2,	 6,0,0,1,	 8,0,1,1},	// MOVE.L Dn,-(An)
	{0,	2,	 6,0,0,1,	 8,0,1,1},	// MOVE.L An,-(An)
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.L (An),-(An)
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.L (An)+,-(An)
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.L -(An),-(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (d16,An),-(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d8,An,Xn),-(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (xxx).W,-(An)
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.L (xxx).L,-(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (d16,PC),-(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d8,PC,Xn),-(An)
	{6,	0,	10,0,0,1,	15,0,2,1},	// MOVE.L #<data>.L,-(An)
	{2,	0,	 6,0,0,1,	 9,0,1,1},	// MOVE.L Dn,(d16,An)
	{2,	0,	 6,0,0,1,	 9,0,1,1},	// MOVE.L An,(d16,An)
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.L (An),(d16,An)
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.L (An)+,(d16,An)
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.L -(An),(d16,An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (d16,An),(d16,An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d8,An,Xn),(d16,An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (xxx).W,(d16,An)
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.L (xxx).L,(d16,An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.L (d16,PC),(d16,An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d8,PC,Xn),(d16,An)
	{6,	0,	10,0,0,1,	15,0,2,1},	// MOVE.L #<data>.L,(d16,An)
	{4,	0,	 8,0,0,1,	11,0,1,1},	// MOVE.L Dn,(d8,An,Xn)
	{4,	0,	 8,0,0,1,	11,0,1,1},	// MOVE.L An,(d8,An,Xn)
	{5,	1,	13,1,0,1,	16,1,1,1},	// MOVE.L (An),(d8,An,Xn)
	{4,	1,	13,1,0,1,	16,1,1,1},	// MOVE.L (An)+,(d8,An,Xn)
	{6,	2,	14,1,0,1,	17,1,1,1},	// MOVE.L -(An),(d8,An,Xn)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d16,An),(d8,An,Xn)
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.L (d8,An,Xn),(d8,An,Xn)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (xxx).W,(d8,An,Xn)
	{5,	0,	14,1,0,1,	20,1,2,1},	// MOVE.L (xxx).L,(d8,An,Xn)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d16,PC),(d8,An,Xn)
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.L (d8,PC,Xn),(d8,An,Xn)
	{8,	0,	12,0,0,1,	17,0,2,1},	// MOVE.L #<data>.L,(d8,An,Xn)
	{4,	0,	 8,0,0,1,	11,0,1,1},	// MOVE.L Dn,(xxx).W
	{4,	0,	 8,0,0,1,	11,0,1,1},	// MOVE.L An,(xxx).W
	{5,	1,	13,1,0,1,	16,1,1,1},	// MOVE.L (An),(xxx).W
	{4,	1,	13,1,0,1,	16,1,1,1},	// MOVE.L (An)+,(xxx).W
	{6,	2,	14,1,0,1,	17,1,1,1},	// MOVE.L -(An),(xxx).W
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d16,An),(xxx).W
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.L (d8,An,Xn),(xxx).W
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (xxx).W,(xxx).W
	{5,	0,	14,1,0,1,	20,1,2,1},	// MOVE.L (xxx).L,(xxx).W
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L (d16,PC),(xxx).W
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.L (d8,PC,Xn),(xxx).W
	{8,	0,	12,0,0,1,	17,0,2,1},	// MOVE.L #<data>.L,(xxx).W
	{0,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.L Dn,(xxx).L
	{0,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.L An,(xxx).L
	{1,	1,	13,1,0,1,	18,1,2,1},	// MOVE.L (An),(xxx).L
	{0,	1,	13,1,0,1,	18,1,2,1},	// MOVE.L (An)+,(xxx).L
	{2,	2,	14,1,0,1,	19,1,2,1},	// MOVE.L -(An),(xxx).L
	{2,	2,	14,1,0,1,	21,1,3,1},	// MOVE.L (d16,An),(xxx).L
	{4,	2,	16,1,0,1,	23,1,3,1},	// MOVE.L (d8,An,Xn),(xxx).L
	{2,	2,	14,1,0,1,	21,1,3,1},	// MOVE.L (xxx).W,(xxx).L
	{1,	0,	14,1,0,1,	22,1,3,1},	// MOVE.L (xxx).L,(xxx).L
	{2,	2,	14,1,0,1,	21,1,3,1},	// MOVE.L (d16,PC),(xxx).L
	{4,	2,	16,1,0,1,	23,1,3,1},	// MOVE.L (d8,PC,Xn),(xxx).L
	{4,	0,	12,0,0,1,	19,0,3,1},	// MOVE.L #<data>.L,(xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// MOVE.W Dn,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// MOVE.W An,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// MOVE.W (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// MOVE.W (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// MOVE.W -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// MOVE.W (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// MOVE.W (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVE.W (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// MOVE.W (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVE.W (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// MOVE.W (d8,PC,Xn),Dn
	{2,	0,	 4,0,0,0,	 8,0,2,0},	// MOVE.W #<data>.W,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// MOVEA.W Dn,An
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// MOVEA.W An,An
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// MOVEA.W (An),An
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// MOVEA.W (An)+,An
	{2,	2,	 8,1,0,0,	10,1,1,0},	// MOVEA.W -(An),An
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVEA.W (d16,An),An
	{4,	2,	10,1,0,0,	14,1,2,0},	// MOVEA.W (d8,An,Xn),An
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVEA.W (xxx).W,An
	{1,	0,	 8,1,0,0,	13,1,2,0},	// MOVEA.W (xxx).L,An
	{2,	2,	 8,1,0,0,	12,1,2,0},	// MOVEA.W (d16,PC),An
	{4,	2,	10,1,0,0,	14,1,2,0},	// MOVEA.W (d8,PC,Xn),An
	{2,	0,	 4,0,0,0,	 8,0,2,0},	// MOVEA.W #<data>.W,An
	{0,	1,	 5,0,0,1,	 8,0,1,1},	// MOVE.W Dn,(An)
	{0,	1,	 5,0,0,1,	 8,0,1,1},	// MOVE.W An,(An)
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.W (An),(An)
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.W (An)+,(An)
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.W -(An),(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (d16,An),(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d8,An,Xn),(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (xxx).W,(An)
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.W (xxx).L,(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (d16,PC),(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d8,PC,Xn),(An)
	{4,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.W #<data>.W,(An)
	{0,	1,	 5,0,0,1,	 8,0,1,1},	// MOVE.W Dn,(An)+
	{0,	1,	 5,0,0,1,	 8,0,1,1},	// MOVE.W An,(An)+
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.W (An),(An)+
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.W (An)+,(An)+
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.W -(An),(An)+
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (d16,An),(An)+
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d8,An,Xn),(An)+
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (xxx).W,(An)+
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.W (xxx).L,(An)+
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (d16,PC),(An)+
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d8,PC,Xn),(An)+
	{4,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.W #<data>.W,(An)+
	{0,	2,	 6,0,0,1,	 8,0,1,1},	// MOVE.W Dn,-(An)
	{0,	2,	 6,0,0,1,	 8,0,1,1},	// MOVE.W An,-(An)
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.W (An),-(An)
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.W (An)+,-(An)
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.W -(An),-(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (d16,An),-(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d8,An,Xn),-(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (xxx).W,-(An)
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.W (xxx).L,-(An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (d16,PC),-(An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d8,PC,Xn),-(An)
	{4,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.W #<data>.W,-(An)
	{2,	0,	 6,0,0,1,	 9,0,1,1},	// MOVE.W Dn,(d16,An)
	{2,	0,	 6,0,0,1,	 9,0,1,1},	// MOVE.W An,(d16,An)
	{3,	1,	11,1,0,1,	14,1,1,1},	// MOVE.W (An),(d16,An)
	{2,	1,	11,1,0,1,	14,1,1,1},	// MOVE.W (An)+,(d16,An)
	{4,	2,	12,1,0,1,	15,1,1,1},	// MOVE.W -(An),(d16,An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (d16,An),(d16,An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d8,An,Xn),(d16,An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (xxx).W,(d16,An)
	{3,	0,	12,1,0,1,	18,1,2,1},	// MOVE.W (xxx).L,(d16,An)
	{4,	2,	12,1,0,1,	17,1,2,1},	// MOVE.W (d16,PC),(d16,An)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d8,PC,Xn),(d16,An)
	{4,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.W #<data>.W,(d16,An)
	{4,	0,	 8,0,0,1,	11,0,1,1},	// MOVE.W Dn,(d8,An,Xn)
	{4,	0,	 8,0,0,1,	11,0,1,1},	// MOVE.W An,(d8,An,Xn)
	{5,	1,	13,1,0,1,	16,1,1,1},	// MOVE.W (An),(d8,An,Xn)
	{4,	1,	13,1,0,1,	16,1,1,1},	// MOVE.W (An)+,(d8,An,Xn)
	{6,	2,	14,1,0,1,	17,1,1,1},	// MOVE.W -(An),(d8,An,Xn)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d16,An),(d8,An,Xn)
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.W (d8,An,Xn),(d8,An,Xn)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (xxx).W,(d8,An,Xn)
	{5,	0,	14,1,0,1,	20,1,2,1},	// MOVE.W (xxx).L,(d8,An,Xn)
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d16,PC),(d8,An,Xn)
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.W (d8,PC,Xn),(d8,An,Xn)
	{6,	0,	10,0,0,1,	15,0,2,1},	// MOVE.W #<data>.W,(d8,An,Xn)
	{4,	0,	 8,0,0,1,	11,0,1,1},	// MOVE.W Dn,(xxx).W
	{4,	0,	 8,0,0,1,	11,0,1,1},	// MOVE.W An,(xxx).W
	{5,	1,	13,1,0,1,	16,1,1,1},	// MOVE.W (An),(xxx).W
	{4,	1,	13,1,0,1,	16,1,1,1},	// MOVE.W (An)+,(xxx).W
	{6,	2,	14,1,0,1,	17,1,1,1},	// MOVE.W -(An),(xxx).W
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d16,An),(xxx).W
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.W (d8,An,Xn),(xxx).W
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (xxx).W,(xxx).W
	{5,	0,	14,1,0,1,	20,1,2,1},	// MOVE.W (xxx).L,(xxx).W
	{6,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W (d16,PC),(xxx).W
	{8,	2,	12,1,0,1,	13,1,2,1},	// MOVE.W (d8,PC,Xn),(xxx).W
	{6,	0,	10,0,0,1,	15,0,2,1},	// MOVE.W #<data>.W,(xxx).W
	{0,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.W Dn,(xxx).L
	{0,	0,	 8,0,0,1,	13,0,2,1},	// MOVE.W An,(xxx).L
	{1,	1,	13,1,0,1,	18,1,2,1},	// MOVE.W (An),(xxx).L
	{0,	1,	13,1,0,1,	18,1,2,1},	// MOVE.W (An)+,(xxx).L
	{2,	2,	14,1,0,1,	19,1,2,1},	// MOVE.W -(An),(xxx).L
	{2,	2,	14,1,0,1,	21,1,3,1},	// MOVE.W (d16,An),(xxx).L
	{4,	2,	16,1,0,1,	23,1,3,1},	// MOVE.W (d8,An,Xn),(xxx).L
	{2,	2,	14,1,0,1,	21,1,3,1},	// MOVE.W (xxx).W,(xxx).L
	{1,	0,	14,1,0,1,	22,1,3,1},	// MOVE.W (xxx).L,(xxx).L
	{2,	2,	14,1,0,1,	21,1,3,1},	// MOVE.W (d16,PC),(xxx).L
	{4,	2,	16,1,0,1,	23,1,3,1},	// MOVE.W (d8,PC,Xn),(xxx).L
	{2,	0,	10,0,0,1,	17,0,3,1},	// MOVE.W #<data>.W,(xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// NEGX.B Dn
	{1,	2,	10,1,0,1,	13,1,1,1},	// NEGX.B (An)
	{0,	2,	10,1,0,1,	13,1,1,1},	// NEGX.B (An)+
	{2,	3,	11,1,0,1,	14,1,1,1},	// NEGX.B -(An)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEGX.B (d16,An)
	{4,	3,	13,1,0,1,	18,1,2,1},	// NEGX.B (d8,An,Xn)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEGX.B (xxx).W
	{1,	1,	11,1,0,1,	17,1,2,1},	// NEGX.B (xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// NEGX.W Dn
	{1,	2,	10,1,0,1,	13,1,1,1},	// NEGX.W (An)
	{0,	2,	10,1,0,1,	13,1,1,1},	// NEGX.W (An)+
	{2,	3,	11,1,0,1,	14,1,1,1},	// NEGX.W -(An)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEGX.W (d16,An)
	{4,	3,	13,1,0,1,	18,1,2,1},	// NEGX.W (d8,An,Xn)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEGX.W (xxx).W
	{1,	1,	11,1,0,1,	17,1,2,1},	// NEGX.W (xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// NEGX.L Dn
	{1,	2,	10,1,0,1,	13,1,1,1},	// NEGX.L (An)
	{0,	2,	10,1,0,1,	13,1,1,1},	// NEGX.L (An)+
	{2,	3,	11,1,0,1,	14,1,1,1},	// NEGX.L -(An)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEGX.L (d16,An)
	{4,	3,	13,1,0,1,	18,1,2,1},	// NEGX.L (d8,An,Xn)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEGX.L (xxx).W
	{1,	1,	11,1,0,1,	17,1,2,1},	// NEGX.L (xxx).L
	{},	// MVSR2.W Dn
	{},	// MVSR2.W (An)
	{},	// MVSR2.W (An)+
	{},	// MVSR2.W -(An)
	{},	// MVSR2.W (d16,An)
	{},	// MVSR2.W (d8,An,Xn)
	{},	// MVSR2.W (xxx).W
	{},	// MVSR2.W (xxx).L
	{},	// CHK.L Dn,Dn
	{},	// CHK.L (An),Dn
	{},	// CHK.L (An)+,Dn
	{},	// CHK.L -(An),Dn
	{},	// CHK.L (d16,An),Dn
	{},	// CHK.L (d8,An,Xn),Dn
	{},	// CHK.L (xxx).W,Dn
	{},	// CHK.L (xxx).L,Dn
	{},	// CHK.L (d16,PC),Dn
	{},	// CHK.L (d8,PC,Xn),Dn
	{},	// CHK.L #<data>.L,Dn
	{},	// CHK.W Dn,Dn
	{},	// CHK.W (An),Dn
	{},	// CHK.W (An)+,Dn
	{},	// CHK.W -(An),Dn
	{},	// CHK.W (d16,An),Dn
	{},	// CHK.W (d8,An,Xn),Dn
	{},	// CHK.W (xxx).W,Dn
	{},	// CHK.W (xxx).L,Dn
	{},	// CHK.W (d16,PC),Dn
	{},	// CHK.W (d8,PC,Xn),Dn
	{},	// CHK.W #<data>.W,Dn
	{},	// LEA.L (An),An
	{},	// LEA.L (d16,An),An
	{},	// LEA.L (d8,An,Xn),An
	{},	// LEA.L (xxx).W,An
	{},	// LEA.L (xxx).L,An
	{},	// LEA.L (d16,PC),An
	{},	// LEA.L (d8,PC,Xn),An
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// CLR.B Dn
	{},	// CLR.B (An)
	{},	// CLR.B (An)+
	{},	// CLR.B -(An)
	{},	// CLR.B (d16,An)
	{},	// CLR.B (d8,An,Xn)
	{},	// CLR.B (xxx).W
	{},	// CLR.B (xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// CLR.W Dn
	{},	// CLR.W (An)
	{},	// CLR.W (An)+
	{},	// CLR.W -(An)
	{},	// CLR.W (d16,An)
	{},	// CLR.W (d8,An,Xn)
	{},	// CLR.W (xxx).W
	{},	// CLR.W (xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// CLR.L Dn
	{},	// CLR.L (An)
	{},	// CLR.L (An)+
	{},	// CLR.L -(An)
	{},	// CLR.L (d16,An)
	{},	// CLR.L (d8,An,Xn)
	{},	// CLR.L (xxx).W
	{},	// CLR.L (xxx).L
	{},	// MVSR2.B Dn
	{},	// MVSR2.B (An)
	{},	// MVSR2.B (An)+
	{},	// MVSR2.B -(An)
	{},	// MVSR2.B (d16,An)
	{},	// MVSR2.B (d8,An,Xn)
	{},	// MVSR2.B (xxx).W
	{},	// MVSR2.B (xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// NEG.B Dn
	{1,	2,	10,1,0,1,	13,1,1,1},	// NEG.B (An)
	{0,	2,	10,1,0,1,	13,1,1,1},	// NEG.B (An)+
	{2,	3,	11,1,0,1,	14,1,1,1},	// NEG.B -(An)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEG.B (d16,An)
	{4,	3,	13,1,0,1,	18,1,2,1},	// NEG.B (d8,An,Xn)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEG.B (xxx).W
	{1,	1,	11,1,0,1,	17,1,2,1},	// NEG.B (xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// NEG.W Dn
	{1,	2,	10,1,0,1,	13,1,1,1},	// NEG.W (An)
	{0,	2,	10,1,0,1,	13,1,1,1},	// NEG.W (An)+
	{2,	3,	11,1,0,1,	14,1,1,1},	// NEG.W -(An)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEG.W (d16,An)
	{4,	3,	13,1,0,1,	18,1,2,1},	// NEG.W (d8,An,Xn)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEG.W (xxx).W
	{1,	1,	11,1,0,1,	17,1,2,1},	// NEG.W (xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// NEG.L Dn
	{1,	2,	10,1,0,1,	13,1,1,1},	// NEG.L (An)
	{0,	2,	10,1,0,1,	13,1,1,1},	// NEG.L (An)+
	{2,	3,	11,1,0,1,	14,1,1,1},	// NEG.L -(An)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEG.L (d16,An)
	{4,	3,	13,1,0,1,	18,1,2,1},	// NEG.L (d8,An,Xn)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NEG.L (xxx).W
	{1,	1,	11,1,0,1,	17,1,2,1},	// NEG.L (xxx).L
	{},	// MV2SR.B Dn
	{},	// MV2SR.B (An)
	{},	// MV2SR.B (An)+
	{},	// MV2SR.B -(An)
	{},	// MV2SR.B (d16,An)
	{},	// MV2SR.B (d8,An,Xn)
	{},	// MV2SR.B (xxx).W
	{},	// MV2SR.B (xxx).L
	{},	// MV2SR.B (d16,PC)
	{},	// MV2SR.B (d8,PC,Xn)
	{},	// MV2SR.B #<data>.B
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// NOT.B Dn
	{1,	2,	10,1,0,1,	13,1,1,1},	// NOT.B (An)
	{0,	2,	10,1,0,1,	13,1,1,1},	// NOT.B (An)+
	{2,	3,	11,1,0,1,	14,1,1,1},	// NOT.B -(An)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NOT.B (d16,An)
	{4,	3,	13,1,0,1,	18,1,2,1},	// NOT.B (d8,An,Xn)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NOT.B (xxx).W
	{1,	1,	11,1,0,1,	17,1,2,1},	// NOT.B (xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// NOT.W Dn
	{1,	2,	10,1,0,1,	13,1,1,1},	// NOT.W (An)
	{0,	2,	10,1,0,1,	13,1,1,1},	// NOT.W (An)+
	{2,	3,	11,1,0,1,	14,1,1,1},	// NOT.W -(An)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NOT.W (d16,An)
	{4,	3,	13,1,0,1,	18,1,2,1},	// NOT.W (d8,An,Xn)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NOT.W (xxx).W
	{1,	1,	11,1,0,1,	17,1,2,1},	// NOT.W (xxx).L
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// NOT.L Dn
	{1,	2,	10,1,0,1,	13,1,1,1},	// NOT.L (An)
	{0,	2,	10,1,0,1,	13,1,1,1},	// NOT.L (An)+
	{2,	3,	11,1,0,1,	14,1,1,1},	// NOT.L -(An)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NOT.L (d16,An)
	{4,	3,	13,1,0,1,	18,1,2,1},	// NOT.L (d8,An,Xn)
	{2,	3,	11,1,0,1,	16,1,2,1},	// NOT.L (xxx).W
	{1,	1,	11,1,0,1,	17,1,2,1},	// NOT.L (xxx).L
	{},	// MV2SR.W Dn
	{},	// MV2SR.W (An)
	{},	// MV2SR.W (An)+
	{},	// MV2SR.W -(An)
	{},	// MV2SR.W (d16,An)
	{},	// MV2SR.W (d8,An,Xn)
	{},	// MV2SR.W (xxx).W
	{},	// MV2SR.W (xxx).L
	{},	// MV2SR.W (d16,PC)
	{},	// MV2SR.W (d8,PC,Xn)
	{},	// MV2SR.W #<data>.W
	{0,	0,	 6,0,0,0,	 8,0,1,0},	// NBCD.B Dn
	{},	// LINK.L An,#<data>.L
	{},	// NBCD.B (An)
	{},	// NBCD.B (An)+
	{},	// NBCD.B -(An)
	{},	// NBCD.B (d16,An)
	{},	// NBCD.B (d8,An,Xn)
	{},	// NBCD.B (xxx).W
	{},	// NBCD.B (xxx).L
	{4,	0,	 4,0,0,0,	 6,0,1,0},	// SWAP.W Dn
	{},	// BKPTQ.L #<data>
	{},	// PEA.L (An)
	{},	// PEA.L (d16,An)
	{},	// PEA.L (d8,An,Xn)
	{},	// PEA.L (xxx).W
	{},	// PEA.L (xxx).L
	{},	// PEA.L (d16,PC)
	{},	// PEA.L (d8,PC,Xn)
	{4,	0,	 4,0,0,0,	 6,0,1,0},	// EXT.W Dn
	{},	// MVMLE.W #<data>.W,(An)
	{},	// MVMLE.W #<data>.W,-(An)
	{},	// MVMLE.W #<data>.W,(d16,An)
	{},	// MVMLE.W #<data>.W,(d8,An,Xn)
	{},	// MVMLE.W #<data>.W,(xxx).W
	{},	// MVMLE.W #<data>.W,(xxx).L
	{4,	0,	 4,0,0,0,	 6,0,1,0},	// EXT.L Dn
	{},	// MVMLE.L #<data>.W,(An)
	{},	// MVMLE.L #<data>.W,-(An)
	{},	// MVMLE.L #<data>.W,(d16,An)
	{},	// MVMLE.L #<data>.W,(d8,An,Xn)
	{},	// MVMLE.L #<data>.W,(xxx).W
	{},	// MVMLE.L #<data>.W,(xxx).L
	{4,	0,	 4,0,0,0,	 6,0,1,0},	// EXT.B Dn
	{0,	0,	 2,0,0,0,	 4,0,1,0},	// TST.B Dn
	{},	// TST.B (An)
	{},	// TST.B (An)+
	{},	// TST.B -(An)
	{},	// TST.B (d16,An)
	{},	// TST.B (d8,An,Xn)
	{},	// TST.B (xxx).W
	{},	// TST.B (xxx).L
	{},	// TST.B (d16,PC)
	{},	// TST.B (d8,PC,Xn)
	{},	// TST.B #<data>.B
	{0,	0,	 2,0,0,0,	 4,0,1,0},	// TST.W Dn
	{},	// TST.W An
	{},	// TST.W (An)
	{},	// TST.W (An)+
	{},	// TST.W -(An)
	{},	// TST.W (d16,An)
	{},	// TST.W (d8,An,Xn)
	{},	// TST.W (xxx).W
	{},	// TST.W (xxx).L
	{},	// TST.W (d16,PC)
	{},	// TST.W (d8,PC,Xn)
	{},	// TST.W #<data>.W
	{0,	0,	 2,0,0,0,	 4,0,1,0},	// TST.L Dn
	{},	// TST.L An
	{},	// TST.L (An)
	{},	// TST.L (An)+
	{},	// TST.L -(An)
	{},	// TST.L (d16,An)
	{},	// TST.L (d8,An,Xn)
	{},	// TST.L (xxx).W
	{},	// TST.L (xxx).L
	{},	// TST.L (d16,PC)
	{},	// TST.L (d8,PC,Xn)
	{},	// TST.L #<data>.L
	{},	// TAS.B Dn
	{},	// TAS.B (An)
	{},	// TAS.B (An)+
	{},	// TAS.B -(An)
	{},	// TAS.B (d16,An)
	{},	// TAS.B (d8,An,Xn)
	{},	// TAS.B (xxx).W
	{},	// TAS.B (xxx).L
	{},	// MULL.L #<data>.W,Dn
	{},	// MULL.L #<data>.W,(An)
	{},	// MULL.L #<data>.W,(An)+
	{},	// MULL.L #<data>.W,-(An)
	{},	// MULL.L #<data>.W,(d16,An)
	{},	// MULL.L #<data>.W,(d8,An,Xn)
	{},	// MULL.L #<data>.W,(xxx).W
	{},	// MULL.L #<data>.W,(xxx).L
	{},	// MULL.L #<data>.W,(d16,PC)
	{},	// MULL.L #<data>.W,(d8,PC,Xn)
	{},	// MULL.L #<data>.W,#<data>.L
	{},	// DIVL.L #<data>.W,Dn
	{},	// DIVL.L #<data>.W,(An)
	{},	// DIVL.L #<data>.W,(An)+
	{},	// DIVL.L #<data>.W,-(An)
	{},	// DIVL.L #<data>.W,(d16,An)
	{},	// DIVL.L #<data>.W,(d8,An,Xn)
	{},	// DIVL.L #<data>.W,(xxx).W
	{},	// DIVL.L #<data>.W,(xxx).L
	{},	// DIVL.L #<data>.W,(d16,PC)
	{},	// DIVL.L #<data>.W,(d8,PC,Xn)
	{},	// DIVL.L #<data>.W,#<data>.L
	{},	// MVMEL.W #<data>.W,(An)
	{},	// MVMEL.W #<data>.W,(An)+
	{},	// MVMEL.W #<data>.W,(d16,An)
	{},	// MVMEL.W #<data>.W,(d8,An,Xn)
	{},	// MVMEL.W #<data>.W,(xxx).W
	{},	// MVMEL.W #<data>.W,(xxx).L
	{},	// MVMEL.W #<data>.W,(d16,PC)
	{},	// MVMEL.W #<data>.W,(d8,PC,Xn)
	{},	// MVMEL.L #<data>.W,(An)
	{},	// MVMEL.L #<data>.W,(An)+
	{},	// MVMEL.L #<data>.W,(d16,An)
	{},	// MVMEL.L #<data>.W,(d8,An,Xn)
	{},	// MVMEL.L #<data>.W,(xxx).W
	{},	// MVMEL.L #<data>.W,(xxx).L
	{},	// MVMEL.L #<data>.W,(d16,PC)
	{},	// MVMEL.L #<data>.W,(d8,PC,Xn)
	{},	// TRAPQ.L #<data>
	{},	// LINK.W An,#<data>.W
	{},	// UNLK.L An
	{4,	0,	 4,0,0,0,	 6,0,1,0},	// MOVE An,USP.L
	{4,	0,	 4,0,0,0,	 6,0,1,0},	// MOVE USP.L,An
	{0,	0,	518,0,0,0,	520,0,1,0}, // RESET.L 
	{0,	0,	 2,0,0,0,	 4,0,1,0},	// NOP.L 
	{},	// STOP.L #<data>.W
	{},	// RTE.L 
	{},	// RTD.L #<data>.W
	{},	// RTS.L 
	{},	// TRAPV.L 
	{},	// RTR.L 
	{},	// MOVEC2.L #<data>.W
	{},	// MOVE2C.L #<data>.W
	{},	// JSR.L (An)
	{},	// JSR.L (d16,An)
	{},	// JSR.L (d8,An,Xn)
	{},	// JSR.L (xxx).W
	{},	// JSR.L (xxx).L
	{},	// JSR.L (d16,PC)
	{},	// JSR.L (d8,PC,Xn)
	{},	// JMP.L (An)
	{},	// JMP.L (d16,An)
	{},	// JMP.L (d8,An,Xn)
	{},	// JMP.L (xxx).W
	{},	// JMP.L (xxx).L
	{},	// JMP.L (d16,PC)
	{},	// JMP.L (d8,PC,Xn)
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// ADDQ.B #<data>,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// ADDQ.B #<data>,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// ADDQ.B #<data>,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// ADDQ.B #<data>,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// ADDQ.B #<data>,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ADDQ.B #<data>,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// ADDQ.B #<data>,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// ADDQ.B #<data>,(xxx).L
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// ADDQ.W #<data>,Dn
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// ADDAQ.W #<data>,An
	{1,	2,	10,1,0,1,	16,1,2,1},	// ADDQ.W #<data>,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// ADDQ.W #<data>,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// ADDQ.W #<data>,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// ADDQ.W #<data>,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ADDQ.W #<data>,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// ADDQ.W #<data>,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// ADDQ.W #<data>,(xxx).L
	{4,	0,	 6,0,0,0,	10,0,2,0},	// ADDQ.L #<data>,Dn
	{4,	0,	 6,0,0,0,	10,0,2,0},	// ADDAQ.L #<data>,An
	{1,	1,	11,1,0,1,	17,1,2,1},	// ADDQ.L #<data>,(An)
	{4,	2,	14,1,0,1,	19,1,2,1},	// ADDQ.L #<data>,(An)+
	{2,	1,	11,1,0,1,	17,1,2,1},	// ADDQ.L #<data>,-(An)
	{4,	1,	13,1,0,1,	22,1,3,1},	// ADDQ.L #<data>,(d16,An)
	{8,	3,	17,1,0,1,	24,1,3,1},	// ADDQ.L #<data>,(d8,An,Xn)
	{6,	3,	15,1,0,1,	22,1,3,1},	// ADDQ.L #<data>,(xxx).W
	{5,	1,	15,1,0,1,	23,1,3,1},	// ADDQ.L #<data>,(xxx).L
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// SUBQ.B #<data>,Dn
	{1,	2,	10,1,0,1,	16,1,2,1},	// SUBQ.B #<data>,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// SUBQ.B #<data>,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// SUBQ.B #<data>,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// SUBQ.B #<data>,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// SUBQ.B #<data>,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// SUBQ.B #<data>,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// SUBQ.B #<data>,(xxx).L
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// SUBQ.W #<data>,Dn
	{4,	0,	 4,0,0,0,	 8,0,2,0},	// SUBAQ.W #<data>,An
	{1,	2,	10,1,0,1,	16,1,2,1},	// SUBQ.W #<data>,(An)
	{2,	2,	12,1,0,1,	17,1,2,1},	// SUBQ.W #<data>,(An)+
	{2,	3,	11,1,0,1,	16,1,2,1},	// SUBQ.W #<data>,-(An)
	{2,	1,	11,1,0,1,	17,1,2,1},	// SUBQ.W #<data>,(d16,An)
	{6,	3,	15,1,0,1,	22,1,3,1},	// SUBQ.W #<data>,(d8,An,Xn)
	{4,	3,	13,1,0,1,	18,1,2,1},	// SUBQ.W #<data>,(xxx).W
	{3,	1,	13,1,0,1,	21,1,3,1},	// SUBQ.W #<data>,(xxx).L
	{4,	0,	 6,0,0,0,	10,0,2,0},	// SUBQ.L #<data>,Dn
	{4,	0,	 6,0,0,0,	10,0,2,0},	// SUBAQ.L #<data>,An
	{1,	1,	11,1,0,1,	17,1,2,1},	// SUBQ.L #<data>,(An)
	{4,	2,	14,1,0,1,	19,1,2,1},	// SUBQ.L #<data>,(An)+
	{2,	1,	11,1,0,1,	17,1,2,1},	// SUBQ.L #<data>,-(An)
	{4,	1,	13,1,0,1,	22,1,3,1},	// SUBQ.L #<data>,(d16,An)
	{8,	3,	17,1,0,1,	24,1,3,1},	// SUBQ.L #<data>,(d8,An,Xn)
	{6,	3,	15,1,0,1,	22,1,3,1},	// SUBQ.L #<data>,(xxx).W
	{5,	1,	15,1,0,1,	23,1,3,1},	// SUBQ.L #<data>,(xxx).L
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Scc.B Dn
	{},	// DBcc.W Dn,#<data>.W
	{},	// Scc.B (An)
	{},	// Scc.B (An)+
	{},	// Scc.B -(An)
	{},	// Scc.B (d16,An)
	{},	// Scc.B (d8,An,Xn)
	{},	// Scc.B (xxx).W
	{},	// Scc.B (xxx).L
	{},	// TRAPcc.L #<data>.W
	{},	// TRAPcc.L #<data>.L
	{},	// TRAPcc.L 
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// BSR.W #<data>.W
	{},	// BSRQ.B #<data>
	{},	// BSR.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{},	// Bcc.W #<data>.W
	{},	// BccQ.B #<data>
	{},	// Bcc.L #<data>.L
	{6,	0,	 6,0,0,0,	10,0,2,0},	// MOVEQ.L #<data>,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// OR.B Dn,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// OR.B (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// OR.B (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// OR.B -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// OR.B (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// OR.B (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// OR.B (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// OR.B (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// OR.B (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// OR.B (d8,PC,Xn),Dn
	{2,	0,	 4,0,0,0,	 8,0,2,0},	// OR.B #<data>.B,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// OR.W Dn,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// OR.W (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// OR.W (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// OR.W -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// OR.W (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// OR.W (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// OR.W (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// OR.W (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// OR.W (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// OR.W (d8,PC,Xn),Dn
	{2,	0,	 4,0,0,0,	 8,0,2,0},	// OR.W #<data>.W,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// OR.L Dn,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// OR.L (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// OR.L (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// OR.L -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// OR.L (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// OR.L (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// OR.L (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// OR.L (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// OR.L (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// OR.L (d8,PC,Xn),Dn
	{4,	0,	 6,0,0,0,	10,0,2,0},	// OR.L #<data>.L,Dn
	{},	// DIVU.W Dn,Dn
	{},	// DIVU.W (An),Dn
	{},	// DIVU.W (An)+,Dn
	{},	// DIVU.W -(An),Dn
	{},	// DIVU.W (d16,An),Dn
	{},	// DIVU.W (d8,An,Xn),Dn
	{},	// DIVU.W (xxx).W,Dn
	{},	// DIVU.W (xxx).L,Dn
	{},	// DIVU.W (d16,PC),Dn
	{},	// DIVU.W (d8,PC,Xn),Dn
	{},	// DIVU.W #<data>.W,Dn
	{},	// SBCD.B Dn,Dn
	{},	// SBCD.B -(An),-(An)
	{},	// OR.B Dn,(An)
	{},	// OR.B Dn,(An)+
	{},	// OR.B Dn,-(An)
	{},	// OR.B Dn,(d16,An)
	{},	// OR.B Dn,(d8,An,Xn)
	{},	// OR.B Dn,(xxx).W
	{},	// OR.B Dn,(xxx).L
	{},	// PACK.L Dn,Dn
	{},	// PACK.L -(An),-(An)
	{},	// OR.W Dn,(An)
	{},	// OR.W Dn,(An)+
	{},	// OR.W Dn,-(An)
	{},	// OR.W Dn,(d16,An)
	{},	// OR.W Dn,(d8,An,Xn)
	{},	// OR.W Dn,(xxx).W
	{},	// OR.W Dn,(xxx).L
	{},	// UNPK.L Dn,Dn
	{},	// UNPK.L -(An),-(An)
	{},	// OR.L Dn,(An)
	{},	// OR.L Dn,(An)+
	{},	// OR.L Dn,-(An)
	{},	// OR.L Dn,(d16,An)
	{},	// OR.L Dn,(d8,An,Xn)
	{},	// OR.L Dn,(xxx).W
	{},	// OR.L Dn,(xxx).L
	{},	// DIVS.W Dn,Dn
	{},	// DIVS.W (An),Dn
	{},	// DIVS.W (An)+,Dn
	{},	// DIVS.W -(An),Dn
	{},	// DIVS.W (d16,An),Dn
	{},	// DIVS.W (d8,An,Xn),Dn
	{},	// DIVS.W (xxx).W,Dn
	{},	// DIVS.W (xxx).L,Dn
	{},	// DIVS.W (d16,PC),Dn
	{},	// DIVS.W (d8,PC,Xn),Dn
	{},	// DIVS.W #<data>.W,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// SUB.B Dn,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// SUB.B (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// SUB.B (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// SUB.B -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// SUB.B (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// SUB.B (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// SUB.B (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// SUB.B (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// SUB.B (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// SUB.B (d8,PC,Xn),Dn
	{2,	0,	 4,0,0,0,	 8,0,2,0},	// SUB.B #<data>.B,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// SUB.W Dn,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// SUB.W An,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// SUB.W (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// SUB.W (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// SUB.W -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// SUB.W (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// SUB.W (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// SUB.W (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// SUB.W (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// SUB.W (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// SUB.W (d8,PC,Xn),Dn
	{2,	0,	 4,0,0,0,	 8,0,2,0},	// SUB.W #<data>.W,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// SUB.L Dn,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// SUB.L An,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// SUB.L (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// SUB.L (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// SUB.L -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// SUB.L (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// SUB.L (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// SUB.L (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// SUB.L (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// SUB.L (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// SUB.L (d8,PC,Xn),Dn
	{4,	0,	 6,0,0,0,	10,0,2,0},	// SUB.L #<data>.L,Dn
	{},	// SUBA.W Dn,An
	{},	// SUBA.W An,An
	{},	// SUBA.W (An),An
	{},	// SUBA.W (An)+,An
	{},	// SUBA.W -(An),An
	{},	// SUBA.W (d16,An),An
	{},	// SUBA.W (d8,An,Xn),An
	{},	// SUBA.W (xxx).W,An
	{},	// SUBA.W (xxx).L,An
	{},	// SUBA.W (d16,PC),An
	{},	// SUBA.W (d8,PC,Xn),An
	{},	// SUBA.W #<data>.W,An
	{},	// SUBX.B Dn,Dn
	{},	// SUBX.B -(An),-(An)
	{},	// SUB.B Dn,(An)
	{},	// SUB.B Dn,(An)+
	{},	// SUB.B Dn,-(An)
	{},	// SUB.B Dn,(d16,An)
	{},	// SUB.B Dn,(d8,An,Xn)
	{},	// SUB.B Dn,(xxx).W
	{},	// SUB.B Dn,(xxx).L
	{},	// SUBX.W Dn,Dn
	{},	// SUBX.W -(An),-(An)
	{},	// SUB.W Dn,(An)
	{},	// SUB.W Dn,(An)+
	{},	// SUB.W Dn,-(An)
	{},	// SUB.W Dn,(d16,An)
	{},	// SUB.W Dn,(d8,An,Xn)
	{},	// SUB.W Dn,(xxx).W
	{},	// SUB.W Dn,(xxx).L
	{},	// SUBX.L Dn,Dn
	{},	// SUBX.L -(An),-(An)
	{},	// SUB.L Dn,(An)
	{},	// SUB.L Dn,(An)+
	{},	// SUB.L Dn,-(An)
	{},	// SUB.L Dn,(d16,An)
	{},	// SUB.L Dn,(d8,An,Xn)
	{},	// SUB.L Dn,(xxx).W
	{},	// SUB.L Dn,(xxx).L
	{},	// SUBA.L Dn,An
	{},	// SUBA.L An,An
	{},	// SUBA.L (An),An
	{},	// SUBA.L (An)+,An
	{},	// SUBA.L -(An),An
	{},	// SUBA.L (d16,An),An
	{},	// SUBA.L (d8,An,Xn),An
	{},	// SUBA.L (xxx).W,An
	{},	// SUBA.L (xxx).L,An
	{},	// SUBA.L (d16,PC),An
	{},	// SUBA.L (d8,PC,Xn),An
	{},	// SUBA.L #<data>.L,An
	{},	// CMP.B Dn,Dn
	{},	// CMP.B (An),Dn
	{},	// CMP.B (An)+,Dn
	{},	// CMP.B -(An),Dn
	{},	// CMP.B (d16,An),Dn
	{},	// CMP.B (d8,An,Xn),Dn
	{},	// CMP.B (xxx).W,Dn
	{},	// CMP.B (xxx).L,Dn
	{},	// CMP.B (d16,PC),Dn
	{},	// CMP.B (d8,PC,Xn),Dn
	{},	// CMP.B #<data>.B,Dn
	{},	// CMP.W Dn,Dn
	{},	// CMP.W An,Dn
	{},	// CMP.W (An),Dn
	{},	// CMP.W (An)+,Dn
	{},	// CMP.W -(An),Dn
	{},	// CMP.W (d16,An),Dn
	{},	// CMP.W (d8,An,Xn),Dn
	{},	// CMP.W (xxx).W,Dn
	{},	// CMP.W (xxx).L,Dn
	{},	// CMP.W (d16,PC),Dn
	{},	// CMP.W (d8,PC,Xn),Dn
	{},	// CMP.W #<data>.W,Dn
	{},	// CMP.L Dn,Dn
	{},	// CMP.L An,Dn
	{},	// CMP.L (An),Dn
	{},	// CMP.L (An)+,Dn
	{},	// CMP.L -(An),Dn
	{},	// CMP.L (d16,An),Dn
	{},	// CMP.L (d8,An,Xn),Dn
	{},	// CMP.L (xxx).W,Dn
	{},	// CMP.L (xxx).L,Dn
	{},	// CMP.L (d16,PC),Dn
	{},	// CMP.L (d8,PC,Xn),Dn
	{},	// CMP.L #<data>.L,Dn
	{},	// CMPA.W Dn,An
	{},	// CMPA.W An,An
	{},	// CMPA.W (An),An
	{},	// CMPA.W (An)+,An
	{},	// CMPA.W -(An),An
	{},	// CMPA.W (d16,An),An
	{},	// CMPA.W (d8,An,Xn),An
	{},	// CMPA.W (xxx).W,An
	{},	// CMPA.W (xxx).L,An
	{},	// CMPA.W (d16,PC),An
	{},	// CMPA.W (d8,PC,Xn),An
	{},	// CMPA.W #<data>.W,An
	{},	// EOR.B Dn,Dn
	{},	// CMPM.B (An)+,(An)+
	{},	// EOR.B Dn,(An)
	{},	// EOR.B Dn,(An)+
	{},	// EOR.B Dn,-(An)
	{},	// EOR.B Dn,(d16,An)
	{},	// EOR.B Dn,(d8,An,Xn)
	{},	// EOR.B Dn,(xxx).W
	{},	// EOR.B Dn,(xxx).L
	{},	// EOR.W Dn,Dn
	{},	// CMPM.W (An)+,(An)+
	{},	// EOR.W Dn,(An)
	{},	// EOR.W Dn,(An)+
	{},	// EOR.W Dn,-(An)
	{},	// EOR.W Dn,(d16,An)
	{},	// EOR.W Dn,(d8,An,Xn)
	{},	// EOR.W Dn,(xxx).W
	{},	// EOR.W Dn,(xxx).L
	{},	// EOR.L Dn,Dn
	{},	// CMPM.L (An)+,(An)+
	{},	// EOR.L Dn,(An)
	{},	// EOR.L Dn,(An)+
	{},	// EOR.L Dn,-(An)
	{},	// EOR.L Dn,(d16,An)
	{},	// EOR.L Dn,(d8,An,Xn)
	{},	// EOR.L Dn,(xxx).W
	{},	// EOR.L Dn,(xxx).L
	{},	// CMPA.L Dn,An
	{},	// CMPA.L An,An
	{},	// CMPA.L (An),An
	{},	// CMPA.L (An)+,An
	{},	// CMPA.L -(An),An
	{},	// CMPA.L (d16,An),An
	{},	// CMPA.L (d8,An,Xn),An
	{},	// CMPA.L (xxx).W,An
	{},	// CMPA.L (xxx).L,An
	{},	// CMPA.L (d16,PC),An
	{},	// CMPA.L (d8,PC,Xn),An
	{},	// CMPA.L #<data>.L,An
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// AND.B Dn,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// AND.B (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// AND.B (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// AND.B -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// AND.B (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// AND.B (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// AND.B (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// AND.B (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// AND.B (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// AND.B (d8,PC,Xn),Dn
	{2,	0,	 4,0,0,0,	 8,0,2,0},	// AND.B #<data>.B,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// AND.W Dn,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// AND.W (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// AND.W (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// AND.W -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// AND.W (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// AND.W (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// AND.W (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// AND.W (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// AND.W (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// AND.W (d8,PC,Xn),Dn
	{2,	0,	 4,0,0,0,	 8,0,2,0},	// AND.W #<data>.W,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// AND.L Dn,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// AND.L (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// AND.L (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// AND.L -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// AND.L (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// AND.L (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// AND.L (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// AND.L (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// AND.L (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// AND.L (d8,PC,Xn),Dn
	{4,	0,	 6,0,0,0,	10,0,2,0},	// AND.L #<data>.L,Dn
	{},	// MULU.W Dn,Dn
	{},	// MULU.W (An),Dn
	{},	// MULU.W (An)+,Dn
	{},	// MULU.W -(An),Dn
	{},	// MULU.W (d16,An),Dn
	{},	// MULU.W (d8,An,Xn),Dn
	{},	// MULU.W (xxx).W,Dn
	{},	// MULU.W (xxx).L,Dn
	{},	// MULU.W (d16,PC),Dn
	{},	// MULU.W (d8,PC,Xn),Dn
	{},	// MULU.W #<data>.W,Dn
	{},	// ABCD.B Dn,Dn
	{},	// ABCD.B -(An),-(An)
	{},	// AND.B Dn,(An)
	{},	// AND.B Dn,(An)+
	{},	// AND.B Dn,-(An)
	{},	// AND.B Dn,(d16,An)
	{},	// AND.B Dn,(d8,An,Xn)
	{},	// AND.B Dn,(xxx).W
	{},	// AND.B Dn,(xxx).L
	{4,	0,	 4,0,0,0,	 6,0,1,0},	// EXG.L Dn,Dn
	{4,	0,	 4,0,0,0,	 6,0,1,0},	// EXG.L An,An
	{},	// AND.W Dn,(An)
	{},	// AND.W Dn,(An)+
	{},	// AND.W Dn,-(An)
	{},	// AND.W Dn,(d16,An)
	{},	// AND.W Dn,(d8,An,Xn)
	{},	// AND.W Dn,(xxx).W
	{},	// AND.W Dn,(xxx).L
	{4,	0,	 4,0,0,0,	 6,0,1,0},	// EXG.L Dn,An
	{},	// AND.L Dn,(An)
	{},	// AND.L Dn,(An)+
	{},	// AND.L Dn,-(An)
	{},	// AND.L Dn,(d16,An)
	{},	// AND.L Dn,(d8,An,Xn)
	{},	// AND.L Dn,(xxx).W
	{},	// AND.L Dn,(xxx).L
	{},	// MULS.W Dn,Dn
	{},	// MULS.W (An),Dn
	{},	// MULS.W (An)+,Dn
	{},	// MULS.W -(An),Dn
	{},	// MULS.W (d16,An),Dn
	{},	// MULS.W (d8,An,Xn),Dn
	{},	// MULS.W (xxx).W,Dn
	{},	// MULS.W (xxx).L,Dn
	{},	// MULS.W (d16,PC),Dn
	{},	// MULS.W (d8,PC,Xn),Dn
	{},	// MULS.W #<data>.W,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// ADD.B Dn,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// ADD.B (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// ADD.B (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// ADD.B -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// ADD.B (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// ADD.B (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// ADD.B (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// ADD.B (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// ADD.B (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// ADD.B (d8,PC,Xn),Dn
	{2,	0,	 4,0,0,0,	 8,0,2,0},	// ADD.B #<data>.B,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// ADD.W Dn,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// ADD.W An,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// ADD.W (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// ADD.W (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// ADD.W -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// ADD.W (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// ADD.W (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// ADD.W (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// ADD.W (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// ADD.W (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// ADD.W (d8,PC,Xn),Dn
	{2,	0,	 4,0,0,0,	 8,0,2,0},	// ADD.W #<data>.W,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// ADD.L Dn,Dn
	{2,	0,	 2,0,0,0,	 4,0,1,0},	// ADD.L An,Dn
	{1,	1,	 7,1,0,0,	 9,1,1,0},	// ADD.L (An),Dn
	{0,	1,	 7,1,0,0,	 9,1,1,0},	// ADD.L (An)+,Dn
	{2,	2,	 8,1,0,0,	10,1,1,0},	// ADD.L -(An),Dn
	{2,	2,	 8,1,0,0,	12,1,1,0},	// ADD.L (d16,An),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// ADD.L (d8,An,Xn),Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// ADD.L (xxx).W,Dn
	{1,	0,	 8,1,0,0,	13,1,2,0},	// ADD.L (xxx).L,Dn
	{2,	2,	 8,1,0,0,	12,1,2,0},	// ADD.L (d16,PC),Dn
	{4,	2,	10,1,0,0,	14,1,2,0},	// ADD.L (d8,PC,Xn),Dn
	{4,	0,	 6,0,0,0,	10,0,2,0},	// ADD.L #<data>.L,Dn
	{},	// ADDA.W Dn,An
	{},	// ADDA.W An,An
	{},	// ADDA.W (An),An
	{},	// ADDA.W (An)+,An
	{},	// ADDA.W -(An),An
	{},	// ADDA.W (d16,An),An
	{},	// ADDA.W (d8,An,Xn),An
	{},	// ADDA.W (xxx).W,An
	{},	// ADDA.W (xxx).L,An
	{},	// ADDA.W (d16,PC),An
	{},	// ADDA.W (d8,PC,Xn),An
	{},	// ADDA.W #<data>.W,An
	{},	// ADDX.B Dn,Dn
	{},	// ADDX.B -(An),-(An)
	{},	// ADD.B Dn,(An)
	{},	// ADD.B Dn,(An)+
	{},	// ADD.B Dn,-(An)
	{},	// ADD.B Dn,(d16,An)
	{},	// ADD.B Dn,(d8,An,Xn)
	{},	// ADD.B Dn,(xxx).W
	{},	// ADD.B Dn,(xxx).L
	{},	// ADDX.W Dn,Dn
	{},	// ADDX.W -(An),-(An)
	{},	// ADD.W Dn,(An)
	{},	// ADD.W Dn,(An)+
	{},	// ADD.W Dn,-(An)
	{},	// ADD.W Dn,(d16,An)
	{},	// ADD.W Dn,(d8,An,Xn)
	{},	// ADD.W Dn,(xxx).W
	{},	// ADD.W Dn,(xxx).L
	{},	// ADDX.L Dn,Dn
	{},	// ADDX.L -(An),-(An)
	{},	// ADD.L Dn,(An)
	{},	// ADD.L Dn,(An)+
	{},	// ADD.L Dn,-(An)
	{},	// ADD.L Dn,(d16,An)
	{},	// ADD.L Dn,(d8,An,Xn)
	{},	// ADD.L Dn,(xxx).W
	{},	// ADD.L Dn,(xxx).L
	{},	// ADDA.L Dn,An
	{},	// ADDA.L An,An
	{},	// ADDA.L (An),An
	{},	// ADDA.L (An)+,An
	{},	// ADDA.L -(An),An
	{},	// ADDA.L (d16,An),An
	{},	// ADDA.L (d8,An,Xn),An
	{},	// ADDA.L (xxx).W,An
	{},	// ADDA.L (xxx).L,An
	{},	// ADDA.L (d16,PC),An
	{},	// ADDA.L (d8,PC,Xn),An
	{},	// ADDA.L #<data>.L,An
	{},	// ASRQ.B #<data>,Dn
	{},	// LSRQ.B #<data>,Dn
	{},	// ROXRQ.B #<data>,Dn
	{},	// RORQ.B #<data>,Dn
	{},	// ASR.B Dn,Dn
	{},	// LSR.B Dn,Dn
	{},	// ROXR.B Dn,Dn
	{},	// ROR.B Dn,Dn
	{},	// ASRQ.W #<data>,Dn
	{},	// LSRQ.W #<data>,Dn
	{},	// ROXRQ.W #<data>,Dn
	{},	// RORQ.W #<data>,Dn
	{},	// ASR.W Dn,Dn
	{},	// LSR.W Dn,Dn
	{},	// ROXR.W Dn,Dn
	{},	// ROR.W Dn,Dn
	{},	// ASRQ.L #<data>,Dn
	{},	// LSRQ.L #<data>,Dn
	{},	// ROXRQ.L #<data>,Dn
	{},	// RORQ.L #<data>,Dn
	{},	// ASR.L Dn,Dn
	{},	// LSR.L Dn,Dn
	{},	// ROXR.L Dn,Dn
	{},	// ROR.L Dn,Dn
	{},	// ASRW.W (An)
	{},	// ASRW.W (An)+
	{},	// ASRW.W -(An)
	{},	// ASRW.W (d16,An)
	{},	// ASRW.W (d8,An,Xn)
	{},	// ASRW.W (xxx).W
	{},	// ASRW.W (xxx).L
	{},	// ASLQ.B #<data>,Dn
	{},	// LSLQ.B #<data>,Dn
	{},	// ROXLQ.B #<data>,Dn
	{},	// ROLQ.B #<data>,Dn
	{},	// ASL.B Dn,Dn
	{},	// LSL.B Dn,Dn
	{},	// ROXL.B Dn,Dn
	{},	// ROL.B Dn,Dn
	{},	// ASLQ.W #<data>,Dn
	{},	// LSLQ.W #<data>,Dn
	{},	// ROXLQ.W #<data>,Dn
	{},	// ROLQ.W #<data>,Dn
	{},	// ASL.W Dn,Dn
	{},	// LSL.W Dn,Dn
	{},	// ROXL.W Dn,Dn
	{},	// ROL.W Dn,Dn
	{},	// ASLQ.L #<data>,Dn
	{},	// LSLQ.L #<data>,Dn
	{},	// ROXLQ.L #<data>,Dn
	{},	// ROLQ.L #<data>,Dn
	{},	// ASL.L Dn,Dn
	{},	// LSL.L Dn,Dn
	{},	// ROXL.L Dn,Dn
	{},	// ROL.L Dn,Dn
	{},	// ASLW.W (An)
	{},	// ASLW.W (An)+
	{},	// ASLW.W -(An)
	{},	// ASLW.W (d16,An)
	{},	// ASLW.W (d8,An,Xn)
	{},	// ASLW.W (xxx).W
	{},	// ASLW.W (xxx).L
	{},	// LSRW.W (An)
	{},	// LSRW.W (An)+
	{},	// LSRW.W -(An)
	{},	// LSRW.W (d16,An)
	{},	// LSRW.W (d8,An,Xn)
	{},	// LSRW.W (xxx).W
	{},	// LSRW.W (xxx).L
	{},	// LSLW.W (An)
	{},	// LSLW.W (An)+
	{},	// LSLW.W -(An)
	{},	// LSLW.W (d16,An)
	{},	// LSLW.W (d8,An,Xn)
	{},	// LSLW.W (xxx).W
	{},	// LSLW.W (xxx).L
	{},	// ROXRW.W (An)
	{},	// ROXRW.W (An)+
	{},	// ROXRW.W -(An)
	{},	// ROXRW.W (d16,An)
	{},	// ROXRW.W (d8,An,Xn)
	{},	// ROXRW.W (xxx).W
	{},	// ROXRW.W (xxx).L
	{},	// ROXLW.W (An)
	{},	// ROXLW.W (An)+
	{},	// ROXLW.W -(An)
	{},	// ROXLW.W (d16,An)
	{},	// ROXLW.W (d8,An,Xn)
	{},	// ROXLW.W (xxx).W
	{},	// ROXLW.W (xxx).L
	{},	// RORW.W (An)
	{},	// RORW.W (An)+
	{},	// RORW.W -(An)
	{},	// RORW.W (d16,An)
	{},	// RORW.W (d8,An,Xn)
	{},	// RORW.W (xxx).W
	{},	// RORW.W (xxx).L
	{},	// ROLW.W (An)
	{},	// ROLW.W (An)+
	{},	// ROLW.W -(An)
	{},	// ROLW.W (d16,An)
	{},	// ROLW.W (d8,An,Xn)
	{},	// ROLW.W (xxx).W
	{},	// ROLW.W (xxx).L
	{},	// BFTST.L #<data>.W,Dn
	{},	// BFTST.L #<data>.W,(An)
	{},	// BFTST.L #<data>.W,(d16,An)
	{},	// BFTST.L #<data>.W,(d8,An,Xn)
	{},	// BFTST.L #<data>.W,(xxx).W
	{},	// BFTST.L #<data>.W,(xxx).L
	{},	// BFTST.L #<data>.W,(d16,PC)
	{},	// BFTST.L #<data>.W,(d8,PC,Xn)
	{},	// BFEXTU.L #<data>.W,Dn
	{},	// BFEXTU.L #<data>.W,(An)
	{},	// BFEXTU.L #<data>.W,(d16,An)
	{},	// BFEXTU.L #<data>.W,(d8,An,Xn)
	{},	// BFEXTU.L #<data>.W,(xxx).W
	{},	// BFEXTU.L #<data>.W,(xxx).L
	{},	// BFEXTU.L #<data>.W,(d16,PC)
	{},	// BFEXTU.L #<data>.W,(d8,PC,Xn)
	{},	// BFCHG.L #<data>.W,Dn
	{},	// BFCHG.L #<data>.W,(An)
	{},	// BFCHG.L #<data>.W,(d16,An)
	{},	// BFCHG.L #<data>.W,(d8,An,Xn)
	{},	// BFCHG.L #<data>.W,(xxx).W
	{},	// BFCHG.L #<data>.W,(xxx).L
	{},	// BFEXTS.L #<data>.W,Dn
	{},	// BFEXTS.L #<data>.W,(An)
	{},	// BFEXTS.L #<data>.W,(d16,An)
	{},	// BFEXTS.L #<data>.W,(d8,An,Xn)
	{},	// BFEXTS.L #<data>.W,(xxx).W
	{},	// BFEXTS.L #<data>.W,(xxx).L
	{},	// BFEXTS.L #<data>.W,(d16,PC)
	{},	// BFEXTS.L #<data>.W,(d8,PC,Xn)
	{},	// BFCLR.L #<data>.W,Dn
	{},	// BFCLR.L #<data>.W,(An)
	{},	// BFCLR.L #<data>.W,(d16,An)
	{},	// BFCLR.L #<data>.W,(d8,An,Xn)
	{},	// BFCLR.L #<data>.W,(xxx).W
	{},	// BFCLR.L #<data>.W,(xxx).L
	{},	// BFFFO.L #<data>.W,Dn
	{},	// BFFFO.L #<data>.W,(An)
	{},	// BFFFO.L #<data>.W,(d16,An)
	{},	// BFFFO.L #<data>.W,(d8,An,Xn)
	{},	// BFFFO.L #<data>.W,(xxx).W
	{},	// BFFFO.L #<data>.W,(xxx).L
	{},	// BFFFO.L #<data>.W,(d16,PC)
	{},	// BFFFO.L #<data>.W,(d8,PC,Xn)
	{},	// BFSET.L #<data>.W,Dn
	{},	// BFSET.L #<data>.W,(An)
	{},	// BFSET.L #<data>.W,(d16,An)
	{},	// BFSET.L #<data>.W,(d8,An,Xn)
	{},	// BFSET.L #<data>.W,(xxx).W
	{},	// BFSET.L #<data>.W,(xxx).L
	{},	// BFINS.L #<data>.W,Dn
	{},	// BFINS.L #<data>.W,(An)
	{},	// BFINS.L #<data>.W,(d16,An)
	{},	// BFINS.L #<data>.W,(d8,An,Xn)
	{},	// BFINS.L #<data>.W,(xxx).W
	{},	// BFINS.L #<data>.W,(xxx).L
	{},	// MMUOP030.L (An),#<data>.W
	{},	// MMUOP030.L (d16,An),#<data>.W
	{},	// MMUOP030.L (d8,An,Xn),#<data>.W
	{},	// MMUOP030.L (xxx).W,#<data>.W
	{},	// MMUOP030.L (xxx).L,#<data>.W
	{},	// FPP.L #<data>.W,Dn
	{},	// FPP.L #<data>.W,An
	{},	// FPP.L #<data>.W,(An)
	{},	// FPP.L #<data>.W,(An)+
	{},	// FPP.L #<data>.W,-(An)
	{},	// FPP.L #<data>.W,(d16,An)
	{},	// FPP.L #<data>.W,(d8,An,Xn)
	{},	// FPP.L #<data>.W,(xxx).W
	{},	// FPP.L #<data>.W,(xxx).L
	{},	// FPP.L #<data>.W,(d16,PC)
	{},	// FPP.L #<data>.W,(d8,PC,Xn)
	{},	// FPP.L #<data>.W,#<data>.L
	{},	// FScc.L #<data>.W,Dn
	{},	// FDBcc.L #<data>.W,Dn
	{},	// FScc.L #<data>.W,(An)
	{},	// FScc.L #<data>.W,(An)+
	{},	// FScc.L #<data>.W,-(An)
	{},	// FScc.L #<data>.W,(d16,An)
	{},	// FScc.L #<data>.W,(d8,An,Xn)
	{},	// FScc.L #<data>.W,(xxx).W
	{},	// FScc.L #<data>.W,(xxx).L
	{},	// FTRAPcc.L #<data>.W
	{},	// FTRAPcc.L #<data>.L
	{},	// FTRAPcc.L 
	{},	// FBccQ.L #<data>,#<data>.W
	{},	// FBccQ.L #<data>,#<data>.L
	{},	// FSAVE.L (An)
	{},	// FSAVE.L -(An)
	{},	// FSAVE.L (d16,An)
	{},	// FSAVE.L (d8,An,Xn)
	{},	// FSAVE.L (xxx).W
	{},	// FSAVE.L (xxx).L
	{},	// FRESTORE.L (An)
	{},	// FRESTORE.L (An)+
	{},	// FRESTORE.L (d16,An)
	{},	// FRESTORE.L (d8,An,Xn)
	{},	// FRESTORE.L (xxx).W
	{},	// FRESTORE.L (xxx).L
	{},	// FRESTORE.L (d16,PC)
	{},	// FRESTORE.L (d8,PC,Xn)
	{},	// CINVLQ.L #<data>,An
	{},	// CINVPQ.L #<data>,An
	{},	// CINVAQ.L #<data>
	{},	// CINVAQ.L #<data>
	{},	// CINVAQ.L #<data>
	{},	// CINVAQ.L #<data>
	{},	// CINVAQ.L #<data>
	{},	// CINVAQ.L #<data>
	{},	// CINVAQ.L #<data>
	{},	// CINVAQ.L #<data>
	{},	// CPUSHLQ.L #<data>,An
	{},	// CPUSHPQ.L #<data>,An
	{},	// CPUSHAQ.L #<data>
	{},	// CPUSHAQ.L #<data>
	{},	// CPUSHAQ.L #<data>
	{},	// CPUSHAQ.L #<data>
	{},	// CPUSHAQ.L #<data>
	{},	// CPUSHAQ.L #<data>
	{},	// CPUSHAQ.L #<data>
	{},	// CPUSHAQ.L #<data>
	{},	// PFLUSHN.L (An)
	{},	// PFLUSH.L (An)
	{},	// PFLUSHAN.L (An)
	{},	// PFLUSHA.L (An)
	{},	// PTESTR.L (An)
	{},	// PTESTW.L (An)
	{},	// PLPAR.L (An)
	{},	// PLPAW.L (An)
	{},	// MOVE16.L (An)+,(xxx).L
	{},	// MOVE16.L (xxx).L,(An)+
	{},	// MOVE16.L (An),(xxx).L
	{},	// MOVE16.L (xxx).L,(An)
	{},	// MOVE16.L (An)+,(An)+
	{},	// LPSTOP.L #<data>.W
	{0,	0,	 6,0,0,0,	 8,0,1,0},	// NBCD.B Dn
	{},	// NBCD.B (An)
	{},	// NBCD.B (An)+
	{},	// NBCD.B -(An)
	{},	// NBCD.B (d16,An)
	{},	// NBCD.B (d8,An,Xn)
	{},	// NBCD.B (xxx).W
	{},	// NBCD.B (xxx).L
	{},	// SBCD.B Dn,Dn
	{},	// SBCD.B -(An),-(An)
	{},	// ABCD.B Dn,Dn
	{}	// ABCD.B -(An),-(An)
};

